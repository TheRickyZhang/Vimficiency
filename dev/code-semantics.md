# Code Semantics

This document is about the semantic meaning carried by names in the codebase, not
the basic project/domain glossary. The goal is to keep names predictable and to
centralize the "hidden baggage" behind common identifiers.

## Core Terms

- **Key**: physical key from the keyboard model
- **Sequence**: Neovim command string
- **KeyedSequence**: command sequence plus the physical keys used to type it
- **Sequence binding**: `KeyedSequence` plus precomputed running effort
- **Effort**: typing difficulty only; independent of search distance

- **Motion**: changes cursor position only
- **Edit**: changes buffer contents, mode, or both
- **`ParsedMotion` / `ParsedEdit`**: parsed command with count semantics, where `count == 0` means "implicit default count of 1"

- **Pos**: geometric position only (`line`, `col`)
- **CursorPos**: `Pos` plus `targetCol` (Vim curswant state)
- **`Line` / `Lines`**: richer text containers, not raw `std::string` / `std::vector<std::string>`

## Range Shapes

### Half-open vs inclusive

- **Begin/End** means a half-open range: `[begin, end)`
- **First/Last** means an inclusive range: `[first, last]` or also `[initial, goal]` for specifics

These should be suffixes

- `beginPos` / `endPos`
- `rangeBegin` / `rangeEnd`
- `beginLine` / `endLine`
- `bufferIndexStart` / `bufferIndexEnd`
- `rangeFirst_` / `rangeLast_`

### Practical rule

- Use `Begin` / `End` for half-open ranges in edit/diff and text-slicing semantics
- Use `First` / `Last` for inclusive motion geometry and landing semantics

Common pattern:

- edit/diff modules produce half-open `[rangeBegin, rangeEnd)`
- motion modules consume inclusive `[rangeFirst, rangeLast]`
- convert once at the module boundary (half-open -> inclusive), not inside motion internals

### Point ranges

A single concrete motion target is represented as a degenerate inclusive interval:

- point target at `goalPos`
- represented as `[goalPos, goalPos]`

This keeps motion semantics uniform for single-goal and range-goal search.

## Position and Coordinate Spaces

### `Pos` vs `CursorPos`

- Use **`Pos`** when only geometry matters
- Use **`CursorPos`** when Vim cursor semantics matter

Use `Pos` for:

- range boundaries (`rangeBegin`/`rangeEnd`, `rangeFirst`/`rangeLast`)
- diff region endpoints (`DiffState::beginPos`/`endPos`)
- indexed landing positions (`BufferIndex` storage and query params)
- any position used only for geometric comparison, storage, or coordinate translation

`CursorPos` is needed only when:

- the position feeds into a `MotionState` or `CompositionState` (search nodes preserve curswant)
- the position is a motion endpoint that may involve `$`, j/k, or scroll commands
- `distanceToGoal` / `distanceToRange` uses `targetCol` for the A* heuristic

### Horizontal vs vertical column updates

- Use `setCol(c)` for horizontal moves that establish a new desired column
- Use `clampColPreservingTarget(c)` for vertical moves that preserve curswant

If a name implies vertical movement, preserving `targetCol` is usually part of its semantics.

## Exclusive CursorPos Semantics

An exclusive range endpoint can have two different meanings:

### 1. Same-line exclusive endpoint

- `CursorPos(line, line.size())` means "one past the end of this line"
- Deletion operations only affect the line content, producing an empty line

### 2. Next-line 0 col endpoint

- This is the next reachable cursor position when moving through the buffer
- `CursorPos(line + 1, 0)` means everything up to but not excluding this point,
  which includes the entire previous line

### Explicit invalid / boundary-crossing sentinel

- If all we care about is if we are outside the allowed region,
  `POSITION_OUTSIDE_BOUNDARY == (-1, -1, -1)` is sufficient

### Local vs global coordinates

- **local**: relative to a sliced subset being searched
- **global**: relative to the larger buffer/index the subset came from
- **buffer coordinates**: relative to the current full buffer state at that layer

Typical naming:

- `localPos`, `localRangeBegin`, `localRangeEnd`
- `globalPos`, `globalRangeFirst`, `globalRangeLast`
- `lineOffset`

### `lineOffset`

`lineOffset` means:

- add it to local line numbers to query the borrowed/global structure
- subtract it from global line numbers to map results back into the local subset


## Goal vs Target vs Boundary

These terms are intentionally different and should not be collapsed.

### Goal

- **`goalPos`** is the concrete cursor position after an action or result executes
- In single-sink search, the goal is one exact position

`goalPos` is post-action state, not "where we want to navigate before doing the command."

### Target range

- **`rangeBegin` / `rangeEnd`** defines which positions count as success
- It is the acceptable sink region for a motion search

The target range is a success condition, not a movement clamp.

For `MotionOptimizer` specifically:

- public target type is inclusive (`CharInterval`, `[first, last]`)
- half-open edit ranges (`CharRange`) must be converted at the composition/edit boundary

### Boundary

- **`MotionBoundary`** defines what the cursor is allowed to traverse within
- Boundary affects motion semantics like `$`, `0`, `^`, paragraph movement, and line availability

Boundary is the navigable envelope. It is often strictly larger than the target range.

### Important distinction

When slicing:

- target range = where success is allowed
- boundary = the full sliced search window

Using the target range as the boundary is usually a semantic bug.

## Direction Vocabulary

### Absolute direction

- **Forward / Backward** refers to absolute buffer direction
- Template parameter `Forward` should always mean "toward larger positions" when true, and smaller when false

### Relative direction

- **ahead / behind**
- **before / after**
- **undershoot / overshoot**

These are relative to a current query target, not absolute buffer direction.

### Direction classes

