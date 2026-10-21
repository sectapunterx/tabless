.pragma library

// The pictures the file viewer steps through with ← / → when a picture in a
// text is opened (dogfood 2026-10-21): a ticket's text (its description and
// its comments) goes through all the ticket's pictures, any other text
// through its own. Pure: the lists are the caller's.

// "attachments/<id>" → "<id>", else "".
function attIdOf(src) {
    const m = /^attachments\/([0-9a-f]{32}(?:\.[a-z0-9]{1,12})?)$/.exec(String(src || ""));
    return m ? m[1] : "";
}

// The file a stand-in names: "tracker-image:loading/<name>" or
// "tracker-image:failed/<name>/<why>" (integrations/TrackerImages.h).
function standInName(src) {
    const s = String(src || "");
    if (!s.startsWith("tracker-image:")) return "";
    const part = s.substring("tracker-image:".length).split("/")[1] || "";
    try { return decodeURIComponent(part); } catch (e) { return part; }
}

// Whether a picture's address is one the viewer can open.
function openable(src) {
    return attIdOf(src).length > 0 || standInName(src).length > 0;
}

// {items: [{attId, name, taskId, attachmentId}], index} for the picture
// `src` was clicked on. `ticketImages` is AppController.trackerImages(taskId)
// (empty for a text that is not a ticket's); `docImages` the text's own
// pictures, [{src, alt}], in order. No items when nothing can be opened.
function build(src, taskId, ticketImages, docImages) {
    const att = attIdOf(src);
    const name = standInName(src);
    const ticket = ticketImages || [];
    if (String(taskId || "").length > 0 && ticket.length > 0) {
        let at = -1;
        for (let i = 0; i < ticket.length && at < 0; i++) {
            if (att.length > 0 ? ticket[i].attId === att : ticket[i].name === name) at = i;
        }
        if (at < 0 && name.length > 0) {
            for (let i = 0; i < ticket.length && at < 0; i++)
                if (String(ticket[i].name).toLowerCase() === name.toLowerCase()) at = i;
        }
        if (at >= 0) {
            const items = [];
            for (let i = 0; i < ticket.length; i++)
                items.push({ attId: String(ticket[i].attId || ""), name: String(ticket[i].name || ""),
                             taskId: String(taskId), attachmentId: String(ticket[i].attachmentId || "") });
            return { items: items, index: at };
        }
    }
    if (att.length === 0) return { items: [], index: 0 };
    const items = [];
    let index = 0;
    const seen = {};
    const doc = docImages || [];
    for (let i = 0; i < doc.length; i++) {
        const id = attIdOf(doc[i].src);
        if (id.length === 0 || seen[id]) continue;
        seen[id] = true;
        if (id === att) index = items.length;
        items.push({ attId: id, name: String(doc[i].alt || "") });
    }
    if (!seen[att]) {
        index = items.length;
        items.push({ attId: att, name: "" });
    }
    return { items: items, index: index };
}
