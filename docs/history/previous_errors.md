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

## Subset boundary used target range instead of full subset extent

In `CompositionOptimizer.cpp`, when creating sub-buffers for `MotionOptimizer::optimizeToRange`, the `MotionBoundary` was constructed using the **target edit range** (`localRangeFirst`, `localRangeLast`) as the boundary positions. This caused `BoundaryContext` to compute `leftColOffset`/`rightColOffset` that clamped motions like `$` and `0` to the edit range edges instead of the full subset extent.

The bug existed in both code paths: the edit/motion transition path and the pure insertion path (`exploreInsertionStrategy` lambda). Both construct subsets and call `optimizeToRange`.

**Symptom:** Motions like `$` would land at the edit range edge instead of end-of-line, producing incorrect sequences. Manifested as failures in `SingleLine_Substitution`, `PureInsertion`, and `PureDeletion` tests.

**Fix:** Use `subsetFirst(0, 0)` and `subsetLast(subset.size()-1, subset.back().size()-1)` for the boundary, since the boundary should represent the full navigable extent of the subset, not the target range. The target range is only for `optimizeToRange`'s `isInRange` check.

```cpp
// Boundary uses full subset extent, not the target range.
Position subsetFirst(0, 0);
Position subsetLast(static_cast<int>(subset.size()) - 1,
    std::max(0, static_cast<int>(subset.back().size()) - 1));
MotionBoundary subsetBoundary(subset, subsetFirst, subsetLast, ...);
```

## $ motion missing TARGETCOL_EOL

In `MotionExplorer.h`, the `$` motion emitted its goal position using the 2-param Position constructor `{pos.line, dollarCol}`, which sets `targetCol = dollarCol`. The correct behavior is `targetCol = TARGETCOL_EOL` (INT_MAX), which makes subsequent vertical motions (`j`, `k`, `<C-d>`, etc.) stick to end-of-line.

**Symptom:** Sequences like `$<C-d>` would land at the wrong column on the target line. After `$` on a line of length 10 (dollarCol=9), a subsequent `j` to a line of length 20 would go to col 9 instead of col 19.

**Fix:** Use the 3-param constructor to explicitly set `TARGETCOL_EOL`:
```cpp
emitMotion(base, "$", {pos.line, dollarCol, TARGETCOL_EOL}, {Key::Key_Shift, Key::Key_4});
```

**Pattern:** This is a specific instance of the general targetCol pitfall documented above — always use the 3-param Position constructor when the target column semantics differ from the actual column.

## cc collapse sequence overcounting on single-line buffers

In `EditOptimizer.cpp`, the dd→cc conversion computes how many `<BS>`/`<Del>` keystrokes are needed to collapse a multi-line cc result to a single line. The original code used `lines.size() + 1` as the total line count, where `lines` is the post-dd buffer. The `+1` accounts for the line that `cc` preserves but `dd` removes.

However, when `dd` operates on the **last line** of a single-line buffer, the buffer invariant (`lines.empty() → lines.push_back("")`) creates an artificial empty line. So `lines.size()` is already 1, and `+1` gives 2 — but `cc` on a 1-line buffer has only 1 line (no collapse needed). This produced a spurious `<Del>` keystroke.

**Symptom:** Sequences like `cc<Del>text<Esc>` would produce an empty buffer instead of the expected single line. The extra `<Del>` deleted the line that `cc` just created.

**Fix:** Use `base.getLines().size()` (the pre-dd line count) instead of `lines.size() + 1`. The pre-dd buffer accurately represents what `cc` would operate on:
```cpp
int ccLineCount = static_cast<int>(base.getLines().size());
auto [collapseSeq, collapseKeys] = buildCollapseSequence(ccLineCount, line);
```

## calculateLinesAfterDiffs check-after-mutation

In `calculateLinesAfterDiffs`, the code adjusts `beginPos` then checks `hasDeletedContent()` (which compares `beginPos != endPos`) to decide whether to adjust `endPos`. But after adjusting `beginPos`, the comparison is between the **adjusted** beginPos and the **unadjusted** endPos. When the cumulative offset from prior diffs equals the deleted text length, the adjusted beginPos coincidentally equals the unadjusted endPos, making `hasDeletedContent()` return false. The diff is then misclassified as a pure insertion, generating `i` (insert without deletion) instead of `ce`/`cw` (change).

**Trigger condition:** `cumulativeOffset == deletedText.size()` (in flat index terms). This naturally occurs when a pure insertion precedes a replacement and the insertion length equals the replacement's deletion length — e.g., prepending "Dry-brined " (11 chars) before replacing "pretty nice" (11 chars).

**Symptom:** Optimizer produces `wi excellent` instead of `wce excellent`, inserting text without deleting the original.

**Fix:** Save `hasDeletedContent()` before mutating `beginPos`:
```cpp
bool hadDeletedContent = diffStates[i].hasDeletedContent();
diffStates[i].beginPos = adjustPos(diffStates[i].beginPos);
if (hadDeletedContent) { ... }
```

**Pattern:** Never check a predicate that depends on a field you just mutated. Save the check result first, or use an immutable proxy (`!deletedText.empty()`).

# Known vim simulation gaps

## Autoindent with cc and A+Enter

Neovim's `cc` on a line with leading whitespace preserves the indentation, placing the cursor at the first non-blank column in insert mode. Similarly, `A<CR>` copies indentation from the current line to the new line. The optimizer does not model autoindent behavior, so it may suggest sequences that produce incorrect results on indented lines.

## cw trailing space (delete→change conversion)

Vim treats `cw`/`cW` like `ce`/`cE` — they don't include trailing whitespace, unlike `dw`/`dW`. The `deleteToChange` function in EditOptimizer previously converted `dw` → `cw`, which would produce incorrect results when `dw` deleted trailing whitespace to reach the goal.

**Fix:** Convert `dw`/`dW` to `dwi`/`dWi` (delete then enter insert mode) instead of `cw`/`cW`. No per-call equivalence check is needed because `de`/`dE` (WordEdge) is explored before `dw`/`dW` (GapEdge) — when the ranges are identical, `de` stores its result first and `dw` is skipped. So `dw` only reaches the goal when trailing whitespace made the difference, meaning `cw` is always wrong. See `docs/optimizer/edit-optimizer.md` § dw/dW → dwi/dWi.


