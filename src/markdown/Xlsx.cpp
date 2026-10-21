#include "markdown/Xlsx.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QtEndian>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>

#include <zlib.h>

namespace heap::md {

namespace {

// Little-endian reads, byte by byte: qFromLittleEndian's memcpy, inlined
// here, made GCC flag QByteArray's 1-byte empty buffer (-Warray-bounds) even
// behind the size check.
quint16 u16(const QByteArray& d, qsizetype at) {
  if(at < 0 || at + 2 > d.size()) {
    return 0;
  }
  const auto* p = reinterpret_cast<const uchar*>(d.constData()) + at;
  return static_cast<quint16>(p[0] | (p[1] << 8));
}

quint32 u32(const QByteArray& d, qsizetype at) {
  if(at < 0 || at + 4 > d.size()) {
    return 0;
  }
  const auto* p = reinterpret_cast<const uchar*>(d.constData()) + at;
  return static_cast<quint32>(p[0]) | (static_cast<quint32>(p[1]) << 8) | (static_cast<quint32>(p[2]) << 16) |
         (static_cast<quint32>(p[3]) << 24);
}

// Raw deflate (no zlib header), as a zip stores it.
bool inflateRaw(const QByteArray& in, qsizetype expected, QByteArray* out) {
  z_stream s{};
  if(inflateInit2(&s, -MAX_WBITS) != Z_OK) {
    return false;
  }
  out->resize(std::max<qsizetype>(expected, 1));
  s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.constData()));
  s.avail_in = static_cast<uInt>(in.size());
  s.next_out = reinterpret_cast<Bytef*>(out->data());
  s.avail_out = static_cast<uInt>(out->size());
  int rc = Z_OK;
  while(rc == Z_OK) {
    rc = inflate(&s, Z_NO_FLUSH);
    if(rc == Z_OK && s.avail_out == 0) {
      // The size the directory gave was wrong: grow and go on.
      const qsizetype done = out->size();
      out->resize(done * 2);
      s.next_out = reinterpret_cast<Bytef*>(out->data() + done);
      s.avail_out = static_cast<uInt>(out->size() - done);
    }
  }
  const bool ok = rc == Z_STREAM_END;
  out->resize(static_cast<qsizetype>(s.total_out));
  inflateEnd(&s);
  return ok;
}

// The text of an element, its children's text included (a shared string
// split into runs), skipping phonetic hints.
QString textOf(QXmlStreamReader& x) {
  QString out;
  int depth = 1;
  bool skip = false;
  while(depth > 0 && !x.atEnd()) {
    x.readNext();
    if(x.isStartElement()) {
      ++depth;
      if(x.name() == QLatin1String("rPh")) {
        skip = true;
      }
    } else if(x.isEndElement()) {
      --depth;
      if(x.name() == QLatin1String("rPh")) {
        skip = false;
      }
    } else if(x.isCharacters() && !skip) {
      out += x.text();
    }
  }
  return out;
}

// Whether a number format shows a date: the built-in date ids, or a code
// with day, month or year letters outside quotes and [colour] sections.
bool isDateFormat(int id, const QString& code) {
  if((id >= 14 && id <= 22) || (id >= 27 && id <= 36) || (id >= 45 && id <= 47) || (id >= 50 && id <= 58)) {
    return true;
  }
  if(code.isEmpty()) {
    return false;
  }
  static const QRegularExpression noise(QStringLiteral("\"[^\"]*\"|\\[[^\\]]*\\]|\\\\."));
  const QString bare = QString(code).remove(noise).toLower();
  return bare.contains(QLatin1Char('d')) || bare.contains(QLatin1Char('y')) ||
         (bare.contains(QLatin1Char('m')) && !bare.contains(QLatin1Char('0')) && !bare.contains(QLatin1Char('#')));
}

// Per cell style (the "s" of a cell): whether it shows a date.
QList<bool> dateStyles(const QByteArray& xml) {
  QHash<int, QString> codes;
  QList<bool> out;
  QXmlStreamReader x(xml);
  bool inCellXfs = false;
  while(!x.atEnd()) {
    x.readNext();
    if(x.isStartElement() && x.name() == QLatin1String("numFmt")) {
      codes.insert(x.attributes().value(QLatin1String("numFmtId")).toInt(), x.attributes().value(QLatin1String("formatCode")).toString());
    } else if(x.isStartElement() && x.name() == QLatin1String("cellXfs")) {
      inCellXfs = true;
    } else if(x.isEndElement() && x.name() == QLatin1String("cellXfs")) {
      inCellXfs = false;
    } else if(inCellXfs && x.isStartElement() && x.name() == QLatin1String("xf")) {
      const int id = x.attributes().value(QLatin1String("numFmtId")).toInt();
      out << isDateFormat(id, codes.value(id));
    }
  }
  return out;
}

