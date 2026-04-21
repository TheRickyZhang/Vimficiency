# Key normalization for the C++ tokenizer

Any Lua code that forwards `vim.on_key` output to the C++ optimizer (directly
via a sequence string, or indirectly via an event record later flattened into
one) must convert raw key bytes into the tokenizer's canonical printable form
**before** the bytes reach the FFI. Skip this step and the tokenizer hits
`assert(false && "Malformed key sequence")` on any Ctrl-letter input.

The shared helper is `lua/vimficiency/capture/keynorm.lua` — route every
conversion through `keynorm.normalize(input)`.

## Why two transformations, not one

`vim.on_key` delivers keys in two shapes that both need fixing:

1. **Raw bytes.** `typed` comes in as the underlying byte sequence — e.g.
   `"\x15"` for `<C-u>`. The C++ tokenizer keys on the Vim printable form
   (`"<C-u>"`), not raw control bytes. `vim.fn.keytrans(typed)` handles this
   half.

2. **Modifier-letter case.** `vim.fn.keytrans("\x15")` returns `"<C-U>"`
   (uppercase U). The registered tokens in
   `src/Keyboard/ToKeys/MotionToKeysPrimitives.h` and
   `src/Keyboard/ToKeys/EditToKeys.cpp` are **lowercase** (`"<C-u>"`). This
   is an unconditional project convention — see the token tables for the
   full list (`<C-a>` … `<C-z>` plus `<C-Space>`, `<C-BS>`, etc.). Without
   lowercasing the modifier letter, keytrans output misses the table.

`keynorm.normalize` does both in order: `keytrans`, then
`gsub("<C%-([A-Z])>", ...)` to lowercase the modifier-letter half.

`normalize` is **not** idempotent on already-printable input:
`vim.fn.keytrans("<C-u>")` escapes the literal `<` as `<lt>`, producing
`"<lt>C-u>"`. Only pass raw bytes from `vim.on_key` into `normalize`. If a
code path needs to tolerate either shape, gate on `input:byte(1) < 0x20` or
`input:sub(1,1) == "<"` at the call site — don't build that conditional
into the helper.

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
`parseMotions` before appending to `acceptedSeq`. If the keys don't parse as
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
