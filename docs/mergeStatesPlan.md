# Multi-Source A* State Merging - Exploration Notes

## Problem Statement

When multiple starting positions converge to the same intermediate state `(lines, pos, mode)` with equal cost, the current merge-on-reach approach can cause some positions to find suboptimal paths.

**Example:** Positions 6-10 apply `dd` and converge to the same state. Only position 6 continues to find `dddd`; others find `dededd` or similar.

## Attempted Solution: Pending-Based Merging (Merge on Pop)

### Hypothesis

The issue was that merging happens on **reach** (when pushing to PQ), which is premature. A source might be merged before its optimal path is discovered.

**Key insight:** For paths to the same state S, all paths share the same heuristic h(S). Therefore f = effort + h(S), and lower effort means lower f. When we pop S, we should have seen all paths with effort ≤ current.

**Proposed fix:** Merge on **pop** instead of on **reach**.

### Implementation Attempted

Created `PendingEditOptimizer` with:
- `pending` map: Collects entries when states are pushed
- `finalized` map: Set when state is first popped, contains merged sources
- `goalSuffix`: Records path from state to goal for late-arriving sources

### Results

The pending-based approach found better results in some cases:
- 12 positions found `dddd` (vs 8 with old approach)
- Lower total cost (78 vs 85)

However, critical issues emerged:

1. **goalSuffix IS required**: Sources can arrive at a state AFTER it has been finalized and reached the goal. Without goalSuffix, these sources never get results. Test failures confirmed this.

2. **~28x performance degradation**: Two causes:
   - `recalculateEffort(fullSequence)` called for every source at every exploration step (O(sequence_length) vs O(1) for incremental RunningEffort)
   - Iterating over all merged sources per exploration (50 sources × 10 actions = 500 iterations vs 10)

### Why goalSuffix Is Necessary

```
Position 0:  start → A → B → goal  (short path, cost 5)
Position 14: start → X → Y → B → goal  (longer path, cost 7)
```

1. Position 0 reaches B first, B is finalized with source [0]
2. B explores to goal, records goalSuffix
3. Position 14 reaches B later, B already finalized
4. Without goalSuffix: position 14 never gets result
5. With goalSuffix: position 14 gets result immediately

This happens because A* pops states by f-value, and state B (from position 0's path) can be popped before position 14 even reaches B via its longer path.

### Potential Optimizations (Not Implemented)

1. **Store RunningEffort in PendingEntry**: Would reduce per-exploration cost from O(sequence_length) to O(action_keys)

2. **Use representative source during exploration**: Only recalculate true cost when recording final results

However, even with these optimizations, the fundamental issue of iterating over all merged sources remains.

## Conclusion

**The merge-on-pop optimization is not viable** for this codebase due to:
1. Unavoidable complexity from goalSuffix handling
2. Performance overhead from tracking individual sources through search
3. The current merge-on-reach approach, while imperfect, provides acceptable results with good performance

The original problem (some positions finding suboptimal paths) appears to be an inherent limitation of multi-source A* search with state merging. The cost of perfect merging exceeds its benefits.

## Files Created (Now Removed)

- `src/Optimizer/PendingEditOptimizer.h`
- `src/Optimizer/PendingEditOptimizer.cpp`
- `tests/EditOptimizer/BenchmarkTest.cpp`

## Key Learnings

1. A* search with multiple sources is fundamentally different from single-source A*
2. State merging creates timing dependencies that require careful handling
3. The "merge on pop" insight from the plan is correct in theory, but practical implementation requires goalSuffix handling for late-arriving sources
4. Performance costs can make theoretically better algorithms impractical
