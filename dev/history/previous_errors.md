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
  - Cursor at original first line length (position where join occurred), but clamp to last valid normal-mode column. When the next line is empty (or all-whitespace with `J`), `originalLen == currentLine.size()` which is past-end in normal mode.

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

**Fix:** Removed `adjustForSequential` entirely. Inlined the adjustment into `calculateLinesAfterDiffs`, which already has the intermediate `Lines` at each step. The adjustment converts positions to flat character indices against the original buffer, applies a cumulative offset (sum of `insertedText.size() - deletedText.size()` from prior diffs), then converts back to `(line, col)` against the current intermediate buffer. See `dev/optimizer/diff-generation.md`.

## Subset boundary used target range instead of full subset extent

In `CompositionOptimizer.cpp`, when creating sub-buffers for `NavOptimizer::optimize` (the `CharInterval`-goal overload, formerly `optimizeToRange`), the `NavBoundary` was constructed using the **target edit range** (`localRangeFirst`, `localRangeLast`) as the boundary positions. This caused `BoundaryContext` to compute `leftColOffset`/`rightColOffset` that clamped motions like `$` and `0` to the edit range edges instead of the full subset extent.

The bug existed in both code paths: the edit/motion transition path and the pure insertion path (`exploreInsertionStrategy` lambda). Both construct subsets and call the range-goal `optimize`.

**Symptom:** Motions like `$` would land at the edit range edge instead of end-of-line, producing incorrect sequences. Manifested as failures in `SingleLine_Substitution`, `PureInsertion`, and `PureDeletion` tests.

**Fix:** Use `subsetFirst(0, 0)` and `subsetLast(subset.size()-1, subset.back().size()-1)` for the boundary, since the boundary should represent the full navigable extent of the subset, not the target range. The target range is only for the optimizer's `isInRange` check.

```cpp
// Boundary uses full subset extent, not the target range.
Position subsetFirst(0, 0);
Position subsetLast(static_cast<int>(subset.size()) - 1,
    std::max(0, static_cast<int>(subset.back().size()) - 1));
NavBoundary subsetBoundary(subset, subsetFirst, subsetLast, ...);
```

## $ motion missing TARGETCOL_EOL

In `NavExplorer.h`, the `$` motion emitted its goal position using the 2-param Position constructor `{pos.line, dollarCol}`, which sets `targetCol = dollarCol`. The correct behavior is `targetCol = TARGETCOL_EOL` (INT_MAX), which makes subsequent vertical motions (`j`, `k`, `<C-d>`, etc.) stick to end-of-line.

**Symptom:** Sequences like `$<C-d>` would land at the wrong column on the target line. After `$` on a line of length 10 (dollarCol=9), a subsequent `j` to a line of length 20 would go to col 9 instead of col 19.

**Fix:** Use the 3-param constructor to explicitly set `TARGETCOL_EOL`:
```cpp
emitMotion(base, "$", {pos.line, dollarCol, TARGETCOL_EOL}, {Key::Key_Shift, Key::Key_4});
```

**Pattern:** This is a specific instance of the general targetCol pitfall documented above — always use the 3-param Position constructor when the target column semantics differ from the actual column.

## cc collapse sequence overcounting on single-line buffers

In `TransformOptimizer.cpp`, the dd→cc conversion computes how many `<BS>`/`<Del>` keystrokes are needed to collapse a multi-line cc result to a single line. The original code used `lines.size() + 1` as the total line count, where `lines` is the post-dd buffer. The `+1` accounts for the line that `cc` preserves but `dd` removes.

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

## calculateLinesAfterDiffs skipped adjustment when cumulativeOffset was 0

In `calculateLinesAfterDiffs`, position adjustment for diffs was gated on `if (cumulativeOffset != 0)`. This meant that when earlier diffs changed the *line structure* of the buffer without changing the total character count (offset = 0), subsequent diff positions were not adjusted.

**Trigger condition:** A diff replaces `\n` with ` ` (or vice versa). The deleted and inserted text have the same length (1 char each), so `cumulativeOffset` remains 0. But the buffer's line structure changes: what was line 1, col 0 in the original may now be line 0, col N in the intermediate buffer.

