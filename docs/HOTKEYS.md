# Keyboard reference

Tabless has two keymaps; **Settings → Keys → Keymap** switches between them.

- **Classic** (the default of a new install): arrows, `Enter`, `Del`, `F2`,
  `Home` / `End`, and chords with `Ctrl` or `Alt` — the way Linear and Todoist
  are driven. Nothing needs a bare letter or a two-key sequence (the Calendar's
  `P` / `T` / `N` are an extra, on its grid only). Its keys are
  listed in [Classic keymap](#classic-keymap) below.
- **Vim**: `h j k l`, the `g` / `y` / `z` / `c` sequences, single letters on
  the task under the cursor, `d d`. An install that was on the Vim-based keymap
  of 0.8 stays on it after the update. The rest of this page is written for
  this keymap.

The arrows, `Enter`, `Esc`, `Del` and `Space` work in both. A key you gave an
action yourself stays when you switch; a default of the other keymap that it
holds steps aside, and **Reset** gives back the key of the keymap that is on.

`?` (or `Ctrl+/`) opens the cheat sheet:
every key by area, in columns, with a search by action or by key. The cheat
sheet is read-only; **Change shortcuts…** at its foot (or **Settings →
Keys**) opens the panel that rebinds. There, `Enter` on a binding starts
recording, the new key (or two keys in a row, like `g` then `b`) and `Enter`
save it, `Esc` cancels, `Backspace` clears. A key another action has is
handed over (a toast names what was freed); a key that is the start of
another's sequence (`g` while `g b` exists) and keys the system keeps
(`Alt+F4`, the Windows key, `Tab`) are refused with the reason. `↺` restores a
single default, `↺ all` the whole catalog after a second press. The settings
search finds a key both ways: by the action's name (in either language) and by
the key itself (`ctrl+k`, `g b`, `F2`, `del`).

The same catalog is in the command line, the menus and the hints: a rebinding
changes the key everywhere at once.

## Classic keymap

| Action | Classic |
|--------|---------|
| Move the cursor | `↑` `↓` `←` `→` (`Down`, `Up`); first / last `Home` / `End`; a page `PgUp` / `PgDown` |
| Open | `Enter` |
| Mark | `Space`; grow the selection `Shift+↑` / `Shift+↓` |
| Done | `Ctrl+D` |
| Rename / open the task | `F2` |
| New task below / above | `Ins` / `Shift+Ins` (or `Ctrl+N`) |
| When / due | `Ctrl+Shift+S` / `Ctrl+Shift+D` |
| Priority P0…P3 | `Alt+1`, `Alt+2`, `Alt+3`, `Alt+4` |
| Timer | `Ctrl+T` |
| Estimate of the open task (in its document) | `Alt+E` |
| Copy id / branch / link | `Ctrl+Shift+C` / `Ctrl+Shift+G` / `Ctrl+Shift+Y` |
| Create a branch | `Ctrl+Alt+B` |
| The card's failed CI: jobs and the log's end | `Ctrl+Alt+I` |
| Whom the merge request waits on → Who to message | `Ctrl+Alt+P` |
| Move to another profile | `Ctrl+Alt+M` |
| Open in the tracker | `Ctrl+Shift+O` |
| Card menu | `Shift+F10` or `Menu` |
| Archive | `Ctrl+E` |
| Fold the column | `Alt+F` |
| Move the card | `Ctrl+Up`, `Ctrl+Down`, `Ctrl+Left`, `Ctrl+Right` |
| Move the column (on its header) | `Ctrl+Shift+Left` / `Ctrl+Shift+Right` |
| Delete | `Del` (tasks, the note or page on the Knowledge list) |
| Delete the block in a document | `Ctrl+Shift+K` |
| Board / List / Calendar / Docs | `Alt+B` / `Alt+L` / `Alt+C` / `Alt+D` |
| Day / week / month | `Alt+Shift+D` / `Alt+Shift+W` / `Alt+Shift+M` |
| Back to today | `Alt+T`; on the Calendar's grid also `T` |
| Previous / next period on the Calendar's grid (as on its ‹ › buttons) | `P` / `N`, or `[` / `]` |
| A day back / forward | `Ctrl+Alt+Left` / `Ctrl+Alt+Right` |
| Go to a date | `Ctrl+G` |
| Block longer / shorter | `Ctrl+Shift+Down` / `Ctrl+Shift+Up` |
| Back / forward | `Alt+Left` / `Alt+Right` |
| Week recap | `Alt+R` |
| Command line on its commands | `F1` |
| Redo | `Ctrl+Y` or `Ctrl+Shift+Z` |

Everything else (`Ctrl+K`, `Ctrl+N`, `Ctrl+1`…`Ctrl+9` — Today, Tasks, Notes, Calendar, Docs, then My views — `/`, `?`, `[` / `]`,
`F6`, the profile and zoom keys) is the same in both keymaps.

## How the keys read

- **Single letters act on the task under the cursor**, and only while the
  focus is in the content — never in a text field, a dialog or a menu. Chords
  with `Ctrl` work everywhere outside a modal.
- **Case is Vim's**: `d` and `Shift D` are different keys. A lowercase letter
  is pressed without Shift; Shift is written as a word.
- **Prefixes**: `g` (go), `y` (copy), `z` (view), `c` (create) wait a second
  for their second key; a bar at the bottom shows what can follow, `Esc`
  cancels, and a prefix with nothing after it does nothing.
  A letter that is also the start of a sequence live where you are is only
  that start: `d` in the task document, where `d d` deletes the block, does
  nothing on its own or before another key — a slow `d d` never closes the
  task. Everywhere `d d` means nothing, `d` is Done at once.
- **Physical keys**: keys are read by their place on the keyboard, so they work
  the same in the Russian layout (`g b` is `п и`, `Ctrl+K` is `Ctrl+Л`).
- Not a Vim emulator: no counts, no operators with motions, no registers.
  Pressing `d` twice within half a second (the `dd` habit) does not take Done
  back.

## Who gets a key

Keys go to the innermost thing that holds the keyboard:

- `Esc` closes a menu, dialog, the task document or the command line first; a
  selection and the board cursor are let go of last. In the filter line `Esc`
  clears the text, and a second `Esc` (or `Enter`) hands the keyboard back to
  the view.
- The view keys (letters, arrows, `Enter`, `Esc`, `Del`, `Ctrl+A`) stand down
  while a text field, dialog, popup, menu or inline rename has focus, and while
  a control outside the view that you reached with `Tab` has it — press `Esc`
  there to give the keyboard back to the view.
- The global shortcuts stand down behind a modal — the task or event editor,
  the command line, a capture popup, the welcome tour, a confirmation.

## Global

| Action | Default |
|--------|---------|
| Command line | `Ctrl+K` or `Ctrl+P` |
| Command line on its commands | `:` |
| Section filter | `/`, `Ctrl+F` or `Ctrl+L` |
| New task (in Notes: a new note) | `Ctrl+N` |
| Quick-capture task (from any app) | `Ctrl+Shift+Space` |
| Quick-capture note | `Ctrl+Shift+N` |
| Undo | `U` or `Ctrl+Z` (not while a dialog is open; in a text field `Ctrl+Z` undoes the typing) |
| Redo | `Ctrl+R` or `Ctrl+Shift+Z` |
| Cheat sheet | `?` or `Ctrl+/` |
| Settings | `Ctrl+,` |
| Toggle light / dark | `Ctrl+Shift+T` |
| New contact | `Ctrl+Shift+U` |
| Event log | `Ctrl+Shift+L` |

## Go to — g

| Where | Default |
|--------|---------|
| Today | `G, T` or `Ctrl+1` |
| Tasks, on the last lens | `Ctrl+2` |
| Tasks · Board | `G, B` |
| Tasks · List | `G, L` |
| Notes | `G, N` or `Ctrl+3` |
| Calendar (on the zoom left last) | `Ctrl+4` or `G, C` |
| Docs | `Ctrl+5` or `G, D` |
| The week's summary | `G, W` |
| My view 1 … 9 | `G, 1` `G, 2` `G, 3` `G, 4` `G, 5` `G, 6` `G, 7` `G, 8` `G, 9`; also `Ctrl+6` `Ctrl+7` `Ctrl+8` `Ctrl+9` for the first four |
| Open the task in its tracker | `G, X` |
| Back / forward through where you have been | `Ctrl+O` / `Ctrl+I` |

The archive has no key of its own (it is the `is:archived` filter) and can be
given one.

## Command line

`Ctrl+K` opens it, `Ctrl+K` again closes it. What you type is read in the
language quick capture speaks: `p0` … `p3` priority, `until fri` / `до пт`
a due date, `#label`, a column by the start of a word (`block`, `заблок`),
`APP-101` a task by ID, `>` commands only. A finished condition becomes a chip
(`Backspace` on an empty line takes the last one back); `Tab` makes a chip of
the word still being typed. The syntax card beside the results can be hidden.

Results come in groups: the tasks found (fifty, then "more — show as a list";
archived ones last), what to do with them (*Mark done*, *Unblock*), what to do
with this filter (*Save as view*, *Open as board*, *Open as list*), the task the
cursor was on, the commands, then notes, docs and people. Nothing found offers
to create the task.

| Action | Key |
|---|---|
| Next / previous result | `↓` / `↑` |
| Run | `Enter` |
| Everything found as a list in Tasks | `Ctrl+Enter` |
| Save the filter as a view | `Ctrl+S` |
| Take the hint | `Tab` |
| Close | `Esc` |

## Task under the cursor

Every view has one keyboard cursor (0.8.0): the card on the board, the row
in the List, the meeting or task on Today and in the calendar, the note in
Notes. Without one the keys act on the selection or the task under the
pointer.

| Action | Default |
|--------|---------|
| Done; on a done task, back | `D` |
| New task below / above (same column) | `O` / `Shift+O` |
| Rename | `I` |
| Schedule: a small field that reads "fri 15:00", "tomorrow", "no" (on an empty calendar day: go to a date) | `S` |
| Due date, the same way | `Shift+S` |
| Priority P0 … P3 | `1` `2` `3` `4` |
| Timer start / pause | `T` |
| Estimate of the open task: a field under its estimate chip ("1d 2h", "90m", "1.5h"; `Enter` saves, `Esc` cancels). In the document only; on the board `E` stays Archive | `E` |
| Archive | `E` |
| Menu | `M` (or the `Menu` key) |
| Copy ID / branch name / tracker link | `Y, Y` / `Y, B` / `Y, L` |
| Create a git branch | `C, B` |
| The card's failed CI: jobs and the log's end | `C, I` |
| Whom the task's merge request waits on → Who to message (one undo) | `Shift+P` |
| Move to another profile (a list: type to filter, `Enter`; one undo in either profile) | `Shift+M` |
| Delete the selection, or the task under the cursor (undoable) | `Del` |

## Board cursor

Arrow keys work alongside the letters.

| Action | Default |
|--------|---------|
| Next / previous card | `J` / `K` |
| Previous / next column | `H` / `L` |
| First / last card of the column | `G, G` / `Shift+G` |
| Half a screen down / up | `Ctrl+D` / `Ctrl+U` |
| Open the card | `Return` |
| Add to the selection | `Space` or `V` |
| Select a range (then `J` / `K`) | `Shift+V` |
| Move the card (left / right: the selection, when there is one) | `Shift+J` / `Shift+K` / `Shift+H` / `Shift+L`, or `Ctrl+↓` / `Ctrl+↑` / `Ctrl+←` / `Ctrl+→` |
| Grow / shrink the selection down / up | `Shift+Down` / `Shift+Up` |
| Select the whole column, then step left / right | `Shift+Left` / `Shift+Right` |
| Fold / unfold the cursor's column | `Z, A` |

The List lens of Tasks walks with the same keys (`J` / `K`, `Return`,
`Space` or `V` to mark, `M`, `E`, `S`, `1`–`4`, `D`); `Z` folds the group the
cursor is in.

While a card's menu is open its arrows and letters belong to the menu.

**Type to search.** On the board, start typing a letter that is no key of its
own: the filter opens with it and the board narrows as you go.

## Calendar

| Action | Default |
|--------|---------|
| Back to today | `0`; on the Calendar's grid also `T` (the timer's `T` stands down there) |
| Previous / next period (a day on Today; a day, week or month in the Calendar, as it shows) | `[` / `]`, `P` / `N` |
| Calendar: day / week / month | `Z, D` / `Z, W` / `Z, M` |
| Day panel: previous / next day | `Alt+Left` / `Alt+Right` |
| New event at the next free slot | `Ctrl+Alt+E` |
| Move the task a day earlier / later | `Ctrl+Left` / `Ctrl+Right` |
| Move the task a week earlier / later | `Ctrl+Shift+Left` / `Ctrl+Shift+Right` |
| Move a timed task a grid step earlier / later | `Ctrl+Up` / `Ctrl+Down` |
| The cursor: next / previous meeting or task of the day | `J` / `K` |
| The cursor: the day before / after | `H` / `L` |
| Open what the cursor is on | `Return` |
| Move it a day / a grid step (a week in Month) | `Shift+H` / `Shift+L`, `Shift+J` / `Shift+K` |
| Its block a grid step longer / shorter | `Ctrl+Shift+J` / `Ctrl+Shift+K`, or `Ctrl+Shift+Down` / `Ctrl+Shift+Up` |

