**[← Effort model](09-effort-model.md)** | **[Index](./README.md)** | **[Next: Workflows →](11-workflows.md)**

---

# 10. Commands reference

| Command                                    | Purpose                                              |
|--------------------------------------------|------------------------------------------------------|
| `:Vimfy start <name>`                      | Start a manual session (alphabetic, e.g. `a`).       |
| `:Vimfy end <alias>`                       | Finish and optimize.                                 |
| `:Vimfy close <alias>`                     | Discard a session without optimizing.                |
| `:Vimfy save <selector>\|@ [as] <name>`    | Save a finished result to disk. `@` = last finished. |
| `:Vimfy sim <alias> [count] [ms]`          | Animate results side-by-side.                        |
| `:Vimfy view [name]`                       | View a saved result (or list saved names).           |
| `:Vimfy list`                              | List active sessions and saved files.                |
| `:Vimfy recall <on\|off\|toggle>`          | Control rolling ring (enables `end N` / `end Ns`).   |
| `:Vimfy suggest <on\|off\|toggle>`         | Runtime toggle for auto-suggest (config-driven).     |
| `:Vimfy config`                            | Show the current configuration.                      |
| `:Vimfy reload`                            | Rebuild the C++ library (needs restart).             |
| `:Vimfy help`                              | Show the command list.                               |

## Alias grammar

The `<alias>` argument to `end` / `close` / `sim` is one of:

| Form       | Means                                                                |
|------------|----------------------------------------------------------------------|
| `a`, `foo` | Alphabetic → a manual session name.                                  |
| `6`        | Digits → recall N keys ago.                                          |
| `3s`       | Digits + `s` → recall N seconds ago (see [4. Recall](04-recall.md)). |

`save` accepts the same grammar plus `@`, which resolves to the most
recently finished session — handy when the session you want to keep is a
recall window whose alias (`3s`) is moving out from under you:

```
:Vimfy end 3s
:Vimfy save @ as nested-refactor
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
- Manual handles for `start`.
- All active/recall aliases plus `3s`/`5s`/`10s`/`30s` hints for `end` /
  `close` / `sim`.
- Selectors (plus `@`) for `save`; the `as` keyword after a selector.
- Saved names for `view`.
- `on` / `off` / `toggle` for `recall` and `suggest`.

## `<Plug>` mappings

For each subcommand above, a `<Plug>VimfyX...` map is also registered so
you can bind keys without having to type `:Vimfy ...` (which would count
as admin activity anyway). See [7. Keymaps](07-keymaps.md) for the full
list and the binding contract.

---

**[← Effort model](09-effort-model.md)** | **[Index](./README.md)** | **[Next: Workflows →](11-workflows.md)**