// An Excel serial day (1900 system: day 0 is 1899-12-30) as an ISO date;
// with its time when it has one.
QString serialDate(const QString& value) {
  bool ok = false;
  const double serial = value.toDouble(&ok);
  if(!ok || serial < 0 || serial > 2958465) {
    return value;
  }
  const auto days = static_cast<qint64>(serial);
  const QDate date = QDate(1899, 12, 30).addDays(days);
  const auto ms = static_cast<int>(std::lround((serial - static_cast<double>(days)) * 86400000.0));
  // ISO: the same in either UI language, in the viewer and in a pasted table.
  if(ms <= 0 || ms >= 86400000) {
    return date.toString(Qt::ISODate);
  }
  return date.toString(Qt::ISODate) + QLatin1Char(' ') + QTime(0, 0).addMSecs(ms).toString(Qt::ISODate).left(5);
}

QStringList sharedStrings(const QByteArray& xml) {
  QStringList out;
  QXmlStreamReader x(xml);
  while(!x.atEnd()) {
    x.readNext();
    if(x.isStartElement() && x.name() == QLatin1String("si")) {
      out << textOf(x);
    }
  }
  return out;
}

QList<QStringList> sheetRows(const QByteArray& xml, const QStringList& shared, const QList<bool>& dates, int maxRows) {
  QList<QStringList> rows;
  QXmlStreamReader x(xml);
  int rowIndex = -1;
  int column = -1;
  QString type;
  while(!x.atEnd()) {
    x.readNext();
    if(!x.isStartElement()) {
      continue;
    }
    if(x.name() == QLatin1String("row")) {
      bool ok = false;
      const int r = x.attributes().value(QLatin1String("r")).toInt(&ok);
      rowIndex = ok && r > 0 ? r - 1 : rowIndex + 1;
      if(maxRows > 0 && rowIndex >= maxRows) {
        break;
      }
      column = -1;
    } else if(x.name() == QLatin1String("c")) {
      const QString ref = x.attributes().value(QLatin1String("r")).toString();
      const int c = xlsxColumn(ref);
      column = c >= 0 ? c : column + 1;
      type = x.attributes().value(QLatin1String("t")).toString();
      const int style = x.attributes().value(QLatin1String("s")).toInt();
      QString value;
      // The cell's own <v> or <is>.
      int depth = 1;
      while(depth > 0 && !x.atEnd()) {
        x.readNext();
        if(x.isStartElement()) {
          if(x.name() == QLatin1String("v")) {
            value = x.readElementText();
          } else if(x.name() == QLatin1String("is")) {
            value = textOf(x);
          } else {
            ++depth;
          }
        } else if(x.isEndElement()) {
          --depth;
        }
      }
      if(type == QLatin1String("s")) {
        bool ok = false;
        const int i = value.toInt(&ok);
        value = ok && i >= 0 && i < shared.size() ? shared.at(i) : QString();
      } else if(type == QLatin1String("b")) {
        value = value == QLatin1String("1") ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
      } else if((type.isEmpty() || type == QLatin1String("n")) && style >= 0 && style < dates.size() && dates.at(style) &&
                !value.isEmpty()) {
        value = serialDate(value);
      }
      if(rowIndex < 0) {
        rowIndex = 0;
      }
      while(rows.size() <= rowIndex) {
        rows << QStringList();
      }
      QStringList& row = rows[rowIndex];
      while(row.size() <= column) {
        row << QString();
      }
      row[column] = value;
    }
  }
  qsizetype width = 0;
  for(const QStringList& r : rows) {
    width = std::max(width, r.size());
  }
  for(QStringList& r : rows) {
    while(r.size() < width) {
      r << QString();
    }
  }
  return rows;
}

}  // namespace