A meeting that repeats asks "only this one / all" before it moves, as a drag
does. Today walks with the same keys. `Del` on a meeting deletes it (Undo in the toast); on
one that repeats, or a read-only one from a calendar by link, it opens the
meeting's menu in the week and says why, and what to press instead, on
Today and in the month.

To go to a date, type it in the command line: `:` then the date.

The Calendar is its own place in the sidebar (`Ctrl+4`). Its ‹ Today › buttons
and the Day / Week / Month tabs show their key only under the pointer or with
the keyboard on them. **Layers** under the sidebar's places choose what the
grid shows: meetings, tasks with a time, reminders, due dates.

The task moves are the keyboard side of drag-to-reschedule, live in Week,
Month and Timeline only (on the board `Ctrl`+arrows move cards). Each move is
one undo step, with an undo toast. While dragging a task the target date is
shown at the pointer, and `Esc` cancels the drag.

In the date picker: arrows move the day (↑/↓ a week), `PgUp`/`PgDn` a month
(`Shift` a year), `Home`/`End` the month's ends, `T` today, `Enter` picks,
`Esc` closes. In the event editor, `Ctrl+Enter` saves; with unsaved changes
the first `Esc` warns and the second discards.

## Menus and drop-downs

A list opened from a menu row (Priority ›, Column ›) starts with "‹ back";
`←` or `Esc` goes back to the menu, a second `Esc` closes it. Each row shows
its key from the catalog; in Priority `1`–`4` pick, in Column the column's
number. Typing letters finds a row by the start of its name (a pause of a
second starts over); in a drop-down the letters open the list on the match and
`Enter` takes it.