The motion code uses several different direction notions at once:

- **Left / Right**: horizontal only
- **Up / Down**: vertical only
- **Forward-crossing / Backward-crossing**: moves that may cross line boundaries

Use the narrowest directional term that matches the semantics.

## State Progress Naming

### Temporal names

- **initial**: starting user-visible state
- **current**: state at the current search step
- **next**: immediately following candidate or next diff/edit
- **goal**: desired post-operation state

These should reflect time/order, not geometry.

Examples:

- `initialLines`, `initialPos`
- `currentLines`
- `nextEdit`
- `goalLines`, `goalPos`

### Progress counters

- **`editsCompleted`** means completed transitions, not "index of current edit"
- `linesAfterNEdits_[n]` means the buffer state after exactly `n` edits

Fencepost rule:

- arrays keyed by "after N steps" should usually have size `total + 1`

## Buffer Slices and Windows

These names carry specific expectations:

- **subset**: sliced `Lines` passed into a lower-layer optimizer
- **window**: a search extent chosen for efficiency, often before materializing a `Lines` subset
- **padding**: extra context outside the immediate target span
- **indexed range**: the line interval covered by a precomputed helper like `BufferIndex`

Use:

- `beginLine` / `endLine` for half-open slice bounds
- `subsetFirst` / `subsetEnd` only for actual cursor positions inside the subset

## Cost Vocabulary

These names are not interchangeable.

- **effort**: typing cost only
- **distance**: positional or heuristic closeness measure
- **heuristic**: estimated remaining search work
- **cost**: total priority currently assigned to a state
- **penalty**: targeted adjustment applied to part of the cost model

Practical rule:

- if a value mixes effort with heuristic distance, call it `cost`
- if it is "future-only estimate," call it `heuristic`
- if it is attached to one rule (count prefixes, overshoot), call it `penalty`

## Search Accounting

These names describe different parts of search bookkeeping and should stay distinct.

- **`nodesProcessed`**: non-stale states actually processed
- **`totalPops`**: all priority-queue pops, including stale states
- **`statesSkipped`**: popped states discarded without full processing
- **`resultsFound`**: returned results
- **`uniquePositionsFound`**: distinct landing positions, not distinct sequences

Practical rule:

- use `processed` for work actually explored
- use `pops` for queue budgeting
- use `skipped` for bookkeeping discards
- use `unique` only when de-duplication by endpoint matters

## Count Semantics

Count names carry distinct meanings depending on layer.

- **parsed count**: `0` means implicit default, positive means explicit prefix
- **prefix count**: an emitted numeric prefix in a generated command
- **repeat count**: how many times an action/search branch is explored or conceptually repeated

Rules:

- Do not treat parsed `count == 0` as an actual zero repetition
- Generated count-prefixed motions/edits should generally only emit when count is meaningfully greater than the unprefixed form
- `minPrefixCount` / `maxPrefixCount` constrain emitted count-prefixed search candidates, not all command semantics globally
- `minPrefixCount` should normally be at least `2`, so emitted prefixes avoid the redundant count-1 form
- `minPrefixCount > maxPrefixCount` explicitly disables count-prefixed exploration

## Validity and Failure Terms

- **invalid position**: usually a sentinel like `(-1, -1)` or `!isValid()`
- **outside boundary**: operation would cross the allowed navigable envelope
- **stale state**: queue entry superseded by a lower-cost equivalent state
- **no-op**: command is semantically valid but leaves state unchanged

These mean different things:

- invalid = malformed or sentinel
- outside boundary = blocked by search constraints
- stale = search bookkeeping discard
- no-op = real Vim behavior with no state change

## Type Suffixes With Semantic Weight

These suffixes are already used as meaningfully different categories.

- **`*Params`**: caller-controlled tuning knobs
- **`*Context`**: shared precomputed environment/state used by a search or subsystem
- **`*State`**: one search node or mutable machine state
- **`*Result`**: externally consumable output
- **`*Plan`**: precomputed executable strategy that may compete with other strategies
- **`*Boundary`**: navigational constraints, not targets
- **`*Flags`**: compact semantic capability masks

If a type is named `*Context`, it should usually:

- outlive many helper calls
- bundle reused derived data
- avoid pretending to be an immutable value object

If a type is named `*Result`, it should usually:

- represent what the caller receives
- not require the caller to understand hidden intermediate coordinate spaces

## Boolean Prefixes

Boolean names already carry consistent shades of meaning.

- **`is*`**: classification of current state (`isGoal`, `isPureInsertion`)
- **`has*`**: presence of data or reachable context (`hasLinesAbove`, `hasDeletedContent`)
- **`can*`**: capability or legality if attempted
- **`should*`**: policy decision or control-flow gate (`shouldContinue`)
- **`use*` / `allow*` / `track*`**: caller-configurable behavior toggles in params

Prefer the prefix that matches the semantic source of truth:

- state fact -> `is*`
- available resource/context -> `has*`
- choice/setting -> `use*`, `allow*`, `track*`

## Semantic Traps Worth Calling Out

- `endPos` in diffs is exclusive and may be a virtual one-past position
- `goalPos` is often a concrete cursor landing position, even when the search target was a range
- pure insertions can have many valid entry positions but one final `goalPos`
- `rangeEnd` may not itself be a valid landing position, because it is the exclusive bound
- `totalPops` is broader than processed nodes; it includes stale queue pops

## Naming Defaults

When no stronger domain term exists, prefer:

- `initial` / `current` / `next` / `goal` for temporal flow
- `begin` / `end` for stored/search ranges
- `first` / `last` for inclusive derived geometry (also uses: `initial` / `goal`)
- `local` / `global` for coordinate-space conversion
- `subset` for a materialized sliced buffer
- `offset` only when it is an additive tra
