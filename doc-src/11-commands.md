---
title: "Commands reference"
---

# Commands reference

| Command                                    | Purpose                                              |
|--------------------------------------------|------------------------------------------------------|
| `:Vimfy start <name>`                      | Mark a session start (alphabetic alias, e.g. `a`).   |
| `:Vimfy watch <name>`                      | Start a Watch session (auto-end on idle).            |
| `:Vimfy finish <alias>`                    | Finish a manual session (Mark or Watch) and optimize.|
| `:Vimfy recall <N\|Ns>`                    | Finish a retrospective recall window.                |
| `:Vimfy close <alias>`                     | Discard a session without optimizing.                |
| `:Vimfy save <selector>\|@ [<name>]`       | Copy a finished result to disk (keeps the workspace copy). Name defaults to selector. `@` = last finished. |
| `:Vimfy store <selector>\|@ [<name>]`      | Move a finished result to disk (removes from workspace). |
| `:Vimfy fetch <name> [<alias>]`            | Copy a saved result from disk into the current workspace. |
| `:Vimfy play <alias> [count]`              | Animate results side-by-side (memory first, disk fallback). |
| `:Vimfy focus <N>`                         | Focus the active replay on the Nth buffer (full-screen).|
| `:Vimfy escape`                            | Restore the side-by-side replay layout.              |
| `:Vimfy view [name]`                       | View a saved result (or list saved names).           |
| `:Vimfy rm <name>`                         | Delete a saved result from disk.                     |
| `:Vimfy list`                              | Open the interactive session picker (see below).     |
| `:Vimfy stats`                             | Show lifetime session stats (see below).             |
| `:Vimfy suggest <on\|off\|toggle>`         | Runtime toggle for auto-suggest (config-driven).     |
| `:Vimfy config`                            | Show the current configuration.                      |
| `:Vimfy reload`                            | Rebuild the C++ library (needs restart).             |
| `:Vimfy help`                              | Show the command list.                               |

## Alias grammar

The `<alias>` argument splits by subcommand:

| Subcommand         | Accepts                                     |
|--------------------|---------------------------------------------|
| `start` / `watch`  | Alphabetic only (`a`, `refactor`).          |
| `finish`           | Alphabetic only — manual handles.           |
| `recall`           | `N` (digits) or `Ns` (digits + `s`).        |
| `close` / `play`   | Any of the three: alphabetic, `N`, or `Ns`. `play` also accepts a saved name. |
| `save` / `store`   | Any of the three, plus `@` for last finished. Same grammar. |
| `fetch`            | Saved name → new workspace alias (alphabetic). |

| Form       | Means                                                                |
|------------|----------------------------------------------------------------------|
| `a`, `foo` | Alphabetic → a manual session name.                                  |
| `6`        | Digits → recall N keys ago.                                          |
| `3s`       | Digits + `s` → recall N seconds ago (see [5. Recall](05-recall.md)). |

`save` accepts the full grammar plus `@`, which resolves to the most
recently finished session — handy when the session you want to keep is a
recall window whose alias (`3s`) is moving out from under you:

```
:Vimfy recall 3s
:Vimfy save @ nested-refactor
```

`:Vimficiency` is accepted as a full-name alias for `:Vimfy`.

## Workspace vs. storage

Finished sessions live in two places: **workspace** (in-memory,
indexed by alias) and **storage** (on disk, indexed by filename). The
namespaces are separate; the same text can name both. Move results
between them with `save` / `store` / `fetch`; see
[7a. Session storage](07a-session-storage.md) for the full model and
collision rules. Omitting the second argument on `save` / `store` /
`fetch` repeats the first argument verbatim.

## Session picker (`:Vimfy list`)

Opens a centered float with two panes — **Active** (in-memory workspace
sessions) and **Saved** (files under `stdpath("data")/vimficiency/saved`)
— and a live-filter prompt at the bottom. `<Tab>` toggles between panes.

Ongoing sessions always sit in a pinned **Ongoing** section at the top,
regardless of sort mode. Auto-generated recall ring entries (one per
keystroke) are folded into a single `recall ring (N)` summary row — the
per-entry aliases drift, so they're not individually actionable from the
picker; resolve a specific window with `:Vimfy recall N` or `:Vimfy
recall Ns` instead.

| Key        | Action                                                |
|------------|-------------------------------------------------------|
| `/`        | Fuzzy-filter by alias (prompt at bottom, live)        |
| `<CR>`     | Open (default action, see below)                      |
| `<Tab>`    | Switch Active ↔ Saved pane                            |
| `sn`/`sN`  | Sort by name (A→Z / Z→A)                              |
| `sc`/`sC`  | Sort by category (A→Z / Z→A)                          |
| `st`/`sT`  | Sort by time (newest / oldest)                        |
| `s`        | Sort hint popup (also on `s?`)                        |
| `d`        | Delete the current entry                              |
| `m`        | Toggle mark on the current entry                      |
| `D`        | Delete all marked entries                             |
| `r`        | Rename (see below)                                    |
| `y`        | Duplicate (see below)                                 |
| `?`        | Show the keymap popup                                 |
| `q`, `<Esc>` | Close                                               |

