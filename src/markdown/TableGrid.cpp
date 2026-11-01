#include "markdown/TableGrid.h"

#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <cmath>

namespace heap::md {

namespace {

// Laid out as Qt Quick's Text lays out its text (design metrics: no
// hinting, fractional advances), so a width measured here fits there; the
// font's own metrics ran a few pixels short and a cell wrapped that should
// not have. `width` 0: one line, as long as the text. The lines' height
// (the leading in) and the widest line.
QSizeF laidOut(const QString& text, const QFont& font, qreal width) {
  QTextLayout layout(text, font);
  QTextOption option;
  option.setUseDesignMetrics(true);
  option.setWrapMode(width > 0 ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
  layout.setTextOption(option);
  qreal height = 0;
  qreal widest = 0;
  layout.beginLayout();
  for(QTextLine l = layout.createLine(); l.isValid(); l = layout.createLine()) {
    l.setLeadingIncluded(true);
    l.setLineWidth(width > 0 ? width : 1e6);
    height += l.height();
    widest = std::max(widest, l.naturalTextWidth());
  }
  layout.endLayout();
  return {widest, height};
}

}  // namespace

qreal wrappedTextHeight(const QString& text, const QFont& font, qreal width) {
  const qreal line = laidOut(QStringLiteral("M"), font, 0).height();
  if(text.isEmpty() || width <= 0) {
    return line;
  }
  return std::max(line, laidOut(text, font, width).height());
}

qreal textAdvance(const QString& text, const QFont& font) {
  return text.isEmpty() ? 0 : laidOut(text, font, 0).width();
}

QList<qreal> fitTableColumns(const QList<qreal>& natural, qreal room, qreal minColumn) {
  qreal total = 0;
  for(const qreal w : natural) {
    total += w;
  }
  if(natural.isEmpty() || room <= 0 || total <= room) {
    return natural;
  }
  QList<qreal> floors;
  qreal least = 0;
  for(const qreal w : natural) {
    floors << std::min(w, minColumn);
    least += floors.last();
  }
  if(least >= room) {
    return floors;
  }
  // The cap where the columns, each min(own, max(floor, cap)), fill the room.
  const auto widthAt = [&](qreal cap) {
    qreal sum = 0;
    for(qsizetype k = 0; k < natural.size(); ++k) {
      sum += std::min(natural.at(k), std::max(floors.at(k), cap));
    }
    return sum;
  };
  qreal lo = 0;
  qreal hi = *std::max_element(natural.begin(), natural.end());
  for(int step = 0; step < 40 && hi - lo > 0.25; ++step) {
    const qreal mid = (lo + hi) / 2;
    (widthAt(mid) > room ? hi : lo) = mid;
  }
  QList<qreal> out;
  for(qsizetype k = 0; k < natural.size(); ++k) {
    out << std::floor(std::min(natural.at(k), std::max(floors.at(k), lo)));
  }
  return out;
}

TableGrid layoutTableGrid(const QList<QStringList>& rows, const TableGridStyle& style) {
  TableGrid grid;
  grid.room = style.room;
  qsizetype columns = 0;
  for(const QStringList& row : rows) {
    columns = std::max(columns, row.size());
  }
  QList<qreal> natural(columns, 0.0);
  for(qsizetype r = 0; r < rows.size(); ++r) {
    for(qsizetype c = 0; c < rows.at(r).size(); ++c) {
      // A pixel over: the drawn Text rounds its width.
      natural[c] = std::max(natural.at(c), std::ceil(textAdvance(rows.at(r).at(c), r == 0 ? style.head : style.body)) + 1);
    }
  }
  // An empty column still has room to click into.
  const qreal least = std::ceil(textAdvance(QStringLiteral("MM"), style.body));
  for(qreal& w : natural) {
    w = std::max(w, least) + 2 * style.padX;
  }
  grid.widths = fitTableColumns(natural, style.room, style.minColumn);
  for(const qreal w : grid.widths) {
    grid.width += w;
  }
  for(qsizetype r = 0; r < rows.size(); ++r) {
    const QFont& font = r == 0 ? style.head : style.body;
    qreal tallest = wrappedTextHeight(QString(), font, 0);
    for(qsizetype c = 0; c < rows.at(r).size(); ++c) {
      tallest = std::max(tallest, wrappedTextHeight(rows.at(r).at(c), font, grid.widths.at(c) - 2 * style.padX));
    }
    grid.heights << std::ceil(tallest + 2 * style.padY);
  }
  return grid;
}

}  // namespace heap::md
