// Reading and writing .ics.
//
// Every meeting heap holds arrives from somewhere else — Google, Outlook, a
// conference's "add to calendar" link — and RFC 5545 is the only thing all of
// them agree on. The format's traps each produce a bug that looks like data
// loss: a folded line turns a long summary into nonsense, an exclusive DTEND
// read inclusively makes every all-day event a day too long, and a VALARM
// inside a VEVENT ends the event early if the first END: is trusted.

#include "cal/IcsCodec.h"
#include "cal/IcsSubscription.h"
#include "cal/Occurrences.h"
#include "cal/OutlookDesktop.h"

#include <gtest/gtest.h>

using heap::cal::IcsImport;
using heap::cal::parseIcs;
using heap::cal::toIcs;

namespace {

// A minimal document around one VEVENT body.
QString wrap(const QString& body) {
  return QStringLiteral("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\n") + body +
         QStringLiteral("\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
}

CalEvent one(const QString& body) {
  const IcsImport in = parseIcs(wrap(body));
  return in.events.isEmpty() ? CalEvent{} : in.events.first();
}

}  // namespace

// ── Line structure ──

TEST(Ics, AFoldedLineIsJoinedBeforeItIsRead) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nSUMMARY:A very long title that\r\n  continues here"));

  EXPECT_EQ(e.title, QStringLiteral("A very long title that continues here"));
}

TEST(Ics, ATabContinuesALineToo) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nSUMMARY:split\r\n\there"));

  EXPECT_EQ(e.title, QStringLiteral("splithere"));
}

// Plenty of files in the wild use bare LF.
TEST(Ics, BareLineFeedsWork) {
  const IcsImport in =
      parseIcs(QStringLiteral("BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:a\nDTSTART:20260921T100000\nSUMMARY:x\nEND:VEVENT\nEND:VCALENDAR\n"));

  ASSERT_EQ(in.events.size(), 1);
  EXPECT_EQ(in.events.first().title, QStringLiteral("x"));
}

TEST(Ics, EscapesAreUndoneOnRead) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nSUMMARY:Standup\\, daily\\; and long\\nsecond line"));

  EXPECT_EQ(e.title, QStringLiteral("Standup, daily; and long\nsecond line"));
}

TEST(Ics, AParameterIsNotMistakenForTheValue) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART;VALUE=DATE:20260921\r\nSUMMARY:x"));

  EXPECT_TRUE(e.allDay);
  EXPECT_EQ(e.date, QDate(2026, 9, 21));
}

// A colon inside a quoted parameter does not end the name part.
TEST(Ics, AColonInsideAQuotedParameterIsNotTheSeparator) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART;TZID=\"Weird:Zone\":20260921T100000\r\nSUMMARY:x"));

  EXPECT_EQ(e.date, QDate(2026, 9, 21));
  EXPECT_NEAR(e.start, 10.0, 0.001);
}

// ── Times ──

TEST(Ics, AFloatingTimeIsTakenAsWritten) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART:20260921T093000\r\nDTEND:20260921T100000\r\nSUMMARY:x"));

  EXPECT_EQ(e.date, QDate(2026, 9, 21));
  EXPECT_NEAR(e.start, 9.5, 0.001);
  EXPECT_NEAR(e.end, 10.0, 0.001);
}

// A UTC stamp shown at its literal hour would put a 14:00Z meeting at 14:00
// everywhere on earth.
TEST(Ics, AUtcTimeIsConvertedToLocal) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART:20260921T120000Z\r\nDTEND:20260921T130000Z\r\nSUMMARY:x"));

  const QDateTime expected = QDateTime(QDate(2026, 9, 21), QTime(12, 0), QTimeZone::utc()).toLocalTime();
  EXPECT_EQ(e.date, expected.date());
  EXPECT_NEAR(e.start, expected.time().hour() + expected.time().minute() / 60.0, 0.001);
}

// An unknown zone is floating local rather than dropped: better to show the
// meeting at the hour written down than to lose it.
TEST(Ics, AnUnknownTimeZoneFallsBackToTheWrittenHour) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART;TZID=Mars/Olympus:20260921T100000\r\nSUMMARY:x"));

  EXPECT_EQ(e.date, QDate(2026, 9, 21));
  EXPECT_NEAR(e.start, 10.0, 0.001);
}

TEST(Ics, AMissingEndMeansAnHour) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nSUMMARY:x"));

  EXPECT_NEAR(e.start, 10.0, 0.001);
  EXPECT_NEAR(e.end, 11.0, 0.001);
  EXPECT_FALSE(e.endDate.isValid());
}

// The whole reason spans exist: a DTEND on the following day is an event that
// crosses midnight, not a broken one.
TEST(Ics, ATimedEventMayCrossMidnight) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART:20260921T220000\r\nDTEND:20260922T020000\r\nSUMMARY:x"));

  EXPECT_EQ(e.date, QDate(2026, 9, 21));
  EXPECT_EQ(e.endDate, QDate(2026, 9, 22));
  EXPECT_NEAR(e.start, 22.0, 0.001);
  EXPECT_NEAR(e.end, 2.0, 0.001);
}

// ── DURATION ──

TEST(Ics, ADurationInHoursIsUnderstood) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nDURATION:PT2H\r\nSUMMARY:x"));

  EXPECT_NEAR(e.end, 12.0, 0.001);
}

TEST(Ics, ADurationInMinutesIsUnderstood) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nDURATION:PT30M\r\nSUMMARY:x"));

  EXPECT_NEAR(e.end, 10.5, 0.001);
}

TEST(Ics, AMixedDurationIsUnderstood) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nDURATION:PT1H30M\r\nSUMMARY:x"));

  EXPECT_NEAR(e.end, 11.5, 0.001);
}

TEST(Ics, ADurationMayCrossMidnight) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART:20260921T230000\r\nDURATION:PT3H\r\nSUMMARY:x"));

  EXPECT_EQ(e.endDate, QDate(2026, 9, 22));
  EXPECT_NEAR(e.end, 2.0, 0.001);
}

// Both present is a contradiction the spec forbids; DTEND is the more explicit
// of the two, so it wins rather than the file being rejected.
TEST(Ics, DtEndBeatsDuration) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nDTEND:20260921T103000\r\nDURATION:PT5H\r\nSUMMARY:x"));

  EXPECT_NEAR(e.end, 10.5, 0.001);
}

// ── All-day ──

TEST(Ics, AnAllDayEventHasNoHours) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART;VALUE=DATE:20260921\r\nSUMMARY:holiday"));

  EXPECT_TRUE(e.allDay);
  EXPECT_NEAR(e.start, 0.0, 0.001);
  EXPECT_NEAR(e.end, 24.0, 0.001);
}

