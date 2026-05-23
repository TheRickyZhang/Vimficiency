# Diff Generation & Sequential Application

## Overview

The CompositionOptimizer breaks buffer changes into individual planned edit
regions. The default generator is character-level Myers diff; `TreeDiff` is an
alternate generator selected with `composition:diffAlgorithm=1`. Both methods
return `DiffState`s computed against the **original** buffer, but the diffs must
be applied **sequentially** to intermediate buffer states. This requires
adjusting diff positions as earlier diffs shift content.

## Diff Output Contract

Diff generators return `vector<DiffState>` where each diff has:
- `beginPos`, `endPos`: positions in the **original** buffer (half-open `[begin, end)`)
- `deletedText`, `insertedText`: flattened content with `\n` for newlines

All positions reference the original buffer so `OriginalDiffMapper` can remap
each planned edit into the current intermediate buffer.

## Algorithms

- `diffAlgorithm=0`: `Myers::calculate`, the historical character-level
  shortest-edit script plus local split/merge heuristics.
- `diffAlgorithm=1`: `TreeDiff::calculate`, an experimental structural
  planner. It builds a fixed-depth, lossless hierarchy over the flattened text
  and runs a forward DP to choose flat replacement regions.

## TreeDiff

`TreeDiff` is not generic tree edit distance. Its output is still the same flat
`vector<DiffState>` contract used by composition. The tree exists to give the
planner Vim-shaped candidate boundaries:

```
Root -> Paragraph -> Line -> BigWord -> Word -> Char
```

The planner's job is to choose ordered, non-overlapping original-buffer spans:

```
replace initialText[oldBegin:oldEnd]
with    goalText[newBegin:newEnd]
```

Those spans are converted to `DiffState` only after planning. `Char` leaves make
the planner exact: any changed buffer can still be represented at character
granularity.

### Tree Shape

The tree is built over `Lines::flatten()` and every node stores:

- `text`: a half-open flat text range owned by that node
- `children`: a half-open index range into the next level

Important construction details:

- `Root` always has one node.
- Paragraphs are separated by empty or whitespace-only lines.
- Lines include their terminating newline when one exists.
- A final newline creates a trailing zero-length line node.
- An empty buffer has a zero-length paragraph and line node.
- BigWord/Word nodes exclude newline characters.
- Leading whitespace attaches to the first word unit on a line.
- Inter-word whitespace attaches to the previous word unit.

Children are ordered, but a parent can own text outside the text covered by its
children. The common case is line/paragraph newline text. DP recursion must only
descend through children when the text outside the children is identical between
old and new nodes; otherwise the enclosing node must remain a replacement
candidate.

### Current Cost Model

The prototype intentionally uses only the first two planned cost terms:

```
cost = diffOpenCost * number_of_diffs
     + total_inserted_text_length
```

Current implementation details:

- `diffOpenCost` is `DIFF_COST` in `TreeDiff.cpp` and is currently `8`.
- Inserted text length is counted in flattened characters.
- Deleted text length currently costs `0`.
- Movement between diffs currently costs `0`.

Deleted text length is deliberately not charged yet. It is expected to be
handled together with movement/edit complexity in the later model, because
deleting more text is not a typing payload cost in the same way inserted text
is.

This simplified cost model has visible consequences. For example:

```
aaa bbb ccc -> xxx bbb yyy
```

can choose one larger diff:

```
"aaa bbb ccc" -> "xxx bbb yyy"
```

because one diff plus retyping the unchanged middle may be cheaper than opening
two diffs. Once movement/edit complexity is added, distant edits should have a
reason to split again.

### DP Structure

The DP solves sibling-list ranges recursively. At each level it compares the
old and new children of a pair of parent ranges.

There are two conceptual states:

- `OUT(i, j)`: not currently building a replacement span
- `IN(i, j)`: currently building one contiguous replacement span

`i` and `j` are offsets into the old and new sibling lists.

From `OUT`:

- keep identical child text at zero cost
- recurse into paired child nodes when both the node text differs and the
  non-child text is unchanged
- open a diff and enter `IN`

From `IN`:

- consume an old child only: delete text, currently zero cost
- consume a new child only: insert text, cost is inserted text length
- close the current diff and return to `OUT`

The current implementation does not use an open-boundary recursive model.
Recursive child plans are closed before returning to the parent. This keeps the
prototype simple. If split artifacts across tree boundaries become a real
problem, a future version may return plan variants for closed/open entry and
exit states.

### Range Refinement

Tree tokenization can differ across old and new text:

```
aa aa -> aaaa
```

At the BigWord level this is:

```
old: ["aa ", "aa"]
new: ["aaaa"]
```

There is no one-to-one child pair. When the DP reconstructs a coarse diff over a
changed range, it asks the next level whether that whole range can be explained
more cheaply. In this example the char-level refinement wins:

```
delete " "
```

