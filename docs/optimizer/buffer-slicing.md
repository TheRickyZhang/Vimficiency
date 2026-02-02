# Buffer Slicing in Optimizers

This document describes how buffer slicing works at each layer of the optimizer stack.

## Overview

Buffer slicing limits the search region to only the relevant portion of the buffer, with optional padding for overshoot scenarios. This improves performance by reducing the search space while still allowing motions that temporarily overshoot the target.

## Padding Rules

| Target | Padding | Rationale |
|--------|---------|-----------|
| MotionOptimizer | Yes (linewise) | Need context for overshoot motions (j, k, w, etc.) |
| EditOptimizer | No | Exact character-wise edit regions from diff |

**Key principle**: Anything calling MotionOptimizer should pad linewise. Anything calling EditOptimizer should NOT pad (exact character-wise regions).

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

### 2. MotionOptimizer

**File**: `src/Optimizer/MotionOptimizer/MotionOptimizerParams.h`

```cpp
int linePaddingAbove = 2;  // Default for motion searches
int linePaddingBelow = 2;
```

MotionOptimizer itself does NOT do internal slicing - it operates on whatever buffer is passed to it. The padding params are available for callers to use when creating sub-buffers.

### 3. EditOptimizer

**File**: `src/Optimizer/EditOptimizer/EditOptimizerParams.h`

```cpp
int motionLinePaddingAbove = 1;  // For internal MotionOptimizer calls
int motionLinePaddingBelow = 1;
```

EditOptimizer operates on exact edit regions (no padding). However, it internally calls MotionOptimizer for the visual delete path (`v + motion + d`), and uses the `motionLinePadding*` params for that call.

Lower default (1) because `effectiveLines` already includes prefix/suffix context.

### 4. CompositionOptimizer → MotionOptimizer

**File**: `src/Optimizer/CompositionOptimizer/CompositionOptimizerParams.h`

```cpp
int motionLinePaddingAbove = 2;  // For MotionOptimizer calls
int motionLinePaddingBelow = 2;
```

CompositionOptimizer creates sub-buffers when calling MotionOptimizer:

```cpp
int subsetStart = max(0, regionStart - params.motionLinePaddingAbove);
int subsetEnd = min(currentLines.lastLine(), regionEnd + params.motionLinePaddingBelow);

Lines subset = currentLines.getLineRange(subsetStart, subsetEnd + 1);
int lineOffset = subsetStart;

// Remap positions to subset coordinates
Position subsetPos(pos.line - lineOffset, pos.col, pos.targetCol);

// Call MotionOptimizer on subset
auto results = motionOptimizer.optimizeToRange(subset, subsetPos, ...);

// Remap results back to original coordinates
for (auto& r : results) {
  r.goalPos.line += lineOffset;
}
```

### 5. CompositionOptimizer → EditOptimizer

**File**: `src/Optimizer/CompositionOptimizer/CompositionSearchContext.cpp`

```cpp
// No padding - exact regions from DiffState
EditResult result = editOptimizer.optimizeEdit(
    diff.deletedLines(),   // Exact lines being deleted
    diff.insertedLines(),  // Exact lines being inserted
    diff.boundary);
```

EditOptimizer receives exact character-wise regions from the Myers diff, with no additional padding.

## Coordinate Remapping

When creating sub-buffers, positions must be remapped:

**To subset coordinates** (before calling optimizer):
```cpp
Position subsetPos(pos.line - lineOffset, pos.col, pos.targetCol);
```

**Back to original coordinates** (after getting results):
```cpp
result.goalPos.line += lineOffset;
```

Note: Only the line is offset; column stays the same.

## Boundary Inheritance

When creating sub-buffers, boundary flags are inherited from the parent OR set based on subset position:

```cpp
MotionBoundary subsetBoundary(subset, subsetFirst, subsetLast,
    subsetStart > 0 || parentBoundary.hasLinesAbove(),   // lines above
    subsetEnd < fullBuffer.lastLine() || parentBoundary.hasLinesBelow());  // lines below
```

## Summary Table

| Layer | Slicing | Padding | Params Location |
|-------|---------|---------|-----------------|
| Lua → C++ | Yes | `SLICE_PADDING` = 5 | `lua/vimficiency/config.lua` |
| MotionOptimizer | No | N/A | (caller responsibility) |
| EditOptimizer | No | N/A | (exact regions) |
| EditOptimizer → MotionOptimizer | Yes | `motionLinePadding*` = 1 | `EditOptimizerParams.h` |
| CompositionOptimizer → MotionOptimizer | Yes | `motionLinePadding*` = 2 | `CompositionOptimizerParams.h` |
| CompositionOptimizer → EditOptimizer | No | N/A | (exact character-wise) |
