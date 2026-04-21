# Session storage: workspace (memory) vs. archive (disk)

A finished session result lives in one of two places at any given time. This
note is the Lua-layer reference for how they're indexed, how code moves
results between them, and what the disk JSON looks like. The user-facing
version is `doc-src/07a-session-storage.md`; this page covers the implementation.

## Indexing

- `session/store.lua` owns the in-memory side:
  - `session_records[id]` — the canonical table keyed by stable session id.
    One entry per session over its entire lifecycle (active → finished).
  - `manual_alias_to_id[alias] -> id` — workspace lookup for Mark aliases
    and for records inserted via `register_fetched_result`.
  - `recall_id_order[]` — ordered deque of Recall/Suggest ids (oldest → newest).
  - `last_finished_id` — backs the `@` shorthand selector.
- `session/init.lua` owns the disk side. Files live at
  `stdpath('data')/vimficiency/saved/<name>.json`, one result per file.

Aliases and saved-filenames are **separate namespaces**. `alias.is_valid_manual`
matches `^%a+$`; `alias.is_valid_saved_name` matches `^[%w_][%w._-]*$`. The same
text is allowed in both because the command grammar (`save`/`store` work on
aliases, `fetch`/`view`/`rm` work on filenames) disambiguates.

## Public APIs

### Workspace side (in `session/store.lua`)

| Function                               | Use                                                           |
|----------------------------------------|---------------------------------------------------------------|
| `get_result(alias) -> ResultSession?`  | Resolve any alias (manual or recall) to its stored result.    |
| `has_result(alias) -> boolean`         | Same, boolean.                                                |
| `get_id(alias) -> id?`                 | Alias → canonical session id (for `remove`, `finish_session`).|
| `get_last_finished_id() -> id?`        | Id behind `@`.                                                |
| `get_last_finished_result() -> result?`| Convenience: result behind `@`.                               |
| `get_last_finished_alias() -> string?` | Literal alias the user passed to the last finish.             |
| `register_fetched_result(alias, result) -> id?, err?` | Insert a disk-loaded result as a finished record. Refuses if alias is occupied. |
| `remove(id)`                           | Drop the record and its index entries. `id`, not alias.       |

### Archive side (in `session/init.lua`)

| Function                      | Use                                                              |
|-------------------------------|------------------------------------------------------------------|
| `M.save(selector, name)`      | Copy workspace→archive; WARN-overwrites an existing file.        |
| `M.store(selector, name)`     | Move workspace→archive; WARN-overwrites + removes workspace alias.|
| `M.fetch(name, alias)`        | Copy archive→workspace; refuses if `alias` is occupied.          |
| `M.rm(name)`                  | Delete archive file.                                             |
| `M.list_saved() -> string[]`  | Filenames currently on disk.                                     |
| `M.view(name)`                | Display a saved result textually (no replay).                    |
| `M.simulate(alias, count)`    | Workspace-first lookup with disk fallback + implicit fetch.      |

All four verbs validate names through `alias.is_valid_saved_name` before
concatenating into a filesystem path.

## Collision policy

- `save` / `store` onto an existing disk file: overwrites the file and
  emits a WARN (`vim.notify(..., vim.log.levels.WARN)`). No refusal. The
  shared helper in `session/init.lua` is `write_to_disk_with_overwrite_warn`.
- `fetch` into an occupied workspace alias: **refuses**
  (`register_fetched_result` returns `nil, err`; caller surfaces the error).
- `:Vimfy play <x>` branches by where `<x>` exists:
  - Both: WARN, workspace wins.
  - Workspace only: normal path.
  - Disk only: implicit `fetch x as x` (only for alpha-alias names);
    non-alpha filenames must fetch explicitly under a different alias.
  - Neither: error.

## JSON schema on disk

Produced by `vim.json.encode(result, { indent = true })` on a
`ResultSession` table. Fields:

```json
{
  "lines":            ["..."],
  "start_row":        0,
  "start_col":        0,
  "end_row":          0,
  "end_col":          0,
  "user_seq":         "cw xxx <Esc>",
  "user_cost":        15.0,
  "optimal_results":  [{ "seq": "...", "cost": 8.0 }, ...],
  "start_time":       123456789,
  "key_count":        17,
  "timestamp":        123456999,
  "finish_reason":    "manual"
}
```

`finish_reason` ∈ `{"manual", "watch_idle", "suggest_idle", "suggest_keys",
"suggest_cost"}`.

When the file is loaded back by `fetch` (or by the implicit fallback in
`sim`), `register_fetched_result` wraps it in a minimal `SessionRecord`
with `status = "finished"`, `start_kind = end_kind = "manual"`,
`finish_alias = <target alias>`, and `key_nsid = -1` (no key tracking).

## Invariants worth preserving

- `session_store.remove` takes an **id**, not an alias. Aliases are
  time-varying (recall ring rotates); passing an alias risks removing
  the wrong record. Resolve via `get_id` at the call site first.
- `register_fetched_result` sets `last_finished_id` so a `:Vimfy save @`
  immediately after a fetch still works.
- The disk file is untouched by `fetch` — fetch is always a copy, never
  a move. If a user wants to delete from disk, they call `:Vimfy rm`.
