# Optimizer Architecture and Logic

We have three different Optimizers.
They all have similar configuration settings and include E = effort for typing the key sequence in their heuristics.
When optimizing commands, we only want to send the relevant buffer context to guard against redundant searching. Thus, Optimizers take in boundary info so that gg/G, for instance, are not searched if they would "spill over".

For more details about how boundaries are handled, see /plans/EditBoundaryLogic.typ.

## MotionOptimizer
- Finds best ways to move cursor from start to end.
- Does a pure A* search over possible motions
- Uses the heuristic of E + (Manhattan distance to goal)
- Builds an index over text objects for efficiency, as we guarantee the buffer contents stay the same

## EditOptimizer
- Finds best ways to change starting text to ending text, assuming that all ending text will be typed. We can start from any position, but always end at the very last character.
- It is very important to handle edit boundaries here, as start/end can span multiple lines, and straddle lines/words from the original context.
- TBD: Does a multi-source A* search over deleting all characters. Look for improvement.
- Uses the heuristic of E + (Remaining characters to delete)

## CompositionOptimizer
- Finds best ways to change any buffer state to any other buffer state by content.
- First, uses Myer's diff logic over characters to represent the change into many Diff states.
- Then, solves each Diff state using EditOptimizer. This gives us possible "actions" with a start, end, and cost, and that resolve a Diff State.
- Does a pure A* search, using MotionOptimizer to get to the next edit region, and EditResults to resolve the current edit region.
- Uses the heuristic of E + (Distance to next edit region) + (expected cost of edit regions not yet completed). We penalize overshooting distance more, to enforce resolving the edit regions in order.

## Buffer Flattening
For edit distance analysis, buffers are flattened to single strings with `\n` characters:
```cpp
flattenLines({"aaa", "bbb", "ccc"}) → "aaa\nbbb\nccc"
```
