# comment-and-docs

Use when finishing a substantial, non-trivial change in `src/` that adds or
revises behavior, or whenever about to write/edit a code comment or a file
under `dev/`.

Vimficiency keeps comments and design docs lean. This is the checklist for
both, applied at the *end* of a behavioral change (not preemptively, mid-edit).

## Comments in code

Default: write none.

Add a comment ONLY when the *why* is non-obvious — a hidden invariant, a
workaround for a specific Vim/Neovim quirk, a constraint imposed by the A* /
replay contract, or behavior that would surprise a reader. If removing the
comment would not confuse a future reader, do not write it.

The comment should be concise. It should not include any of our deliberation
for the design decision, but if it is really helpful it can link to the
correct location in `/dev` where we do discuss it.

Bad:
```
// Cap up to `params.maxResultsPerEndPos` distinct tokens per landing
// cell. Default 1 keeps just the cheapest token per cell; values > 1
// surface multiple distinct paths to the same cell (e.g. `w` / `W` /
// `e` all reaching the same word start). Already clamped to ≥1 above.
const int cap = params.maxResultsPerEndPos;
```

Good:
```
(nothing)
```

If the behavior were complex enough to warrant a comment, we would have
something like:
```
// params attribute is not const for these reasons. TODO fix in the future.
```

Never:

- Restate what the code does (`// increment i`).
- Reference the current task, fix, ticket, or caller (`// added for X flow`,
  `// fix for issue 42`). That belongs in the commit message.
- Write multi-paragraph comments or multi-line docstrings. 1-2 short lines,
  with length proportional to the complexity, is the max.

At the end of a large change: re-read comments touched by the diff and
**update or delete any that are now stale or wrong**. Do not add new ones
reflexively.

## Docs under `dev/`

Vimficiency's `dev/` tree is the design-doc canon. Update existing files; you
can ask the user if you want to create a new one.

Routing — pick the smallest set that actually applies:

- `dev/core/vim-edge-cases.md` — newly discovered Vim/Neovim semantic quirks
  (motion, edit, exclusive-linewise, autoindent, etc.).
- `dev/core/counted-edit-semantics.md` — anything touching how counts compose
  with motions/operators.
- `dev/core/boundary-logic.md` — changes to edit-region / prefix / suffix /
  `effectiveLines` / `leftColOffset` accounting.
- `dev/optimizer/*.md` — explorer changes go to the matching file
  (`transform-optimizer.md`, `composition-optimizer.md`,
  `motion-optimizer.md`, `optimizer-architecture.md`, etc.).
- `dev/lua/*.md` — anything crossing the FFI boundary or living in the Lua
  session layer.
- `dev/history/previous_errors.md` — append a one-paragraph entry when a
  non-trivial bug is fixed and the *cause* is non-obvious from the diff.

If a change spans multiple areas, update each relevant file briefly rather
than duplicating prose across them.

## Style for `dev/` edits

- Match the surrounding tone of the file. Most are terse, bullet-heavy.
- Update incorrect or now-stale text in the same pass — do not append a new
  section that contradicts an old one.
- No changelog-style entries unless the file already uses that format.
