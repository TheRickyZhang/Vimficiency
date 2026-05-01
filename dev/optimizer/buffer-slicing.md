# Buffer Slicing in Optimizers

This document describes how buffer slicing works at each layer of the optimizer stack.

## Overview

Buffer slicing limits the search region to only the relevant portion of the buffer, which improves performance by reducing the search space.

## Padding Rules

| Target | Padding | Rationale |
|--------|---------|-----------|
| NavOptimizer | Yes (linewise) | Potential for overshoot, like $b or }k |
| TransformOptimizer | No | Need exact edit regions (overshoot never optimal )  |


When creating sub-buffers, positions must be remapped:

**To subset coordinates** (before calling optimizer):
```cpp
Position subsetPos(pos.line - lineOffset, pos.col, pos.targetCol);
```

**Back to original coordinates** (after getting results):
```cpp
result.goalPos.line += lineOffset;
```

## Boundary Inheritance

When creating sub-buffers, boundary flags are inherited from the parent OR set based on subset position:

```cpp
NavBoundary subsetBoundary(subset, subsetFirst, subsetLast,
    subsetStart > 0 || parentBoundary.hasLinesAbove(),   // lines above
    subsetEnd < fullBuffer.lastLine() || parentBoundary.hasLinesBelow());  // lines below
```

## Layer-by-Layer Behavior

### 1. Lua → C++ (FFI Boundary)

**File**: `lua/vimficiency/util.lua` (function `get_search_boundaries`)

```lua
local padding = config.SLICE_PADDING  -- Default: 5 lines
local start_search = math.max(0, begin_row - padding)
local end_search = math.min(nlines-1, end_row + padding)
```

- Adds `SLICE_PADDING` (configurable, default 5) lines above and below the cursor movement region
- Optional paragraph expansion (`SLICE_EXPAND_TO_PARAGRAPH`)
- Positions converted to slice-relative coordinates before FFI call
- Boundary flags (`has_lines_above`, `has_lines_below`) passed to C++

### 2. NavOptimizer

**File**: `src/Optimizer/NavOptimizer/NavOptimizerParams.h`

```cpp
int linePaddingAbove = 2;  // Default for motion searches
int linePaddingBelow = 2;
```

NavOptimizer itself does NOT do internal slicing - it operates on whatever buffer is passed to it. The padding params are available for callers to use when creating sub-buffers.

### 3. TransformOptimizer

**File**: `src/Optimizer/TransformOptimizer/TransformOptimizerParams.h`

```cpp
int navLinePaddingAbove = 1;  // For internal NavOptimizer calls
int navLinePaddingBelow = 1;
```

TransformOptimizer operates on exact edit regions (no padding). However, it internally calls NavOptimizer for the visual delete path (`v + motion + d`), and uses the `navLinePadding*` params for that call.

Lower default (1) because `effectiveLines` already includes prefix/suffix context and we only need a single result.

### 4. CompositionOptimizer → NavOptimizer

**File**: `src/Optimizer/CompositionOptimizer/CompositionOptimizerParams.h`

```cpp
int navLinePaddingAbove = 2;  // For NavOptimizer calls
int navLinePaddingBelow = 2;
```

CompositionOptimizer creates sub-buffers when calling NavOptimizer:

```cpp
int subsetStart = max(0, regionStart - params.navLinePaddingAbove);
int subsetEnd = min(currentLines.lastLine(), regionEnd + params.navLinePaddingBelow);

Lines subset = currentLines.getLineRange(subsetStart, subsetEnd + 1);
int lineOffset = subsetStart;

// Remap positions to subset coordinates
Position subsetPos(pos.line - lineOffset, pos.col, pos.targetCol);

// Call NavOptimizer on subset (CharInterval goal -> LandingNavResult)
LandingNavResult navResult = navOptimizer.optimize(subset, subsetPos, interval, ...);

// Remap each LandingResult's landing back to original coordinates
for (const LandingResult& r : navResult.getResults()) {
  CursorPos goalPos = r.getGoalPos();
  goalPos.line += lineOffset;
  // ... use goalPos in the original buffer's coordinate space
}
```

### 5. CompositionOptimizer → TransformOptimizer

**File**: `src/Optimizer/CompositionOptimizer/CompositionSearchContext.cpp`

```cpp
// No padding - exact regions from DiffState
TransformResult result = transformOptimizer.optimizeTransform(
    diff.deletedLines(),   // Exact lines being deleted
    diff.insertedLines(),  // Exact lines being inserted
    diff.boundary);
```

TransformOptimizer receives exact character-wise regions from the Myers diff, with no additional padding.


Note: Only the line is offset; column stays the same.

## Boundary vs Target Range

When calling `NavOptimizer::optimize` with a `CharInterval` goal, the **boundary** and the **target range** serve different purposes:

- **Target range** (`goalInterval.first`, `goalInterval.last`): Defines which positions count as "in range" for the optimizer's goal check. This is the edit region or insertion point.
- **Boundary** (`NavBoundary`): Defines the navigable extent that clamps motion endpoints like `$`, `0`, `^`. This should be the full subset extent, not the target range.

Using the target range as the boundary is incorrect — it causes `BoundaryContext` to compute `leftColOffset`/`rightColOffset` that clamp motions to the target range edges. For example, `$` would land at the edit region's last column instead of the actual end-of-line.

```cpp
// CORRECT: boundary = full subset extent
Position subsetFirst(0, 0);
Position subsetLast(static_cast<int>(subset.size()) - 1,
    std::max(0, static_cast<int>(subset.back().size()) - 1));
NavBoundary subsetBoundary(subset, subsetFirst, subsetLast, ...);

// WRONG: boundary = target range (would clamp $ to range edge)
// NavBoundary subsetBoundary(subset, localRangeFirst, localRangeLast, ...);
```

This applies to both code paths in CompositionOptimizer: the edit/motion transition path and the pure insertion path.

## Summary Table

| Layer | Slicing | Padding | Params Location |
|-------|---------|---------|-----------------|
| Lua → C++ | Yes | `SLICE_PADDING` = 5 | `lua/vimficiency/config.lua` |
| NavOptimizer | No | N/A | (caller responsibility) |
| TransformOptimizer | No | N/A | (exact regions) |
| TransformOptimizer → NavOptimizer | Yes | `navLinePadding*` = 1 | `TransformOptimizerParams.h` |
| CompositionOptimizer → NavOptimizer | Yes | `navLinePadding*` = 2 | `CompositionOptimizerParams.h` |
| CompositionOptimizer → TransformOptimizer | No | N/A | (exact character-wise) |