## What changed for 0.7 users

| Was | Now |
|---|---|
| `Ctrl+1`…`Ctrl+8` (Board, Timeline, Week, Month, Archive, Docs, Notes, Settings) | `Ctrl+1`–`Ctrl+3` sections, `G, B` / `G, L` / `G, C` / `G, N`, `Ctrl+,`; archive is a filter |
| `O` open in tracker | `G, X` |
| `Z` fold column | `Z, A` |
| `T` back to today | `0`; `T` is the timer |
| `G` go to date | `:` and the date; `Shift+G` is "to the last" |
| 0.9: `Ctrl+4` / `Ctrl+5` My views 1 and 2 | `Ctrl+4` the Calendar, `Ctrl+5` Docs; My views are `Ctrl+6`…`Ctrl+9` |

The first time after the update that you press one of the old keys you
actually used, a toast says once where its action went. Keys you rebound
yourself are not touched.

## Interface scale

| Action | Default |
|--------|---------|
| Zoom in | `Ctrl+=` (also the fixed aliases `Ctrl++`, `Ctrl+Shift+=`, numpad `Ctrl++`) |
| Zoom out | `Ctrl+-` (also numpad `Ctrl+-`) |
| Back to 100 % | `Ctrl+0` (also numpad `Ctrl+0`) |

