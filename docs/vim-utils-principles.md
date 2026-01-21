# VimCore Design Principles

## 1. Validate State Strictly
When calling our underlying VimCore, we should be using assertions to error on any redundant actions. This is because early pruning is always preferred, so it should be assumed when searching that we will never explore options that are easily checkable (for instance don't search j if we are on the last line). This extends to count validation of deterministic outcomes.

For instance, "j" should never be explored if on the last line, and neither should 3dd on the second to last line.

## 2. Handle Empty Representation
Since we must distinguish one empty line/column, and no columns, we thus must handle emptiness explicitly.

To put it more explicitly:
- `line < lines.size()` EXCEPT when lines.empty(), in which case line=0, col=0.
- `col < lines[line].size()` EXCEPT when `lines[line].empty()`, in which case col=0.

## 3. Minimal API
Single-line operations only need the context of the line.

## 4. Movement vs Endpoint Utils Separation

VimCore has two parallel structs for motion operations:

### VimMovementUtils
- **Mutates** Position in-place (void return)
- Used for **executing** motions during simulation
- Example: `motionWord(pos, lines, forward, edgeType, big)`

### VimEndpointUtils
- **Returns** endpoint Position/Range without mutation
- Used for **predicting** motion results during A* search
- Supports optional boundary parameters for crossing checks
- Example: `motionWordEndpoint(cursor, lines, forward, edgeType, big, skipCurrent, boundary)`

### Why Two Structs?
During A* search, we need to check if a motion would cross edit boundaries *without* actually moving. VimEndpointUtils provides this by:
1. Computing where a motion would land
2. Comparing against boundaries
3. Returning sentinel values (`POSITION_OUTSIDE_BOUNDARY`, `RANGE_OUTSIDE_BOUNDARY`) if crossed

The parallel naming makes the relationship clear:
```
VimMovementUtils::motionWord()       ↔  VimEndpointUtils::motionWordEndpoint()
VimMovementUtils::textObjectRange()  ↔  VimEndpointUtils::textObjectRange()
```

See `boundary-logic.md` for the crossing table model used by VimEndpointUtils.

## 5. targetCol (Sticky Column) Handling

Vim has "sticky column" behavior: when moving vertically through lines of varying length, the cursor remembers the column it "wants" to be at. This is tracked in `Position::targetCol`.

### The Two Column Update Methods

```cpp
// Use for HORIZONTAL movements - resets the sticky column
pos.setCol(c);              // Sets both col and targetCol to c

// Use for VERTICAL movements - preserves the sticky column
pos.clampColPreservingTarget(clampedCol);  // Sets col only, keeps targetCol
```

### When to Use Each

**`setCol()` - Horizontal operations (resets targetCol):**
- Character movements: `h`, `l`, `0`, `$`, `^`, `f`, `t`, `w`, `e`, `b`
- After deletions: `x`, `d{motion}`, `D`
- After linewise deletions: `dd`, `d{linewise}`
- Insert mode entry: `i`, `I`, `a`, `A`
- Line joins: `J`, `gJ`
- Any operation that establishes a new horizontal position

**`clampColPreservingTarget()` - Vertical operations (preserves targetCol):**
- Vertical movements: `j`, `k`
- Line jumps: `gg`, `G`, `{count}G`
- Paragraph/sentence motions that change lines
- Scroll commands: `C-d`, `C-u`, `C-f`, `C-b`

### The `dd` Special Case

`dd` (linewise delete) has unique behavior:
1. **Uses** `targetCol` to compute the cursor position on the new line
2. **Resets** `targetCol` to the clamped column afterward

```cpp
// In deleteRangeLinewise():
pos.line = min(r.startLine, static_cast<int>(lines.size()) - 1);
// dd resets targetCol to the clamped column (unlike k which preserves it)
if (lines[pos.line].empty()) {
  pos.setCol(0);
} else {
  pos.setCol(min(pos.targetCol, static_cast<int>(lines[pos.line].size()) - 1));
}
```

This means:
- `k` then `dd`: Uses targetCol from before `k`, then resets
- `dd` then `dd`: Second `dd` uses the reset targetCol from first `dd`

### Position Constructor Pitfalls

```cpp
Position(line, col)           // 2-arg: sets targetCol = col
Position(line, col, targetCol) // 3-arg: sets targetCol explicitly
```

**Bug pattern**: When creating a Position for vertical movement result, the 2-arg constructor resets targetCol:

```cpp
// WRONG - resets targetCol to newCol
Position newPos(cursor.line - 1, newCol);

// CORRECT - preserves original targetCol
Position newPos(cursor.line - 1, newCol, cursor.targetCol);
```

### Common Bugs

1. **Using 2-arg Position for vertical movements**: Silently resets targetCol
2. **Using `clampColPreservingTarget` for deletions**: Leaves stale targetCol
3. **Forgetting `dd` resets targetCol**: Subsequent `dd` uses wrong column
4. **Using `pos.col` instead of `pos.targetCol` for clamping**: After short line traversal, col != targetCol

### Debugging targetCol Issues

Use `SequenceTracer` in `tests/Debug.cpp` to trace step-by-step:
```cpp
auto tracer = makeTracer({"line1", "longer line 2", "short"}, 1, 10);
tracer.trace("k");   // Check: does col clamp but targetCol stay 10?
tracer.trace("dd");  // Check: does targetCol reset?
tracer.printSummary();
```

Compare with Neovim to identify divergence points.
