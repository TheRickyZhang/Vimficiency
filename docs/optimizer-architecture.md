# Optimizer Architecture and Logic

We have three different Optimizers.
They all have similar configuration settings and include E = effort for typing the key sequence in their heuristics.
When optimizing commands, we only want to send the relevant buffer context to guard against redundant searching. Thus, Optimizers take in boundary info so that gg/G, for instance, are not searched if they would "spill over".

For more details about how boundaries are handled, see `docs/boundary-logic.md`.

## MotionOptimizer
- Finds best ways to move cursor from start to end.
- Does a pure A* search over possible motions
- Uses the heuristic of E + (Manhattan distance to goal)
- Builds an index over text objects for efficiency, as we guarantee the buffer contents stay the same

### MotionBoundary

When operating on a sub-buffer, MotionOptimizer uses `MotionBoundary` to prevent motions from escaping the region:

```cpp
struct MotionBoundary {
  int leftColOffset = 0;      // Protected columns at start of line 0
  int rightColOffset = 0;     // Protected columns at end of last line
  bool hasLinesAbove = false; // Are there lines above the sub-buffer?
  bool hasLinesBelow = false; // Are there lines below the sub-buffer?
};
```

### Endpoint-First Boundary Checking

Motions are checked using `VimEndpointUtils` functions BEFORE being applied. This prevents "silent clamping" where a motion would escape the sub-buffer but gets clamped to the edge, producing incorrect results.

| Motion Type | Boundary Check | Endpoint Function |
|-------------|----------------|-------------------|
| Simple line (h, l, 0, ^, $) | None needed | N/A (same-line only) |
| Vertical (j, k) | Line bounds check | Inline |
| Scroll (<C-d/u/f/b>) | Edge line check | `scrollEndpoint()` |
| Word (w, b, e, W, B, E, ge, gE) | Col offset + hasLinesOutside | `motionWordEndpoint()` |
| Paragraph ({, }) | Edge line check | `motionParagraphEndpoint()` |
| Sentence ((, )) | Edge line check | `motionSentenceEndpoint()` |

**Edge line rule**: When `hasLinesAbove`/`hasLinesBelow` is true, motions landing on the first/last line are excluded - they may have been clamped and would behave differently in the full buffer.

### Special Cases (No Endpoint Check)

These motions use different boundary handling:

- **f/F motions**: Same-line only, no escape risk. Explored via `generateFMotions()`.
- **gg/G**: Excluded entirely via `MotionBoundary.excludeGG()`/`excludeG()` flags.
- **Count searches**: See below.

### Count Search Boundary Handling

Count searches (e.g., `3w`, `2}`) use `BufferIndex` to find positions. Boundary handling differs by motion type:

| Count Search Type | When Bounded |
|-------------------|--------------|
| Word (w, b, e, W, B, E, ge, gE) | Allowed if result not on edge line |
| Paragraph/Sentence ({, }, (, )) | Only count=1 allowed (count>1 may have intermediate positions on edge lines) |

## EditOptimizer
- Finds best ways to change starting text to ending text.
- Two strategies available:
  1. **Type-all**: Delete content, enter insert mode, type all new text
  2. **Replacement**: For same-length transformations, use `r{c}` or `R{chars}<Esc>`
- Multi-source A* search over delete operations from any starting position.
- Uses the heuristic of E + (Remaining characters to delete)

### API
```cpp
// Main entry point - returns results for all starting positions
EditResult optimizeEdit(const Lines& editRegion, EditBoundary boundary);

// Replacement strategy for same-length strings (no newlines)
Result tryReplacement(const string& deleted, const string& inserted, const Config& config);
```

### EditResult Structure
```cpp
struct EditResult {
  vector<Result> typeAllResults;      // Indexed by flat position (no newlines in count)
  vector<Result> replacementResults;  // For replacement strategy
  int replacementEnd;
};
```

### Flat Position Indexing
Results are indexed by character position count, NOT including newlines:
- For `{"aa", "bb"}`: 4 positions (indices 0-3)
- Position (0,0)→0, (0,1)→1, (1,0)→2, (1,1)→3

### EditBoundary

Stores full prefix/suffix strings and line context for boundary-aware editing:

```cpp
struct EditBoundary {
  string prefix_;            // Content before edit region on first line
  string suffix_;            // Content after edit region on last line
  bool hasLinesAbove_;       // Lines exist above the edit region
  bool hasLinesBelow_;       // Lines exist below the edit region
  // + QuoteFlags/BracketFlags for text object support

  // Convenience accessors
  char leftChar() const;     // Last char of prefix (or '\n'/NO_CHAR)
  char rightChar() const;    // First char of suffix (or '\n'/NO_CHAR)
  bool hasPrefix() const;
  bool hasSuffix() const;
};
```

### EditSearchContext

Encapsulates boundary-aware search state. Key responsibilities:

- **effectiveLines**: Edit region with prefix/suffix prepended/appended for correct cursor clamping
- **leftColOffset / rightColOffset**: Column offsets protecting prefix/suffix content
- **Endpoint checking**: Uses `VimEndpointUtils` functions with boundary parameters

### Endpoint-First Deletion Checking

Similar to MotionOptimizer, EditOptimizer checks motion endpoints BEFORE applying deletions:

```cpp
// Forward word deletions
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
