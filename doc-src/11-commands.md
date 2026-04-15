**[← Effort model](10-effort-model.md)** | **[Index](./README.md)** | **[Next: Workflows →](12-workflows.md)**

---

# 11. Commands reference

| Command                                    | Purpose                                              |
|--------------------------------------------|------------------------------------------------------|
| `:Vimfy start <name>`                      | Mark a session start (alphabetic alias, e.g. `a`).   |
| `:Vimfy watch <name>`                      | Start a Watch session (auto-end on idle).            |
| `:Vimfy end <alias>`                       | Finish a manual session (Mark or Watch) and optimize.|
| `:Vimfy recall <N\|Ns>`                    | Finish a retrospective recall window.                |
| `:Vimfy close <alias>`                     | Discard a session without optimizing.                |
| `:Vimfy save <selector>\|@ [<name>]`       | Save a finished result to disk. Name defaults to selector. `@` = last finished. |
| `:Vimfy sim <alias> [count] [ms]`          | Animate results side-by-side.                        |
| `:Vimfy view [name]`                       | View a saved result (or list saved names).           |
| `:Vimfy list`                              | List active sessions and saved files.                |
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
| `close` / `sim`    | Any of the three: alphabetic, `N`, or `Ns`. |

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

## Manual handles vs. saved names

These are separate namespaces. A manual handle (e.g. `refactor`) points
to an in-memory session you can `end` / `close` / `sim`. A saved name
(e.g. `refactor`) points to a finished result on disk, viewed with
`:Vimfy view`. The same text is allowed in both — the command grammar
disambiguates which one you mean.

## Tab completion

Works on:

- Subcommands (`:Vimfy s<Tab>` → `save`, `sim`, `start`, `suggest`).
- Manual (alphabetic) handles for `start`, `watch`, and `end`.
- `3s`/`5s`/`10s`/`30s` hints for `recall`.
- All active/recall aliases plus time hints for `close` / `sim`.
- Selectors (plus `@`) for `save`.
- Saved names for `view`.
- `on` / `off` / `toggle` for `suggest`.

## `<Plug>` mappings

For each subcommand above, a `<Plug>VimfyX...` map is also registered so
you can bind keys without having to type `:Vimfy ...` (which would count
as admin activity anyway). See [8. Keymaps](08-keymaps.md) for the full
list and the binding contract.

---

**[← Effort model](10-effort-model.md)** | **[Index](./README.md)** | **[Next: Workflows →](12-workflows.md)**
