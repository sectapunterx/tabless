// Settings → Integrations: the "Calendars by link" card (APP-118) and the
// "How integrations work" block above it.
import QtQuick
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "CalendarSubscriptionsCard"
    when: windowShown
    visible: true
    width: 900
    height: 700

    Item { id: host; anchors.fill: parent }

    property string savedSettings: ""

    function init() {
        tc.savedSettings = AppController.appSettingsJson;
        AppController.appSettingsJson = "";
    }
    function cleanup() {
        AppController.appSettingsJson = tc.savedSettings;
    }

    function make(qml) {
        const o = createTemporaryQmlObject(qml, host);
        verify(o !== null);
        return o;
    }

    // A link that is not http(s)/webcal is refused on the spot, with the
    // reason under the field, and nothing is added.
    function test_a_bad_link_is_refused() {
        const card = make('import TodoCpp; CalendarSubscriptionsCard { width: 860 }');
        findChild(card, "calsub-link").text = "ftp://example.com/cal.ics";
        verify(!card.add());
        verify(findChild(card, "calsub-error").visible);
        compare(AppController.calendarSubscriptions.length, 0);
    }

    // A calendar in the settings is listed with its refresh interval and how
    // its last fetch went; this one has no link in the keychain, which it says.
    function test_a_subscription_is_listed_with_its_status() {
        AppController.appSettingsJson = JSON.stringify({ calendars: { subscriptions: [
            { id: "qprobe", name: "Work", minutes: 30 } ] } });
        const card = make('import TodoCpp; CalendarSubscriptionsCard { width: 860 }');
        tryVerify(() => findChild(card, "calsub-row-qprobe") !== null, 2000);
        tryVerify(() => String(findChild(card, "calsub-status-qprobe").text).length > 0, 2000);
        verify(findChild(card, "calsub-refresh-qprobe") !== null);
    }

    // The interval of a calendar already added can be changed (dogfood
    // 2026-10-19, item 13): Outlook on this computer down to a minute, a link
    // no oftener than every 5 minutes, whatever is asked for.
    function test_an_added_calendar_changes_its_interval() {
        AppController.appSettingsJson = JSON.stringify({ calendars: { subscriptions: [
            { id: "outlook", kind: "outlook", name: "Outlook", minutes: 15 },
            { id: "qlink", name: "Work", minutes: 30 } ] } });
        const card = make('import TodoCpp; CalendarSubscriptionsCard { width: 860 }');
        tryVerify(() => findChild(card, "calsub-every-outlook-1") !== null, 2000);
        verify(findChild(card, "calsub-every-outlook-15").selected);
        verify(findChild(card, "calsub-every-qlink-1") === null, "a link offers no 1-minute pill");
        findChild(card, "calsub-every-outlook-1").clicked();
        const subs = AppController.calendarSubscriptions;
        compare(subs.find((s) => s.id === "outlook").minutes, 1);
        compare(subs.find((s) => s.id === "outlook").minMinutes, 1);
        AppController.setCalendarSubscriptionMinutes("qlink", 1);
        compare(AppController.calendarSubscriptions.find((s) => s.id === "qlink").minutes, 5);
        tryVerify(() => findChild(card, "calsub-every-outlook-1").selected, 2000);
    }

    function test_the_interval_pills_pick_one() {
        const card = make('import TodoCpp; CalendarSubscriptionsCard { width: 860 }');
        compare(card.everyIndex, 1);
        findChild(card, "calsub-every-60").clicked();
        compare(card.everyIndex, 3);
        verify(findChild(card, "calsub-every-60").selected);
        verify(!findChild(card, "calsub-every-15").selected);
    }
}
