# Key normalization for the C++ tokenizer

Any Lua code that forwards `vim.on_key` output to the C++ optimizer (directly
via a sequence string, or indirectly via an event record later flattened into
one) must convert raw key bytes into the tokenizer's canonical printable form
**before** the bytes reach the FFI. Skip this step and the tokenizer hits
`assert(false && "Malformed key sequence")` on any Ctrl-letter input.

The shared helper is `lua/vimficiency/capture/keynorm.lua` — route every
conversion through `keynorm.normalize(input)`.

## Why three transformations, not one

`vim.on_key` delivers keys in two shapes, and the tokenizer has an
additional case convention — so `normalize` does three things in order:

1. **Parse to raw bytes.** `vim.api.nvim_replace_termcodes(input, true,
   true, true)` interprets `input` as a Vim key-notation string and emits
   raw bytes. Given `"<C-u>"` it produces `"\x15"`; given already-raw
   `"\x15"` it leaves it alone (no `<…>` pattern to parse); given `"<lt>"`
   it produces `"<"`. This step makes the rest of the pipeline oblivious
   to whether the caller handed us raw or printable text.

2. **Print to canonical notation.** `vim.fn.keytrans(raw)` converts raw
   bytes back to the Vim printable form — e.g. `"\x15"` → `"<C-U>"`. The
   C++ tokenizer keys on this printable form, not raw control bytes.

3. **Lowercase modifier-letter.** `vim.fn.keytrans("\x15")` returns
   `"<C-U>"` (uppercase U), but the registered tokens in
   `src/Keyboard/ToKeys/MovementToKeysPrimitives.h` and
   `src/Keyboard/ToKeys/EditToKeys.cpp` are **lowercase** (`"<C-u>"`).
   This is an unconditional project convention — see the token tables for
   the full list (`<C-a>` … `<C-z>` plus `<C-Space>`, `<C-BS>`, etc.).
   Without lowercasing the modifier letter, keytrans output misses the
   table.

Because step 1 always parses first, `normalize` is **idempotent**:
`normalize(normalize(x)) == normalize(x)` for every input shape. Any
pre-existing `<lt>` in printable input collapses back to `<` during the
parse, then `keytrans` re-escapes it exactly once — no `<` → `<lt>C-u>`
corruption. Call sites can forward `vim.on_key` bytes unconditionally
without format-detection heuristics.

## Call sites

Two places consume `on_key` output and feed it into a sequence string that
eventually reaches the tokenizer:

- **`lua/vimficiency/explore/init.lua`** — the explore session's on_key
  buffer. Bytes flow through `explore_accept_cursor_move` into
  `InteractiveExploreSession::acceptCursorMove`, which appends them to
  `acceptedSeq` and calls `getEffort(acceptedSeq, config_)`. `getEffort`
  runs the tokenizer.

- **`lua/vimficiency/capture/key_tracking.lua`** — the mark/watch/recall
  capture layer. Events are flattened via `vimficiency_build_sequence` into
  a `user_seq` string and passed as `keyseq` to `vimficiency_analyze`, which
  computes `userCost = getEffort(keyseqText, ...)` and also passes the
  sequence into the optimizer as `userSequence` for A* bounding. Same
  tokenizer, same failure mode.

Any new consumer — a future stats pipeline, a debug logger, an export path
— must also normalize, or the assert comes back.

## Defensive backstop in C++

`InteractiveExploreSession::acceptCursorMove` pre-validates `rawKeys` through
`parseMovements` before appending to `acceptedSeq`. If the keys don't parse as
a motion sequence, the cursor is still updated but the sequence/cost are left
alone. This catches bytes that slip through Lua normalization (rare forms
like `<C-S-A>`, chord notations, or anything else the tokenizer doesn't
register) without crashing the session. It is a backstop, not a substitute
for the Lua-side normalization — it silently drops the sequence append, so
`acceptedSeq` will not reflect what the user typed.

## Testing expectations

The printable form surfaces in user-visible places (explore header, session
results view, stats, etc.). Because lowercase is the project-canonical form,
downstream displays already expect it. No test currently asserts an uppercase
`<C-X>` form; search `grep -rnE '<C-[A-Z]>' lua/ tests/` to confirm before
reintroducing one.
