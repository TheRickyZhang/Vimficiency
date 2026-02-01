# Optimizer Architecture and Logic

We have three different Optimizers. Here is a short description:
- MotionOptimizer: Pure movements from start -> (finish or finishRange), which allows for creating an index and simpler searching
- EditOptimizer: Changing a start buffer state to an end buffer state from startRange -> lastPos, assuming that we delete all old text first, then type new text out. Contains special handling for pure deletions.
- CompositionOptimizer: Orchestrates overall logic combining MotionOptimizer and EditOptimizer. Determines how to break up edit regions and chain movement and edit search to satisfy regions in order. For instance, would be appropriate level to consider the dot (.) motion for repeating the previous edit, or quote/bracket text object motions (ci") for combining moving and deleting.

They all share:
- Configuration settings
- Effort for typing the sequence
- Boundary of the subbuffer received, so that they do not search commands that would land outside.
- Usage of Endpoint/Range functions for checking if a motion would cross outside the boundary.
- SearchContext helper for streamlining search logic, including **effectiveLines** and **leftColOffset / rightColOffset**.
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

### Endpoint-First Boundary Checking

Motions are checked using `VimEndpointUtils` functions BEFORE being applied. This prevents "silent clamping" where a motion would escape the sub-buffer but gets clamped to the edge, producing incorrect results.

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

**Key properties:**
- Starting positions converted to effectiveLines coordinates
- Goal check: `lines == goalLines` (prefix + suffix only)
- Boundary protection: positions in prefix/suffix columns trigger limited exploration
- Line operations (D, d0, dd) gated by `hasPrefix()`, `hasSuffix()`


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

## MotionOptimizer
- Does a pure A* search over possible motions
- Heuristic: effort * factor + (Manhattan distance to goal)
- Builds an index over text objects for efficiency, as we guarantee the buffer contents stay the same

### Direction-Based Motion Exploration (6-Class Model)

`MotionExplorer` organizes motions into 6 classes based on their movement behavior:

| Class | Motions | Behavior |
|-------|---------|----------|
| Left | `h`, `0`, `^` (when fnb < pos.col) | Pure horizontal left |
| Right | `l`, `$`, `^` (when fnb > pos.col) | Pure horizontal right |
| Up | `k`, `<C-u>`, `gg` | Vertical up (predictable column) |
| Down | `j`, `<C-d>`, `G` | Vertical down (predictable column) |
| Forward-crossing | `w`, `W`, `e`, `E`, `}`, `)` | Traverses text, unpredictable column when crossing lines |
| Backward-crossing | `b`, `B`, `ge`, `gE`, `{`, `(` | Same, but backward |

**Selection logic** (in `exploreDirectionalStandardMotions`):
- Same line: Left+Backward OR Right+Forward (2/6 classes)
- Same column (different line): Up+Backward OR Down+Forward (2/6 classes)
- Different line and column:
  - Vertical: Down+Forward OR Up+Backward based on goal.line vs pos.line
  - Horizontal: Left+Backward OR Right+Forward based on goal.col vs pos.col
  - Result: 3-4 classes per state

**Toggle**: `MotionOptimizerParams::useDirectionalPruning` (default: `true`)
- When `true`: Uses 6-class pruning (~50% fewer motions explored per state)
- When `false`: Uses `exploreAllStandardMotions` (explores all directions)

The default is off because benchmarks showed some edge cases regress with pruning.
The infrastructure exists for future tuning.

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

### MotionResult Structure
```cpp
// TODO: We don't need a mode, since by default always normal
struct MotionResult {
  Position pos;
  Mode mode;
  MotionResult(Position p, Mode mode) : pos(p), mode(mode) {}
};

```

## EditOptimizer
- Finds best ways to change starting text to ending text.
- Pure deletion (ending text is all blank lines, <= start count): no need to go into insert mode, delete everything to leave exact new lines remaining.
- Two strategies available:
  1. **Type-all**: Delete content with last command change, do backspace/delete to get to single line, then type all new text
  2. **Replacement**: For same-length transformations, use `r{c}` or `R{chars}<Esc>`
- Multi-source A* search over delete operations from any starting position.
- Uses the heuristic of E + (Remaining characters to delete)
Results are indexed by effective character position count
- For `{"aa", "bb"}`: 4 positions (indices 0-3)
- Position (0,0)→0, (0,1)→1, (1,0)→2, (1,1)→3

### EditResult Structure
```cpp
struct EditResult {
  vector<Result> typeAllResults;      // Indexed by flat position (no newlines in count)
  vector<Result> replacementResults;  // For replacement strategy
  int replacementEnd;
};
```

### A* vs Dijkstra Trade-off

EditOptimizer uses **multi-source A*** search where all starting positions explore simultaneously.
This creates challenges with heuristic admissibility:

**The Problem: Inadmissible Heuristic**

Our heuristic `h = remaining characters to delete` is **inadmissible** (overestimates true cost):
- `dd` deletes an entire line in effort 2, but heuristic counts each char as effort 1
- Example: 11-char line → heuristic says 11, actual cost is 2

With inadmissible heuristics, A* loses its optimality guarantee:
- First path found to a goal state may not be optimal
- One starting position can dominate exploration (its children have lower `f = g + h`)
- Other starting positions get "starved" — never popped from the priority queue

**Solution: Templated Comparator**

`EditSearchContext.h` provides compile-time selection via templated comparator:

```cpp
template<bool UseDijkstra>
struct EditStateComparator {
  bool operator()(const EditState& a, const EditState& b) const {
    double aPriority = UseDijkstra ? a.getEffort() : a.getCost();
    double bPriority = UseDijkstra ? b.getEffort() : b.getCost();
    if (aPriority != bPriority) return aPriority > bPriority;
    return a.getStartIndex() > b.getStartIndex();  // tie-break for fairness
  }
};

using DijkstraComparator = EditStateComparator<true>;   // effort-only, optimal
using AStarComparator = EditStateComparator<false>;     // effort+heuristic, faster
```

**Current default**: `AStarComparator` (faster, may be suboptimal)

To switch: change `using PriorityQueue` in `EditSearchContext`.

**Key Design Decisions**:

1. **startIndex in EditStateKey**: Each starting position's search path is tracked independently.
   Without this, paths from different starting positions that reach the same `(lines, position)`
   state get merged, breaking multi-source search.

2. **Early stopping**: Once a result is found for a startIndex, skip further exploration of that
   startIndex. With Dijkstra, first result is guaranteed optimal. With A*, it's best-effort.

3. **Tie-break by startIndex**: Ensures fair round-robin exploration across starting positions
   when priorities are equal.

**Philosophy**: We prioritize efficiency and accept some accuracy trade-offs. Results must be
**correct** (valid Vim sequences that achieve the goal), but not necessarily **optimal**.
The key constraint is fair exploration — no starting position should be completely starved.

**TODO**: Extend templated comparator pattern to MotionOptimizer and CompositionOptimizer.

## CompositionOptimizer
- Still not fully implemented
- Finds best ways to change any buffer state to any other buffer state by content, with heuristic: effort * factor + (distance to next edit region) + (median cost of remaining edit regions)
- First, uses Myer's diff logic over characters to represent the change into many Diff states.
- Then, solves each Diff state using EditOptimizer. This gives us possible "actions" with a start, end, and cost, and that resolve a Diff State.
- Does a pure A* search, using MotionOptimizer to get to the next edit region by starting state, or using a quote/bracket text object motion to get to complete next edit region or start from motified state.
- Queries EditResults, or tries "." to repeat previous edit if inside of edit region.

