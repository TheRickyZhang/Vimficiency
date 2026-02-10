# EditOptimizer
- Supports edit search from any positions in initial state to a goal state, done primarily by deleting and typing.
- Heuristic: How much left to delete, sum(effective columns) + 2 * sum(additional lines) (+ effort)

## Result Structure
- Two strategies available:
  1. **Type-all**: Delete content with last command change, do backspace/delete to get to single line, then type all new text
  2. **Replacement**: For same-length transformations, use a combination of {cnt}r and moving with l/f{}.

```cpp
struct EditResult {
  vector<Result> typeAllResults;      // Indexed by flat position (no newlines in count)
  vector<Result> replacementResults;  // For replacement strategy
  int replacementEnd; // Index the replacement ends at, since it may not be the last
};
```

## Goal Check Timing
- Because we are dedicated to typing our result text, it is optimal to only go into insert mode at the very end. The best way to have this is to convert the last delete to a change, but since it is messy to do retrospectively, we have slightly different timing to anticipate.
- In Motion/Composition, we pop state -> check if goal position -> explore commands.
But here, we pop state -> explore deletions -> check if goal, so that we can convert to change before it is committed.
- This is a bit more costly but not significant.
- Note we handle a pure deletion case in separate call, as we no longer need to worry about going into insert mode.

## Multi-source Handling
- We consider states unique by their position and mode, independent of starting index. Thus, it is possible to "exhaust" good states from searching from a specific starting index.
- This is much better for efficiency, and we are only concerned with the best sequences, so this is not a major issue.

### Early Stopping
Both `optimizeEdit` and `optimizePureDeletion` use early stopping: once a result is found for a startIndex, further states from that index are skipped (`if (results[s.getStartIndex()].isValid()) continue`). This is safe because A* explores states in cost order, and typed content cost is fixed regardless of path, so the first deletion path found for a position is optimal.

Without early stopping, heuristic bias causes start-position starvation in multi-line edits. The A* heuristic favors states that are "closer" to the goal in character count. For multi-line regions, `D`/`dd` reduce character count dramatically (removing entire lines), while `J` barely moves the needle (newline → space = net -1 character). This causes the search to heavily favor delete-based paths, and once position 0 finds its result, the search continues to re-explore position 0's continuations (each still cheaper by heuristic) while other positions starve.

Additionally, the shared costMap (states keyed by buffer+cursor, not startIndex) means multiple positions converging to the same intermediate state only keep the cheapest path, further starving positions that need more expensive initial operations.

Early stopping resolves this: once a position's result is found, its states are immediately pruned, freeing the search budget for remaining positions.

## State Hashing (CostMap Keys)

The A* search tracks visited states in an `unordered_map<EditStateKey, double>` (the costmap). Each state is keyed by `(buffer content, cursor position, mode, startIndex)`. Since the buffer mutates during search (deletions, joins), the key must capture buffer identity.

### Problem: Buffer Copying

Naively, `EditStateKey` stored a full `Lines` copy. With `getKey()` called 2+ times per explored state (once in `exploreNewState`, once in `getNextValidState`), and buffers of ~300 bytes on a 10-line edit, this produced megabytes of unnecessary copying per search. The hash function also only hashed `lines[0]` + `lines.size()`, producing many collisions and triggering expensive `operator==` comparisons over full buffer content.

### Solution: Precomputed Content Hash

`EditState` carries a precomputed 64-bit FNV-1a hash (`linesHash_`) over all buffer content. This hash is:
- Computed once in the `EditState` constructor
- Recomputed after each buffer mutation (`afterDeletion`, `afterLinewiseDeletion`, `afterJoin`)

`EditStateKey` stores `(linesHash, lineCount, line, col, mode, startIndex)` instead of the full `Lines` object. Both the hash function and equality operator use only these scalar fields — no buffer copying or content comparison.

The same pattern applies to `SuffixKey` in the suffix cache.

**Collision risk**: 64-bit FNV-1a over ~10^4 states gives collision probability ~5×10^-12 per search. A hash collision would cause a state to be incorrectly pruned as "already visited," potentially missing a better path for one starting position — a minor quality degradation, not a correctness violation.