**Example:**
```
Initial: ["aaa", "bbb", "ccc", "ddd"]  (4 lines)
Goal:    ["aaa bbb", "ccc ddd"]         (2 lines)
Diff 0: '\n' -> ' ' at (0,3)..(1,0)    (offset = 0)
Diff 1: '\n' -> ' ' at (2,3)..(3,0)    (offset = 0)
```
After diff 0, buffer is `["aaa bbb", "ccc", "ddd"]`. Diff 1's original position `(2,3)..(3,0)` is wrong in this intermediate buffer -- it should be `(1,3)..(2,0)`. But since `cumulativeOffset == 0`, adjustment was skipped entirely.

**Fix:** Changed the condition from `if (cumulativeOffset != 0)` to `if (i > 0)` -- always adjust positions for diffs after the first, regardless of cumulative offset. The flat-index round-trip through `posToFlat` / `flatToPos` correctly handles line structure changes even when the total character count is unchanged.

**Pattern:** Don't use a shortcut condition (offset == 0) to skip position adjustment when the underlying coordinate system (line/col) can change independently of the flat character count.

## cw trailing space (delete→change conversion)

Vim treats `cw`/`cW` like `ce`/`cE` — they don't include trailing whitespace, unlike `dw`/`dW`. The `deleteToChange` function in TransformOptimizer previously converted `dw` → `cw`, which would produce incorrect results when `dw` deleted trailing whitespace to reach the goal.

**Fix:** Convert `dw`/`dW` to `dwi`/`dWi` (delete then enter insert mode) instead of `cw`/`cW`. No per-call equivalence check is needed because `de`/`dE` (WordEdge) is explored before `dw`/`dW` (GapEdge) — when the ranges are identical, `de` stores its result first and `dw` is skipped. So `dw` only reaches the goal when trailing whitespace made the difference, meaning `cw` is always wrong. See `dev/optimizer/edit-optimizer.md` § dw/dW → dwi/dWi.

## exploreDeletion collapse sequence used d{motion} line count instead of c{motion}

In `TransformOptimizer.cpp`, `exploreDeletion` converts `d{motion}` results to `c{motion}` equivalents and computes how many `<BS>`/`<Del>` keystrokes are needed to collapse multi-line `cc` results to a single line. For multi-line `d{motion}` (e.g., `dj`), the code used the post-delete buffer's line count (`lines.size()`), but `c{motion}` doesn't remove lines the way `d{motion}` does — it replaces the range with an empty insert-mode line. The effective line count for the collapse should reflect the pre-delete buffer minus the lines *merged* by the motion, not the lines *removed*.

**Example:** `dj` on a 3-line buffer removes 2 lines, leaving 1 line. `cj` on the same buffer replaces lines 0-1 with an empty line, leaving 2 lines (the empty line + the original line 2). Using post-`dj` line count (1) undercounts the collapse.

**Fix:** Use `base.getLines().size() - (lastLine - firstLine)` where `firstLine`/`lastLine` come from the deletion range. This computes what `c{motion}` would produce: original lines minus the merged span (but keeping one line for the insertion point). The `dw`/`dW` case still uses the post-delete line count since those convert to `dwi`/`dWi` (delete-then-insert), not change.

## lineBaseIndex computation for empty lines in TransformResult

In `TransformResult::TransformResult`, the constructor builds a `lineBaseIndex_` for O(1) flat-index lookup from buffer coordinates. The cumulative sum added `initialLines[i].size()` per line, but empty lines have 0 `size()` despite having 1 valid cursor position (col 0). This caused flat-index underflow for positions on or after empty lines.

**Fix:** Use `max(1, size)` for the per-line position count:
```cpp
int positions = initialLines[i].empty() ? 1 : static_cast<int>(initialLines[i].size());
cumSum += positions;
```
This matches `initStartingPositions` in the search, which also counts 1 position for empty lines.

## textObjectRange daw rejects when boundary clips leading whitespace

In `VimEndpointUtils.cpp`, `textObjectRange` handles `daw` on a word with no trailing whitespace by including leading whitespace instead. The backward `GapEdge` search finds the start of leading whitespace. If it returns col 0 (indicating only indentation before the word), the range falls back to `WordEdge` (word-only, no whitespace).

However, when used in the TransformOptimizer with a prefix boundary (`leftColOffset > 0`), the backward `GapEdge` search can return the boundary start instead of the true whitespace start. This could land at col > 0, making the code think there's a prior word — producing a range that disagrees with Vim's actual `daw` behavior (which would include whitespace all the way to line start).

