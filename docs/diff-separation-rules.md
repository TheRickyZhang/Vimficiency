# Diff Separation Rules

The Myers diff algorithm in `DiffState.cpp` uses heuristic rules to decide when to merge adjacent changes vs keep them as separate diffs. This document explains the rules and their rationale.

## Overview

When the algorithm finds a sequence of matching characters between two change operations, it must decide:
- **Preserve**: Finalize the current diff, keep the match as a boundary
- **Absorb**: Include the matching chars in the diff (merge changes together)

## Rule Hierarchy

### MIN_MATCH_LENGTH Threshold (≥4 chars)

Matches of 4+ characters are always preserved. This is the primary separator.

### Pure Newlines Rule (checked before other short-match rules)

**Condition:** Match consists ONLY of newline characters

**Action:** Preserve (finalize current diff)

**Rationale:** Pure newlines (`\n`, `\n\n`) are structural boundaries that separate logical content. They should always be preserved as diff boundaries.

### Rule 0: Cross-Line Weak Content

**Condition:** Match contains newline AND has <3 non-whitespace chars AND not at end AND is NOT pure newlines

**Action:** Absorb (merge)

**Rationale:** Matches like `"\n   "` (newline + indent) are often coincidental - both files happen to have similar indentation.

**Examples:**
| Match | Pure Newlines? | Action |
|-------|----------------|--------|
| `"\n"` | Yes | **Preserve** (pure newlines rule) |
| `"\n\n\n"` | Yes | **Preserve** (pure newlines rule) |
| `"\n  "` | No (has spaces) | Absorb (Rule 0) |
| `"\n  x"` | No (has 'x') | Absorb (Rule 0) |

### Rule 1: End with Boundary

**Condition:** At end of text AND match contains word boundary chars

**Action:** Preserve

**Rationale:** Trailing punctuation like `)` or `]` should not be absorbed.

### Rule 2: Pure Insert/Delete at End

**Condition:** At end AND diff is pure insertion or pure deletion

**Action:** Preserve

**Rationale:** Keep minimal diffs when possible.

### Rule 3: Boundary-Wrapped Content

**Condition:** Match has boundaries AND starts with boundary AND ends with boundary AND has non-boundary content

**Action:** Preserve

**Rationale:** Matches like `" b "` (space-word-space) are semantically meaningful even if short.

### Rule 4: Small Diff at End

**Condition:** At end AND diff size ≤ match size

**Action:** Preserve

**Rationale:** Don't absorb a long match just to extend a tiny diff.

### Default

If no rule matches, absorb the short match.

## Behavioral Impact

### Single Newline Between Changes

```
"a\nc" → "ab\n\nc"
```

- **Before (absorb):** 1 replacement: `"\nc"` → `"b\n\nc"`
- **After (preserve):** 2 insertions: `""→"b"`, `""→"\n"`

### Multiple Independent Line Changes

```
"a\nb\nc" → "x\ny\nz"
```

- **Before:** 1 big replacement
- **After:** 3 separate replacements (one per line)

The "more diffs" result is actually more accurate - each line change is independent and can be optimized separately.

## Post-Processing: Adjacent Insertion Merging

At the end of `Myers::calculate`, a merge pass combines certain adjacent pure insertions.

### Why post-processing instead of modifying the algorithm?

The split comes from the LCS alignment itself: Myers matches the original `\n` with the
first goal `\n`, which is a valid optimal alignment. The separation heuristics make **local**
decisions ("should I preserve this match?") without seeing adjacent diffs. The merge pass
has **global** knowledge — it sees both diffs and can recombine them.

### Why only pure insertions?

For deletions/replacements, merging across a line boundary requires absorbing the matched
`\n` into both the deleted and inserted text (since the newline is present in both original
and goal). This converts two pure deletions into a replacement, changing the diff type and
losing optimization opportunities (e.g., `x` vs `dd` become unavailable). Pure insertions
have empty deleted text, so concatenation preserves the type.

### Merge Conditions

1. Both diffs are pure insertions
2. First insertion doesn't contain newlines (single-line content)
3. Second insertion is exactly `"\n"`
4. First is at end of its line, second is at start of next line

Condition 3 is tight by design: `"\n\n"` or `"\nfoo"` as a separate insertion at (L+1,0)
would require Myers to split an insertion across a line boundary mid-content, which doesn't
arise from the LCS alignment. The pattern that occurs is always: content at end of line +
single newline at start of next.

### Example

```
"a\nc" → "ab\n\nc"
```

Without merge: 2 diffs - `""→"b"` at (0,1), `""→"\n"` at (1,0)
With merge: 1 diff - `""→"b\n"` at (0,1)

**Optimizer handling:** For merged insertions ending with newline at end of line, the
optimizer uses `A` + content + `<CR>` + `<Esc>` (e.g., `Ab<CR><Esc>`).

## Code Location

`src/Optimizer/CompositionOptimizer/DiffState.cpp`:
- Pure newlines rule: lines 309-312
- Rule 0 (cross-line weak content): lines 273-302
- Rules 1-4 (short match handling): lines 314-371
- Adjacent insertion merge: end of `calculate()`
