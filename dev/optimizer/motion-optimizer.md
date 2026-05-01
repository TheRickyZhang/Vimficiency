# NavOptimizer
- Heuristic: In addition to effort, uses Manhattan distance to goal (|dx| + |dy|)
- Can prune explored motions based on direction
- Builds an index over text objects for efficiency, as we guarantee the buffer contents stay the same. Thus, we can easily search what {cnt}w will be closest to goal.

## Range Semantics Contract

- Motion search targets are inclusive intervals: `[first, last]` (`CharInterval`).
- Edit/diff code uses half-open ranges: `[begin, end)` (`CharRange`).
- Conversion from half-open to inclusive happens only at the boundary before invoking `NavOptimizer` (via `toMotionInterval`).
- `NavOptimizer` internals do not operate on half-open target ranges.

## API Surface

`NavOptimizer::optimize` is one overloaded entry point but returns one of
two types depending on what the caller asked for. The per-result element
shape mirrors the goal:

| Overload | Goal type | Returns | Per-result element |
|----------|-----------|---------|--------------------|
| `optimize(lines, initialPos, goalInterval, params, ...)` | `CharInterval` (inclusive `[first, last]`) | `LandingNavResult` | `LandingResult` (sequence + cost + landing `CursorPos`) |
| `optimize(lines, initialPos, goalInterval, params, ..., bufferIndex, lineOffset)` | `CharInterval` | `LandingNavResult` | `LandingResult` (same as above; reuses a pre-built `BufferIndex`) |
| `optimize(lines, initialPos, goalPos, params, ...)` | `CursorPos` (single point) | `NavResult` | `Result` (sequence + cost only) |

Why the asymmetry: for an interval goal, different motions land in
different cells of the interval, so each result must carry its landing.
For a single-cursor goal, the landing is always the input `goalPos` — a
per-result `goalPos_` field would just echo it, so it's omitted. The
type system enforces this: callers can't ask for the landing of a
single-cursor result, because the question has no per-result answer.

Both overloads share one A* implementation. The single-cursor overload
runs the interval search with `CharInterval(goalPos, goalPos)` and
`allowMultiplePerPosition = true` (so distinct sequences to the one
point are enumerated), then strips the redundant landing before
returning `NavResult`.

`NavOptimizerParams::allowMultiplePerPosition` controls dedup for the
interval overload:
- `false` (default): at most one result per unique landing position —
  the cheapest path wins.
- `true`: every found path is emitted; dedup keys on sequence text only.

`LandingResult` lives in `src/Optimizer/LandingResult.h`. It is the
landing-carrying variant of `Result` and is named for what it carries
(a landing position), not for the query that produced it.


### Direction-Based Motion Exploration (6-Class Model)

There are 6 natural movement classes:

| Class | Motions | Behavior |
|-------|---------|----------|
| Left | `h`, `0` | Pure horizontal left |
| Right | `l`, `$` | Pure horizontal right |
| Up | `k`, `<C-u>`, `gg` | Vertical up (predictable column) |
| Down | `j`, `<C-d>`, `G` | Vertical down (predictable column) |
| Forward-crossing | `w`, `W`, `e`, `E`, `}`, `)` | Traverses forward, may cross lines|
| Backward-crossing | `b`, `B`, `ge`, `gE`, `{`, `(` | Traverses backward, may cross lines |

**Selection logic** (in `exploreDirectionalStandardMotions`):
- Same line: Left+Backward OR Right+Forward (2/6 classes)
- Same column (different line): Up+Backward OR Down+Forward (2/6 classes)
- Different line and column:
  - Vertical: Down+Forward OR Up+Backward based on goal.line vs pos.line
  - Horizontal: Left+Backward OR Right+Forward based on goal.col vs pos.col
  - Result: 3-4 classes per state

**Toggle**: `NavOptimizerParams::useDirectionalPruning` (default: `true`)
- When `true`: Uses 6-class pruning (~50% fewer motions explored per state)
- When `false`: Uses `exploreAllStandardMotions` (explores all directions)


### $ and TARGETCOL_EOL

The `$` motion must emit its goal position with `targetCol = TARGETCOL_EOL` (INT_MAX), not `targetCol = dollarCol`. This ensures subsequent vertical motions (`j`, `k`, `<C-d>`) stick to end-of-line via `clampColPreservingTarget`. Use the 3-param Position constructor:

```cpp
emitMotion(base, "$", {pos.line, dollarCol, TARGETCOL_EOL}, ...);
```

## Buffer Index
- We first process the entire buffer once with crossing motions (as they aren't easily predictable) to determine where the "anchor" points are.
- Then, simulating {n}{motion} becomes finding the nth anchor from this position.
- We use this to find the precise counts that will land us before/after our goal.
