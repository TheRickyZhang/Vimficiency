# TransformOptimizer (Transform Layer)
- Historical name retained. Conceptually this is the transform optimizer: it searches for buffer/mode-changing transitions from any valid start position to a goal state.
- It is broader than direct "edit commands" in the glossary. Replace, paste, and join-style changes still belong to the transform layer even when they are not direct edits.
- The current implementation mostly models transforms as deleting all initial text and typing all goal text (with minor exceptions).
- Heuristic: How much left to delete, sum(effective columns) + 2 * sum(additional lines) (+ effort)

## Result Structure
There are special transform shapes we note:
- *Pure Deletions*: when goalLines == {""}
- *Pure Insertions*: when initialLines = {""}
Otherwise it is a *Regular Transform*. The following describes regular transform search:

```cpp
struct TransformResult {
  vector<Result> typeAllResults; // Indexed by flat position (no newlines in count)
};
```

## Goal Check Timing
- Because we are dedicated to typing our result text, it is optimal to only go into insert mode at the very end. The best way to have this is to convert the last delete to a change, but since it is messy to do retrospectively, we have slightly different timing to anticipate.
- In Navigation/Composition, we pop state -> check if goal position -> explore commands.
But here, we pop state -> explore deletions -> check if goal, so that we can convert to change before it is committed.
- This is a bit more costly but not significant.
- Note we handle a pure deletion case in separate call, as we no longer need to worry about going into insert mode.

## Multi-source Handling
- We consider states unique by their position and mode, independent of starting index. Thus, it is possible to "exhaust" good states from searching from a specific starting index.
- This is much better for efficiency, and we are only concerned with the best sequences, so this is not a major issue.

### Early Stopping
`optimizeTransform` use early stopping: once a result is found for a startIndex, further states from that index are skipped (`if (results[s.getStartIndex()].isValid()) continue`). Ignoring small differences in running effort patterns, this is optimal.

## State Hashing (CostMap Keys)

The A* search tracks visited states in an `unordered_map<TransformStateKey, double>` (the costmap). Each state is keyed by `(buffer content, cursor position, mode, startIndex)`. Since the buffer mutates during search (deletions, joins), the key must capture buffer identity.


`TransformEditorState` carries the editor snapshot used by both simulation and search: `Lines`, cursor, mode, and a precomputed 64-bit FNV-1a hash over all buffer content. This hash is:
- Computed when an editor snapshot is constructed
- Refreshed by `TransformSimulator` after buffer mutations (`afterDeletion`, `afterLinewiseDeletion`, `afterJoin`, and counted variants)

`TransformState` wraps that editor snapshot with A* path/ranking data: sequence, running effort, cached effort, cost, dot-repeat context, and `startIndex`. Queue-ready states are created through `TransformStateFactory`, which records the editor snapshot, command effort, and heuristic cost together. This keeps pure editor simulation separate from ranked search states and avoids half-valid states with stale cost or repeat metadata.

`TransformStateKey` stores `(linesHash, lineCount, line, col, mode, startIndex)` instead of the full `Lines` object. Both the hash function and equality operator use only these scalar fields — no buffer copying or content comparison.

The same pattern applies to `SuffixKey` in the suffix cache (see below).

**Collision risk**: 64-bit FNV-1a over ~10^4 states gives collision probability ~5×10^-12 per search. A hash collision would cause a state to be incorrectly pruned as "already visited," potentially missing a better path for one starting position — a minor quality degradation, not a correctness violation.

## Suffix Cache

When a search path reaches the goal, `replayAndCacheSuffix` replays the winning sequence forward from the seed state, caching the remaining suffix at each intermediate buffer state. This enables cross-position sharing: if a different starting position reaches the same intermediate state, the cached suffix completes the path without further exploration.

**Key**: `SuffixKey = (linesHash, lineCount, pos, mode)` — deliberately excludes `startIndex` to enable sharing.

**Dot-context handling**: A cached suffix starting with `.` is ambiguous if we do not retain context. Thus, if a suffix starts with `.`, it is expanded to the explicit command it repeats (e.g., `..s` → `x.s`). Both variants are stored in `SuffixValue`:

- `ks` / `effort`: Expanded variant (first dot → explicit command). Always correct regardless of `lastEdit` context.
- `dotKs` / `dotEffort`: Original variant with leading `.`. Lower cost but only valid when `lastEdit` matches.
- `expandedDotCmd`: The command that replaced `.` (empty if no expansion needed).

At lookup: if `lastEdit == expandedDotCmd`, use the dot variant; otherwise use the expanded variant.

Subsequent dots (after the first explicit command) are unambiguous.

## Boundary Shift Handling
- Since transforms are naturally the only exact-position constrained region, we have particular method of resolving among flat indices.
- Consider an original buffer with:
Line 5: abchello
Line 6: world
Edit Region: hello world

