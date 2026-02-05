# Previous Error Patterns

This document catalogs recurring error patterns and/or general solutions discovered during debugging, to help avoid similar issues in the future. Many of these can be found by always verifying with NeovimOracle!

# Design Principles

## Validate State Strictly
When calling our underlying VimCore, we should be using assertions to error on any redundant actions, as we should always do any early pruning we can.

## Handling Empty Representation
Just like Neovim, we must distinguish empty lines, while still allowing the cursor to be on an empty line. Thus we must handle these conditions everywhere:
- `lines.size() > 0`
- `col < lines[line].size()` EXCEPT when `lines[line].empty()`, in which case col=0.

## targetCol (curswant) Handling

Vim has "sticky column" behavior: where the cursor goes to the column it "wants" to be at with vertical motions. We must choose which of these functions to use very carefully:

```cpp
// Use for HORIZONTAL movements - resets the sticky column (col = targetCol = c)
pos.setCol(c);
// Use for VERTICAL movements - preserves the sticky column (col = clampedCol)
// j, k, gk, G, gg, {count}G, jumps. (Note startofline = false by default!)
pos.clampColPreservingTarget(clampedCol);
```

Also, makes sure to use correct constructor:
```cpp
Position(line, col)           // 2-arg: sets targetCol = col
Position(line, col, targetCol) // 3-arg: sets targetCol explicitly
```

`dd` (linewise delete) has unique behavior:
1. **Uses** `targetCol` to compute the cursor position on the new line
2. **Resets** `targetCol` to the clamped column afterward

```cpp
pos.line = min(r.startLine, static_cast<int>(lines.size()) - 1);
if (lines[pos.line].empty()) {
  pos.setCol(0);
} else {
  pos.setCol(min(pos.targetCol, static_cast<int>(lines[pos.line].size()) - 1));
}
```

This means:
- `k` then `dd`: Uses targetCol from before `k`, then resets
- `dd` then `dd`: Second `dd` uses the reset targetCol from first `dd`

## Motion vs operator differences
Some motions behave differently when used with operators:
- `w` motion crosses lines, but `dw` does not
- Special case: cw and cW are treated like ce and cE if the cursor is on a non-blank


# Incorrect implementations:
## J/gJ
  - Keep trailing whitespace intact instead of stripping
  - Only insert space if first line doesn't already end with whitespace
  - Ensure cursor = original first line length (position where join occurred)

## db/dB from col 0

In addition to the totally unique property of not deleting current character,
there are special cursor placement rules when backward deletion crosses lines from column 0.
- When `db` or `dB` is executed from column 0, cursor is at **first non-blank character** of the line, not just at col 0.

**Example:**
```
Before: "abc" / "def" / "   ghi"  cursor at [2,0] (the space)
After db: "abc" / "   ghi"  cursor at [1,3] (the 'g', first non-blank)
```

## adjustForSequential was a no-op

`Myers::adjustForSequential()` was supposed to convert diff positions from original-buffer coordinates to intermediate-buffer coordinates (after prior diffs applied). Instead it was a no-op -- it returned the input `diffs` unchanged (the function computed offsets but never applied them, and ended with `return diffs;` on the const input).

This worked by accident when diffs didn't insert/delete lines before subsequent diffs, since column-only changes within a single line don't shift positions of diffs on later lines. It would break when earlier diffs add or remove lines (or characters on the same line) before later diffs.

**Fix:** Removed `adjustForSequential` entirely. Inlined the adjustment into `calculateLinesAfterDiffs`, which already has the intermediate `Lines` at each step. The adjustment converts positions to flat character indices against the original buffer, applies a cumulative offset (sum of `insertedText.size() - deletedText.size()` from prior diffs), then converts back to `(line, col)` against the current intermediate buffer. See `docs/optimizer/diff-generation.md`.


