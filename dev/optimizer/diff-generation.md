# Diff Generation & Sequential Application

## Overview

The CompositionOptimizer breaks buffer changes into individual planned edit
regions. The default generator is now `TreeDiff` (`composition:diffAlgorithm=1`);
`composition:diffAlgorithm=0` switches back to the historical character-level
Myers diff. Both methods return `DiffState`s computed against the **original**
buffer, but the diffs must be applied **sequentially** to intermediate buffer
states. This requires adjusting diff positions as earlier diffs shift content.

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

`TreeDiff` is not tree edit distance. It is a flat diff-region planner that uses
a fixed hierarchy as candidate boundaries:

```
Root -> Paragraph -> Line -> BigWord -> Word -> Char
```

It chooses ordered, non-overlapping original-buffer spans:

```
replace initialText[oldBegin:oldEnd]
with    goalText[newBegin:newEnd]
```

The spans are converted to `DiffState` after planning. `Char` leaves keep the
planner exact.

### Tree Shape

The tree is built over `Lines::flatten()`. Each node stores:

- `text`: a half-open flat text range owned by that node
- `children`: a half-open index range into the next level

Construction invariants:

- `Root` has one node.
- Paragraphs split after empty or whitespace-only lines.
- Lines include their terminating newline when present.
- Empty buffers and final newlines create zero-length line nodes.
- Empty buffers also create a zero-length paragraph node.
- Word/BigWord nodes exclude newlines.
- Leading whitespace attaches to the first word unit on a line.
- Inter-word whitespace attaches to the previous word unit.
- Whitespace-only spans do not create Word/BigWord nodes.

Children are ordered but do not always cover all parent text. Newlines are the
common gap. Recursion into a child pair is allowed only when the text outside
the child coverage is identical; otherwise the parent remains a replacement
candidate.

### Cost Model

Every term is in approximate keystrokes, on the same scale as inserted-text
effort, so the planner trades retyping against moving/deleting honestly:

- **Per region:** `diffOpenPenalty` (default `2`, from
  `composition:treeDiffOpenPenalty`) — fixed operator/mode-entry overhead.
- **Insert:** `typed_effort(inserted_text)` via `KeyedSequence` + `RunningEffort`
  on the active `Config`.
- **Deletion** (`deleteCost`): the delete command's keystrokes for the region's
  level, **count-independent** — `x`=1, `dw`=2, `dd`=2, `dW`/`d}`=3. Charged
  once per region that deletes anything (one command), so a fine-level counted
  delete (`4dd`) is not dominated by a coarse one (`dap`).
- **Movement** (`levelCost`): bare motion keystrokes per *kept* child traversed
  *between* edits — `l`/`w`/`j`=1, `W`/`}`=2. Matched runs before the first edit
  and after the last are free (`leadingFree`/`tailFree`); those end "bumpers"
  are partition-neutral. The count-dependent per-child sum is partition-
  equivalent to one counted motion (`4j`), so no gap-extent tracking is needed.

`diffOpenPenalty` is ~2, not 1: at 1 the recurse fragments a nested edit at a
token seam (e.g. `((b))`->`(X)` splits into `(b`->`X` + delete `)` instead of one
`(b)`->`X`); 2 makes the single region win without merging genuinely separate
edits. The deep fix (next section) would let it drop to 1.

### DP Structure

For a pair of parent ranges, `ListDp` compares their child lists at the next
level. `i` and `j` are offsets into those old/new child lists.

`outer(i, j)` is the best cost while no diff is active:

- keep identical child text (charges `levelCost` movement, free on the
  leading/trailing matched run via `leadingFree`/`tailFree`)
- recurse into a paired child node when non-child text matches
- open a diff: `diffOpenPenalty + inner(i, j)`
- a fully-matching remainder short-circuits to 0 (`tailFree`)

`inner(i, j)` chooses one contiguous region, minimizing over the end column
`nextJ` two shapes:

