pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import TodoCpp

// The one-time coach mark card every hint is drawn with (src/hints/CoachMarks.h),
// mockup 6A:
//
//   NEW · 1 OF 2                                   Esc got it
//   Title
//   [what it did, drawn — optional]
//   Body
//   [ key ] where it works                           (optional)
//   Skip all                          [secondary] [primary]
//
// Beside its anchor, never over a modal and never while typing (CoachMarkHost
// decides when). Any way out — a button, Esc, a press outside — counts as
// read: it never comes back ("Later" may put a feature hint off one day).
// "Skip all" reads every one for good (Help → "Show hints again" undoes it).
// It explains, points, or does the thing once; it never turns a setting on.
// Enter is the primary button, Tab walks the buttons. The primary button is
// the neutral one (DG-005).
Popup {
    PopupArrival { popup: root; dialog: false }
    id: root

    // The id in src/hints/CoachMarks.h, and the item it is about.
    required property string markId
    property Item anchor: null
    // "right" (flips left when there is no room), "left", "below", "above".
    property string side: "right"
    // Below or above: centred on the anchor rather than at its left edge.
    property bool centered: false
    property int step: 1
    property int stepCount: 1
    // "New" / "New · 1 of 2" unless a coach mark says otherwise.
    property string badgeText: root.stepCount > 1 ? I18n.t("coach.badge").arg(root.step).arg(root.stepCount) : I18n.t("coach.new")
    property string title: ""
    property string body: ""
    property string primaryText: I18n.t("coach.gotIt")
    property string secondaryText: ""
    // A key chip under the body: the key as bound now, and where it works.
    property string keys: ""
    property string keysCaption: ""
    // The real thing ringed and the rest of the window dimmed (6A); the
    // window behind still takes the pointer, a press there reads the card.
    property bool spotlight: false
    // Steals no keys: quick capture keeps its caret (captureDates).
    property bool takesFocus: true
    // What the coach mark points at, drawn between title and body.
    default property alias extra: extraBox.data

    signal primaryClicked()
    signal secondaryClicked()

    objectName: "coachmark-" + root.markId
    parent: root.anchor
    width: Theme.px(400)
    padding: Theme.spXl
    margins: Theme.spMd
    focus: root.takesFocus
    background: PopupSurface {}
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    modal: false
    dim: root.spotlight
    Overlay.modeless: Item {
        id: spot
        // The anchor in window coordinates, read when the layer appears.
        property rect r: Qt.rect(0, 0, 0, 0)
        function _measure() {
            const a = root.anchor;
            if (!a) return;
            const p = a.mapToItem(null, 0, 0);
            const pad = Theme.spXs;
            const r = Qt.rect(p.x - pad, p.y - pad, a.width + 2 * pad, a.height + 2 * pad);
            if (r.x !== spot.r.x || r.y !== spot.r.y || r.width !== spot.r.width || r.height !== spot.r.height)
                spot.r = r;
        }
        Component.onCompleted: spot._measure()
        // The anchor moves under it: a card's CI chip slid ~45 px down as
        // the card opened its details after the layer was measured, and the
        // ring stayed on the description above it (dogfood 2026-10-22). Its
        // place in the window has no signal of its own: looked at each frame
        // while the ring is up (one mapToItem).
        FrameAnimation {
            running: root.spotlight && root.opened
            onTriggered: spot._measure()
        }
        // A card of steps stands by another item on the next one.
        Connections {
            target: root
            function onAnchorChanged() { Qt.callLater(spot._measure); }
        }
        onWidthChanged: spot._measure()
        onHeightChanged: spot._measure()
        Behavior on opacity { NumberAnimation { duration: Theme.durPop; easing.type: Theme.easeEnter } }
        Rectangle { color: Theme.scrim; x: 0; y: 0; width: spot.width; height: Math.max(0, spot.r.y) }
        Rectangle { color: Theme.scrim; x: 0; y: spot.r.y + spot.r.height; width: spot.width; height: Math.max(0, spot.height - spot.r.y - spot.r.height) }
        Rectangle { color: Theme.scrim; x: 0; y: spot.r.y; width: Math.max(0, spot.r.x); height: spot.r.height }
        Rectangle { color: Theme.scrim; x: spot.r.x + spot.r.width; y: spot.r.y; width: Math.max(0, spot.width - spot.r.x - spot.r.width); height: spot.r.height }
        Rectangle {
            objectName: "coachmark-ring"
            x: spot.r.x; y: spot.r.y; width: spot.r.width; height: spot.r.height
            color: "transparent"
            radius: Theme.radiusLg
            border.width: Theme.spXs
            border.color: Theme.focusRing
        }
    }

    // Runs `after` (a dialog the card opens), then closes: closing reads the
    // coach mark, and its anchor lets go of it straight after.
    function finish(after) {
        if (after) after();
        root.close();
    }
    // Settings, opened on the row a coach mark names (APP-210's reveal).
    // Main.qml's window (openSettingsItem, showQuery…), or none in a test.
    readonly property var hostWindow: root.anchor ? root.anchor.Window.window : null
    function openSettings(section, objectName) {
        const w = root.hostWindow;
        root.close();
        if (w && w.openSettingsItem)
            w.openSettingsItem({ section: section, objectName: objectName || "" });
    }

    function _place() {
        const a = root.anchor;
        if (!a) return;
        const gap = Theme.spMd;
        let s = root.side;
        if (s === "right") {
            const p = a.mapToItem(null, a.width, 0);
            if (a.Window.width > 0 && p.x + gap + root.width > a.Window.width - Theme.spMd) s = "left";
        }
        if (s === "right") { root.x = a.width + gap; root.y = 0; }
        else if (s === "left") { root.x = -root.width - gap; root.y = 0; }
        else if (s === "below") { root.x = root.centered ? (a.width - root.width) / 2 : 0; root.y = a.height + Theme.spSm; }
        else { root.x = root.centered ? (a.width - root.width) / 2 : 0; root.y = -root.height - Theme.spSm; }
    }

    onAboutToShow: root._place()
    onStepChanged: if (root.opened) Qt.callLater(root._place)
    onAnchorChanged: if (root.opened) Qt.callLater(root._place)

    onOpened: AppController.coachMarkShown(root.markId)
    onClosed: AppController.dismissCoachMark(root.markId)

    contentItem: ColumnLayout {
        spacing: Theme.spLg
        // Takes the keys when the card does: Enter is the primary button;
        // Tab goes on to Skip all and the buttons.
        focus: root.takesFocus
        Keys.onReturnPressed: (event) => { event.accepted = true; root.primaryClicked(); }
        Keys.onEnterPressed: (event) => { event.accepted = true; root.primaryClicked(); }

        // Without the keyboard (takesFocus false) Esc still reads it — the
        // first Esc closes the card, the next one is the anchor's again. In
        // an item: a Shortcut finds its window through one.
        Shortcut {
            sequence: "Esc"
            enabled: root.opened && !root.takesFocus
            onActivated: root.close()
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spSm
            Text {
                objectName: "coachmark-badge"
                Layout.fillWidth: true
                text: root.badgeText.toUpperCase()
                elide: Text.ElideRight
                color: Theme.accentStrong
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                font.weight: Theme.fwTitle
            }
            KeyHint { keys: "Esc"; always: true }
            Text {
                text: I18n.t("coach.close")
                color: Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
            }
        }
        Text {
            objectName: "coachmark-title"
            Layout.fillWidth: true
            text: root.title
            wrapMode: Text.WordWrap
            color: Theme.text
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsLg
            font.weight: Theme.fwTitle
        }
        ColumnLayout {
            id: extraBox
            Layout.fillWidth: true
            visible: extraBox.children.length > 0
            spacing: Theme.spSm
        }
        Text {
            objectName: "coachmark-body"
            Layout.fillWidth: true
            visible: root.body.length > 0
            text: root.body
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSm
        }

        Rectangle {
            objectName: "coachmark-keys"
            Layout.fillWidth: true
            visible: root.keys.length > 0
            implicitHeight: keysRow.implicitHeight + 2 * Theme.spMd
            radius: Theme.radiusMd
            color: Theme.panel2
            RowLayout {
                id: keysRow
                anchors.fill: parent
                anchors.margins: Theme.spMd
                spacing: Theme.spSm
                KeyHint { keys: root.keys; always: true }
                Text {
                    objectName: "coachmark-keys-caption"
                    Layout.fillWidth: true
                    text: root.keysCaption
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSm
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spXs
            spacing: Theme.spSm
            // Quiet: every hint read for good (Help brings them back).
            Text {
                id: skipAll
                objectName: "coachmark-skipAll"
                text: I18n.t("coach.skipAll")
                color: skipArea.hovered || skipArea.keyboardFocused ? Theme.text : Theme.textDim
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsXs
                font.underline: true
                function activate() {
                    AppController.skipAllCoachMarks();
                    root.close();
                }
                ClickArea {
                    id: skipArea
                    label: I18n.t("coach.skipAll")
                    showTip: false
                    onActivated: skipAll.activate()
                }
            }
            Item { Layout.fillWidth: true }
            PillButton {
                objectName: "coachmark-secondary"
                visible: root.secondaryText.length > 0
                small: true
                text: root.secondaryText
                onClicked: root.secondaryClicked()
            }
            PillButton {
                objectName: "coachmark-primary"
                small: true
                primary: true
                text: root.primaryText
                onClicked: root.primaryClicked()
            }
        }
    }
}