**Fix:** After the `GapEdge` search returns `POSITION_OUTSIDE_BOUNDARY` or col 0, check whether whitespace actually exists before the word but falls within the boundary. If the `WordEdge` start is at col > 0 and the character before it is blank, the boundary is clipping leading whitespace — reject the range by setting `start = POSITION_OUTSIDE_BOUNDARY`:
```cpp
if (wordStart != POSITION_OUTSIDE_BOUNDARY &&
    wordStart.line == cursor.line && wordStart.col > 0 &&
    isBlank(lines[wordStart.line][wordStart.col - 1])) {
  start = POSITION_OUTSIDE_BOUNDARY;
}
```

## tryReplacement cursor position mismatch with goalPos

In `TransformOptimizer.cpp`, `tryReplacement` builds a sequence of `r{char}` commands to transform same-length text. After executing the replacements, the cursor lands on the last *differing* position (`diff.back()`). But the `CompositionOptimizer` sets `goalPos` to the last character of the inserted text (`beginPos.col + inserted.size() - 1`), matching where `c{motion} + typed + <Esc>` would leave the cursor.

When Myers diff merges a short common suffix into the edit region (e.g., `"ffb"→"cbb"` includes the common `'b'`), the last differing position is before the end of inserted text. The `CompositionOptimizer` uses `goalPos` to plan subsequent motions, so the mismatch causes wrong motion targets.

**Example:** `"ffb"→"cbb"`: diff positions are [0, 1]. `tryReplacement` produces `rclrb`, cursor at col 1. But `goalPos` is col 2. Next motion is planned from col 2 but cursor is actually at col 1.

**Fix:** After the last replacement, append `l` movements to reach the end of the inserted text, matching goalPos:
```cpp
int lastDiff = diff.back();
int endPos = static_cast<int>(inserted.size()) - 1;
if (lastDiff < endPos) {
  int dist = endPos - lastDiff;
  if (dist <= 2) ks.appendRepeated(lCmd, dist);
  else ks.appendCounted(dist, lCmd);
}
```
This ensures all result paths in an `TransformResult` leave the cursor at the same `goalPos`.

## Boundary region edits: centralized escape

Cursor positions in the prefix/suffix boundary region cannot perform regular edits — only escape motions (h/l/j/k to get back into the content region) and backward word edits from the first suffix column. Originally, boundary checks were scattered across `exploreAllDeletions`, `exploreCountedWordEdits`, and `exploreCountedCharEdits`, each with slightly different logic.

**Fix:** Centralized into `exploreBoundaryEscape()` at the top of the main search loop. Returns `true` (with escape motions/safe backward edits emitted) when cursor is in boundary; caller does `continue`. A single `assert(!inBoundaryRegion)` after it guarantees all subsequent exploration functions operate on content positions. Fine-grained per-edit endpoint checks (whether a deletion range crosses *into* the boundary) remain in the individual exploration functions.

## Linewise deletion cursor past-end with hasLinesBelow

When `dd` operates on the last line of effective lines and `transformBoundary.hasLinesBelow()` is true, the real buffer has a line below that the cursor lands on. Previously, `deleteRangeLinewise` always clamped cursor to `min(firstLine, size-1)`, hiding this.

**Fix:** Added `hasLinesBelow` parameter to `deleteRangeLinewise` (and `TransformState::afterLinewiseDeletion`/`afterMultiLinewiseDeletion`). When `hasLinesBelow && firstLine >= newSize`, cursor stays past-end (at `firstLine`) instead of clamping. The A* search detects `needsKEscape = pos.line >= lines.size()`, clamps cursor to `size()-1`, then:
- **Goal path:** cursor is valid for `emitEditGoal`/`buildCollapseSequence`
- **Non-goal path:** `k` is appended to the command sequence; `applyEdit` handles past-end `k` during replay

Key design: cursor clamping happens **before** the goal check in `exploreLinewise`/`exploreCountedLinewise`, guaranteeing valid cursor at all subsequent points.

## CompositionOptimizerBench MultiLine vector OOB

In `BM_CompEditSize`'s `MultiLine` case, `goal.erase()` shrinks the vector but subsequent iterations still compute `line` from `DEFAULT_LINES` (the original size). Fixed by using `goal.size()` and clamping after each iteration.

## CompositionOptimizer skipped motion search inside edit range (always-lazy)