## Boundary Shift Handling
- Since Edits are naturally the only exact-position constrained region, we have particular method of resolving among flat indices.
- Consider an original buffer with:
Line 5: abchello
Line 6: world
And our edit region is the hello world

Then:
`firstLine = 5
firstCol = 3
flatIndex = lineBaseIndex[bufferLine - firstLine] + bufferCol`
With
`lineBaseIndex[0] = -3 (only subtract firstCol on first line)
lineBaseIndex[1] = 5 (afterwards consider cumulative character count)`

Then, converting a real position to flat index takes only 3 operations. This improves over a 5-operation resolution without tracking lineBaseIndex, and a 2-operation resolution would require a lot of storage for looking up region -> flatIndex directly.

## dw/dW → dwi/dWi (not cw/cW)

The delete→change conversion (`deleteToChange`) converts `dw`/`dW` to `dwi`/`dWi` instead of `cw`/`cW`. This is because vim treats `cw`/`cW` like `ce`/`cE` — they don't include trailing whitespace, while `dw`/`dW` does (GapEdge vs WordEdge).

We unconditionally use `dwi`/`dWi` without checking whether `cw` would be equivalent, because the exploration order makes the check unnecessary. In `exploreAllDeletions`, WordEdge edits (`de`/`dE`) are explored **before** GapEdge edits (`dw`/`dW`). When `dw` and `de` produce the same deletion range (no trailing whitespace), `de` reaches the goal first and stores its result. The `dw` callback then hits the early-out `if (result.results[idx].isValid()) return` and is skipped. So `dw` only reaches the goal when `de` didn't — meaning the trailing whitespace is what made `dw` reach the goal, and `cw` would always be wrong.

This is a case where **exploration order affects correctness** of the delete→change conversion. If GapEdge were explored before WordEdge, we would need a per-call equivalence check.

## dd→cc Conversion

When the best edit strategy is a linewise delete (`dd`) followed by insert-mode typing, the optimizer converts this to `cc` (change line) which preserves the line while clearing its contents. Since `cc` keeps the line but `dd` removes it, the conversion must account for the difference in line count.

The `buildCollapseSequence` function generates `<BS>`/`<Del>` keystrokes to collapse extra lines that `cc` leaves behind (when the replacement text has fewer lines than the original). It takes `totalLines` (the number of lines `cc` operates on) and `line` (the cursor line within that range):
- Lines before cursor → `<BS>` keystrokes
- Lines after cursor → `<Del>` keystrokes

The correct `totalLines` for this conversion is `base.getLines().size()` (the pre-dd line count), since that represents the buffer state that `cc` would actually operate on. Using `lines.size() + 1` (post-dd buffer + 1) fails when `dd` on the last line of a single-line buffer triggers the buffer invariant (`lines.empty() → lines.push_back("")`), making `lines.size()` already 1 and the `+1` overcounting.

## Autoindent Handling

When `VimOptions::autoindent()` is true (Neovim default), entering insert mode via `cc`/`c{motion}` copies leading whitespace from the source line. Each subsequent `<CR>` copies indent from the current line. The `buildTypedCommands` function (in `src/Optimizer/BuildTypedCommands.h`, shared by EditOptimizer and CompositionOptimizer) accounts for this.

### Core Problem

For each goal line, we must determine what indent Neovim provides automatically (the "autoindent") and handle the mismatch:

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

When autoindent provides more spaces than the goal line needs, `<BS>` can be more efficient than `<C-u>`. However, `<BS>` in autoindent context **deletes to the previous `shiftwidth` boundary** (default 8), not just 1 space. This means `<BS>` can only land on multiples of `shiftwidth`:

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
- `bsNeeded = bsCountForIndent(autoindent.size(), goalIndent.size(), shiftwidth)`
- `remainder = goalLine.size() - goalIndent.size()`

**Important:** Neovim requires `\x08` (ASCII BS) for backspace in insert mode, not `\x7f` (ASCII DEL). While most terminals send `\x7f` for the Backspace key, Neovim only treats `\x08` as able to delete autoindent. The `\x7f` byte is silently ignored over autoindent whitespace.

