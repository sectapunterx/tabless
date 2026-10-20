// A person (X-Dlg-Small "Человек"): Name, What to ask and When to write,
// for "Кому написать" and @mentions. New and edit alike. The handle is made
// from the name; role, state and colour are kept as they are (they come from
// an import or from the Today rail). "Когда написать" is optional: a day, or
// a day and a time that notifies then ("завтра 15:00", "через 2 ч", "15:00"
// = today, tomorrow once today's has gone); empty clears it.
import QtQuick
import QtQuick.Layouts
import TodoCpp

SmallDialog {
    id: root
    objectName: "person-editor"

    property var draft: ({})
    property bool isNew: false
    // Why the last save was refused; the dialog stays open on the draft.
    property string _error: ""
    // What the when field said when the dialog opened: unchanged, the stored
    // moment stays as it is (a time already gone is not moved to tomorrow).
    property string _initialWhen: ""
    // The when field read as it is typed: "→ завтра 15:00".
    readonly property var _when: whenField.text.trim() === root._initialWhen.trim()
                                 ? ({ ok: true, unchanged: true })
                                 : AppController.parsePingWhen(whenField.text)

    title: I18n.t("editor.person.title")
    fact: I18n.t("editor.person.fact")

    function showFor(initialDraft) {
        draft = initialDraft || {};
        isNew = !!draft._isNew || !(draft.id && String(draft.id).length > 0);
        nameField.text = draft.name || "";
        questionField.text = draft.question || "";
        _initialWhen = draft.state === "todo" || !draft.state ? (draft.when || "") : "";
        whenField.text = _initialWhen;
        _error = "";
        open();
        if ((draft.name || "").length > 0) questionField.focusField();
        else nameField.focusField();
    }

    function _save() {
        const name = nameField.text.trim();
        if (name.length === 0) {
            root._error = I18n.t("editor.person.err.name");
            nameField.input.forceActiveFocus();
            return;
        }
        let remindAt = root._initialWhen.length > 0 ? (root.draft.remindAt || null) : null;
        let remindHasTime = remindAt !== null && !!root.draft.remindHasTime;
        const w = root._when;
        if (!w.unchanged) {
            if (!w.ok) {
                root._error = I18n.t(w.error === "past" ? "editor.person.err.past" : "editor.person.err.when");
                whenField.input.forceActiveFocus();
                return;
            }
            remindAt = w.empty ? null : w.at;
            remindHasTime = !w.empty && !!w.hasTime;
        }
        // A time to write is an ask to write: a wait or a reply given one
        // goes back on the list.
        let state = root.draft.state || "todo";
        if (remindAt !== null && !w.unchanged) state = "todo";
        const d = {
            _isNew: root.isNew,
            _contactKey: root.draft._contactKey || "",
            _createContact: !!root.draft._createContact,
            _originalId: root.isNew ? "" : (root.draft.id || ""),
            // A new person's handle comes from the name and is free by
            // construction; an existing one keeps theirs.
            id: root.isNew ? AppController.suggestPersonId(name, "") : root.draft.id,
            name: name,
            role: root.draft.role || "",
            question: questionField.text.trim(),
            state: state,
            color: root.draft.color || Theme.swatches[0],
            remindAt: remindAt,
            remindHasTime: remindHasTime
        };
        if (!AppController.savePerson(d)) {
            root._error = I18n.t("editor.err.refused");
            return;
        }
        root.close();
    }
    onAccepted: root._save()

    DialogField {
        id: nameField
        fieldName: "pe-name"
        label: I18n.t("editor.label.name")
        onAccepted: questionField.focusField()
        onTextChanged: root._error = ""
    }
    DialogField {
        id: questionField
        fieldName: "pe-question"
        label: I18n.t("editor.label.question")
        onAccepted: whenField.focusField()
    }
    DialogField {
        id: whenField
        fieldName: "pe-when"
        label: I18n.t("editor.label.pingWhen")
        placeholderText: I18n.t("editor.person.whenHint")
        onAccepted: root._save()
        onTextChanged: root._error = ""
    }
    // What the field was read as, before it is saved.
    Text {
        objectName: "pe-when-read"
        visible: text.length > 0
        Layout.fillWidth: true
        text: {
            const w = root._when;
            if (w.unchanged || w.empty) return "";
            if (!w.ok) return I18n.t(w.error === "past" ? "editor.person.err.past" : "editor.person.err.when");
            return "→ " + w.label + (w.hasTime ? "" : " · " + I18n.t("editor.person.noTime"))
                   + (w.rolled ? " · " + I18n.t("editor.person.rolled") : "");
        }
        color: root._when.ok ? Theme.textDim : Theme.warning
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsXs
        wrapMode: Text.Wrap
    }
    Text {
        objectName: "pe-error"
        visible: root._error.length > 0
        Layout.fillWidth: true
        text: root._error
        color: Theme.danger
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSm
        wrapMode: Text.Wrap
    }

    buttons: [
        PillButton {
            objectName: "pe-cancel"
            text: I18n.t("common.cancel")
            onClicked: root.close()
        },
        PillButton {
            objectName: "pe-save"
            primary: true
            text: I18n.t("editor.btn.save")
            onClicked: root._save()
        }
    ]
}
