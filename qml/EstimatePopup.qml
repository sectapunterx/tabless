// The estimate of a task, in its card (dogfood 2026-10-20, item 19): a small
// popover under the estimate chip. One field, typed as Jira takes it ("1d 2h",
// "90m", "1,5ч", "1н 2д", "1мес" = 4 weeks), in the tracker's own working day
// and week; what is spent (the tracker's logged time and my timer), what is
// left, a thin bar. Enter saves my estimate on the task, Esc leaves it as it
// was; both hand the keyboard back to the chip. Nothing goes to the tracker:
// an estimate is mine, as a note is.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import TodoCpp

Popup {
    PopupArrival { popup: root; dialog: false }
    id: root
    objectName: "estimate-popup"
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: Theme.spLg
    width: Theme.px(360)

    property string taskId: ""
    // Whose day and week: the ticket's tracker, "" for a local task.
    property string provider: ""
    property string providerName: ""
    // My estimate on the task (0 = none) and the tracker's original one.
    property int localMinutes: 0
    property int trackerMinutes: 0
    // Logged in the tracker, and by my timer.
    property int trackerSpent: 0
    property int timerSpent: 0
    // The tracker's own "remaining", -1 when it gave none.
    property int trackerRemaining: -1

    // Enter or Esc: the chip takes the keyboard back. A press beside it
    // leaves the keyboard where the press put it.
    signal finished(bool byKey)

    readonly property int estimate: root.localMinutes > 0 ? root.localMinutes : root.trackerMinutes
    readonly property int spent: root.trackerSpent + root.timerSpent
    readonly property var units: root.visible ? AppController.timeUnits(root.provider) : ({ hoursPerDay: 8, daysPerWeek: 5 })
    function fmt(m) { return AppController.formatDuration(m, root.provider); }

    property string _prefill: ""
    property bool _bad: false
    property bool _byKey: false

    function openFor() {
        root._prefill = root.estimate > 0 ? root.fmt(root.estimate) : "";
        input.text = root._prefill;
        root._bad = false;
        root._byKey = false;
        root.open();
        input.forceActiveFocus();
        input.selectAll();
    }
    // What Enter would save: {ok, minutes}.
    readonly property var parsed: AppController.parseDuration(input.text, root.provider)
    // Saves my estimate; false (and the field marked) when it reads nothing.
    function apply() {
        const t = String(input.text).trim();
        // The tracker's estimate shown as it came is not made mine by Enter.
        if (t === root._prefill.trim()) {
            root._finish();
            return true;
        }
        if (!root.parsed.ok) {
            root._bad = true;
            return false;
        }
        const task = AppController.taskById(root.taskId);
        if (task && task.id && task.estimateMinutes !== root.parsed.minutes) {
            const d = Object.assign({}, task);
            d._originalId = root.taskId;
            d.estimateMinutes = root.parsed.minutes;
            AppController.saveTask(d);
        }
        root._finish();
        return true;
    }
    function cancel() { root._finish(); }
    function _finish() {
        root._byKey = true;
        root.close();
    }
    onClosed: root.finished(root._byKey)
    onOpened: root._guard()
    onAboutToShow: root._closing = false
    onAboutToHide: root._closing = true

    // The keyboard stays in the field while the popover is open (acceptance
    // 2026-10-20, idiot test). Opened from "+ свойство", the menu was still
    // fading out; when it finished, Qt gave the keyboard back to what had it
    // before the menu (the task's text), and "1.5h" + Enter went in there,
    // behind the popover, which stayed open. Whatever takes the keyboard
    // outside every popup while this one is open is overruled; another
    // popup over it (Ctrl+K) keeps it, and a press beside this one closes
    // it first, so a click elsewhere is never fought.
    readonly property Connections _focusWatch: Connections {
        target: root.opened ? root.contentItem.Window.window : null
        function onActiveFocusItemChanged() { root._guard(); }
    }
    property bool _guarding: false
    property bool _closing: false
    function _guard() {
        if (!root.opened || root._closing || root._guarding) return;
        const win = root.contentItem.Window.window;
        const f = win ? win.activeFocusItem : null;
        // No window focus at all (another app): nothing to take back.
        if (!f) return;
        // In this popover, or in any popup (they all live in the overlay).
        const overlay = root.contentItem.parent ? root.contentItem.parent.parent : null;
        for (let p = f; p; p = p.parent)
            if (p === overlay) return;
        root._guarding = true;
        input.forceActiveFocus();
        // Untouched, the text is still selected, so typing replaces it.
        if (input.text === root._prefill) input.selectAll();
        root._guarding = false;
    }

    // The estimate as typed now, for the lines under the field: what Enter
    // would save, else what the task has.
    readonly property int _shownEstimate: root.parsed.ok && String(input.text).trim().length > 0 ? root.parsed.minutes
                                          : String(input.text).trim().length === 0 ? 0 : root.estimate
    readonly property int _left: root._shownEstimate > 0 ? root._shownEstimate - root.spent
                                 : root.trackerRemaining >= 0 ? root.trackerRemaining : -1
    readonly property real _ratio: root._shownEstimate > 0 ? Math.min(1, root.spent / root._shownEstimate) : 0
    readonly property bool _over: root._shownEstimate > 0 && root.spent > root._shownEstimate

    background: PopupSurface {}

    contentItem: ColumnLayout {
        spacing: Theme.spMd

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spMd
            Text {
                Layout.preferredWidth: Theme.px(84)
                text: I18n.t("estimate.label")
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
            }
            TextField {
                id: input
                objectName: "estimate-input"
                Layout.fillWidth: true
                color: Theme.text
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsMd
                selectByMouse: true
                placeholderText: I18n.t("estimate.ph")
                placeholderTextColor: Theme.textDim
                background: FieldFrame {
                    color: "transparent"
                    border.color: root._bad ? Theme.danger : focused ? Theme.focusRing : Theme.borderStrong
                }
                Accessible.name: I18n.t("estimate.label")
                onTextChanged: root._bad = false
                Keys.onReturnPressed: (e) => { root.apply(); e.accepted = true; }
                Keys.onEnterPressed: (e) => { root.apply(); e.accepted = true; }
                Keys.onEscapePressed: (e) => { root.cancel(); e.accepted = true; }
            }
        }
        Text {
            objectName: "estimate-error"
            Layout.fillWidth: true
            visible: root._bad
            text: I18n.t("estimate.bad")
            wrapMode: Text.Wrap
            color: Style.urgency ? Theme.danger : Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spMd
            Text {
                Layout.preferredWidth: Theme.px(84)
                text: I18n.t("estimate.spent")
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
            }
            Text {
                objectName: "estimate-spent"
                text: root.fmt(root.spent)
                color: Theme.text
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsSm
            }
            // "Jira 5ч + таймер 2ч 30м": where the time came from.
            Text {
                objectName: "estimate-spent-from"
                Layout.fillWidth: true
                visible: text.length > 0
                text: {
                    const bits = [];
                    if (root.trackerSpent > 0) bits.push((root.providerName || root.provider) + " " + root.fmt(root.trackerSpent));
                    if (root.timerSpent > 0) bits.push(I18n.t("estimate.timer").arg(root.fmt(root.timerSpent)));
                    return bits.length > 1 || (bits.length === 1 && root.provider.length > 0) ? bits.join(" + ") : "";
                }
                elide: Text.ElideRight
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root._left !== -1 || root._over
            spacing: Theme.spMd
            Text {
                Layout.preferredWidth: Theme.px(84)
                text: I18n.t("estimate.left")
                color: Theme.textMuted
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSm
            }
            Text {
                objectName: "estimate-left"
                text: root._over ? I18n.t("estimate.over").arg(root.fmt(root.spent - root._shownEstimate))
                                 : root.fmt(Math.max(0, root._left))
                color: root._over ? Theme.warning : Theme.text
                font.family: root._over ? Theme.fontUi : Theme.fontMono
                font.pixelSize: Theme.fsSm
            }
        }
        Rectangle {
            objectName: "estimate-bar"
            Layout.fillWidth: true
            visible: root._shownEstimate > 0
            implicitHeight: Theme.px(4)
            radius: Theme.radiusXs
            color: Theme.panel3
            Rectangle {
                width: parent.width * root._ratio
                height: parent.height
                radius: parent.radius
                color: root._over ? Theme.warning : Theme.textMuted
            }
        }
        // "1н = 5д · 1д = 8ч (как в Jira)", then that it stays here.
        Text {
            objectName: "estimate-units"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: {
                const u = root.units;
                const w = I18n.t("estimate.units").arg(String(u.daysPerWeek)).arg(String(u.hoursPerDay));
                return root.provider.length > 0 ? w + " " + I18n.t("estimate.asIn").arg(root.providerName || root.provider) : w;
            }
            color: Theme.textDim
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsXs
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spSm
            Text {
                objectName: "estimate-local-only"
                Layout.fillWidth: true
                text: root.provider.length > 0 ? I18n.t("estimate.localOnly").arg(root.providerName || root.provider)
                                               : I18n.t("estimate.examples")
                wrapMode: Text.Wrap
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
            KeyHint { keys: "Enter"; always: true }
            KeyHint { keys: "Esc"; always: true }
        }
    }
}