// The classic .ics bug. DTEND is exclusive for a DATE value, so 21 to 24 is
// three days ending on the 23rd. Reading it inclusively makes every imported
// all-day event one day too long.
TEST(Ics, AnAllDayEndDateIsExclusive) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART;VALUE=DATE:20260921\r\nDTEND;VALUE=DATE:20260924\r\nSUMMARY:trip"));

  EXPECT_EQ(e.date, QDate(2026, 9, 21));
  EXPECT_EQ(e.endDate, QDate(2026, 9, 23));
}

TEST(Ics, AOneDayAllDayEventHasNoEndDate) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART;VALUE=DATE:20260921\r\nDTEND;VALUE=DATE:20260922\r\nSUMMARY:x"));

  EXPECT_FALSE(e.endDate.isValid());
}

// ── Recurrence ──

TEST(Ics, ARecurrenceRuleIsKept) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nRRULE:FREQ=WEEKLY;BYDAY=MO\r\nSUMMARY:x"));

  EXPECT_EQ(e.rrule, QStringLiteral("FREQ=WEEKLY;BYDAY=MO"));
}

// A rule heap does not model must not take the event down with it: dropping it
// would hide something the user put in their calendar.
TEST(Ics, AnUnsupportedRuleImportsASingleEventAndWarns) {
  const IcsImport in = parseIcs(wrap(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nRRULE:FREQ=SECONDLY\r\nSUMMARY:x")));

  ASSERT_EQ(in.events.size(), 1);
  EXPECT_TRUE(in.events.first().rrule.isEmpty());
  EXPECT_FALSE(in.warnings.isEmpty());
}

TEST(Ics, ExdatesOnOneLineAreAllRead) {
  const CalEvent e =
      one(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nRRULE:FREQ=WEEKLY\r\nEXDATE:20260928T100000,20261005T100000\r\nSUMMARY:x"));

  ASSERT_EQ(e.exdates.size(), 2);
  EXPECT_EQ(e.exdates.at(0), QDate(2026, 9, 28));
  EXPECT_EQ(e.exdates.at(1), QDate(2026, 10, 5));
}

TEST(Ics, SeveralExdateLinesAccumulate) {
  const CalEvent e = one(QStringLiteral(
      "UID:a\r\nDTSTART:20260921T100000\r\nRRULE:FREQ=WEEKLY\r\nEXDATE:20260928T100000\r\nEXDATE:20261005T100000\r\nSUMMARY:x"));

  EXPECT_EQ(e.exdates.size(), 2);
}

// An override names the occurrence it replaces and points at the master, which
// is the event sharing its UID.
TEST(Ics, ARecurrenceIdBecomesAnOverride) {
  const CalEvent e = one(QStringLiteral("UID:series-1\r\nRECURRENCE-ID:20260928T100000\r\nDTSTART:20260930T100000\r\nSUMMARY:moved"));

  EXPECT_EQ(e.masterId, QStringLiteral("series-1"));
  EXPECT_EQ(e.originalDate, QDate(2026, 9, 28));
  EXPECT_NE(e.id, QStringLiteral("series-1")) << "an override cannot share the master's id";
}

// ── Structure and robustness ──

TEST(Ics, ACancelledEventIsSkipped) {
  const IcsImport in = parseIcs(wrap(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nSTATUS:CANCELLED\r\nSUMMARY:x")));

  EXPECT_TRUE(in.events.isEmpty());
  EXPECT_EQ(in.skipped, 1);
}

// The first END: inside a VEVENT belongs to the VALARM, not the event.
TEST(Ics, AnAlarmInsideAnEventDoesNotEndIt) {
  const CalEvent e = one(QStringLiteral(
      "UID:a\r\nDTSTART:20260921T100000\r\nBEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT10M\r\nEND:VALARM\r\nSUMMARY:after the alarm"));

  EXPECT_EQ(e.title, QStringLiteral("after the alarm"));
}

TEST(Ics, TimezoneBlocksDoNotBecomeEvents) {
  const QString doc = QStringLiteral(
      "BEGIN:VCALENDAR\r\nBEGIN:VTIMEZONE\r\nTZID:Europe/Moscow\r\nBEGIN:STANDARD\r\nDTSTART:19700101T000000\r\n"
      "END:STANDARD\r\nEND:VTIMEZONE\r\nBEGIN:VEVENT\r\nUID:a\r\nDTSTART:20260921T100000\r\nSUMMARY:x\r\n"
      "END:VEVENT\r\nEND:VCALENDAR\r\n");

  const IcsImport in = parseIcs(doc);

  ASSERT_EQ(in.events.size(), 1);
  EXPECT_EQ(in.events.first().title, QStringLiteral("x"));
}

TEST(Ics, ATodoIsNotAnEvent) {
  const QString doc = QStringLiteral(
      "BEGIN:VCALENDAR\r\nBEGIN:VTODO\r\nUID:t\r\nDTSTART:20260921T100000\r\nSUMMARY:a task\r\nEND:VTODO\r\n"
      "END:VCALENDAR\r\n");

  EXPECT_TRUE(parseIcs(doc).events.isEmpty());
}

TEST(Ics, AnEventWithNoStartIsSkipped) {
  const IcsImport in = parseIcs(wrap(QStringLiteral("UID:a\r\nSUMMARY:when?")));

  EXPECT_TRUE(in.events.isEmpty());
  EXPECT_EQ(in.skipped, 1);
}

// A truncated download is a truncated file, not an event.
TEST(Ics, AnUnterminatedEventIsSkipped) {
  const IcsImport in = parseIcs(QStringLiteral("BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:a\r\nDTSTART:20260921T100000\r\n"));

  EXPECT_TRUE(in.events.isEmpty());
  EXPECT_EQ(in.skipped, 1);
}

TEST(Ics, GarbageParsesToNothing) {
  EXPECT_TRUE(parseIcs(QStringLiteral("not a calendar at all")).events.isEmpty());
  EXPECT_TRUE(parseIcs(QString()).events.isEmpty());
  EXPECT_TRUE(parseIcs(QStringLiteral(":::\r\n;;;\r\nBEGIN:\r\nEND:")).events.isEmpty());
}

// Two events with no UID must not collide, or importing a file would silently
// keep one of them.
TEST(Ics, EventsWithoutUidsGetDistinctIds) {
  const QString doc = QStringLiteral(
      "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART:20260921T100000\r\nSUMMARY:one\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nDTSTART:20260922T100000\r\nSUMMARY:two\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");

  const IcsImport in = parseIcs(doc);

  ASSERT_EQ(in.events.size(), 2);
  EXPECT_NE(in.events.at(0).id, in.events.at(1).id);
}

TEST(Ics, LocationIsTheLocation) {
  const CalEvent e = one(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nLOCATION:Room 3\r\nSUMMARY:x"));

  EXPECT_EQ(e.location, QStringLiteral("Room 3"));
  EXPECT_TRUE(e.context.isEmpty()) << "context is heap's own label, not the room";
}

// Events carry notes now; a description is kept, not warned about.
TEST(Ics, DescriptionBecomesTheNotes) {
  const IcsImport in = parseIcs(wrap(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nDESCRIPTION:line one\\nline two\r\nSUMMARY:one")));

  ASSERT_EQ(in.events.size(), 1);
  EXPECT_EQ(in.events.first().notes, QStringLiteral("line one\nline two"));
  EXPECT_TRUE(in.warnings.isEmpty());
}

// ── Writing ──

namespace {

CalEvent timed() {
  CalEvent e;
  e.id = QStringLiteral("ev-1");
  e.title = QStringLiteral("Design review");
  e.date = QDate(2026, 9, 21);
  e.start = 14.0;
  e.end = 15.5;
  return e;
}

}  // namespace

TEST(Ics, AWrittenDocumentIsWellFormed) {
  const QString doc = toIcs({timed()});

  EXPECT_TRUE(doc.startsWith(QStringLiteral("BEGIN:VCALENDAR")));
  EXPECT_TRUE(doc.contains(QStringLiteral("VERSION:2.0")));
  EXPECT_TRUE(doc.contains(QStringLiteral("BEGIN:VEVENT")));
  EXPECT_TRUE(doc.trimmed().endsWith(QStringLiteral("END:VCALENDAR")));
  EXPECT_TRUE(doc.contains(QStringLiteral("\r\n"))) << "CRLF, per the spec";
}

TEST(Ics, ALongSummaryIsFolded) {
  CalEvent e = timed();
  e.title = QString(200, QLatin1Char('x'));

  for(const QString& line : toIcs({e}).split(QStringLiteral("\r\n"))) {
    EXPECT_LE(line.toUtf8().size(), 75) << line.toStdString();
  }
}

// Folding is by octet, so a Cyrillic summary must not be split mid-character.
TEST(Ics, AFoldedNonAsciiSummarySurvives) {
  CalEvent e = timed();
  e.title = QString(60, QChar(0x0434));  // "д" repeated

  const CalEvent back = parseIcs(toIcs({e})).events.value(0);

  EXPECT_EQ(back.title, e.title);
}

TEST(Ics, AnAllDayEventIsWrittenWithAnExclusiveEnd) {
  CalEvent e = timed();
  e.allDay = true;
  e.start = 0;
  e.end = 24;
  e.endDate = QDate(2026, 9, 23);

  const QString doc = toIcs({e});

  EXPECT_TRUE(doc.contains(QStringLiteral("DTSTART;VALUE=DATE:20260921")));
  EXPECT_TRUE(doc.contains(QStringLiteral("DTEND;VALUE=DATE:20260924"))) << doc.toStdString();
}

// ── Round trips ──

TEST(Ics, AnOrdinaryEventSurvivesARoundTrip) {
  const CalEvent e = timed();

  const CalEvent back = parseIcs(toIcs({e})).events.value(0);

  EXPECT_EQ(back.id, e.id);
  EXPECT_EQ(back.title, e.title);
  EXPECT_EQ(back.date, e.date);
  EXPECT_NEAR(back.start, e.start, 0.001);
  EXPECT_NEAR(back.end, e.end, 0.001);
  EXPECT_FALSE(back.allDay);
}

TEST(Ics, AnAllDayEventSurvivesARoundTrip) {
  CalEvent e = timed();
  e.allDay = true;
  e.start = 0;
  e.end = 24;

  const CalEvent back = parseIcs(toIcs({e})).events.value(0);

  EXPECT_TRUE(back.allDay);
  EXPECT_EQ(back.date, e.date);
  EXPECT_FALSE(back.endDate.isValid());
}

TEST(Ics, AMultiDayAllDayEventSurvivesARoundTrip) {
  CalEvent e = timed();
  e.allDay = true;
  e.start = 0;
  e.end = 24;
  e.endDate = QDate(2026, 9, 25);

  const CalEvent back = parseIcs(toIcs({e})).events.value(0);

  EXPECT_TRUE(back.allDay);
  EXPECT_EQ(back.date, e.date);
  EXPECT_EQ(back.endDate, e.endDate);
}

TEST(Ics, ACrossMidnightEventSurvivesARoundTrip) {
  CalEvent e = timed();
  e.start = 22.0;
  e.end = 2.0;
  e.endDate = QDate(2026, 9, 22);

  const CalEvent back = parseIcs(toIcs({e})).events.value(0);

  EXPECT_EQ(back.date, e.date);
  EXPECT_EQ(back.endDate, e.endDate);
  EXPECT_NEAR(back.start, 22.0, 0.001);
  EXPECT_NEAR(back.end, 2.0, 0.001);
}

TEST(Ics, ARecurringEventSurvivesARoundTrip) {
  CalEvent e = timed();
  e.rrule = QStringLiteral("FREQ=WEEKLY;INTERVAL=2");
  e.exdates = {QDate(2026, 10, 5)};

  const CalEvent back = parseIcs(toIcs({e})).events.value(0);

  EXPECT_EQ(back.rrule, e.rrule);
  ASSERT_EQ(back.exdates.size(), 1);
  EXPECT_EQ(back.exdates.at(0), QDate(2026, 10, 5));
}

// B7: the interval repeats a meeting is made from ("every 3 weeks", "every
// mon, wed", "every other week on mon, thu") go out and come back as said.
TEST(Ics, IntervalAndDayListRulesSurviveARoundTrip) {
  for(const char* rule : {"FREQ=DAILY;INTERVAL=2",
                          "FREQ=WEEKLY;INTERVAL=3",
                          "FREQ=WEEKLY;BYDAY=MO,WE",
                          "FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,TH",
                          "FREQ=MONTHLY;INTERVAL=2;BYMONTHDAY=15"}) {
    CalEvent e = timed();
    e.rrule = QString::fromLatin1(rule);
    const IcsImport in = parseIcs(toIcs({e}));
    ASSERT_EQ(in.events.size(), 1) << rule;
    EXPECT_EQ(in.events.at(0).rrule, e.rrule) << rule;
    EXPECT_TRUE(in.warnings.isEmpty()) << rule;
  }
}

// A title with the characters the format uses for its own punctuation.
TEST(Ics, APunctuatedTitleSurvivesARoundTrip) {
  CalEvent e = timed();
  e.title = QStringLiteral("Review: schema, rank; and \\paths\\");

  EXPECT_EQ(parseIcs(toIcs({e})).events.value(0).title, e.title);
}

TEST(Ics, AnEmptyListWritesAnEmptyCalendar) {
  const IcsImport back = parseIcs(toIcs({}));

  EXPECT_TRUE(back.events.isEmpty());
  EXPECT_EQ(back.skipped, 0);
}

// Importing the same file twice is the commonest thing anyone does with one,
// and the UID is what makes it recognisable as a duplicate rather than a
// second copy.
TEST(Ics, TheSameFileTwiceYieldsTheSameIds) {
  const QString doc = toIcs({timed()});

  EXPECT_EQ(parseIcs(doc).events.value(0).id, parseIcs(doc).events.value(0).id);
}

// ── Time zones (audit TIME-6) ──
//
// Every case names the viewer's zone explicitly, so the result does not
// depend on where the test runs.

namespace {

const QTimeZone kMsk("Europe/Moscow");
const QTimeZone kNy("America/New_York");

double hourAt(const heap::cal::Occurrence& o) {
  return o.event.start;
}

// US Eastern, as Outlook describes it under a name no zone database knows.
const char* kCustomEastern =
    "BEGIN:VTIMEZONE\r\nTZID:Customized Time Zone\r\n"
    "BEGIN:STANDARD\r\nDTSTART:16010101T020000\r\nTZOFFSETFROM:-0400\r\nTZOFFSETTO:-0500\r\n"
    "RRULE:FREQ=YEARLY;BYDAY=1SU;BYMONTH=11\r\nEND:STANDARD\r\n"
    "BEGIN:DAYLIGHT\r\nDTSTART:16010101T020000\r\nTZOFFSETFROM:-0500\r\nTZOFFSETTO:-0400\r\n"
    "RRULE:FREQ=YEARLY;BYDAY=2SU;BYMONTH=3\r\nEND:DAYLIGHT\r\nEND:VTIMEZONE\r\n";

QString calendar(const QString& inner) {
  return QStringLiteral("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n") + inner + QStringLiteral("END:VCALENDAR\r\n");
}

}  // namespace

TEST(IcsZones, AWindowsZoneNameIsResolved) {
  const IcsImport in = parseIcs(wrap(QStringLiteral("UID:a\r\nDTSTART;TZID=Eastern Standard Time:20261005T100000\r\n"
                                                    "DTEND;TZID=Eastern Standard Time:20261005T110000\r\nSUMMARY:x")),
                                kMsk);
  ASSERT_EQ(in.events.size(), 1);
  // A single meeting in another zone stays in it (F1-new-15): 10:00 New York,
  // which the calendar shows at 17:00 Moscow.
  EXPECT_EQ(in.events.first().tz, QStringLiteral("America/New_York"));
  EXPECT_NEAR(in.events.first().start, 10.0, 0.001);
  const auto occ = heap::cal::expandEvents(in.events, QDate(2026, 10, 5), QDate(2026, 10, 5), nullptr, kMsk);
  ASSERT_EQ(occ.size(), 1);
  EXPECT_NEAR(hourAt(occ.first()), 17.0, 0.001) << "10:00 EDT is 17:00 in Moscow";
}

// A single meeting written in the viewer's own zone is just converted.
TEST(IcsZones, ASingleEventInTheViewersZoneIsNotPinned) {
  const IcsImport in = parseIcs(wrap(QStringLiteral("UID:a\r\nDTSTART;TZID=Europe/Moscow:20261005T100000\r\n"
                                                    "DTEND;TZID=Europe/Moscow:20261005T110000\r\nSUMMARY:x")),
                                kMsk);
  ASSERT_EQ(in.events.size(), 1);
  EXPECT_TRUE(in.events.first().tz.isEmpty());
  EXPECT_NEAR(in.events.first().start, 10.0, 0.001);
}

TEST(IcsZones, AnUnknownNameIsReadThroughItsVtimezone) {
  const IcsImport in = parseIcs(calendar(QString::fromLatin1(kCustomEastern) +
                                         QStringLiteral("BEGIN:VEVENT\r\nUID:a\r\nDTSTART;TZID=Customized Time Zone:20261215T100000\r\n"
                                                        "DTEND;TZID=Customized Time Zone:20261215T110000\r\nSUMMARY:x\r\nEND:VEVENT\r\n")),
                                kMsk);
  ASSERT_EQ(in.events.size(), 1);
  const auto occ = heap::cal::expandEvents(in.events, QDate(2026, 12, 15), QDate(2026, 12, 15), nullptr, kMsk);
  ASSERT_EQ(occ.size(), 1);
  EXPECT_NEAR(hourAt(occ.first()), 18.0, 0.001) << "10:00 EST is 18:00 in Moscow";
}

// A New York series follows New York's DST: 17:00 Moscow in October, 18:00
// after the US clocks go back on Nov 1.
TEST(IcsZones, ARecurringSeriesKeepsItsSourceZone) {
  const IcsImport in = parseIcs(wrap(QStringLiteral("UID:ny\r\nDTSTART;TZID=America/New_York:20261026T100000\r\n"
                                                    "DTEND;TZID=America/New_York:20261026T110000\r\nRRULE:FREQ=DAILY\r\nSUMMARY:x")),
                                kMsk);
  ASSERT_EQ(in.events.size(), 1);
  EXPECT_EQ(in.events.first().tz, QStringLiteral("America/New_York"));
  const auto occ = heap::cal::expandEvents(in.events, QDate(2026, 10, 30), QDate(2026, 11, 3), nullptr, kMsk);
  ASSERT_EQ(occ.size(), 5);
  EXPECT_NEAR(hourAt(occ.first()), 17.0, 0.001);
  EXPECT_NEAR(hourAt(occ.last()), 18.0, 0.001);
}

// Tuesday 20:00 in New York is Wednesday 03:00 in Moscow — including the first
// occurrence, which used to go missing.
TEST(IcsZones, AnEveningSeriesLandsOnTheViewersNextDay) {
  const IcsImport in = parseIcs(wrap(QStringLiteral("UID:ny\r\nDTSTART;TZID=America/New_York:20261006T200000\r\n"
                                                    "DTEND;TZID=America/New_York:20261006T210000\r\n"
                                                    "RRULE:FREQ=WEEKLY;BYDAY=TU\r\nSUMMARY:x")),
                                kMsk);
  const auto occ = heap::cal::expandEvents(in.events, QDate(2026, 10, 1), QDate(2026, 10, 21), nullptr, kMsk);
  ASSERT_EQ(occ.size(), 3);
  EXPECT_EQ(occ.first().event.date, QDate(2026, 10, 7));
  EXPECT_EQ(occ.first().event.date.dayOfWeek(), 3);
  EXPECT_NEAR(hourAt(occ.first()), 3.0, 0.001);
  EXPECT_EQ(occ.first().occurrenceDate, QDate(2026, 10, 6)) << "keyed by the source date";
}

// The other direction: a Moscow standup seen from New York moves when New
// York's clocks change, because Moscow's do not.
TEST(IcsZones, ASeriesMovesWhenOnlyTheViewersClockChanges) {
  const IcsImport in = parseIcs(wrap(QStringLiteral("UID:msk\r\nDTSTART;TZID=Europe/Moscow:20261026T093000\r\n"
                                                    "DTEND;TZID=Europe/Moscow:20261026T094500\r\nRRULE:FREQ=DAILY\r\nSUMMARY:x")),
                                kNy);
  const auto occ = heap::cal::expandEvents(in.events, QDate(2026, 10, 30), QDate(2026, 11, 2), nullptr, kNy);
  ASSERT_EQ(occ.size(), 4);
  EXPECT_NEAR(hourAt(occ.first()), 2.5, 0.001) << "09:30 MSK = 02:30 EDT";
  EXPECT_NEAR(hourAt(occ.last()), 1.5, 0.001) << "09:30 MSK = 01:30 EST";
}

TEST(IcsZones, AVtimezoneSeriesIsMatchedToARealZone) {
  const IcsImport in = parseIcs(calendar(QString::fromLatin1(kCustomEastern) +
                                         QStringLiteral("BEGIN:VEVENT\r\nUID:a\r\nDTSTART;TZID=Customized Time Zone:20261026T100000\r\n"
                                                        "DTEND;TZID=Customized Time Zone:20261026T110000\r\nRRULE:FREQ=DAILY\r\n"
                                                        "SUMMARY:x\r\nEND:VEVENT\r\n")),
                                kMsk);
  ASSERT_EQ(in.events.size(), 1);
  ASSERT_FALSE(in.events.first().tz.isEmpty());
  const auto occ = heap::cal::expandEvents(in.events, QDate(2026, 10, 30), QDate(2026, 11, 3), nullptr, kMsk);
  ASSERT_EQ(occ.size(), 5);
  EXPECT_NEAR(hourAt(occ.first()), 17.0, 0.001);
  EXPECT_NEAR(hourAt(occ.last()), 18.0, 0.001);
}

TEST(IcsZones, AUtcRecurrenceIdNamesTheSeriesOwnDate) {
  const IcsImport in =
      parseIcs(calendar(QStringLiteral("BEGIN:VEVENT\r\nUID:ny\r\nDTSTART;TZID=America/New_York:20261006T200000\r\n"
                                       "DTEND;TZID=America/New_York:20261006T210000\r\nRRULE:FREQ=WEEKLY\r\nSUMMARY:x\r\nEND:VEVENT\r\n"
                                       "BEGIN:VEVENT\r\nUID:ny\r\nRECURRENCE-ID:20261014T000000Z\r\n"
                                       "DTSTART;TZID=America/New_York:20261013T190000\r\nDTEND;TZID=America/New_York:20261013T200000\r\n"
                                       "SUMMARY:moved\r\nEND:VEVENT\r\n")),
               kMsk);
  ASSERT_EQ(in.events.size(), 2);
  EXPECT_EQ(in.events.at(1).originalDate, QDate(2026, 10, 13)) << "00:00Z on the 14th is the 13th in New York";
}

// ── Import details (audit TIME-18, TIME-26, TIME-32) ──

TEST(Ics, AttendeesAreRead) {
  const CalEvent e =
      one(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nATTENDEE;CN=Ann Lee:mailto:ann@x.io\r\n"
                         "ATTENDEE:mailto:bob@x.io\r\nATTENDEE;CN=\"Oleg\":invalid:nomail\r\nSUMMARY:x"));
  EXPECT_EQ(e.attendees, QStringLiteral("Ann Lee, bob@x.io, Oleg"));
}

TEST(Ics, AnAlarmBecomesTheEventsReminder) {
  const CalEvent e = one(
      QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nBEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\nSUMMARY:x"));
  EXPECT_EQ(e.reminderMinutes, 15);
}

TEST(Ics, AnEventWithoutAUidGetsTheSameIdEveryTime) {
  const QString doc = wrap(QStringLiteral("DTSTART:20260921T100000\r\nSUMMARY:no uid"));
  EXPECT_EQ(parseIcs(doc).events.value(0).id, parseIcs(doc).events.value(0).id);
}

TEST(Ics, TextThatIsNotACalendarIsNotRecognised) {
  EXPECT_FALSE(parseIcs(QStringLiteral("hello, world")).recognised);
  EXPECT_TRUE(parseIcs(wrap(QStringLiteral("UID:a\r\nDTSTART:20260921T100000"))).recognised);
}

TEST(Ics, AnUnsupportedRuleSaysWhichPart) {
  const IcsImport in = parseIcs(wrap(QStringLiteral("UID:a\r\nDTSTART:20260921T100000\r\nRRULE:FREQ=YEARLY;BYWEEKNO=20\r\nSUMMARY:x")));
  ASSERT_EQ(in.warnings.size(), 1);
  EXPECT_TRUE(in.warnings.first().contains(QStringLiteral("BYWEEKNO")));
}

// ── Export details (audit TIME-18, TIME-19) ──

TEST(Ics, ExportCarriesTheStampZoneAndEverythingHeapKnows) {
  CalEvent e = timed();
  e.type = QStringLiteral("focus");
  e.attendees = QStringLiteral("Ann, bob@x.io");
  e.notes = QStringLiteral("agenda");
  e.url = QStringLiteral("https://meet.example/x");
  e.location = QStringLiteral("Room 1");
  e.reminderMinutes = 10;
  const QString doc = toIcs({e}, kMsk, QDateTime(QDate(2026, 9, 1), QTime(8, 0), QTimeZone::utc()));
  EXPECT_TRUE(doc.contains(QStringLiteral("DTSTAMP:20260901T080000Z")));
  EXPECT_TRUE(doc.contains(QStringLiteral("DTSTART:20260921T110000Z"))) << "14:00 Moscow, written in UTC";
  EXPECT_TRUE(doc.contains(QStringLiteral("ATTENDEE:mailto:bob@x.io")));

  const CalEvent back = parseIcs(doc, kMsk).events.value(0);
  EXPECT_EQ(back.type, QStringLiteral("focus"));
  EXPECT_EQ(back.attendees, QStringLiteral("Ann, bob@x.io"));
  EXPECT_EQ(back.notes, e.notes);
  EXPECT_EQ(back.url, e.url);
  EXPECT_EQ(back.location, e.location);
  EXPECT_EQ(back.reminderMinutes, 10);
  EXPECT_NEAR(back.start, 14.0, 0.001);
}

TEST(Ics, ExportWritesASeriesInANamedZone) {
  CalEvent e = timed();
  e.rrule = QStringLiteral("FREQ=WEEKLY");
  e.tz = QStringLiteral("America/New_York");
  const QString doc = toIcs({e}, kMsk);
  EXPECT_TRUE(doc.contains(QStringLiteral("DTSTART;TZID=America/New_York:20260921T140000")));
  EXPECT_TRUE(doc.contains(QStringLiteral("BEGIN:VTIMEZONE")));
  EXPECT_TRUE(doc.contains(QStringLiteral("TZID:America/New_York")));
  EXPECT_EQ(parseIcs(doc, kMsk).events.value(0).tz, QStringLiteral("America/New_York"));
}

// F1-new-1: a fixed-offset zone's id has a colon. Unquoted, it ended the
// parameter list and the meeting was skipped on import.
TEST(Ics, AFixedOffsetZoneIsQuotedAndComesBack) {
  CalEvent e = timed();
  e.title = QStringLiteral("Team sync");
  e.rrule = QStringLiteral("FREQ=WEEKLY;INTERVAL=2;BYDAY=MO");
  e.tz = QStringLiteral("UTC-05:00");
  e.exdates = {QDate(2026, 10, 5)};
  CalEvent single = timed();
  single.id = QStringLiteral("ev-2");
  single.tz = QStringLiteral("UTC+05:00");
  const QString doc = toIcs({e, single}, kMsk);
  EXPECT_TRUE(doc.contains(QStringLiteral("DTSTART;TZID=\"UTC-05:00\":20260921T140000"))) << doc.toStdString();
  EXPECT_TRUE(doc.contains(QStringLiteral("EXDATE;TZID=\"UTC-05:00\":20261005T140000"))) << doc.toStdString();
  EXPECT_FALSE(doc.contains(QStringLiteral("TZID=UTC-05:00:"))) << doc.toStdString();

  const IcsImport back = parseIcs(doc, kMsk);
  ASSERT_EQ(back.events.size(), 2) << "nothing skipped";
  const QDateTime at(QDate(2026, 9, 21), QTime(0, 0), QTimeZone::utc());
  const CalEvent& s = back.events.at(0);
  EXPECT_EQ(s.title, QStringLiteral("Team sync"));
  EXPECT_EQ(s.rrule, e.rrule);
  EXPECT_EQ(QTimeZone(s.tz.toUtf8()).offsetFromUtc(at), -5 * 3600);
  EXPECT_NEAR(s.start, 14.0, 0.001);
  EXPECT_EQ(s.exdates, QList<QDate>{QDate(2026, 10, 5)});
  EXPECT_EQ(QTimeZone(back.events.at(1).tz.toUtf8()).offsetFromUtc(at), 5 * 3600);
  EXPECT_NEAR(back.events.at(1).start, 14.0, 0.001);
}

// F1-new-15: a floating series stays floating through a round trip instead of
// coming back pinned to the exporting machine's zone.
TEST(Ics, AFloatingSeriesGoesOutAndComesBackFloating) {
  CalEvent e = timed();
  e.rrule = QStringLiteral("FREQ=WEEKLY;INTERVAL=2");
  e.exdates = {QDate(2026, 10, 5)};
  CalEvent ov = timed();
  ov.id = QStringLiteral("ov-1");
  ov.masterId = e.id;
  ov.originalDate = QDate(2026, 10, 19);
  ov.date = QDate(2026, 10, 19);
  ov.start = 16.0;
  ov.end = 17.0;
  const QString doc = toIcs({e, ov}, kMsk);
  EXPECT_TRUE(doc.contains(QStringLiteral("DTSTART:20260921T140000\r\n"))) << doc.toStdString();
  EXPECT_TRUE(doc.contains(QStringLiteral("EXDATE:20261005T140000\r\n"))) << doc.toStdString();
  EXPECT_TRUE(doc.contains(QStringLiteral("RECURRENCE-ID:20261019T140000\r\n"))) << doc.toStdString();
  EXPECT_FALSE(doc.contains(QStringLiteral("VTIMEZONE")));

  const IcsImport back = parseIcs(doc, kNy);
  ASSERT_EQ(back.events.size(), 2);
  EXPECT_TRUE(back.events.at(0).tz.isEmpty());
  EXPECT_NEAR(back.events.at(0).start, 14.0, 0.001) << "the same wall time anywhere";
  EXPECT_EQ(back.events.at(0).exdates, QList<QDate>{QDate(2026, 10, 5)});
  EXPECT_EQ(back.events.at(1).originalDate, QDate(2026, 10, 19));
}

// F1-new-15: a single New York meeting comes back a New York meeting.
TEST(Ics, ASingleMeetingKeepsItsZoneThroughARoundTrip) {
  CalEvent e = timed();
  e.tz = QStringLiteral("America/New_York");
  const IcsImport back = parseIcs(toIcs({e}, kMsk), kMsk);
  ASSERT_EQ(back.events.size(), 1);
  EXPECT_EQ(back.events.first().tz, QStringLiteral("America/New_York"));
  EXPECT_NEAR(back.events.first().start, 14.0, 0.001);
}

TEST(Ics, AMovedOccurrenceIsNamedByItsOriginalStart) {
  CalEvent m = timed();
  m.rrule = QStringLiteral("FREQ=WEEKLY");
  m.tz = QStringLiteral("Europe/Moscow");
  CalEvent ov = timed();
  ov.id = QStringLiteral("ov-1");
  ov.masterId = m.id;
  ov.originalDate = QDate(2026, 9, 28);
  ov.date = QDate(2026, 9, 29);
  ov.start = 16.0;
  ov.end = 17.0;
  const QString doc = toIcs({m, ov}, kMsk);
  EXPECT_TRUE(doc.contains(QStringLiteral("RECURRENCE-ID;TZID=Europe/Moscow:20260928T140000"))) << doc.toStdString();
}

TEST(Ics, AMidnightEndIsTheNextDay) {
  CalEvent e = timed();
  e.rrule = QStringLiteral("FREQ=DAILY");
  e.tz = QStringLiteral("Europe/Moscow");
  e.start = 23.0;
  e.end = 24.0;
  const QString doc = toIcs({e}, kMsk);
  EXPECT_TRUE(doc.contains(QStringLiteral("DTEND;TZID=Europe/Moscow:20260922T000000"))) << doc.toStdString();
}

// ── TIME-9 (audit 2026-09-30): a cancelled occurrence is a deletion ──

// How Google and CalDAV cancel one meeting of a series: a second VEVENT with
// the series' UID, the occurrence's RECURRENCE-ID and STATUS:CANCELLED.
TEST(Ics, ACancelledOccurrenceBecomesAnExdate) {
  const QTimeZone berlin("Europe/Berlin");
  const IcsImport in = parseIcs(QStringLiteral("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                                               "BEGIN:VEVENT\r\nUID:weekly@google.com\r\nSUMMARY:Planning\r\n"
                                               "DTSTART;TZID=Europe/Berlin:20360505T110000\r\n"
                                               "DTEND;TZID=Europe/Berlin:20360505T120000\r\nRRULE:FREQ=WEEKLY;BYDAY=MO\r\nEND:VEVENT\r\n"
                                               "BEGIN:VEVENT\r\nUID:weekly@google.com\r\nSTATUS:CANCELLED\r\n"
                                               "RECURRENCE-ID;TZID=Europe/Berlin:20360526T110000\r\n"
                                               "DTSTART;TZID=Europe/Berlin:20360526T110000\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n"),
                                berlin);

  ASSERT_EQ(in.events.size(), 1);
  EXPECT_EQ(in.skipped, 0) << "a cancellation is not a VEVENT that failed to read";
  EXPECT_EQ(in.events.first().exdates, QVector<QDate>{QDate(2036, 5, 26)});
  const QVector<CalEvent> shown = heap::cal::expandedEvents(in.events, QDate(2036, 5, 25), QDate(2036, 5, 31), berlin);
  EXPECT_TRUE(shown.isEmpty()) << "the cancelled meeting must not be in the calendar";
}

// A RECURRENCE-ID in UTC names the series' date in the series' zone, and an
// override of the same occurrence in the file goes with it.
TEST(Ics, ACancelledOccurrenceInUtcIsTheSeriesDate) {
  const QTimeZone tokyo("Asia/Tokyo");
  const IcsImport in = parseIcs(QStringLiteral("BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
                                               "BEGIN:VEVENT\r\nUID:s\r\nDTSTART;TZID=Asia/Tokyo:20360505T080000\r\n"
                                               "RRULE:FREQ=WEEKLY\r\nEND:VEVENT\r\n"
                                               "BEGIN:VEVENT\r\nUID:s\r\nRECURRENCE-ID;TZID=Asia/Tokyo:20360512T080000\r\n"
                                               "DTSTART;TZID=Asia/Tokyo:20360512T100000\r\nSUMMARY:moved\r\nEND:VEVENT\r\n"
                                               "BEGIN:VEVENT\r\nUID:s\r\nSTATUS:CANCELLED\r\n"
                                               "RECURRENCE-ID:20360511T230000Z\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n"),
                                tokyo);

  ASSERT_EQ(in.events.size(), 1) << "the override of the cancelled occurrence is dropped";
  EXPECT_EQ(in.events.first().exdates, QVector<QDate>{QDate(2036, 5, 12)});
}

// A cancellation for a series the file does not carry is handed to the
// importer, which knows the stored series.
TEST(Ics, ACancellationAloneIsReportedForTheStoredSeries) {
  const IcsImport in = parseIcs(wrap(QStringLiteral("UID:s\r\nSTATUS:CANCELLED\r\nRECURRENCE-ID;VALUE=DATE:20360526")));

  EXPECT_TRUE(in.events.isEmpty());
  EXPECT_EQ(in.skipped, 0);
  ASSERT_EQ(in.cancelled.size(), 1);
  EXPECT_EQ(in.cancelled.first().masterId, QStringLiteral("s"));
  EXPECT_EQ(in.cancelled.first().date, QDate(2036, 5, 26));
}

// ── Calendar subscriptions (APP-118) ─────────────────────────────────

// Outlook and Apple hand out webcal:// links; they are fetched over HTTPS.
TEST(IcsSubscription, WebcalLinksAreFetchedOverHttps) {
  EXPECT_EQ(heap::cal::subscriptionFetchUrl(QStringLiteral(" webcal://outlook.office365.com/owa/calendar/x/reachcalendar.ics ")).toString(),
            QStringLiteral("https://outlook.office365.com/owa/calendar/x/reachcalendar.ics"));
  EXPECT_EQ(heap::cal::subscriptionFetchUrl(QStringLiteral("webcals://p01.icloud.com/a.ics")).scheme(), QStringLiteral("https"));
  EXPECT_EQ(heap::cal::subscriptionFetchUrl(QStringLiteral("https://calendar.google.com/x/basic.ics")).host(),
            QStringLiteral("calendar.google.com"));
  EXPECT_TRUE(heap::cal::subscriptionFetchUrl(QStringLiteral("file:///etc/passwd")).isEmpty());
  EXPECT_TRUE(heap::cal::subscriptionFetchUrl(QStringLiteral("not a link")).isEmpty());
  EXPECT_TRUE(heap::cal::subscriptionFetchUrl(QString()).isEmpty());
}

TEST(IcsSubscription, EventIdsSayWhichCalendarTheyCameFrom) {
  EXPECT_EQ(heap::cal::subscriptionPrefix(QStringLiteral("ab12")), QStringLiteral("sub:ab12:"));
  EXPECT_TRUE(heap::cal::isSubscriptionEventId(QStringLiteral("sub:ab12:uid-1")));
  EXPECT_FALSE(heap::cal::isSubscriptionEventId(QStringLiteral("ev-123")));
  EXPECT_EQ(heap::cal::subscriptionOfEventId(QStringLiteral("sub:ab12:uid-1")), QStringLiteral("ab12"));
  EXPECT_TRUE(heap::cal::subscriptionOfEventId(QStringLiteral("ev-123")).isEmpty());
}

// A feed's events take the subscription's prefix, series links included; an
// occurrence the feed cancels becomes an exdate; an override whose series the
// feed does not carry is dropped rather than left floating.
TEST(IcsSubscription, FeedEventsArePrefixedAndCancellationsApplied) {
  const IcsImport in = parseIcs(QStringLiteral(
      "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
      "BEGIN:VEVENT\r\nUID:standup\r\nSUMMARY:Standup\r\nDTSTART:20261005T090000\r\nDTEND:20261005T091500\r\nRRULE:FREQ=DAILY\r\nEND:"
      "VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:standup\r\nRECURRENCE-ID:20261007T090000\r\nSUMMARY:Standup\r\nDTSTART:20261007T100000\r\nDTEND:"
      "20261007T101500\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:standup\r\nRECURRENCE-ID:20261008T090000\r\nSTATUS:CANCELLED\r\nDTSTART:20261008T090000\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:orphan\r\nRECURRENCE-ID:20261009T090000\r\nSUMMARY:Gone\r\nDTSTART:20261009T090000\r\nDTEND:"
      "20261009T093000\r\nEND:VEVENT\r\n"
      "END:VCALENDAR\r\n"));
  const QVector<CalEvent> evs = heap::cal::subscriptionEvents(in, QStringLiteral("s1"));
  ASSERT_EQ(evs.size(), 2);
  for(const CalEvent& e : evs) {
    EXPECT_TRUE(e.id.startsWith(QStringLiteral("sub:s1:"))) << e.id.toStdString();
    EXPECT_TRUE(e.profileId.isEmpty());
  }
  const CalEvent& master = evs.at(0).masterId.isEmpty() ? evs.at(0) : evs.at(1);
  const CalEvent& moved = evs.at(0).masterId.isEmpty() ? evs.at(1) : evs.at(0);
  EXPECT_EQ(moved.masterId, master.id);
  EXPECT_TRUE(master.exdates.contains(QDate(2026, 10, 8)));
}

// ── The desktop Outlook (APP-118, Exchange) ──────────────────────────

TEST(OutlookDesktop, TheJoinLinkIsTheFirstUrlInLocationThenBody) {
  EXPECT_EQ(heap::cal::firstMeetingUrl(QStringLiteral("Talk: https://talk.corp.ru/j/42."), QStringLiteral("https://other")),
            QStringLiteral("https://talk.corp.ru/j/42"));
  EXPECT_EQ(heap::cal::firstMeetingUrl(QStringLiteral("Room 5"), QStringLiteral("Join <https://talk.corp.ru/j/7?pwd=x> now")),
            QStringLiteral("https://talk.corp.ru/j/7?pwd=x"));
  EXPECT_TRUE(heap::cal::firstMeetingUrl(QStringLiteral("Room 5"), QStringLiteral("no link")).isEmpty());
}

TEST(OutlookDesktop, OccurrencesBecomeReadOnlyMeetings) {
  heap::cal::OutlookItem timed;
  timed.key = QStringLiteral("G1-202610061100");
  timed.subject = QStringLiteral(" Sprint sync ");
  timed.start = QDateTime(QDate(2026, 10, 6), QTime(11, 0));
  timed.end = QDateTime(QDate(2026, 10, 6), QTime(11, 30));
  timed.location = QStringLiteral("https://talk.corp.ru/j/1");
  heap::cal::OutlookItem allDay;
  allDay.key = QStringLiteral("G2-202610090000");
  allDay.subject = QStringLiteral("Offsite");
  allDay.start = QDateTime(QDate(2026, 10, 9), QTime(0, 0));
  allDay.end = QDateTime(QDate(2026, 10, 11), QTime(0, 0));  // Outlook: the next midnight
  allDay.allDay = true;
  heap::cal::OutlookItem keyless;
  keyless.start = timed.start;
  keyless.end = timed.end;

  const QVector<CalEvent> evs = heap::cal::outlookEvents({timed, allDay, keyless}, QStringLiteral("outlook"));
  ASSERT_EQ(evs.size(), 2);
  EXPECT_EQ(evs.at(0).id, QStringLiteral("sub:outlook:G1-202610061100"));
  EXPECT_EQ(evs.at(0).title, QStringLiteral("Sprint sync"));
  EXPECT_EQ(evs.at(0).date, QDate(2026, 10, 6));
  EXPECT_DOUBLE_EQ(evs.at(0).start, 11.0);
  EXPECT_DOUBLE_EQ(evs.at(0).end, 11.5);
  EXPECT_EQ(evs.at(0).url, QStringLiteral("https://talk.corp.ru/j/1"));
  EXPECT_TRUE(evs.at(1).allDay);
  EXPECT_EQ(evs.at(1).date, QDate(2026, 10, 9));
  EXPECT_EQ(evs.at(1).endDate, QDate(2026, 10, 10));
}

// A meeting that runs to midnight ends on its own day, not on a zero-length
// sliver of the next one.
TEST(OutlookDesktop, AMeetingUntilMidnightStaysOnItsDay) {
  heap::cal::OutlookItem late;
  late.key = QStringLiteral("G3");
  late.start = QDateTime(QDate(2026, 10, 6), QTime(22, 0));
  late.end = QDateTime(QDate(2026, 10, 7), QTime(0, 0));
  const QVector<CalEvent> evs = heap::cal::outlookEvents({late}, QStringLiteral("outlook"));
  ASSERT_EQ(evs.size(), 1);
  EXPECT_EQ(evs.at(0).date, QDate(2026, 10, 6));
  EXPECT_FALSE(evs.at(0).endDate.isValid());
  EXPECT_DOUBLE_EQ(evs.at(0).end, 24.0);
}

// The refresh interval (dogfood 2026-10-19, item 13): a link no oftener than
// every 5 minutes, the local Outlook every minute; a day at most.
TEST(OutlookDesktop, CadenceIsBoundedByKind) {
  using heap::cal::subscriptionMinutes;
  EXPECT_EQ(subscriptionMinutes(false, 0), 15) << "none given is the default";
  EXPECT_EQ(subscriptionMinutes(false, 1), 5);
  EXPECT_EQ(subscriptionMinutes(true, 1), 1);
  EXPECT_EQ(subscriptionMinutes(true, 2), 2);
  EXPECT_EQ(subscriptionMinutes(true, 100000), 24 * 60);
}

// While Outlook reports its own changes the timer is only a safety net.
TEST(OutlookDesktop, LiveMakesTheTimerASafetyNet) {
  using heap::cal::effectiveSubscriptionMinutes;
  EXPECT_EQ(effectiveSubscriptionMinutes(true, 1, false), 1);
  EXPECT_EQ(effectiveSubscriptionMinutes(true, 1, true), 30);
  EXPECT_EQ(effectiveSubscriptionMinutes(true, 60, true), 60);
  EXPECT_EQ(effectiveSubscriptionMinutes(false, 5, true), 5) << "a link is never live";
}

// A burst of Outlook events reads once after it goes quiet, and never later
// than the cap after it began.
TEST(OutlookDesktop, ABurstIsDebouncedWithACap) {
  using heap::cal::desktopDebounceMs;
  const QDateTime first(QDate(2026, 10, 19), QTime(10, 0));
  EXPECT_EQ(desktopDebounceMs(first, first), heap::cal::kDesktopQuietMs);
  EXPECT_EQ(desktopDebounceMs(first, first.addMSecs(10000)), heap::cal::kDesktopQuietMs);
  EXPECT_EQ(desktopDebounceMs(first, first.addMSecs(14000)), 1000);
  EXPECT_EQ(desktopDebounceMs(first, first.addMSecs(60000)), 0);
  EXPECT_EQ(desktopDebounceMs(QDateTime(), first), heap::cal::kDesktopQuietMs);
}
