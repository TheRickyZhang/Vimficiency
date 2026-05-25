# Vim Operator Edge Cases: Delete vs Change

Vim's `d` (delete) and `c` (change) operators share motions but diverge in subtle, critical ways. These differences propagate through the simulator (`Edit.cpp`), the deletion utility (`VimEditUtils.cpp`), and the optimizer (`TransformOptimizer.cpp`). This document catalogs every known divergence and why it matters.

## 1. Exclusive-to-Linewise Conversion (`:help exclusive-linewise`)

**The rule:** When an exclusive motion's endpoint lands at column 0 of a line past the start, Vim promotes the operation to **linewise**. `d` removes those lines. `c` replaces those lines in Insert mode, preserving autoindent from the first changed line.

### `d}` vs `c}` (paragraph forward)

When `}` lands at col 0 (a blank-line separator):

| Operator | pos.col == 0 | pos.col > 0 | EOF (last non-blank line) |
|----------|-------------|-------------|--------------------------|
| `d}` | Linewise: delete lines `[pos.line, goalPos.line-1]` | Characterwise: back up to `(goalPos.line-1, lastCol)` | Characterwise: inclusive through `(lastLine, lastCol)` |
| `c}` | Linewise change: replace affected lines with one autoindented insert line | Characterwise if content before cursor must be preserved | Same as d} but enter Insert |

**Why `d}` with `pos.col > 0` is also characterwise:** The linewise conversion only fires when the start position is at or before the first non-blank. With `pos.col > 0`, there's content before the cursor on the start line that must be preserved, so Vim falls through to characterwise with the backed-up endpoint.

**Code:** `EditInterpreter.cpp` handles `c`'s linewise replacement via `tryApplyExclusiveLinewiseChange`; backed-up and delete cases use `VimCore::resolveExclusiveDeleteRange`.

### `d)` / `d(` vs `c)` / `c(`

Same principle applies to sentence motions. `c)` / `c(` use the autoindented linewise-change path for col-0 exclusive-linewise geometry, otherwise they fall back to the characterwise change path.

### `d{` / `c{`