This range refinement is what preserves exactness and allows the tree to guide
candidate boundaries without forcing coarse replacements whenever tokenization
changes.

### Current Examples

Observed output from `vimficiency_diff_debug` for `TreeDiff`:

```
aa aa -> aaaa
  del " "
```

```
hello -> hello world
  ins " world"
```

```
hello world -> hello
  del " world"
```

```
aaa bbb ccc -> xxx bbb yyy
  "aaa bbb ccc" -> "xxx bbb yyy"
```

```
a -> a\n
  "a" -> "a\n"
```

The final-newline example is a whole-node replacement because newline text is
outside word children; recursing into identical child text would otherwise hide
the newline change.

### Design Intent

The tree is a search scaffold, not the output representation. It should:

- provide Vim-relevant candidate boundaries
- allow coarse line/paragraph replacement to compete with smaller edits
- refine to char-level exactness when cheaper
- avoid arbitrary whole-buffer replacement unless the cost model prefers it

The tree should not become a hard boundary system. Output diffs remain flat text
regions in original-buffer coordinates.

### Deferred Work

The next modeling step is movement/edit complexity:

```
cost += movement_between_diff_regions
cost += deletion/edit complexity for the removed span
```

Until that exists, large replacements can be preferred more often than a human
would expect because deleted text and cursor travel are free.

Open-boundary recursive plans are also deferred. They are a principled way to
let a diff opened in one child continue through adjacent siblings, but they
expand the state space and may make the tree behave more like a raw interval DP.
Do not add them until the simpler closed-plan model has been evaluated.

## The Sequential Application Problem

When applying diffs one at a time to build intermediate states, earlier diffs change the buffer, shifting all subsequent positions. For example:

```
Original: "aaa bbb ccc"  (11 chars)
Diff 0: replace "aaa" -> "xx" at [0,0)..[0,3)     (-1 char)
Diff 1: replace "ccc" -> "dddd" at [0,8)..[0,11)   (+1 char)
```

After applying diff 0, the buffer is `"xx bbb ccc"` (10 chars). Diff 1's original position `[0,8)` now points to the wrong location--it should be `[0,7)` in the intermediate buffer.

## Solution: Flat-Index Adjustment in `calculateLinesAfterDiffs`

Each diff changes the flat text by `insertedText.size() - deletedText.size()` characters. We track a `cumulativeOffset` across diffs. For diff `i`:

1. Convert `beginPos`/`endPos` to flat indices against the **original** buffer (`posToFlat`)
2. Add `cumulativeOffset` (sum of all prior deltas)
3. Convert back to `(line, col)` against the **current intermediate** buffer (`flatToPos`)

This works because generated diffs are non-overlapping original-buffer spans; a
flat character offset exactly captures how much prior diffs shifted subsequent
content.

**Important:** Adjustment must run for all diffs after the first (`i > 0`), not only when `cumulativeOffset != 0`. A diff like `\n` -> ` ` has offset 0 but changes line structure, so flat-to-position conversion produces different `(line, col)` coordinates in the intermediate buffer. See `dev/history/previous_errors.md` § calculateLinesAfterDiffs for the full bug description.

The adjustment is done inline in `calculateLinesAfterDiffs`, which already builds each intermediate `Lines` state. This avoids needing a separate adjustment pass or passing extra parameters.

```
For diff i:
  flatIdx = posToFlat(pos, initialLines) + cumulativeOffset
  adjustedPos = flatToPos(flatIdx, linesAfterNEdits[i])
```

## Performance

The adjustment loop is O(num_diffs x num_lines) -- two linear scans per position per diff. This is negligible compared to the dominant costs in construction:

| Operation | Complexity | Relative Cost |
|-----------|------------|---------------|
| `calculateTransformResults` (A* per edit) | O(diffs x A* nodes x ops) | ~95% |
| diff generation | varies by selected algorithm | ~3% |
| `computeTextObjectContexts` | O(diffs x line_len^2) | ~1% |
| **Position adjustment** | **O(diffs x num_lines)** | **<1%** |

## Downstream Consumers of Adjusted Positions

After adjustment, `diffStates[i].beginPos`/`endPos` are in intermediate-buffer coordinates, matching `linesAfterNEdits[i]`. Two consumers rely on this:

1. **`heuristic()`** -- compares cursor position against `diffStates[i].beginPos`/`endPos` to compute distance to next edit region
2. **`computeTextObjectContexts()`** -- reads `diff.beginPos.line`/`.col` and `diff.endPos.col` to scan for quote/bracket pairs on the correct line in `linesAfterNEdits[i]`

## Code Location

- Adjustment logic: `CompositionSearchContext::calculateLinesAfterDiffs()` in `CompositionSearchContext.cpp`
- Helpers: `posToFlat()`, `flatToPos()` (file-static in same file)
- Myers diff separation heuristics: see `dev/diff-separation-rules.md`
- Tree diff planner: `src/Optimizer/CompositionOptimizer/TreeDiff.cpp`
