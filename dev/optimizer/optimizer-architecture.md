# Optimizer Architecture and Logic

We have three different Optimizers. Here is a short description:
- NavOptimizer: navigation optimizer. Solves cursor-only state transitions from start -> (finish or finishRange) over a constant buffer
- EditOptimizer: historical code name for the transform optimizer. Solves buffer/mode-changing state transitions; the current implementation mostly models them as delete old text + type goal text
- CompositionOptimizer: orchestrates NavOptimizer and EditOptimizer to solve full transitions. Determines how to break up transform regions and chain navigation and transform search to satisfy them in order. Also pre-computes J (join lines) plans for diffs where source has more lines than target, offering them as alternative transform transitions in the A* search (see `dev/optimizer/composition-optimizer.md` § J Plans).

Terminology note:
- Motion/Edit are direct command families
- Navigation/Transform are search/state-transition families
- `NavOptimizer` already matches the broader navigation concept; `EditOptimizer` keeps its historical name even though it is really the transform layer

## Dependence
NavOptimizer: independent
EditOptimizer: calls NavOptimizer very briefly for specific visual delete.
CompositionOptimizer: calls EditOptimizer and NavOptimizer

They all share:
- Configuration settings
- Effort for typing the sequence
- Boundary of the subbuffer received. Navigation can have padding to allow for the possibility of overshoot + revert, while transforms must be exact.
- Usage of Endpoint/Range functions for checking if a motion would cross outside the boundary.
- Counted-command cognitive penalties (see `dev/optimizer/count-penalty.md`)
- Return some container of Results:
``` cpp
struct Result {
  std::vector<Sequence> sequences;
  double keyCost;

  Result() : keyCost(0) {}
  Result(std::vector<Sequence> seqs, double c) : sequences(std::move(seqs)), keyCost(c) {}
};

```

For more details about how boundaries are handled, see `dev/core/boundary-logic.md`.

### Exploring Valid Commands

All commands use the endpoint/range methodology. First, we search the span effect that a command will have, breaking early (not searching at all) if it would cross our boundary. Then, for a motion we move the cursor to that endpoint, and for a transform candidate we apply the buffer/mode effect associated with that endpoint or range. This ensures we do not duplicate work.


```cpp
// Forward word
Position endpoint = VimCore::motionWordEndpoint(
    cursor, lines, forward, edgeType, isBig, skipCurrent,
    rightColOffset, hasLinesBelow);
if (endpoint == POSITION_OUTSIDE_BOUNDARY) continue;

// Text objects
Range range = VimCore::textObjectRange(
    cursor, lines, isInner, isBig,
    leftColOffset, rightColOffset, hasLinesAbove, hasLinesBelow);
if (range.first == POSITION_OUTSIDE_BOUNDARY) continue;
```


| Motion Type | Boundary Check | Endpoint Function |
|-------------|----------------|-------------------|
| Simple line (h, l, 0, ^, $) | Col check | N/A (same-line only) |
| Vertical (j, k) | Line bounds check | Inline |
| Scroll (<C-d/u/f/b>) | Edge line check | `scrollEndpoint()` |
| Word (w, b, e, W, B, E, ge, gE) | Col offset + hasLinesOutside | `motionWordEndpoint()` |
| Paragraph ({, }) | Edge line check | `motionParagraphEndpoint()` |
| Sentence ((, )) | Edge line check | `motionSentenceEndpoint()` |


### Special Cases (No Endpoint Check)

These motions use different boundary handling:

- **f/F motions**: Same-line only, check if they don't go into prefix/suffix. Explored via `generateFMotions()`.
- **Count searches**: use `BufferIndex` to find positions not in prefix/suffix

### Templated Motion Specs

Motion exploration uses templated functions with direction-split spec vectors for compile-time dispatch:

```cpp
// Word motions - templated on Forward and EdgeType
exploreWordMotions<true, EdgeType::NextEdge>(Motion::FORWARD_NEXTEDGE_MOTIONS, base);  // w, W
exploreWordMotions<true, EdgeType::WordEdge>(Motion::FORWARD_WORDEDGE_MOTIONS, base);  // e, E
exploreWordMotions<false, EdgeType::WordEdge>(Motion::BACKWARD_WORDEDGE_MOTIONS, base); // b, B
exploreWordMotions<false, EdgeType::NextEdge>(Motion::BACKWARD_NEXTEDGE_MOTIONS, base); // ge, gE

// Paragraph/Sentence - templated on Forward
exploreParagraphMotions<true>(Motion::FORWARD_PARAGRAPH_MOTIONS, base);   // }
exploreParagraphMotions<false>(Motion::BACKWARD_PARAGRAPH_MOTIONS, base); // {

// Scroll - templated on Forward
exploreScrollMotions<true>(base);   // <C-d>
exploreScrollMotions<false>(base);  // <C-u>
```

See `MotionToSpec.h` for the spec definitions.


### A* vs Dijkstra Trade-off

Our distance heuristics are generally **inadmissible** (overestimates true cost), which makes A* loses its optimality guarantee, but for greater efficiency:
- First path found to a goal state may not be optimal
- One starting position can dominate exploration (its children have lower `f = g + h`)
- Other starting positions get "starved" — never popped from the priority queue

However, if we use pure effort, with no distance heuristic, then we can have a guaranteed correct Dijkstra exploration.

We expose distanceWeight and effortWeight in baseOptimzerParams to get a balance between these two.

## Search Stats Policy

Search stats are split into two categories:

- Core counters: cheap aggregate values such as `nodesExplored`, `totalPops`, `resultsFound`, and `queueSizeAtStop`
- Trace payloads: heavier debug data such as `exploredStates`

Core counters are part of normal optimizer outputs and benchmark workflows, so they should stay cheap and always available. Trace payloads should be gated carefully because they allocate memory and often construct strings.

`SearchStats` now centralizes mutation so callers do not write arbitrary fields directly. This gives us a single place to control expensive collection paths and to add aggregate helpers used by benchmark/reporting workflows.

### Compile-Time vs Runtime Gating

Clang can completely remove a stats helper only when the disabled mode is known at compile time. We use a centralized compile-time switch:

```cpp
constexpr bool SEARCH_TRACE_STATS_ENABLED = ...;

template<typename SequenceFn>
void maybeRecordExploredState(bool enabled, ..., SequenceFn&& makeSequence) {
  if constexpr (SEARCH_TRACE_STATS_ENABLED) {
    if (enabled) {
      exploredStates.push_back(... makeSequence());
    }
  }
}
```

When `SEARCH_TRACE_STATS_ENABLED` is false, the trace path becomes a true no-op after optimization.

By contrast, a runtime branch such as:

```cpp
if (params.trackExploredStates) {
  stats.recordExploredState(...);
}
```

still leaves a branch in the hot path when compiled, even if the branch is usually false at runtime.

Also note that a `debug(...)`-style helper only removes the helper body. Function arguments are still evaluated before the call. That means expensive payload construction such as `s.getSequence().str()` must be delayed inside the helper, not computed eagerly at the call site.

### Workflow Guidance

- `vimficiency_tests`: should rely on core counters being present; avoid enabling heavy trace collection unless the test is explicitly about exploration details
- `vimficiency_benchmarks`: should consume aggregate counters only; do not enable `exploredStates`
- `vimficiency_debug` and exploration export tooling: may enable trace payloads when investigating behavior, but should do so explicitly because they are not cheap

By default, detailed trace stats follow `DEBUG_ENABLED`. If a non-debug workflow needs trace payloads, build with `VIMFICIENCY_TRACE_SEARCH_STATS` to enable them explicitly without tying that decision to the general debug stream.

If we later add more expensive stats, they should follow the same rule: keep aggregate reporting cheap and move payload-style diagnostics behind an explicit gate.