int xlsxColumn(const QString& ref) {
  int col = 0;
  int letters = 0;
  for(const QChar ch : ref) {
    if(ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) {
      col = col * 26 + (ch.unicode() - 'A' + 1);
      ++letters;
    } else {
      break;
    }
  }
  return letters == 0 ? -1 : col - 1;
}

QHash<QString, QByteArray> unzip(const QByteArray& zip, QString* error) {
  QHash<QString, QByteArray> out;
  const auto fail = [&](const char* why) {
    if(error != nullptr) {
      *error = QString::fromLatin1(why);
    }
    return QHash<QString, QByteArray>();
  };
  // The end of central directory record, searched from the end (a comment
  // may follow it).
  qsizetype eocd = -1;
  for(qsizetype i = zip.size() - 22; i >= 0 && i >= zip.size() - 22 - 65535; --i) {
    if(u32(zip, i) == 0x06054b50U) {
      eocd = i;
      break;
    }
  }
  if(eocd < 0) {
    return fail("not a zip archive");
  }
  const int entries = u16(zip, eocd + 10);
  qsizetype at = u32(zip, eocd + 16);
  for(int e = 0; e < entries; ++e) {
    if(u32(zip, at) != 0x02014b50U) {
      return fail("damaged zip directory");
    }
    const int method = u16(zip, at + 10);
    const qsizetype packed = u32(zip, at + 20);
    const qsizetype size = u32(zip, at + 24);
    const int nameLen = u16(zip, at + 28);
    const int extraLen = u16(zip, at + 30);
    const int commentLen = u16(zip, at + 32);
    const qsizetype local = u32(zip, at + 42);
    const QString name = QString::fromUtf8(zip.mid(at + 46, nameLen));
    at += 46 + nameLen + extraLen + commentLen;
    if(u32(zip, local) != 0x04034b50U) {
      return fail("damaged zip entry");
    }
    const qsizetype data = local + 30 + u16(zip, local + 26) + u16(zip, local + 28);
    if(data + packed > zip.size()) {
      return fail("truncated zip entry");
    }
    const QByteArray raw = zip.mid(data, packed);
    if(method == 0) {
      out.insert(name, raw);
    } else if(method == 8) {
      QByteArray inflated;
      if(!inflateRaw(raw, size, &inflated)) {
        return fail("damaged compressed data");
      }
      out.insert(name, inflated);
    }
  }
  return out;
}

QList<XlsxSheet> readXlsx(const QByteArray& data, int maxRows, QString* error) {
  const QHash<QString, QByteArray> files = unzip(data, error);
  if(files.isEmpty()) {
    return {};
  }
  const QByteArray workbook = files.value(QStringLiteral("xl/workbook.xml"));
  if(workbook.isEmpty()) {
    if(error != nullptr) {
      *error = QStringLiteral("not an xlsx workbook");
    }
    return {};
  }
  // Relationship id -> part path.
  QHash<QString, QString> targets;
  {
    QXmlStreamReader x(files.value(QStringLiteral("xl/_rels/workbook.xml.rels")));
    while(!x.atEnd()) {
      x.readNext();
      if(x.isStartElement() && x.name() == QLatin1String("Relationship")) {
        QString target = x.attributes().value(QLatin1String("Target")).toString();
        target = target.startsWith(QLatin1Char('/')) ? target.mid(1) : QStringLiteral("xl/") + target;
        targets.insert(x.attributes().value(QLatin1String("Id")).toString(), target);
      }
    }
  }
  const QStringList shared = sharedStrings(files.value(QStringLiteral("xl/sharedStrings.xml")));
  const QList<bool> dates = dateStyles(files.value(QStringLiteral("xl/styles.xml")));
  QList<XlsxSheet> sheets;
  QXmlStreamReader x(workbook);
  while(!x.atEnd()) {
    x.readNext();
    if(!x.isStartElement() || x.name() != QLatin1String("sheet")) {
      continue;
    }
    QString id;
    for(const QXmlStreamAttribute& a : x.attributes()) {
      if(a.name() == QLatin1String("id")) {
        id = a.value().toString();
      }
    }
    XlsxSheet sheet;
    sheet.name = x.attributes().value(QLatin1String("name")).toString();
    const QByteArray part = files.value(targets.value(id));
    sheet.rows = sheetRows(part, shared, dates, maxRows);
    sheets << sheet;
  }
  return sheets;
}

}  // namespace heap::md