Backward paragraph. Currently characterwise for both operators (the linewise col-0 case would require the endpoint to be at col 0, which `{` doesn't produce in the same way). Still passes the correct mode to `deleteRange`.

## 2. Empty Merged Line Removal (`deleteRange` Mode Behavior)

**The rule:** When a characterwise deletion produces an empty merged line and there are other lines in the buffer, `Mode::Normal` **removes** the empty line. `Mode::Insert` **keeps** it (the user will type there).

```
deleteRange(lines, range, pos, Mode::Normal)  // d-operators: remove empty lines
deleteRange(lines, range, pos, Mode::Insert)  // c-operators: keep empty lines
```

This is in `VimEditUtils.cpp`, lines ~61-69. Two locations enforce it: single-line deletion (line was emptied entirely) and multi-line deletion (merged line is empty).

**Concrete example:**
```
Buffer: ["abc", "def"]    cursor at (0,1)
d}: deletes "bc\ndef" → merged line "" → REMOVED → ["a"]  cursor (0,0)
c}: deletes "bc\ndef" → merged line "" → KEPT    → ["a", ""]  cursor (1,0) Insert
```

### Impact on the Optimizer

The TransformOptimizer explores deletion snapshots in Normal mode (`TransformSimulator` deletion helpers call VimCore deletion utilities with `Mode::Normal`). When converting a deletion to a change command for the goal suffix, the optimizer must compute the collapse sequence (`<BS>`/`<Del>` to join lines) based on **Insert mode** line counts, not Normal mode.

`buildGoalSuffix` in `TransformOptimizer.cpp` re-simulates the deletion with `Mode::Insert` to get the correct line count. Without this, `c}` sequences would be missing the `<BS>` needed to join the extra empty line that Insert mode preserves.

### Position Clamping Also Differs

After deletion, cursor column is clamped differently:
- `Mode::Normal`: `[0, line.size() - 1]` (must be on a character)
- `Mode::Insert`: `[0, line.size()]` (can be one past the last character)

## 3. `dd` vs `cc` / `S` (Linewise Operations)

**`dd`:** Erases the entire line(s) from the buffer. If the buffer becomes empty, inserts one empty line. Cursor goes to first non-blank of the next line (or previous if at EOF).

**`cc`/`S`:** Clears the line content but **keeps the line**. Enters Insert mode at col 0. With `autoindent` enabled, Neovim automatically inserts indentation matching the cleared line.

### Autoindent Implications for the Optimizer

When converting `dd` to `cc` in the optimizer:
- If the line had leading whitespace and `autoindent` is on, `cc` will auto-insert that whitespace
- The optimizer must emit `<C-u>` (or `0C`) to clear the autoindent before typing the replacement
- `deleteToChangeLine` in `TransformOptimizer.cpp` checks for this:
  ```
  dd on indented line + autoindent → "0C" instead of "cc"
  dd on non-indented line          → "cc"
  ```

The `<BS>` approach (deleting autoindent char-by-char) is unreliable because autoindent `<BS>` deletes to the previous `shiftwidth` boundary, not one character. If the target indent can't be reached exactly via `<BS>` steps, fall through to `<C-u>`.

## 4. `cw`/`cW` vs `dw`/`dW` (Word Motion Special Case)

**The rule (`:help cw`):** `cw` is equivalent to `ce`, NOT `c` + `w`. It changes to the end of the current word only, excluding trailing whitespace. `dw` uses the full `w` motion and includes trailing whitespace.

| Cursor on | `dw` deletes | `cw` deletes |
|-----------|-------------|-------------|
| Word char | To start of next word (includes trailing space) | To end of current word only |
| Whitespace | To start of next word | To start of next word (same as `dw`) |

**Optimizer impact:** When converting `dw` → change equivalent, the optimizer uses `dwi` (delete + enter insert), NOT `cw`, because `cw` would delete less text. This conversion is only reached when `dw` deletes trailing whitespace that `de` didn't cover (since `de`/`ce` are explored first).

Similarly `dW` → `dWi`, not `cW`.

## 5. `cb`/`cB` vs `db`/`dB` (Backward Word Newline Crossing)

`cb`/`cB` from col 0 do NOT cross to the previous line's content in the same way as `db`/`dB`. The change variants are line-bounded: they restrict deletion to avoid spanning the newline boundary. The delete variants freely cross lines.

Additionally, when `db`/`dB` crosses lines from col 0 and the resulting merged line is empty (and removed), the cursor goes to the **first non-blank** of the remaining line — a special cursor placement rule that only applies to delete operators with line removal.

## 6. `D` vs `C` and `d$` vs `c$`

Both delete from cursor to end of line (with count: through multiple lines). The only difference: `C`/`c$` enters Insert mode after, `D`/`d$` stays in Normal. The deletion itself is identical — both use `deleteRange` with the default `Mode::Normal` because there's no empty-line-removal concern (the start position has content before the cursor).

**Note:** `D` and `C` are the single-char shortcuts. They map identically to `d$` and `c$` respectively.

## 7. `x`/`X` vs `s` (Char Deletion)

`x` (delete char under cursor) → `s` (substitute: delete char, enter Insert). Same deletion, different final mode. The optimizer converts `x` → `s` and `X` → `hs` in `deleteToChangeChar`.

## 8. Text Objects with Invalid Ranges

When a text object produces an invalid/empty range (e.g., `ciw` on an empty line, `ci(` with no surrounding parens):
- `d` + text object: does nothing, stays in Normal mode
- `c` + text object: does nothing to the buffer but **still enters Insert mode**

## Summary: When Does It Matter?

The d/c divergence matters most when:

1. **A motion endpoint lands at col 0** — triggers the exclusive-to-linewise rule, which `c` overrides
2. **A characterwise deletion leaves an empty merged line** — `d` removes it, `c` keeps it
3. **The optimizer converts d→c** — must account for Insert-mode line preservation and autoindent

When implementing new motions or optimizer conversions, always ask: "Does the change variant behave identically to the delete variant here, or does Vim's operator-dependent logic kick in?"
