# NavOptimizer
- Heuristic: In addition to effort, uses per-axis sqrt distance to the closest goal cell (`sqrt(|dline|) + sqrt(|dcol|)`, see `NavHeuristic::distanceToRange`). It is concave because linear Manhattan overestimated true effort and railroaded the search into counted-`j` chains.
- Can prune explored motions based on direction
- Builds an index over word and paragraph landings for efficiency, as we guarantee the buffer contents stay the same. Thus, we can easily search what {cnt}w will be closest to goal.

## Range Semantics Contract

- Motion search targets are inclusive intervals: `[first, last]` (`CharInterval`).
- Edit/diff code uses half-open ranges: `[begin, end)` (`CharRange`).
- Conversion from half-open to inclusive happens only at the boundary before invoking `NavOptimizer` (via the `CharInterval(range, lines)` constructor).
- `NavOptimizer` internals do not operate on half-open target ranges.

## API Surface

All `NavOptimizer::optimize` overloads return `LandingNavResult` whose
results are `LandingResult` (sequence + cost + landing `CursorPos` via
`getGoalPos()`). There is one underlying impl — the range-goal A*
search. Single-cursor is handled as a 1-cell interval.

| Overload | Goal type | Notes |
|----------|-----------|-------|
| `optimize(lines, initialPos, goalInterval, params, ...)` | `CharInterval` (inclusive `[first, last]`) | Multi-sink shortest path. |
| `optimize(lines, initialPos, goalInterval, params, ..., bufferIndex, lineOffset)` | `CharInterval` | Same; reuses a pre-built `BufferIndex`. |
| `optimize(lines, initialPos, goalPos, params, ...)` | `CursorPos` (single point) | Convenience wrapper that forwards to the range overload with `CharInterval(goalPos, goalPos)`. Every result lands at `goalPos`. |

`NavOptimizerParams::maxResultsPerEndPos` controls result dedup in `optimize`:
- `1` (default): at most one result per unique landing position —
  the cheapest path wins. For single-point goals, this collapses to
  "first goal hit wins" since there's only one landing cell.
- `> 1`: every popped goal state is emitted until `maxResults`, with no
  per-position cap or dedup. Use this for the single-point overload when
  you want distinct sequences to the goal (e.g. `G` and `3j` to the same
  line). Only `NavFrontier` treats the value as a per-cell cap.

`LandingResult` lives in `src/Optimizer/LandingResult.h`. It is the
landing-carrying variant of `Result`.


### Direction-Based Motion Exploration (6-Class Model)

There are 6 natural movement classes:

| Class | Motions | Behavior |
|-------|---------|----------|
| Left | `h`, `0`, `^` | Pure horizontal left |
| Right | `l`, `$`, `^` | Pure horizontal right |
| Up | `k`, `<C-u>`, `gg` | Vertical up (predictable column) |
| Down | `j`, `<C-d>`, `G` | Vertical down (predictable column) |
| Forward-crossing | `w`, `W`, `e`, `E`, `}`, `)` | Traverses forward, may cross lines|
| Backward-crossing | `b`, `B`, `ge`, `gE`, `{`, `(` | Traverses backward, may cross lines |

**Selection logic** (`classesForSingleGoal` in `NavClassMask.h`, per goal endpoint; `classesForRange` takes the union over both endpoints, plus Up/Down when the cursor is on a multi-line range's edge line):
- Same line: Left+Backward OR Right+Forward (2/6 classes)
- Same column (different line): Up+Backward OR Down+Forward (2/6 classes)
- Different line and column:
  - Vertical: Down+Forward OR Up+Backward based on goal.line vs pos.line
  - Horizontal: Left+Backward OR Right+Forward based on goal.col vs pos.col
  - Result: 3-4 classes per state

**Toggle**: `NavOptimizerParams::useDirectionalPruning` (default: `true`)
- When `true`: Uses 6-class pruning
- When `false`: Uses `exploreAllStandardMotions` (explores all directions)
- Either way, `exploreCountedMotions` handles counted and `f`/`F` motions separately, choosing direction from the side of the goal the cursor is on


### $ and TARGETCOL_EOL

The `$` motion must emit its goal position with `targetCol = TARGETCOL_EOL` (INT_MAX), not `targetCol = dollarCol`. This ensures subsequent vertical motions (`j`, `k`, `<C-d>`) stick to end-of-line via `clampColPreservingTarget`. Use the 3-param `CursorPos` constructor:

```cpp
emitMotion(KSId::Dollar, {pos.line, dollarCol, TARGETCOL_EOL}, onStatic);
```

## Buffer Index
- We first process the entire buffer once with word and paragraph motions (`LandingType`; as they aren't easily predictable) to determine where the "anchor" points are. Counted sentence motions are not indexed, because Vim's sentence scan is stateful.
- Then, simulating {n}{motion} becomes finding the nth anchor from this position.
- We use this to find the precise counts that will land us before/after our goal.
