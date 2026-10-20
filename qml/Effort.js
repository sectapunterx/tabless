// Time spent against the estimate (item 19), as the ticket's chip and the
// board card say it: "7ч 30м / 10ч", and over it "12ч / 8ч · +4ч". Not a
// library: it uses the AppController singleton of the file that imports it
// (the tracker's working day and week, the UI language).

// { shown, text, over, overText }: nothing shown with neither an estimate
// nor any time spent. `provider` picks whose day and week ("" = Jira's 8h/5d).
function summary(spent, estimate, provider) {
    const s = Math.max(0, Math.round(Number(spent) || 0));
    const e = Math.max(0, Math.round(Number(estimate) || 0));
    if (s <= 0 && e <= 0) return { shown: false, text: "", over: false, overText: "" };
    const p = String(provider || "");
    const fmt = function (m) { return AppController.formatDuration(m, p); };
    const over = e > 0 && s > e;
    const overText = over ? "+" + fmt(s - e) : "";
    const text = fmt(s) + " / " + (e > 0 ? fmt(e) : "—");
    return { shown: true, text: over ? text + " · " + overText : text, over: over, overText: overText };
}

// A card's model row: my estimate, else the tracker's original one; the
// tracker's logged time plus my timer's.
function ofCard(task) {
    if (!task) return summary(0, 0, "");
    const ticket = task.ticket || {};
    const est = (task.estimateMinutes || 0) > 0 ? task.estimateMinutes : (ticket.originalMinutes || 0);
    const spent = (ticket.spentMinutes || 0) + Math.round((task.trackedSeconds || 0) / 60);
    return summary(spent, est, ticket.provider || "");
}