## Saved views

A saved view is a named set of filters plus the view it opens in. They are
listed in the sidebar under *My views*, numbered; `G, 1` … `G, 9` and
`Ctrl+6` … `Ctrl+9` (the first four) apply them. In the sidebar list (`Tab` to it):

| Action | Key |
|---|---|
| Previous / next view | `↑` / `↓` |
| Apply | `Enter` |
| Move the view up / down | `Ctrl+↑` / `Ctrl+↓` |
| Rename | `F2` |
| Delete (Undo in the toast, or `Ctrl+Z`) | `Del` |
| Menu | `Menu` or `Shift+F10`, or right click |

## Profiles

| Action | Default |
|--------|---------|
| Next profile | `Ctrl+]` |
| Previous profile | `Ctrl+[` |
| New profile | `Ctrl+Shift+P` |
| Export active profile to Markdown (clipboard) | `Ctrl+Shift+E` |
| Weekly shipped report (clipboard) | `Ctrl+Shift+W` |
| Sync every tracker now | `Ctrl+Shift+R` |

## Panels

| Action | Default |
|--------|---------|
| Expand / collapse the sidebar | `Ctrl+Shift+B` |
| Focus mode on / off (once turned on in Settings → Safety net; `Esc` also leaves it) | `Ctrl+Shift+F` |

