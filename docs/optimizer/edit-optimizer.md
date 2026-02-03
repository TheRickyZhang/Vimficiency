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

