# oracle-verify

Use when implementing or modifying any Vim/Neovim semantic — motions,
operators, text objects, edit operators, exclusive/linewise/blockwise
behavior, autoindent, backspace, sentence/paragraph/word boundaries, or
anything that must match Neovim's runtime. Also use when writing or updating
tests that assert Vim behavior.

Vimficiency's correctness floor is "behaves identically to Neovim." Guessing
from `:help` or memory is not enough — Neovim's source and a live oracle are
the two authorities.

## Workflow

1. **Find the Neovim source reference.**
   The behavior you are implementing or fixing is defined in Neovim's C source
   (typically `src/nvim/edit.c`, `ops.c`, `normal.c`, `textobject.c`,
   `memline.c`, `mark.c`, `search.c`). Locate the function and read it. Note
   the file and key line range in your reasoning so the reader can follow.
   Common references already documented in memory:
   - `inc/incl/dec/decl` — `memline.c` ~4096–4168 (NUL-terminator position model)
   - `findsent()` — `textobject.c` (sentence motion)
   - `startPS()` — paragraph / nroff macro boundaries

2. **Match Neovim's position model, not C++ string indexing.**
   Neovim treats the end-of-line as a NUL position with `cls() == 0`. Several
   functions (`fwdWord`, sentence/paragraph scans) only behave correctly if
   our cursor model can sit at `col == line.size()`. If your port stops one
   position short, re-read `inc()` / `incl()` — that is almost always the bug.

3. **Write an oracle-backed test.**
   Add it to `tests/.../ManualTest.cpp` following the existing
   `ExclusiveLineAdjust_*`, `ChangeLinewise_*`, `DeleteLinewise_*` style.
   The test should:
   - Set up a buffer + cursor.
   - Run the command both through our impl and through `NeovimOracle`.
   - Assert the resulting buffer + cursor match.

4. **Run the test through the real oracle binary** before declaring success.
   Common targets: `vimficiency_tests`, `edit_optimizer_tests`. Filter with
   `--gtest_filter=...` to the new test name.

## Oracle gotchas (high-frequency footguns)

- **`<BS>` byte:** send as `\x08` (ASCII BS), NOT `\x7f` (DEL). DEL silently
  fails to delete autoindent whitespace; BS works. See `NeovimOracle.cpp`.
- **`-u NONE` ≠ blank options.** Neovim's compiled-in defaults still apply:
  `autoindent=on`, `backspace=indent,eol,start`, `startofline=off`,
  `joinspaces=off`, `shiftwidth=8`. These match `VimOptions.h` — keep them in
  sync if you change either side.
- **Step vs. full sequence divergence.** Calling `nvim_input` once per command
  (step trace) can give different results than one combined `nvim_input` call,
  because autoindent "pending" state is stripped between calls. If a test
  passes step-by-step but fails as a full sequence (or vice versa), this is
  why — pick the trace mode that matches what production does.
- **`<BS>` in autoindent whitespace** deletes to the previous `shiftwidth`
  boundary, not one column. With sw=8: indent 1–8 → 0 in one BS, indent 9 → 8
  in one BS. Use `bsCountForIndent()` in `StringUtils.h`. If BS cannot land
  exactly on the target indent, fall through to `<C-u>`.

## A* / replay consistency

If the change touches anything the optimizer can emit (motions or edits used
inside `exploreCountedWordEdits`, `exploreCountedCharEdits`,
`replayAndCacheSuffix`, etc.), the new semantic must hold under **both**:

- Direct application via `Edit::applyEdit`.
- A* state transitions used during search.

A divergence here surfaces as a replay crash, not a wrong answer. The fix is
almost never "tweak A*" — it is "make A* call the same primitive that
`applyEdit` calls." Existing precedent:

- `exploreCountedWordEdits` uses raw `motionE / motionW / motionB / motionGe`,
  not `motionWordEndpoint`.
- `dd`-from-last-effective-line clamps cursor to `max(0, size-2)` to match
  what `deleteRangeLinewise` + `k` produces during replay.

## When this does NOT apply

- Pure infrastructure: build, logging, telemetry, formatting.
- Optimizer-internal heuristics that do not change observable Vim semantics
  (e.g. cost-function tuning).
- Lua / FFI session-layer changes that do not call into Vim emulation.
