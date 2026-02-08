# Optimizer Architecture and Logic

We have three different Optimizers. Here is a short description:
- MotionOptimizer: Pure movements from start -> (finish or finishRange) over a constant buffer
- EditOptimizer: Changing start buffer state -> end buffer state, assuming that we delete all old text first, then type new text out
- CompositionOptimizer: Orchestrates logic combining MotionOptimizer and EditOptimizer for any transition. Determines how to break up edit regions and chain movement and edit search to satisfy regions in order. Also pre-computes J (join lines) plans for diffs where source has more lines than target, offering them as alternative edit transitions in the A* search (see `docs/optimizer/composition-optimizer.md` § J Plans).

## Dependence
MotionOptimizer: independent
EditOptimizer: calls MotionOptimizer very briefly for specific visual delete.
CompositionOptimizer: calls EditOptimizer and MotionOptimizer

They all share:
- Configuration settings
- Effort for typing the sequence
- Boundary of the subbuffer received. Motions can have padding to allow for the possibility of overshoot + revert, while Edits must be exact.
- Usage of Endpoint/Range functions for checking if a motion would cross outside the boundary.
- Return some container of Results:
``` cpp
struct Result {
  std::vector<Sequence> sequences;
  double keyCost;

  Result() : keyCost(0) {}
  Result(std::vector<Sequence> seqs, double c) : sequences(std::move(seqs)), keyCost(c) {}
};

```

For more details about how boundaries are handled, see `docs/boundary-logic.md`.

### Exploring Valid Commands

All commands use the endpoint/range methodology. First, we search the span effect that a commands will have, breaking early (not searching at all) if it would cross our boundary. Then, for a motion we move the cursor to that endpoint, and for an edit we change the buffer to that endpoint. This ensures we do not duplicate work.


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
