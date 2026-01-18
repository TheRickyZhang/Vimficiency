# Edit Boundary Limitations

## Overview

The EditOptimizer operates on `effectiveLines` - a representation of the edit region with boundary characters added. This approach enables correct cursor behavior for most operations but has a fundamental limitation when word motions cross into lines containing prefix/suffix content.

## The Boundary Character Model

When optimizing edits, we only receive:
- `leftChar`: The single character immediately before the edit region
- `rightChar`: The single character immediately after the edit region
- `hasLinesAbove`/`hasLinesBelow`: Whether there are lines outside the edit region

We build `effectiveLines` by prepending/appending these boundary chars:

```
Full buffer:     "prefix_content" + "edit_region" + "suffix_content"
                          ↓                              ↓
                    leftChar='t'                   rightChar='s'
                          ↓                              ↓
effectiveLines:         "t" + "edit_region" + "s"
```

## Why This Works for Most Cases

1. **Cursor clamping**: After deletions, cursor clamps to line bounds. With effectiveLines, line lengths match the simulated context, so cursor positions are consistent.

2. **Word boundary detection**: For operations within the edit region, the boundary char provides enough context to determine if a word motion would "escape" the region.

3. **Single-line embedded regions**: Work perfectly because all positions are on the same line, and we can track column offsets precisely.

## The Multi-Line Cross-Line Deletion Limitation

### Problem Scenario

Consider a 3-line edit region embedded in a buffer:

```
Full buffer:
  Line 0: "eed" + "dbcba"     (prefix="eed", edit content="dbcba")
  Line 1: "cdbe"              (pure edit content)
  Line 2: "ecfb" + "bce"      (edit content="ecfb", suffix="bce")

effectiveLines (what optimizer sees):
  Line 0: "d" + "dbcba"       (leftChar='d', edit content="dbcba")
  Line 1: "cdbe"
  Line 2: "ecfb" + "b"        (edit content="ecfb", rightChar='b')
```

### The Divergence

Consider executing `dgedge` (two backward word deletions) starting from [2,3]:

**First `dge`**: From [2,3], deletes backward to end of "cdbe" at [1,3]. Both effectiveLines and fullBuffer behave identically here - the motion stays within lines that have the same content.

**Second `dge`**: From [1,3], deletes backward to 'a' at end of line 0.
- Both land at 'a' (the word boundary is the same character)
- In effectiveLines: 'a' is at [0, 5]
- In fullBuffer: 'a' is at [0, 7]

After this multi-line deletion merges line 0 and line 1:
- effectiveLines: `{"ddbcb"}` (5 chars) → cursor **clamps to [0, 4]**
- fullBuffer: `{"eeddbcbce"}` (9 chars) → cursor **stays at [0, 7]**

**The divergence is cursor position after the merge**, not the motion endpoint. Subsequent operations (`XX`) delete at different positions because the cursor diverged.

### Why We Can't Fix This Without Full Prefix

The core issue is that **merged line lengths differ** when the prefix has more than one character:

```
effectiveLines line 0: "ddbcba"     (6 chars = 1 boundary + 5 edit)
fullBuffer line 0:     "eeddbcba"   (8 chars = 3 prefix + 5 edit)
```

After merging with content from line 1, the length difference persists, causing cursor clamping to behave differently. The single boundary character cannot represent the full prefix length.

## When This Limitation Manifests

The issue occurs when ALL of these conditions are true:

1. Multi-line edit region (2+ lines)
2. Prefix/suffix is more than 1 character
3. A multi-line deletion merges content with line 0 (prefix) or last line (suffix)
4. Cursor clamping occurs after the merge (resulting line is shorter than cursor position)

## Practical Impact

In stress testing with random content:
- **97%+ sequences work correctly**
- Failures occur specifically when backward word motions like `dge` cross multiple lines and land on line 0 with multi-char prefix

## Potential Future Solutions

1. **Pass full prefix/suffix length to optimizer**: Track actual prefix length (not just boundary char) to compute correct merged line lengths
2. **Pass full prefix/suffix content**: More memory but enables exact simulation
3. **Conservative motion pruning**: Don't generate sequences with multi-line deletions that merge into prefix/suffix lines
4. **Accept the limitation**: Document it and allow small failure rate in edge cases

Currently we accept the limitation with documentation, as:
- The failure rate is low (~3%)
- Most real-world edit regions are single-line or have short prefix/suffix
- The issue only manifests when cursor clamping is needed after merging