In `CompositionOptimizer.cpp`, when the cursor is inside the edit range but no edit result exists at that position, the code unconditionally skipped motion search (`continue`). This was correct when every position in the range had a result, but with always-lazy mode (limited search budget), many positions may lack results while nearby positions within the range DO have results.

**Symptom:** "No result" failures in `SingleLine_Substitution` — the cursor starts inside the edit range at a position without a result, and no transition is explored.

**Fix:** Instead of unconditionally skipping, scan the edit range for nearby positions with valid results and generate simple `h`/`l` motion transitions to reach them. The range-goal `NavOptimizer::optimize` can't be used here because it asserts `initialPos` is not inside the range. The intra-range motion search only fires for same-line positions:
```cpp
if (pos >= nextEdit.beginPos && pos < nextEdit.endPos) {
  for (int col = rangeBeginCol; col < rangeEndCol; ++col) {
    if (col == pos.col) continue;
    const Result* nearby = transformResult.resultAt(pos.line, col);
    if (!nearby) continue;
    // Generate counted h/l motion to reach col
    ctx.exploreMotionTransition(s, movementSeq, Position(pos.line, col), editsCompleted);
  }
  continue;
}
```

## CompositionOptimizer move-before-use in return statement

In `CompositionOptimizer.cpp`, the `optimize()` return statement used braced-init-list construction:
```cpp
return {std::move(results), ctx.getStats(static_cast<int>(results.size())),
        resultGoalPos, std::move(ctx.diffStates)};
```

C++ braced-init-list evaluation is left-to-right (guaranteed since C++11). `std::move(results)` empties the vector, then `results.size()` evaluates to 0. So `stats.resultsFound` was always 0 regardless of how many results were actually found.

**Symptom:** Benchmark "Found" counter always showed 0 for all CompositionOptimizer benchmarks, despite the optimizer successfully finding 5-10 results per run.

**Fix:** Capture the size before the move:
```cpp
int numResults = static_cast<int>(results.size());
return {std::move(results), ctx.getStats(numResults),
        resultGoalPos, std::move(ctx.diffStates)};
```

**Pattern:** Never read a value from a container after `std::move`-ing it in the same expression. Braced-init-list left-to-right evaluation makes this especially treacherous — it compiles without warning but silently produces wrong results.

## Benchmark stats accumulation: overwrite instead of sum

All three benchmark files (`TransformOptimizerBench.cpp`, `NavOptimizerBench.cpp`, `CompositionOptimizerBench.cpp`) used assignment instead of accumulation for per-iteration stats:
```cpp
lastStats = result.stats;  // Only captures the LAST iteration
```

With `benchmark::Counter::kAvgIterations`, Google Benchmark divides the counter value by iteration count. Since only the last iteration's stats were stored, the displayed values were 1/N of one iteration instead of the true per-iteration average.

**Fix:** Added `accumulateStats()` in `BenchUtils.h` that sums all fields across iterations, and changed all benchmark loops to use it:
```cpp
accumulateStats(totalStats, result.stats);
```



### Problem: Buffer Copying

Naively, `TransformStateKey` stored a full `Lines` copy. With `getKey()` called 2+ times per explored state (once in `exploreNewState`, once in `getNextValidState`), and buffers of ~300 bytes on a 10-line edit, this produced megabytes of unnecessary copying per search. The hash function also only hashed `lines[0]` + `lines.size()`, producing many collisions and triggering expensive `operator==` comparisons over full buffer content.

### Solution: Precomputed Content Hash

`TransformState` carries a precomputed 64-bit FNV-1a hash (`linesHash_`) over all buffer content. This hash is:
- Computed once in the `TransformState` constructor
- Recomputed after each buffer mutation (`afterDeletion`, `afterLinewiseDeletion`, `afterJoin`)

`TransformStateKey` stores `(linesHash, lineCount, line, col, mode, startIndex)` instead of the full `Lines` object. Both the hash function and equality operator use only these scalar fields — no buffer copying or content comparison.

The same pattern applies to `SuffixKey` in the suffix cache (see below).

**Collision risk**: 64-bit FNV-1a over ~10^4 states gives collision probability ~5×10^-12 per search. A hash collision would cause a state to be incorrectly pruned as "already visited," potentially missing a better path for one starting position — a minor quality degradation, not a correctness violation.