## Selection

| Action | Default |
|--------|---------|
| Select all visible tasks | `Ctrl+A` |
| Clear selection | `Esc` |
| Delete selection (undoable 5 s) | `Del` |

On a selection `D`, `S`, `1`–`4` and `T` act on every selected task; on the board `E` too.

## Task editor

Fixed keys, live while the editor is open.

| Action | Key |
|---|---|
| Save | `Ctrl+Enter` |
| Close without saving | `Esc` |
| Next / previous field (leaves the description too) | `Tab` / `Shift+Tab` |
| Indent / outdent a list line in the description | `Tab` / `Shift+Tab` on that line |
| Insert indentation anywhere in the description | `Ctrl+Tab` |
| Open / close Details, switch edit ↔ preview | `Tab` to it, then `Space` / `Enter` |
| Attach files (file dialog, several at once) | `Ctrl+Shift+A` |
| On an attachment chip: open / show in folder / detach | `Enter` / `Shift+Enter` / `Del` |
| Paste a screenshot or copied files into the description (stored, linked at the cursor) | `Ctrl+V` |

A new task's editor opens with the cursor in the title.

## Moving around without a mouse

| Where | Keys |
|---|---|
| Regions: sidebar → content → task panel / right panel → header | `F6` / `Shift+F6` (also out of a text field) |
| Sidebar | `↑` / `↓` walk the rows, `Enter` opens (the keyboard goes into the content) |
| Board column header | `Ctrl+Shift+H` / `Ctrl+Shift+L` move the column (as its menu's Move left / right) |
| Filter bar (P0–P3, Clear, Sort, Archived) | `Tab` to a chip, `Space` / `Enter`; `↓` opens Sort |
| Profile pill, breadcrumbs | `Tab`, then `Enter` (menu / edit; `F2` edits a crumb) |
| Mini week | `←` / `→` a day, `PgUp` / `PgDn` a week, `Home` today |
| Day panel | `←` / `→` a day, `Home` today, `↑` / `↓` walk the events, `Enter` opens one |
| People list | `↑` / `↓`, `Enter` edits, `Menu` or `Shift+F10` for actions |
| Sidebar "Waiting on me" row | `S` (Vim) / `Ctrl+Shift+S` (Classic) puts it off until tomorrow's workday; Undo brings it back |
| Sidebar "Who to message" row | `Del` — done with my own ask (Undo brings it back) |
| Settings | `Enter` in the search opens the first match, `↓` into the sections, `↑` / `↓` move between them; switches `Space`, segmented rows `←` / `→`, sliders `←` / `→` |
| Tweaks, Hotkeys panels, cheat sheet | `Tab` through every control; `Esc` closes |

## Notes view

Rebindable in Settings → Hotkeys; live only while Notes is on screen.

| Action | Shortcut |
|---|---|
| New note (cursor goes in) | `Ctrl+Alt+N`, or `Ctrl+N` while in Notes |
| Back to the note opened before (tap again to come back) | `Ctrl+Tab` |
| Further back through recent notes | hold `Ctrl`, press `Tab` again (`Shift+Tab` steps back); let go of `Ctrl` to open, `Esc` keeps the note |
| Recent notes from the oldest | `Ctrl+Shift+Tab` |
| Close the note (back to the one before) | `Ctrl+W` |
| Next / previous note in the list | `Ctrl+PgDown` / `Ctrl+PgUp`, or `Alt+Down` / `Alt+Up` (from the note's text too) |
| Rename or re-file the open note | `F2` |
| Show / hide the list of notes | `Ctrl+Alt+L` |
| Filter the notes (title, folder and body) | `Ctrl+F` |
| Delete the open note or doc page (keyboard on the list; Undo brings it back) | Vim `D, D` or `Del`, Classic `Del` |

Arriving in Notes (`G, N`, `Ctrl+3`, a click on the sidebar) opens a new,
empty note with the cursor in it; the first line of text you type names it
(tables, code and rules are skipped) until you rename it with `F2`. Leave it
empty and it is gone again — nothing piles up. The note you had open is one
`Ctrl+Tab` away. A link, a search hit or the palette opening a particular note
opens that note instead.

In the drawn (live) mode the keyboard lands on the note as drawn, not in its
text: the keymap's chords work there and the line above the title says
`Enter — write`. `Return` (or
any letter that is no key) opens the text; `Esc` steps out — text → note →
list, and stays on the list. `F6` walks the list, the note and its links
before it leaves the screen. `Tab` reaches every link in the note, `Return`
follows it. `U` on the note takes back the note's own last edit, then only
what was done to notes and pages; a change made on another screen is
`Ctrl+Z`'s.

## Notes and doc page editor

These work while the cursor is in the notes editor or a Docs page, and only
there — `Ctrl+K` still opens the command line everywhere else. With text
selected, the formatting keys wrap the selection, and `Tab`, `Shift+Tab` and
the heading key act on every selected line. `Ctrl+Z` with nothing left to undo
in the text undoes the last app action (a deleted note or page, an import).

| Action | Shortcut |
|---|---|
| Bold | `Ctrl+B` |
| Italic | `Ctrl+I` |
| Inline code | `Ctrl+E` |
| Link | `Ctrl+K` |
| Strikethrough | `Ctrl+Shift+X` |
| Highlight (`==text==`) | `Ctrl+Shift+H` |
| Heading level — cycles none → H1 … H6 → none | `Ctrl+Shift+L` |
| Indent / outdent list line | `Tab` / `Shift+Tab` |
| Tick the checkbox on this line | `Ctrl+Enter` |
| Delete the block (the paragraph, list or code block; Undo brings it back) | Classic `Ctrl+Shift+K` while typing; Vim `D, D` on the drawn document, for the block last edited |
| Cycle edit → split → preview | `Ctrl+Shift+M` |
| Attach files to the note (linked at the cursor) | `Ctrl+Shift+A` (notes editor) |
| Paste a screenshot or copied files as attachments | `Ctrl+V` (notes editor; text pastes as text) |
| Paste a copied table (web page, spreadsheet, spec) as a markdown table | `Ctrl+V` (notes editor) |
| Paste as plain text (no table, no attachment) | `Ctrl+Shift+V` (notes editor) |

In a table of the notes editor (rebindable in Settings → Hotkeys; also in the
right-click menu, the command line, and the `/` menu in an empty cell or on
the line under a table):

| Action | Shortcut |
|---|---|
| Add row above / below | `Ctrl+Alt+Up` / `Ctrl+Alt+Down` |
| Add column left / right | `Ctrl+Alt+Shift+Left` / `Ctrl+Alt+Shift+Right` |
| Delete row | `Ctrl+Alt+Backspace` |
| Delete column | `Ctrl+Alt+Shift+Backspace` |

The right edge of the note's text column is drawn as a line; drag it to make
the column wider or narrower, double-click it to go back to the default. Code
blocks and tables have a second, fainter edge of their own. Both are numbers
in Settings → Appearance → Notes too.

`Enter` continues whatever the line is: another bullet, the next number,
another unticked checkbox, another quote marker. On an empty item it removes
the marker instead, which is how you end a list. Inside a fenced code block
`Enter` keeps the indentation and `Tab` inserts spaces.

Pasting a URL over selected text turns it into a link.

Every one of these is a single undo step.

## Notes

- **Quick-capture** opens a popup that parses your text as you type
  — see [TUTORIAL.md](TUTORIAL.md#quick-capture-syntax) for the syntax.
  `Return` (or `Ctrl+Return`) adds and closes, `Ctrl+Shift+Return` adds and
  stays open for the next item; `Shift+Return` starts a new line (the lines
  after the first are the description). `Ctrl+M` makes a task with a time a
  calendar meeting too. `Tab` opens what was typed in the task document; while
  suggestions are open, `Tab` (or the arrows, then `Return`) takes one. The
  quick note: `Ctrl+Return` saves, `Return` is a new line, `Esc` closes it and
  keeps the text as a draft for next time. From
  another app it comes up on its own, without the main window, and a
  notification confirms what was created.
- **Task editor:** `Ctrl+Return` saves; in a new task's title `Return` creates
  it. `Esc` (or a click on the backdrop) closes
  a task with no changes; with changes it asks — `Return` saves, `Ctrl+D` discards,
  `Esc` keeps editing.
