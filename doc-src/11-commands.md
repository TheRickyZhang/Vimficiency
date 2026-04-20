---
title: "Commands reference"
---

# Commands reference

| Command                                    | Purpose                                              |
|--------------------------------------------|------------------------------------------------------|
| `:Vimfy start <name>`                      | Mark a session start (alphabetic alias, e.g. `a`).   |
| `:Vimfy watch <name>`                      | Start a Watch session (auto-end on idle).            |
| `:Vimfy end <alias>`                       | Finish a manual session (Mark or Watch) and optimize.|
| `:Vimfy recall <N\|Ns>`                    | Finish a retrospective recall window.                |
| `:Vimfy close <alias>`                     | Discard a session without optimizing.                |
| `:Vimfy save <selector>\|@ [<name>]`       | Copy a finished result to disk (keeps the workspace copy). Name defaults to selector. `@` = last finished. |
| `:Vimfy store <selector>\|@ [<name>]`      | Move a finished result to disk (removes from workspace). |
| `:Vimfy fetch <name> [<alias>]`            | Copy a saved result from disk into the current workspace. |
| `:Vimfy sim <alias> [count]`               | Animate results side-by-side (memory first, disk fallback). |
| `:Vimfy focus <N>`                         | Focus the active replay on the Nth buffer (full-screen).|
| `:Vimfy escape`                            | Restore the side-by-side replay layout.              |
| `:Vimfy view [name]`                       | View a saved result (or list saved names).           |
| `:Vimfy rm <name>`                         | Delete a saved result from disk.                     |
| `:Vimfy list`                              | Open the interactive session picker (see below).     |
| `:Vimfy suggest <on\|off\|toggle>`         | Runtime toggle for auto-suggest (config-driven).     |
| `:Vimfy config`                            | Show the current configuration.                      |
| `:Vimfy reload`                            | Rebuild the C++ library (needs restart).             |
| `:Vimfy help`                              | Show the command list.                               |

## Alias grammar

The `<alias>` argument splits by subcommand:

| Subcommand         | Accepts                                     |
|--------------------|---------------------------------------------|
| `start` / `watch`  | Alphabetic only (`a`, `refactor`).          |
| `end`              | Alphabetic only — manual handles.           |
| `recall`           | `N` (digits) or `Ns` (digits + `s`).        |
| `close` / `sim`    | Any of the three: alphabetic, `N`, or `Ns`. `sim` also accepts a saved name. |
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
| `s`        | Cycle sort (category / alpha / created)               |
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

- `<CR>` — on the Active pane, opens a `:Vimfy sim`-style replay of a
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
on a still-accumulating session. Finish it with `:Vimfy end` (or let
Watch's idle trigger fire) and try again.

## Tab completion

Works on:

- Subcommands (`:Vimfy s<Tab>` → `save`, `sim`, `start`, `suggest`).
- Manual (alphabetic) handles for `start`, `watch`, and `end`.
- `3s`/`5s`/`10s`/`30s` hints for `recall`.
- All active/recall aliases plus time hints for `close`.
- Active/recall aliases, time hints, and saved names for `sim` (matches its disk fallback).
- Selectors (plus `@`) for `save` and `store`.
- Saved names for `view`, `rm`, and `fetch`.
- `on` / `off` / `toggle` for `suggest`.

## `<Plug>` mappings

For each subcommand above, a `<Plug>VimfyX...` map is also registered so
you can bind keys without having to type `:Vimfy ...` (which would count
as admin activity anyway). See [8. Keymaps](08-keymaps.md) for the full
list and the binding contract.

## Scratch output buffer keys

Press `:h g?` in a Vimficiency scratch output buffer to open this section
directly. This applies to buffers opened by `:Vimfy help` and other
detail panes that show Vimficiency text output in a split.

- `q` — close the current Vimficiency output buffer
- `?` — show the short keymap summary popup
- `g?` — open the full help section for scratch output buffers
