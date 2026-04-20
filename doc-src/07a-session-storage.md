---
title: "Session storage"
---

# Session storage

A finished session has two places it can live:

- **Session memory** (the "workspace") — indexed by its alias. This is
  where a session lands the moment `:Vimfy end`, `:Vimfy recall`, or an
  auto trigger finishes it. Cheap, immediate, ephemeral.
- **Disk** (the "archive") — a file under
  `stdpath('data')/vimficiency/saved/<name>.json`. Durable across
  restarts. Indexed by filename.

The two are separate namespaces. The same text can name both an alias
and a saved file.

## Memory rotates; disk is durable

Session memory is small and churns on its own:

- **Mark** slots cap at `MANUAL_CAPACITY = 5`. The oldest drops when a
  6th starts.
- **Recall** / **Suggest** entries age out of the rolling ring by the
  union of `KEY_SESSION_CAPACITY` and `MAX_RETENTION_SECONDS` — drop
  only when both say drop.

Anything on disk is durable: it stays until you `:Vimfy rm` it.

## Four verbs

```vim
:Vimfy save  <selector> [<name>]
:Vimfy store <alias>    [<name>]
:Vimfy fetch <name>     [<alias>]
:Vimfy rm    <name>
```

| Verb    | Moves             | Workspace after        | Disk after          |
|---------|-------------------|------------------------|---------------------|
| `save`  | workspace → disk  | unchanged              | written (overwrite) |
| `store` | workspace → disk  | alias removed          | written (overwrite) |
| `fetch` | disk → workspace  | alias added            | unchanged           |
| `rm`    | deletes disk file | unchanged              | removed             |

`save` is a **copy**; `store` is a **move**. Pick whichever matches
your intent — `store` when you're done with the alias in-session,
`save` when you want to keep working with it.

## The no-`as` rule

Omitting the second argument means "repeat the first":

- `:Vimfy save foo`    ≡ `:Vimfy save foo as foo`
- `:Vimfy store foo`   ≡ `:Vimfy store foo as foo`
- `:Vimfy fetch foo`   ≡ `:Vimfy fetch foo as foo`

For selectors that aren't legal saved names (`@`, `3s`, `5`) the first
argument isn't reusable as a filename — you must pass a name
explicitly:

```vim
:Vimfy save @ my-refactor    " OK — explicit name
:Vimfy save @                " ERROR — @ isn't a legal filename
```

## Collision rules

- **`save` / `store` onto an existing disk file** → overwrites, emits a
  WARN. No refusal; filesystem-style "save over" semantics.
- **`fetch` into an occupied workspace alias** → refuses. Close the
  alias first (`:Vimfy close <alias>`) or fetch under a different name.

## Replay: `sim` covers both

`:Vimfy sim <name>` looks up `<name>` in this order:

- **Workspace only** → replays.
- **Disk only** → implicitly `fetch <name> as <name>` into the
  workspace, then replays. After the command, the session is in your
  current workspace; the disk copy is untouched. This keeps the mental
  model "what I just replayed is in my current session."
- **Both** → WARN that the disk copy is being shadowed; replays the
  workspace copy. To force a refetch, `:Vimfy close <name>` first, then
  `:Vimfy sim <name>`.

The implicit fetch only works when `<name>` is a valid manual alias
(alphabetic). For names with digits/dots/hyphens on disk, use an
explicit `:Vimfy fetch <name> <alias>` first.

## Worked example

```vim
:Vimfy start a               " mark a session start
" ... edit ...
:Vimfy end a                 " finishes session; workspace has alias `a`

:Vimfy save a                " disk now has saved/a.json; workspace still has `a`
:Vimfy store a fix           " workspace drops `a`; disk updated to saved/fix.json

" ... later, even after a restart ...
:Vimfy fetch fix a           " workspace has alias `a` again, populated from saved/fix.json
:Vimfy sim a                 " replay as usual
```

## Listing

```vim
:Vimfy list
```

Shows both sides: active sessions in memory and saved files on disk.
Tab completion for `sim` / `view` / `rm` / `fetch` uses whichever side
is relevant for that command.
