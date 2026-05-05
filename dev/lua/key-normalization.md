# Key normalization for the C++ tokenizer

Any Lua code that forwards `vim.on_key` output to the C++ optimizer (directly
via a sequence string, or indirectly via an event record later flattened into
one) must convert raw key bytes into the tokenizer's canonical printable form
**before** the bytes reach the FFI. Skip this step and the tokenizer hits
`assert(false && "Malformed key sequence")` on any Ctrl-letter input.

The shared helper is `lua/vimficiency/capture/keynorm.lua` — route every
conversion through `keynorm.normalize(input)`.

## Input contract

`normalize` accepts **only raw input bytes** as delivered by
`vim.on_key`. It is not a general-purpose key-string normalizer:
passing already-printable notation like `"<C-u>"` will corrupt it
(`keytrans` re-escapes the `<`, yielding `<lt>C-u>`).

`vim.on_key` is nvim's lowest-level key hook and only produces three
input shapes:
- Plain ASCII / typed UTF-8 bytes (`a`, `é`).
- Single-byte control codes for Ctrl-letters (`\x15` for `<C-u>`).
- Multi-byte K_SPECIAL form for special keys (`\x80kb` for `<BS>`,
  `\x80ku` for arrow up, etc.).

It never produces printable `<...>` notation — that's a vim-script
surface form, never an input-stream form. So the function only needs
to handle raw input.

## What `normalize` does

Two passes, in order:

1. **Print to canonical notation.** `vim.fn.keytrans(raw)` handles all
   three raw shapes uniformly (`"\x15"` → `"<C-U>"`, `"\x80kb"` →
   `"<BS>"`, `"a"` → `"a"`). The C++ tokenizer keys on this form.

2. **Lowercase the modifier letter.** `keytrans` emits `"<C-U>"`
   (uppercase), but the registered tokens in
   `src/Keyboard/ToKeys/MovementToKeysPrimitives.h` and
   `src/Keyboard/ToKeys/EditToKeys.cpp` are **lowercase** (`"<C-u>"`).
   See the token tables for the full list. Without lowercasing,
   `keytrans` output misses the table.

## Why no `nvim_replace_termcodes` pre-pass

A previous version of `normalize` ran `nvim_replace_termcodes(input,
true, true, true)` first, on the theory that doing so would let the
function accept printable notation as well. That was a speculative
flexibility — no caller ever exercised it — and it actively broke
raw input.

`replace_termcodes` is the **inverse** direction (printable → raw).
When fed raw input containing a `0x80` byte (K_SPECIAL), it doesn't
pass it through; it re-escapes the `0x80` as the 3-byte `<80>`-notation
encoding (`0x80 0xFE 0x58`) so the result can be fed back through
without ambiguity. `keytrans` then renders that escape as the literal
string `<80>`, corrupting every K_SPECIAL key in the input (`<BS>` →
`<80>kb`, arrows similarly mangled).

Removing the pre-pass and tightening the contract to raw-only is
strictly correct for every actual call site and avoids the trap.

## Call sites

Two places consume `on_key` output and feed it into a sequence string that
eventually reaches the tokenizer:

- **`lua/vimficiency/explore/handlers.lua`** — the explore session's
  on_key buffer. Bytes flow through `explore_accept_snapshot` with the
  current scratch buffer/cursor snapshot; C++ appends accepted raw keys to
  the explored sequence and calls `getEffort(seq, config_)`. `getEffort`
  runs the tokenizer.

- **`lua/vimficiency/capture/key_tracking.lua`** — the mark/watch/recall
  capture layer. Events are flattened via `vimficiency_build_sequence` into
  a `user_seq` string and passed as `keyseq` to `vimficiency_analyze`, which
  computes `userCost = getEffort(keyseqText, ...)` and also passes the
  sequence into the optimizer as `userSequence` for A* bounding. Same
  tokenizer, same failure mode.

Any new consumer — a future stats pipeline, a debug logger, an export path
— must also normalize, or the assert comes back.

## Explore snapshot path

Explore forwards normalized key bytes together with the live scratch buffer,
cursor, and insert-mode bit to `explore_accept_snapshot`. The physical
snapshot is authoritative; raw keys are effort/sequence evidence for how Vim
reached that state. C++ no longer rejects a Vim-produced state just because
the key string is incomplete, but normalization is still required before
`getEffort(seq, config_)` tokenizes the accepted sequence.

## Testing expectations

The printable form surfaces in user-visible places (explore header, session
results view, stats, etc.). Because lowercase is the project-canonical form,
downstream displays already expect it. No test currently asserts an uppercase
`<C-X>` form; search `grep -rnE '<C-[A-Z]>' lua/ tests/` to confirm before
reintroducing one.