Inside the search prompt, `<C-n>` / `<C-p>` (and `<Down>` / `<Up>`) move
the list cursor without leaving insert mode; `<CR>` opens the current
entry; `<Esc>` returns to the list.

**Per-pane action semantics:**

- `<CR>` — on the Active pane, opens a `:Vimfy play`-style view of a
  finished session; on the Saved pane, fetches the file into the
  workspace and switches focus to Active.
- `d` — on Active, discards the session (equivalent to `:Vimfy close`);
  on Saved, deletes the file on disk.
- `r` / `y` — on Saved, renames/duplicates the file. On Active, renames
  the manual alias / registers a duplicate finished record under a new
  alias. Only entries with a stable manual alias are renameable this
  way; for recall and auto-suggest results, save to disk first (`:Vimfy
  save <alias> <name>`) and then rename from the Saved pane.

**Blocked on in-progress sessions:** `<CR>`, `r`, `y` all refuse to run
on a still-accumulating session. Finish it with `:Vimfy finish` (or let
Watch's idle trigger fire) and try again.

## Session stats (`:Vimfy stats`)

Opens a read-only float summarizing your lifetime activity. Close with
`q` or `<Esc>`.

Sections:

- **Lifetime** — total finished sessions, broken down by type (mark /
  watch / recall / suggest), and total captured keystrokes.
- **Efficiency** — a score out of 100, defined per session as
  `min(best_optimizer_cost / user_cost, 1) × 100` (capped at 100 so a
  beat doesn't inflate the average). Aggregated as a geometric mean.
  The 30-day block below the score is a sparkline of the daily
  geometric mean (▁ = 0, █ = 100; blanks = no sessions that day).
- **Motions** — for every motion molecule that appears at least ten
  times combined across your sequences and the optimizer's best
  suggestions, the ratio `your_proportion / optimizer_proportion`. Two
  lists: what you use *more* than the optimizer (potential inefficient
  habits) and what you use *less* (candidates to practice). Only the
  trailing character of `f`/`F`/`t`/`T`/`r`/`R` is collapsed to `_` —
  everything else is reported as-is, since further grouping hides
  signal.
- **Dev — Optimizer beats queued** — count of finished sessions where
  your sequence came in cheaper than the optimizer's best result.
  These are flagged (`beats: true`) on their log lines. **Beats are
  interesting-to-study, not ground truth**: many are cost-model
  artifacts or unexplored regions, so they are *not* fed back into
  training without manual review.

### Where the data lives

Every finished session (mark, watch, recall, suggest) appends one JSON
object to `stdpath("data")/vimficiency/sessions.jsonl`. The file is
append-only, one record per line. Stats read the whole log on demand —
no aggregate cache in v1, so changing the efficiency formula or adding
new buckets only requires re-reducing, not a migration. A trailing
malformed line (e.g. a crash mid-write) is silently skipped on read.

Per-record schema (version `v: 1`): `finish_epoch`, `type`,
`finish_reason`, `key_count`, `user_cost`, `best_opt_cost`, `user_seq`,
`best_opt_seq`, `beats`. Only the single lowest-cost optimal result is
stored; Top-N alternatives are not retained at log-write time.

## Tab completion

Works on:

- Subcommands (`:Vimfy s<Tab>` → `save`, `start`, `stats`, `store`, `suggest`).
- Manual (alphabetic) handles for `start`, `watch`, and `finish`.
- `3s`/`5s`/`10s`/`30s` hints for `recall`.
- All active/recall aliases plus time hints for `close`.
- Active/recall aliases, time hints, and saved names for `play` (matches its disk fallback).
- Selectors (plus `@`) for `save` and `store`.
- Saved names for `view`, `rm`, and `fetch`.
- `on` / `off` / `toggle` for `suggest`.

## `<Plug>` mappings

For each subcommand above, a `<Plug>VimfyX...` map is also registered so
you can bind keys without having to type `:Vimfy ...` (which would count
as admin activity anyway). See [8. Keymaps](08-keymaps.md) for the full
list and the binding contract.

## Scratch output buffer keys

Press `:h g?` in a Vimfy scratch output buffer to open this section
directly. This applies to buffers opened by `:Vimfy help` and other
detail panes that show Vimfy text output in a split.

- `q` — close the current Vimfy output buffer
- `?` — show the short keymap summary popup
- `g?` — open the full help section for scratch output buffers
