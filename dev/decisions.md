# Design Decisions

Append-only log of non-obvious design choices and the reasoning. Add new entries
at the bottom. Keep each entry to one bullet with a short "because" — link out
to deeper docs when needed.

## Format conventions
- C++ constants use `SCREAMING_SNAKE_CASE`, not `k`-prefixed names — because the existing codebase (`LEVEL_COUNT`, `DEBUG_ENABLED`, `INF`, `LINE_OUTSIDE_BOUNDARY`) already uses it, and one style is better than two.
- Don't `static_cast<size_t>` for indexing into `std::vector`/`std::array` — implicit `int → size_t` is fine, casts add noise without safety. Use `int` for sizes/indices throughout.
- Default to no comments. Only add when the *why* is non-obvious (hidden invariant, workaround, surprising behavior). No headers, no narration, no restating function names.

## Approval tests
- When an approval-test output format changes intentionally, just overwrite `.approved.txt` with `.received.txt` — no separate confirmation step. The diff between received/approved IS the new expected output.

## Pretty text (`src/Utils/PrettyText.h`)
- Named `PrettyText` / `prettify`, not `GlyphText` / `glyphize` — because "pretty" matches the user-facing intent (make whitespace/control chars visible), while "glyph" was implementation jargon and "glyphize" is a made-up verb.
- Centralized in `Utils/PrettyText.h`, not `Interpreter/SequenceDisplay.cpp` — because Tree/TreeDiff also need this, and Optimizer→Interpreter is a backwards layering.
- `PrettyText` is a tagged struct (`struct PrettyText { std::string text; }`) built only via `explicit` constructors — because the type itself signals "rendering has happened", and the `.text` accessor marks the boundary back to untagged text.
- A free function `prettify(c/sv) → std::string` exists alongside the struct — for sites that don't need to keep the type tag (e.g. concatenating into an existing `std::string`). Without this, those sites would have to write `PrettyText(x).text`, which is friction without benefit.
- The plural-string constructor delegates to the singular-char dispatch (`appendGlyph`) — single source of truth for the char→glyph mapping.
- `SequenceDisplay.cpp` uses the raw glyph constants (`PRETTY_SPACE`, `PRETTY_TAB`, `PRETTY_NEWLINE`) directly when mapping `<Space>`/`<Tab>`/`<CR>` key notation, because that's a *different* operation (notation → glyph) from rendering text.

## Tree visualization (`TreeDiff::Tree::operator<<`)
- Output is a constituency-style character-aligned diagram, e.g. `⟦i n t ␣┃b e _ c o┃( ) ␣┃{⟧` — denser and higher-signal than the previous per-node bullet list.
- Outer delimiters are `⟦` / `⟧` (U+27E6/U+27E7), and internal dividers are `┃` (U+2503), not ASCII `[`/`]`/`|` — because source code routinely contains ASCII punctuation, and the ambiguity would force readers to disambiguate by position.
- Char level is excluded from the visualization — because including it would force every adjacent-char gap to width 2 (close+open), roughly doubling row width with no extra information. Counts row still mentions Char.
- Whitespace/control chars are rendered as visible glyphs (`␣`, `⇥`, `↵`) so they participate in alignment as 1-column cells.
- No row labels (Root/Paragraph/...) on the visualization rows — the row order is canonical (top = coarsest), and the `counts:` line above documents the level names. Saved horizontal space.
- Uses `operator<<` directly, not a `toString()` method — idiomatic C++ stream output and lets callers compose with `out << "Initial tree:\n" << tree;` without parens-and-method noise.

## Diff region format (`TreeDiff::formatDiffs`)
- One line per region (was three: `range=...`, `del=...`, `ins=...`) — `[0] replace (1,20)->(1,21) flat=[34,35) "+" → "-"`. Denser without losing information.
- Uses `→` (U+2192) instead of `->` for visual distinction from ASCII content in `del`/`ins`.
- Insert/delete regions skip the redundant empty side (no `"" → "x"`) — just print the non-empty side.
