# Previous Error Patterns

This document catalogs recurring error patterns discovered during debugging, to help avoid similar issues in the future.

## 1. Join Command (J/gJ) Misimplementation

**File:** `VimEditUtils.cpp:joinLines()`

**Pattern:** Implementing J based on intuition rather than precise Vim behavior.

**Specific bugs fixed:**

1. **Incorrectly stripping trailing whitespace from first line**
   - Wrong: Strip trailing whitespace before joining
   - Correct: Vim keeps trailing whitespace intact

2. **Always adding a space between joined lines**
   - Wrong: Always insert a space
   - Correct: Only insert space if first line doesn't already end with whitespace

3. **Wrong cursor placement**
   - Wrong: `cursor = joinCol - 1` where `joinCol` was modified after stripping/adding
   - Correct: Cursor = original first line length (position where join occurred)

**Impact:** Cursor ended up 1 position off, causing boundary checks to fail in EditOptimizer. Commands like `dge` were incorrectly explored from positions that should have been detected as suffix region.

**Lesson:** Vim's J command has subtle whitespace handling rules. Always verify against Neovim for:
- Whether existing whitespace is preserved or stripped
- Whether new whitespace is conditionally or unconditionally added
- Exact cursor placement after the operation

---

## 2. Backward Deletion Cursor Placement (db/dB from col 0)

**File:** `VimEditUtils.cpp:deleteRange()`

**Pattern:** Not accounting for special cursor placement rules when backward deletion crosses lines from column 0.

**Specific behavior:**
- When `db` or `dB` is executed from column 0 and deletes across lines (removing the previous line)
- Vim places cursor at the **first non-blank character** of the current line
- Not simply at column 0

**Example:**
```
Before: "abc" / "def" / "   ghi"  cursor at [2,0] (the space)
After db: "abc" / "   ghi"  cursor at [1,3] (the 'g', first non-blank)
```

**Lesson:** Backward deletions that cross lines have special cursor placement rules that differ from same-line deletions. The cursor position depends on:
- Whether cursor was at column 0 before deletion
- Whether a line was removed
- The whitespace structure of the resulting line

---

## 3. Boundary Checking for Character Deletions (X command)

**File:** `EditSearchContext.cpp:exploreCharEdits()`

**Pattern:** Incomplete boundary checking for commands that delete characters before cursor.

**Specific bug:**
- X deletes the character BEFORE cursor
- Original check: `cursor.col > contentStart` (char before cursor is in content)
- Missing: Also need `cursor.col <= contentEnd` (cursor itself is not in suffix)

**Correct check:**
```cpp
if (cursor.col > contentStart && cursor.col <= contentEnd) {
  // X is valid
}
```

**Lesson:** For commands that operate on positions relative to cursor (before, after), verify BOTH:
1. The target position is within bounds
2. The cursor position itself is within the valid exploration region

---

## 4. General Debugging Strategies

### Use SequenceTracer for step-by-step analysis
When optimizer produces wrong results, trace each command individually:
```cpp
auto tracer = makeTracer(buffer, row, col);
tracer.trace("dd");
tracer.trace("db");
tracer.trace("J");
tracer.printSummary();
```

### Compare against Neovim at each step
The oracle can verify individual command behavior:
```cpp
auto r = oracle->simulate(lines, row, col, "J");
// Compare r.row, r.col, r.lines against our simulation
```

### Check boundary math carefully
When boundary checks seem correct but behavior is wrong, verify:
- `leftColOffset` and `rightColOffset` values
- Line length calculations (`lines.getSize()` vs `lines[i].size()`)
- Whether checks use `>=` vs `>` correctly

### Trace the A* path
When unexpected commands appear in results, the issue may be:
- Command explored from wrong state (cursor/buffer mismatch)
- Boundary check not triggering due to off-by-one in cursor position
- Previous command in sequence placed cursor incorrectly

---

## 5. Common Vim Behavior Gotchas

### Whitespace handling varies by command
- `J`: Keeps trailing whitespace, strips leading whitespace from next line
- `gJ`: Keeps all whitespace (no stripping, no adding)
- `dw`: Does NOT cross lines (unlike `w` motion)
- `db` from col 0: Special cursor placement at first non-blank

### Cursor placement after deletions
- Linewise deletions (`dd`): Use targetCol, then reset targetCol
- Characterwise deletions: Usually go to start of deleted range
- Backward cross-line deletions from col 0: First non-blank of current line

### Motion vs operator differences
Some motions behave differently when used with operators:
- `w` motion crosses lines, but `dw` does not
- `e` motion's exclusivity changes with operators
- Text objects may have different ranges for `d` vs `c` vs `y`
