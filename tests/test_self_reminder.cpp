// A reminder to self said in the capture line (dogfood 2026-10-22): the
// phrase, the title and time left after it, and the times offered when none
// was said. Pure functions of the text and `now`.
#include "capture/CaptureParse.h"
#include "capture/SelfReminder.h"
#include "chrono/ChronoParser.h"

#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QTime>

#include <gtest/gtest.h>

using heap::capture::findRemindPhrase;
using heap::capture::ReminderOption;
using heap::capture::reminderOptions;
using heap::capture::RemindPhrase;
using heap::capture::withoutRemindPhrase;
using heap::capture::WorkHours;

namespace {

const QDateTime kNow(QDate(2026, 5, 20), QTime(10, 0));  // Wednesday 10:00

// What the input would make of the line: the title and the moment.
struct Read {
  bool reminder = false;
  QString title;
  QDateTime at;
};

class SelfReminder : public ::testing::Test {
 protected:
  heap::chrono::ChronoParser ru{QLocale(QLocale::Russian, QLocale::Russia)};
  heap::chrono::ChronoParser en{QLocale(QLocale::English, QLocale::UnitedStates)};
  WorkHours work{9, 17, {}};

  Read read(const QString& text, const heap::chrono::ChronoParser& chrono, const QDateTime& now = kNow) {
    const RemindPhrase ph = findRemindPhrase(text);
    if(!ph.found()) {
      return {};
    }
    const heap::capture::Parsed p = heap::capture::parse(withoutRemindPhrase(text, ph), chrono, now);
    return {true, p.title, heap::capture::reminderMoment(p.when, p.whenHasTime, p.remind, now, work)};
  }

  Read read(const QString& text) {
    return read(text, ru);
  }

  static QStringList keys(const QVector<ReminderOption>& xs) {
    QStringList out;
    for(const ReminderOption& o : xs) {
      out.append(o.key);
    }
    return out;
  }
};

}  // namespace

// ── the phrase ──

TEST_F(SelfReminder, RussianWithTimeAtTheEnd) {
  const Read r = read(QStringLiteral("напомни мне позвонить в банк завтра в 15:00"));
  ASSERT_TRUE(r.reminder);
  EXPECT_EQ(r.title, QStringLiteral("позвонить в банк"));
  EXPECT_EQ(r.at, QDateTime(QDate(2026, 5, 21), QTime(15, 0)));
}

TEST_F(SelfReminder, AnyCaseAndEveryForm) {
  for(const QString& line : {QStringLiteral("Напомни мне позвонить в банк"),
                             QStringLiteral("НАПОМНИ позвонить в банк"),
                             QStringLiteral("напомнить позвонить в банк"),
                             QStringLiteral("напомнить себе позвонить в банк"),
                             QStringLiteral("Напомните мне позвонить в банк"),
                             QStringLiteral("напомни мне, что позвонить в банк")}) {
    const Read r = read(line);
    EXPECT_TRUE(r.reminder) << line.toStdString();
    EXPECT_EQ(r.title, QStringLiteral("позвонить в банк")) << line.toStdString();
    EXPECT_FALSE(r.at.isValid()) << "no time said: the input asks; " << line.toStdString();
  }
}

TEST_F(SelfReminder, InTwentyMinutes) {
  const Read r = read(QStringLiteral("напомни мне через 20 минут проверить пайплайн"));
  EXPECT_EQ(r.title, QStringLiteral("проверить пайплайн"));
  EXPECT_EQ(r.at, kNow.addSecs(20 * 60));
}

TEST_F(SelfReminder, ADayWithNoTimeIsItsStartOfWork) {
  const Read r = read(QStringLiteral("напомни в пятницу отправить отчёт"));
  EXPECT_EQ(r.title, QStringLiteral("отправить отчёт"));
  EXPECT_EQ(r.at, QDateTime(QDate(2026, 5, 22), QTime(9, 0)));
}

TEST_F(SelfReminder, TodayWithNoTimeAsks) {
  const Read r = read(QStringLiteral("напомни сегодня полить цветы"));
  EXPECT_TRUE(r.reminder);
  EXPECT_FALSE(r.at.isValid());
}

TEST_F(SelfReminder, TheTimeRightAfterThePhrase) {
  // "напомни в 15:00 …" was APP-319's reminder on a task; here the phrase is
  // the reminder and "в 15:00" its time.
  const Read r = read(QStringLiteral("напомни в 15:00 позвонить"));
  EXPECT_EQ(r.title, QStringLiteral("позвонить"));
  EXPECT_EQ(r.at, QDateTime(kNow.date(), QTime(15, 0)));
}

TEST_F(SelfReminder, ThePhraseInTheMiddle) {
  const Read a = read(QStringLiteral("завтра в 15:00 напомни мне позвонить в банк"));
  EXPECT_TRUE(a.reminder);
  EXPECT_EQ(a.title, QStringLiteral("позвонить в банк"));
  EXPECT_EQ(a.at, QDateTime(QDate(2026, 5, 21), QTime(15, 0)));
  const Read b = read(QStringLiteral("позвонить в банк, напомни мне"));
  EXPECT_TRUE(b.reminder);
  EXPECT_EQ(b.title, QStringLiteral("позвонить в банк"));
}

