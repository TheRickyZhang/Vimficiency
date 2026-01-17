# Edit Region Strategy: Replace vs Change

## Problem Statement

CompositionOptimizer determines edit regions using Myers diff + thresholding heuristics. Each region assumes we will **type all inserted characters**. This works well for semantic changes:
- `like` → `love` (type "love")
- `a lot` → `!` (type "!")

But misses optimization opportunities for **replacement-heavy** changes:
- `fresh` → `frosh` (only 'e'→'o' differs, could use `fe` then `ro`)
- `0000000` → `1001001` (three `r1` replacements vs typing 7 chars)

## Current Implementation (Updated)

### Myers Diff + Thresholding
1. Myers algorithm finds minimal character-level edit distance
2. Thresholding rules (MIN_MATCH_LENGTH=4, boundary chars) decide when to merge/split regions
3. Each DiffState becomes an independent EditOptimizer problem
4. EditOptimizer provides two strategies:
   - **Type-all**: Delete content, enter insert mode, type all new text
   - **Replacement**: For same-length single-line transformations, use `r{c}` or `R{chars}<Esc>`

### tryReplacement() Function
Implemented in `EditOptimizer.cpp`, handles same-length string replacements:
```cpp
Result tryReplacement(const string& deleted, const string& inserted, const Config& config);
```

**Algorithm**:
1. Reject if lengths differ or content contains newlines
2. Find differing positions between deleted and inserted
3. Group consecutive diffs into runs
4. For each run:
   - Navigate with `{count}l`
   - Single diff: use `r{char}`
   - 2+ consecutive diffs: use `R{chars}<Esc>`

**Examples**:
- `fresh` → `frosh`: `2lro` (navigate to col 2, replace 'e' with 'o')
- `hello` → `jello`: `rj` (replace at col 0)
- `abc` → `xyz`: `Rxyz<Esc>` (R mode for 3 consecutive)
- `0000000` → `1001001`: `r13lr13lr1` (sparse replacements with navigation)

## Design Options

### Option A: Add Replacement to EditOptimizer

Extend EditOptimizer's search space to include replacement operations.

| Pros | Cons |
|------|------|
| Single optimization pass considers all strategies | Breaks "type all inserted text" assumption |
| Naturally finds optimal mix of replace + type | Dramatically increases search space |
| | Need to track which chars are "consumed" by replacements vs typing |
| | State representation becomes complex |

**Complexity**: High - fundamental rearchitecture

### Option B: Pre-analyze at CompositionOptimizer Level

Before calling EditOptimizer, analyze each diff region to decide strategy:
1. Compute character alignment between deleted and inserted text
2. If alignment shows mostly 1:1 matches with few differences → replacement strategy
3. Otherwise → current change strategy

```cpp
enum EditStrategy { CHANGE, REPLACE, HYBRID };

EditStrategy chooseStrategy(const string& deleted, const string& inserted) {
  if (deleted.size() != inserted.size()) return CHANGE;

  int differences = 0;
  for (int i = 0; i < deleted.size(); i++) {
    if (deleted[i] != inserted[i]) differences++;
  }

  double diffRatio = (double)differences / deleted.size();
  if (diffRatio < 0.3) return REPLACE;
  if (diffRatio < 0.6) return HYBRID;
  return CHANGE;
}
```

| Pros | Cons |
|------|------|
| Clean separation of concerns | Pre-commitment to strategy |
| EditOptimizer stays simple | Need to implement replacement path separately |
| Easy to reason about | May miss cases where partial replace + partial type is best |

**Complexity**: Medium

### Option C: Generate Multiple Edit Region Interpretations

For each raw diff, generate multiple DiffState interpretations that CompositionOptimizer explores in parallel:
1. "Change" interpretation: single region, type all inserted text
2. "Replace" interpretation: multiple micro-regions for each differing character
3. Let A* search find optimal combination

**Example**: `fresh` → `frosh`
- Change interpretation: DiffState{delete="fresh", insert="frosh"}
- Replace interpretation: DiffState{delete="e", insert="o", pos=(0,2)}

| Pros | Cons |
|------|------|
| A* naturally finds optimal strategy | Explosion of states for long strings |
| No pre-commitment | May need pruning heuristics |
| Leverages existing MovementOptimizer | More complex DiffState generation |

**Complexity**: Medium-High

### Option D: Hybrid with Length Heuristic

Use simple length-based heuristic:
- If |deleted| == |inserted| and both are short (≤5 chars): explore replacement
- Otherwise: use change strategy

| Pros | Cons |
|------|------|
| Simple to implement | Misses longer replacement opportunities |
| Handles common case | Arbitrary threshold |
| Minimal impact on existing code | |

**Complexity**: Low

## Recommendation

**Start with Option D**, evolve to Option B if needed.

Rationale:
1. Most replacement opportunities are short strings (variable names, small words)
2. Long strings with many differences are better served by full retyping
3. Keeps EditOptimizer simple
4. Can be implemented incrementally

## Replace Mode (`R`) Consideration

For consecutive replacements, `R` mode might be cheaper:
- `000` → `111` is `R111<Esc>` (4 keys) vs `r1lr1lr1` (8 keys)

Decision: Support both `r` (single char) and `R` (replace mode) when replacement strategy is chosen.

## Resolved Questions

1. **Where to compute replacement cost?** → Inline in `tryReplacement()` in EditOptimizer.cpp
2. **How to handle mixed cases?** → CompositionOptimizer can compare replacement vs type-all costs
3. **Threshold tuning?** → Same-length is the only requirement; cost comparison handles the rest

## Implementation Files

Completed:
- `src/Optimizer/EditOptimizer.cpp` - `tryReplacement()` function
- `src/Optimizer/EditOptimizer.h` - `EditResult` struct with `typeAllResults` and `replacementResults`
- `src/Boundary/EditBoundary.h` - Simplified to use raw chars (`leftChar`, `rightChar`)

Future work:
- Expand navigation in replacement to use `f{char}` for potentially cheaper movement
- Integration with CompositionOptimizer to automatically choose best strategy