Then:
`firstLine = 5
firstCol = 3
flatIndex = lineBaseIndex[bufferLine - firstLine] + bufferCol`
With
`lineBaseIndex[0] = -3 (only subtract firstCol on first line)
lineBaseIndex[1] = 5 (afterwards consider cumulative character count)`

Then, converting a real position to flat index takes only 3 operations. This improves over a 5-operation resolution without tracking lineBaseIndex, and a 2-operation resolution would require a lot of storage for looking up region -> flatIndex directly.

## dw/dW → dwi/dWi (not cw/cW)

The delete→change conversion (`deleteToChange`) converts `dw`/`dW` to `dwi`/`dWi` instead of `cw`/`cW` (Note: uppercase variants implicitly included starting from now). This is because vim treats `cw` like `ce` — they don't include trailing whitespace, while `dw` does (GapEdge vs WordEdge).

We unconditionally use `dwi`without checking whether `cw` would be equivalent, because the exploration order makes the check unnecessary. In `exploreAllDeletions`, WordEdge edits (`de`) are explored **before** GapEdge edits (`dw`).When `dw` and `de` produce the same deletion range (no trailing whitespace), `de` reaches the goal first and stores its result. The `dw` callback then hits the early-out `if (result.results[idx].isValid()) return` and is skipped. So `dw` only reaches the goal when `de` didn't, meaning the trailing whitespace is what made `dw` reach the goal.

This is a case where **exploration order affects correctness** of the delete→change conversion. If GapEdge were explored before WordEdge, we would need a per-call equivalence check.

## dd→cc Conversion

Since `cc` keeps the line but `dd` removes it, the conversion must account for the difference in line count.

As `buildCollapseSequence` function generates `<BS>`/`<Del>` keystrokes to collapse extra lines before/after the cursor
TODO may need more elaboration

## Auto-indent Handling

When `VimOptions::autoindent()` is true (Neovim default), entering insert mode via `cc`/`c{motion}` copies leading whitespace from the source line. Each subsequent `<CR>` copies indent from the current line. The `buildTypedCommands` function (in `src/Optimizer/BuildTypedCommands.h`, shared by TransformOptimizer and CompositionOptimizer) accounts for this:


| Case | Condition | Action |
|------|-----------|--------|
| No autoindent | autoindent empty | Type full line |
| Goal matches | goal starts with autoindent | Strip autoindent, type remainder |
| Goal has less indent | autoindent starts with goal's indent | `<BS>` × excess, type remainder |
| Mismatch | neither is prefix | `<C-u>` + type full line |

### Autoindent Source by Line

- **Line 0**: `initialAutoindent` param (from cc's source line, or empty for char-wise)
- **Line 1**: `leadingWhitespace(linePrefix + goalLines[0])` — buffer line 0 is prefix + goal
- **Line 2+**: `leadingWhitespace(goalLines[i-1])` — previous goal line

### Parameters

- `initialAutoindent`: Indent provided on first typed line (cc preserves source line indent)
- `linePrefix`: Text before edit region on first line (for computing line 1 autoindent)
- `suffixLeadingSpaces`: Whitespace to restore on last line for multi-line char-wise edits

### `<BS>` Optimization

`<BS>` in autoindent context **deletes to the previous `shiftwidth` boundary** (default 8), meaning `<BS>` can only land on multiples of `shiftwidth`:

```
autoindent = "                " (16 spaces, sw=8)
goal line  = "        foo" (8 spaces + "foo")
<C-u> + type "        foo" = 2 + 11 = 13 keys
1× <BS> + "foo"             = 1 + 3  = 4 keys  ← better! (16 → 8 in one BS)
autoindent = "     " (5 spaces, sw=8)
goal line  = "  foo" (2 spaces + "foo")
BS would go 5 → 0 (overshoots past 2) — cannot use <BS>, fall through to <C-u>
```

`bsCountForIndent(from, to, sw)` computes the number of `<BS>` presses needed, returning -1 if `<BS>` overshoots past the target (i.e., the target is not reachable via shiftwidth boundaries).

The condition for `<BS>` being better: `bsNeeded + remainder < 2 + goalLine.size()` where:
- `bsfinishNeeded = bsCountForIndent(autoindent.size(), goalIndent.size(), shiftwidth)`
- `remainder = goalLine.size() - goalIndent.size()`

**Important:** Neovim requires `\x08` (ASCII BS) for backspace in insert mode, not `\x7f` (ASCII DEL)

### Pure Deletion

The primary reason we handle pure deletions separately is because we do not need to handle delete -> change conversions. It is implemented very similarly (difference triggered via template), but crucially:
- Positions after the last deletion may be different than the last position, so we add a positions mapping in PureDeletionEditResult:
```
struct PureDeletionEditResult {
  TransformResult transformResult;
  // Per-start goal cursor positions in buffer coordinates (same flat index as TransformResult::getResults()).
  std::vector<Position> goalPosByStart;
};
```
- We can use a normal processing order, ie not duplicate goal checks in anticipation of delete -> change conversion

### Pure Insertion
Handled at the CompositionOptimizer level
