# VimUtils Design Principles

## 1. Validate State Strictly
When calling our underlying VimUtils, we should be using assertions to error on any redundant actions. This is because early pruning is always preferred, so it should be assumed when searching that we will never explore options that are easily checkable (for instance don't search j if we are on the last line). This extends to count validation of deterministic outcomes.

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