TEST_F(SelfReminder, English) {
  const Read a = read(QStringLiteral("remind me to call the bank tomorrow at 3pm"), en);
  ASSERT_TRUE(a.reminder);
  EXPECT_EQ(a.title, QStringLiteral("call the bank"));
  EXPECT_EQ(a.at, QDateTime(QDate(2026, 5, 21), QTime(15, 0)));
  const Read b = read(QStringLiteral("Remind me: renew the domain"), en);
  EXPECT_TRUE(b.reminder);
  EXPECT_EQ(b.title, QStringLiteral("renew the domain"));
  EXPECT_FALSE(b.at.isValid());
  const Read c = read(QStringLiteral("REMIND MYSELF check the pipeline in 20 minutes"), en);
  EXPECT_EQ(c.title, QStringLiteral("check the pipeline"));
  EXPECT_EQ(c.at, kNow.addSecs(20 * 60));
}

TEST_F(SelfReminder, WordsThatAreNotThePhrase) {
  for(const QString& line : {QStringLiteral("напоминание о встрече"),
                             QStringLiteral("Напоминания в уведомлениях не приходят"),
                             QStringLiteral("запомни мне это"),
                             QStringLiteral("remind Anna about the review"),
                             QStringLiteral("Reminder: pay rent"),
                             QStringLiteral("reminders are broken"),
                             QStringLiteral("переименовать напомнилку"),
                             QStringLiteral("позвонить // напомни мне потом"),
                             QStringLiteral("")}) {
    EXPECT_FALSE(findRemindPhrase(line).found()) << line.toStdString();
  }
}

TEST_F(SelfReminder, OnlyTheFirstLine) {
  EXPECT_FALSE(findRemindPhrase(QStringLiteral("позвонить в банк\nнапомни мне про паспорт")).found());
  const QString text = QStringLiteral("напомни мне позвонить\nописание");
  const RemindPhrase ph = findRemindPhrase(text);
  ASSERT_TRUE(ph.found());
  EXPECT_EQ(withoutRemindPhrase(text, ph), QStringLiteral("позвонить\nописание"));
}

TEST_F(SelfReminder, ThePhraseAsTyped) {
  const RemindPhrase ph = findRemindPhrase(QStringLiteral("Напомни мне позвонить"));
  EXPECT_EQ(ph.start, 0);
  EXPECT_EQ(ph.text, QStringLiteral("Напомни мне"));
  EXPECT_EQ(withoutRemindPhrase(QStringLiteral("Напомни мне позвонить"), ph), QStringLiteral("позвонить"));
}

// ── the times offered ──

TEST_F(SelfReminder, InTheDayAnHourFromNowComesFirst) {
  const QDateTime now(QDate(2026, 10, 22), QTime(13, 40, 25));  // Thursday
  const QVector<ReminderOption> o = reminderOptions(now, work);
  EXPECT_EQ(keys(o), (QStringList{"hour", "endOfDay", "nextDay", "none"}));
  EXPECT_EQ(o.at(0).at, QDateTime(now.date(), QTime(14, 40)));
  EXPECT_EQ(o.at(1).at, QDateTime(now.date(), QTime(17, 0)));
  EXPECT_EQ(o.at(2).at, QDateTime(QDate(2026, 10, 23), QTime(9, 0)));
  EXPECT_FALSE(o.at(3).at.isValid());
}

TEST_F(SelfReminder, AnHourThatEndsWithTheDayIsStillIn) {
  const QDateTime now(QDate(2026, 10, 22), QTime(16, 0));
  EXPECT_EQ(keys(reminderOptions(now, work)), (QStringList{"hour", "endOfDay", "nextDay", "none"}));
}

TEST_F(SelfReminder, PastTheEndTomorrowComesFirst) {
  const QDateTime now(QDate(2026, 10, 22), QTime(16, 1));
  const QVector<ReminderOption> o = reminderOptions(now, work);
  EXPECT_EQ(keys(o), (QStringList{"nextDay", "hour", "endOfDay", "none"}));
  EXPECT_EQ(o.at(0).at, QDateTime(QDate(2026, 10, 23), QTime(9, 0)));
  EXPECT_EQ(o.at(1).at, QDateTime(now.date(), QTime(17, 1)));
}

TEST_F(SelfReminder, TheEndOfADayThatHasEndedIsNotOffered) {
  EXPECT_EQ(keys(reminderOptions(QDateTime(QDate(2026, 10, 22), QTime(17, 0)), work)), (QStringList{"nextDay", "hour", "none"}));
  EXPECT_EQ(keys(reminderOptions(QDateTime(QDate(2026, 10, 22), QTime(23, 30)), work)), (QStringList{"nextDay", "hour", "none"}));
}

TEST_F(SelfReminder, TheNextWorkingDaySkipsTheWeekend) {
  const QDateTime friday(QDate(2026, 10, 23), QTime(16, 30));
  const QVector<ReminderOption> o = reminderOptions(friday, work);
  EXPECT_EQ(o.at(0).key, QStringLiteral("nextDay"));
  EXPECT_EQ(o.at(0).at, QDateTime(QDate(2026, 10, 26), QTime(9, 0)));
  // Other working days, other hours.
  const WorkHours sixDays{10, 20, {1, 2, 3, 4, 5, 6}};
  EXPECT_EQ(heap::capture::nextWorkdayStart(friday, sixDays), QDateTime(QDate(2026, 10, 24), QTime(10, 0)));
  EXPECT_EQ(reminderOptions(friday, sixDays).at(0).key, QStringLiteral("hour"));
}

TEST_F(SelfReminder, AWorkdayUntilMidnight) {
  const WorkHours late{9, 24, {}};
  const QVector<ReminderOption> o = reminderOptions(QDateTime(QDate(2026, 10, 22), QTime(22, 0)), late);
  EXPECT_EQ(keys(o), (QStringList{"hour", "endOfDay", "nextDay", "none"}));
  EXPECT_EQ(o.at(1).at.time(), QTime(23, 59));
}