- pure insert (`nextI == i`, only when `nextJ > j`): insert effort + `outer(i, nextJ)`
- delete + insert (`nextI > i`): insert effort + `deleteCost` (flat, one command)
  + the cheapest resume point via `bestOldEnd` (a suffix-min of `outer`)

`inner` stores only the chosen `nextI`/`nextJ`; spans are rebuilt once from the
root plan.

The per-`ListDp` work is `O(a·b²)` after memoizing `solveChildren` (so each
node-pair alignment is solved once) — see `optimizer-architecture.md`. The
earlier formulation re-grid-searched both region endpoints (`O(a²b²)`) and
re-solved subtrees per cost/build/refine pass.

### Range Refinement

After reconstructing a coarse span, the solver asks the next level whether the
same old/new text range has a cheaper child explanation. This handles
tokenization changes:

```
aa aa -> aaaa
old: ["aa ", "aa"]
new: ["aaaa"]
```

The BigWord-level replacement refines to:

```
delete " "
```

### Seam fragmentation and merge cleanup

`Keep` is token-granular, but real matches are character-granular. When an edit
boundary falls *inside* a token — insert after `hello` when the new token is
`hello␣` — the matched part is a char-prefix of that token, so the model recurses
and emits the in-token piece (`" "`) separately from the adjacent token's change
(`world`). One contiguous edit becomes two regions across the recursion seam, and
the model cannot represent the joined region (its only alternatives are the
fragmented recurse or a worse REPLACE that retypes `hello`).

`mergeAdjacentSpans` (run on the final span list) merges regions that abut in
both old and new coordinates, removing the artifact when the fragments are
contiguous: `insert " " + insert "world"` -> `insert " world"`. It cannot merge
fragments with kept content between them (e.g. `((b))`->`(X)` keeping the inner
`)`), so those rely on `diffOpenPenalty` ~2 to stay one region. The fundamental
fix — character-granular matching at region edges (keep a char-prefix/suffix of
the upcoming content, Myers-affix style) — is deferred.

### Current Examples

With default `treeDiffOpenPenalty` and uniform debug config:

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

Use `./build/tests/vimfy_diff_debug <initial> <goal>` for side-by-side Myers
and Tree output. The short approval snapshots for extracted tree structure live
in `tests/Approval/TreeApproval.cpp`.

### Design Intent

The tree is a search scaffold, not an output format. It provides Vim-relevant
boundaries while still returning flat original-buffer diffs. Coarse
line/paragraph replacements should compete with smaller edits, and char-level
refinement should win when cheaper.

### Deferred Work

Not implemented yet:

- character-granular matching at region edges (see Seam fragmentation) — would
  remove the dependence on `diffOpenPenalty` >= 2 for nested-token seams
- open-boundary recursive plans, where a diff can enter or exit a recursive
  child range still open
- the Myers-only behaviors below

### Why Myers is still selectable (diffAlgorithm=0)

Movement and deletion are now priced (see Cost Model), so the old "two missing
axes" gap is closed. What still keeps Myers reachable is structural:

- **Forward-only.** The Myers processing-order flip in
  `CompositionSearchContext.cpp` (gated on `diffAlgorithm == Myers`) reverses
  diff order when the cursor starts nearer the last diff, to save navigation.
  TreeDiff is forward-only.
- **J-plan shapes.** Boundary-crossing `J` plans key off scoped diffs like
  `\nbbb` -> `" bbb"`; TreeDiff's structural partitioning doesn't reliably emit
  those shapes, so some join opportunities don't surface (see
  `composition-optimizer.md` § J Plans).
- **Partition is final.** The generator only picks regions; A* prices the rest
  downstream and never re-partitions, so a poor partition can't be repaired.

Retiring Myers needs processing order made a search choice (or order-agnostic)
and TreeDiff confirmed to subsume the J-plan shapes.

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
