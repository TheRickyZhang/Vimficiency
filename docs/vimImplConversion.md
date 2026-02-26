# Vim Implementation Conversion Guide

Maps names between Neovim's C source and our `VimPortedImpl` / `VimCore` codebase.
Use this when porting new Vim operations to ensure consistent naming and identify
reusable helpers.

## Source Files

| Neovim source | Our file | What it contains |
|---|---|---|
| `memline.c` | `VimCore.cpp` (section 3b) | `inc/incl/dec/decl` cursor stepping with NUL model |
| `search.c` | `VimPortedImpl.cpp` | `findsent()` sentence motion |
| `textobject.c` | `VimPortedImpl.cpp` | `current_word()` iw/aw text objects, `fwd_word()`, `end_word()` |
| `normal.c` / `ops.c` | `VimPortedImpl.cpp` | `dw`/`dW` operator-motion interaction (don't-cross-lines rule) |
| `charset.c` | `VimCore.cpp` (section 1) | Character classification (`cls()`, `vim_iswordc()`) |

## Character Classification

| Neovim | Ours | Notes |
|---|---|---|
| `cls()` returns 0 | `charClass()` returns 0 | whitespace or NUL (end of line) |
| `cls()` returns 1 | `charClass()` returns 1 | keyword chars; **rename candidate**: `isSmallWordChar(c)` matches `vim_iswordc(c)` |
| `cls()` returns 2 | `charClass()` returns 2 | punctuation (non-keyword, non-whitespace) |
| `cls()` bigword mode | `charClass(..., bigword=true)` | merges classes 1+2 into 1 |
| `vim_iswhite(c)` | `isWhitespace(c)` | space or tab; **1:1 match** |
| `vim_iswordc(c)` | `isSmallWordChar(c)` | `isalnum(c) \|\| c == '_'`; **rename candidate** → `isWordChar` |
| — | `isBlank(c)` | space, tab, or newline (no Vim equivalent, used for CursorPos-model word motions) |
| — | `isBigWordChar(c)` | non-blank, non-NUL (no Vim equivalent; Vim uses `cls() != 0`) |
| `ends_in_white(c)` | — | not ported; checks if sentence-end is followed by whitespace |
| `LANGMAP_ADJUST()` | — | not applicable (no langmap support) |

### Sentence-specific

| Neovim | Ours | Notes |
|---|---|---|
| check for `.` `!` `?` | `isSentenceEnd(c)` | **1:1 match** |
| check for `)` `]` `"` `'` | `isSentenceCloser(c)` | **1:1 match** |
| `startPS()` / nroff macros | `isParaBoundaryLine()` | same logic via `inmacro()` |

## Cursor Stepping

### NUL-aware model (for sentence motions)

Vim's position model has a virtual NUL terminator at `col == lineLen`. `inc()` can
land on NUL; `incl()` skips it by calling `inc()` again. This matters for sentence
detection where seeing NUL signals "end of line".

| Neovim (`memline.c`) | Ours (`VimCore.cpp`) | Notes |
|---|---|---|
| `gchar_pos(pos)` | `vimGchar(lines, pos)` | returns 0 at NUL/out-of-range |
| `inc(pos)` | `vimInc(lines, pos)` | can land on NUL; returns `Pos(-1,-1)` at boundary |
| `incl(pos)` | `vimIncl(lines, pos)` | skips NUL of non-empty lines |
| `dec(pos)` | `vimDec(lines, pos)` | can land on NUL |
| `decl(pos)` | `vimDecl(lines, pos)` | skips NUL of non-empty lines |

### Real-char model (for word motions / text objects)

Vim's `inc_cursor()`/`dec_cursor()` never land on NUL — they jump directly from
last real char to col 0 of next line. Used by `current_word()` and word motions.

| Neovim (`textobject.c`) | Ours (`VimPortedImpl.cpp`) | Notes |
|---|---|---|
| `inc_cursor()` | `incCursor(lines, line, col)` | returns -1 (EOF), 1 (crossed line), 0 (normal) |
| `dec_cursor()` | `decCursor(lines, line, col)` | returns -1 (BOF), 0 (normal) |

There are also CursorPos-based stepping functions in `VimCore.h` used by the
original (non-ported) word motion code:

| Ours (`VimCore.h/cpp`) | Role | Notes |
|---|---|---|
| `stepFwd(lines, line, col)` | advance one real char | similar to `incCursor` but different return convention (bool) |
| `stepBack(lines, line, col)` | retreat one real char | similar to `decCursor` but different return convention |
| `step<Forward>(lines, pos)` | CursorPos-based stepping | used by `motionWordCore`; wraps `Lines::getNextPos/getPrevPos` |

## Word Motions

### Ported in `VimPortedImpl.cpp` (anonymous namespace)

| Neovim (`search.c` / `textobject.c`) | Ours | Notes |
|---|---|---|
| `fwd_word(count=1, bigword, eol=true)` | `fwdWord(lines, line, col, bigword)` | move to start of next word |
| `end_word(count=1, bigword, stop, empty)` | `endWord(lines, line, col, bigword)` | move to end of word |
| `back_in_line(cls)` | `backInLine(lines, line, col, bigword)` | go to start of class run on same line |
| `bck_word(count, bigword, stop)` | — | not ported; backward word motion. Our `motionB` uses `motionWordCore` instead |

### Original in `VimCore.cpp` / `VimMotionUtils.cpp`

| Ours | Neovim equivalent | Notes |
|---|---|---|
| `motionWordCore<Fwd, Edge>(...)` | — | unified word motion with EdgeType dispatch; no direct Vim equivalent |
| `motionW(pos, lines, big)` | `nv_wordcmd()` → `fwd_word()` | forward to next word start |
| `motionE(pos, lines, big)` | `nv_wordcmd()` → `end_word()` | forward to word end |
| `motionB(pos, lines, big)` | `nv_wordcmd()` → `bck_word()` | backward to word start |
| `motionGe(pos, lines, big)` | `nv_wordcmd()` → `bckend_word()` | backward to word end |

### EdgeType abstraction (ours, no Vim equivalent)

Our `motionWordCore` uses `EdgeType` to unify forward/backward + which-boundary:
- `WordEdge`: edge of the word being traversed (e.g., `e` forward, `b` backward)
- `GapEdge`: edge of the gap/whitespace between words (e.g., `dw` forward)
- `NextEdge`: start of the next unit (e.g., `w` forward, `ge` backward)

Vim doesn't have this abstraction — it has separate `fwd_word()`, `end_word()`,
`bck_word()`, `bckend_word()` functions.

## Word Deletion (dw/dW)

| Neovim | Ours | Notes |
|---|---|---|
| operator + `w` motion in `do_pending_operator()` | `deleteWordForwardRange()` in `VimPortedImpl.cpp` | ported "don't cross lines" rule |
| — | `deleteWordForwardRangeBounded()` | boundary-aware version for explorer |

Vim's "don't cross lines" rule for `dw`: if the `w` motion would cross to the next
line and the current line is non-empty, delete only to end of current line. This is
NOT part of the `w` motion itself — it's operator-specific adjustment in `ops.c`.

## Sentence Motions

| Neovim (`search.c`) | Ours (`VimPortedImpl.cpp`) | Notes |
|---|---|---|
| `findsent(dir, count)` | `findsentImpl(cursor, lines, forward, bounded, ...)` | core 5-step algorithm |
| — | `findsent(cursor, lines, forward)` | public API (unbounded) |
| — | `findsentBounded(cursor, lines, forward, ...)` | public API (boundary-aware) |

The ported `findsent` uses the NUL-aware stepping model (`vimInc`/`vimIncl`/etc.)
which is critical for correct sentence detection at line boundaries.

### Original sentence code (still used for SentenceEdge/GapEdge text objects)

| Ours | Role | Notes |
|---|---|---|
| `motionSentenceEdgeCore()` | sentence edge computation for text objects | NOT ported from Vim; hand-written |
| `findCurrentSentenceStart()` | find start of containing sentence | used by text objects (`dis`/`das`) |
| `isSentenceEndAt()` | check for `.!?` + closers + whitespace | used by both paths |

## Word Text Objects

| Neovim (`textobject.c`) | Ours (`VimPortedImpl.cpp`) | Notes |
|---|---|---|
| `current_word(oap, count, include, bigword)` | `currentWordImpl(cursor, lines, include, bigword)` | core algorithm |
| — | `currentWord(cursor, lines, include, bigword)` | public API (unbounded) |
| — | `currentWordBounded(cursor, lines, ...)` | public API (boundary-aware) |

### Original text object code (superseded by ported version for word objects)

| Ours | Role | Notes |
|---|---|---|
| `textObjectCore()` | old word text object using EdgeType motions | superseded by `currentWord()` |
| `textObject()` | public API, now delegates to `currentWord()` | wrapper |
| `textObjectRange()` | boundary-aware, now delegates to `currentWordBounded()` | wrapper |

## Porting Checklist

When porting a new Vim operation:

1. **Identify the Neovim source function** and note the file/line reference
2. **Map character classification** — use existing `charClass()`, `isSmallWordChar()`, etc.
3. **Choose stepping model**:
   - NUL-aware (`vimInc`/`vimIncl`) for sentence-like operations
   - Real-char (`incCursor`/`decCursor`) for word-like operations
4. **Implement in `VimPortedImpl.cpp`** with helpers in the anonymous namespace
5. **Add boundary-aware variant** that checks results against boundary parameters
6. **Declare in `VimPortedImpl.h`** with source reference comments
7. **Wire into existing call sites** (interpreter + explorer)
8. **Test with ExplorerInterpreterConsistencyTest** to verify agreement
