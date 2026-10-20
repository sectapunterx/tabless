// SplashScreen: load-smoke against the live singletons, the version line, and
// the dismissal contract Main.qml relies on: ready ends a normal splash at
// once, a press ends it and passes through, the crash card waits.
import QtQuick
import QtQuick.Controls
import QtTest
import TodoCpp

TestCase {
    id: tc
    name: "SplashScreen"
    when: windowShown
    visible: true
    width: 400
    height: 400

    Item { id: host; anchors.fill: parent }

    function make(qml) {
        const o = createTemporaryQmlObject(qml, host);
        verify(o !== null);
        return o;
    }

    // Smoke: instantiates against the live Brand/BrandLogo/AppController
    // singletons; a renamed property or missing type would return null.
    function test_smoke_load() {
        const s = make('import TodoCpp; SplashScreen { }');
        verify(s !== null, "failed to instantiate SplashScreen");
    }

    // The sheet shows the wordmark and which build this is, nothing else
    // (R3-017): "Stable 0.9.5", or "Feature build · abc1234 · …" (dogfood
    // 2026-10-20 #10).
    function test_default_knobs() {
        const s = make('import TodoCpp; SplashScreen { }');
        verify(!s.ready);
        compare(s.version, AppController.appVersion);
        const v = findChild(s, "splash-version");
        verify(v && v.visible);
        compare(v.text, Brand.buildLine());
        if (!Brand.channelBuild) verify(v.text.indexOf(AppController.appVersion) >= 0, v.text);
    }

    // A normal launch leaves as soon as the scene is ready, never on a timer
    // (Review 2): finished() fires once when ready flips, not before.
    function test_ready_dismisses_at_once() {
        const s = make('import TodoCpp; SplashScreen { }');
        let fired = 0;
        s.finished.connect(function() { fired++; });
        wait(100);
        compare(fired, 0, "no timer dismisses the splash on its own");
        s.ready = true;
        compare(fired, 1);
        s.dismiss();
        compare(fired, 1, "finished() fires exactly once");
    }

    // The splash never holds the keyboard on a normal launch, so the first key
    // reaches the app; a press ends it and passes through to what is under it.
    function test_press_dismisses_and_reaches_app() {
        const below = make('import QtQuick; MouseArea { property int hits: 0; anchors.fill: parent; onPressed: hits++ }');
        const s = make('import TodoCpp; SplashScreen { anchors.fill: parent }');
        verify(!s.activeFocus);
        let fired = 0;
        s.finished.connect(function() { fired++; });
        mouseClick(s, 50, 50);
        compare(fired, 1, "the first press ends the splash");
        compare(below.hits, 1, "and the press reaches the app");
    }

    // After an unclean exit the splash says so and waits for an answer
    // (R3-018): the scene being ready does not dismiss it; "Не отправлять" does.
    function test_crash_card_waits_for_answer() {
        AppController.simulateUncleanExitForTest(new Date(2026, 9, 9, 15, 58));
        const s = make('import TodoCpp; SplashScreen { anchors.fill: parent }');
        let fired = 0;
        s.finished.connect(function() { fired++; });
        s.ready = true;
        mouseClick(s, 5, 5);
        compare(fired, 0, "a crashed launch is not dismissed by readiness or a stray press");
        const fact = findChild(s, "splash-crash-fact");
        verify(fact.visible);
        verify(fact.text.indexOf("15:58") >= 0, fact.text);
        // a-34 (N/X-Ntf-Focus): "Данные целы — последнее сохранение 15:58.
        // Отправить отчёт о сбое разработчику?"
        compare(fact.text, I18n.t("crash.fact").arg(I18n.fmtTime(new Date(2026, 9, 9, 15, 58))) + " " + I18n.t("crash.ask"));
        if (I18n.lang === "ru" && I18n.fmtTime(new Date(2026, 9, 9, 15, 58)) === "15:58")
            compare(fact.text, "Данные целы — последнее сохранение 15:58. Отправить отчёт о сбое разработчику?");
        verify(!findChild(s, "splash-version").visible);
        mouseClick(findChild(s, "splash-crash-skip"));
        compare(fired, 1);
        verify(!AppController.lastExitUnclean);
    }

    // Signal contract: finished() takes no arguments and is emittable.
    function test_finished_signal_contract() {
        const s = make('import TodoCpp; SplashScreen { }');
        let fired = 0;
        s.finished.connect(function() { fired++; });
        s.finished();
        compare(fired, 1);
    }
}
