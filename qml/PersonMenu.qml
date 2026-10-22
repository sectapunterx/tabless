import QtQuick
import TodoCpp

// Right-click on a person in "Кому написать" (R2-063, sheet X/N-Menus-Other):
// the name and role on top, mark as written (then replied, or back to
// "write"), edit the question, the tasks and meetings linked to them,
// delete. Nothing is sent anywhere.
AppMenu {
    id: menu
    objectName: "person-menu"

    property var person: ({})
    // Off where the linked tasks are already in view (the people dialog).
    property bool showLinks: true
    // "Готово" (Del): off the list of who to write to, in the sidebar.
    property bool showDone: false

    signal editRequested(string id)
    signal linksRequested(string id)

    function openFor(p) {
        // The stored person: a row's map may not carry the role.
        menu.person = p && p.id ? AppController.personById(p.id) : ({});
        menu.popup();
    }

    AppMenuHeader {
        text: (menu.person.name || "") + ((menu.person.role || "").length ? " · " + menu.person.role : "")
    }
    // The moves from where they are (dogfood 2026-10-20): "Написал" starts
    // the wait, "Ответил" ends it, "Написать снова" goes back.
    AppMenuItem {
        objectName: "person-menu-wrote"
        visible: menu.person.state !== "pinged" && menu.person.state !== "replied"
        height: visible ? implicitHeight : 0
        text: I18n.t("people.menu.wrote")
        onTriggered: AppController.setPersonState(menu.person.id, "pinged")
    }
    AppMenuItem {
        objectName: "person-menu-replied"
        visible: menu.person.state === "pinged"
        height: visible ? implicitHeight : 0
        text: I18n.t("people.state.replied.tag")
        onTriggered: AppController.setPersonState(menu.person.id, "replied")
    }
    AppMenuItem {
        objectName: "person-menu-again"
        visible: menu.person.state === "pinged" || menu.person.state === "replied"
        height: visible ? implicitHeight : 0
        text: I18n.t("people.dialog.again")
        onTriggered: AppController.setPersonState(menu.person.id, "todo")
    }
    AppMenuItem {
        objectName: "person-menu-done"
        visible: menu.showDone && menu.person.state !== undefined && menu.person.state !== "idle"
        height: visible ? implicitHeight : 0
        text: I18n.t("pings.menu.done")
        keyText: "Del"
        onTriggered: AppController.donePersonPing(menu.person.id)
    }
    AppMenuItem {
        objectName: "person-menu-edit"
        text: I18n.t("people.menu.question")
        onTriggered: menu.editRequested(menu.person.id)
    }
    AppMenuItem {
        objectName: "person-menu-links"
        visible: menu.showLinks
        height: visible ? implicitHeight : 0
        text: I18n.t("people.dialog.tasks")
        onTriggered: menu.linksRequested(menu.person.id)
    }
    AppMenuSeparator {}
    AppMenuItem {
        objectName: "person-menu-delete"
        text: I18n.t("people.menu.deletePerson")
        danger: true
        onTriggered: AppController.deletePerson(menu.person.id)
    }
}
