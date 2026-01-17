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

### EditBoundary (Simplified)
Uses raw chars instead of CharType enum:
```cpp
struct EditBoundary {
  char leftChar = NO_CHAR;   // Char before edit region ('\n' at line start)
  char rightChar = NO_CHAR;  // Char after edit region ('\n' at line end)
  bool hasLinesAbove = false;
  bool hasLinesBelow = false;
  // + QuoteFlags/BracketFlags for text object support
};
```

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
