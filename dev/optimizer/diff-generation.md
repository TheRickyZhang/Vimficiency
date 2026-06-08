# Diff Generation & Sequential Application

## Overview

The CompositionOptimizer breaks buffer changes into individual planned edit
regions. The default generator is `VimDiff` (`composition:diffAlgorithm=0`): a
character-granular planner that prices regions using Vim-shaped movement,
deletion, insertion, and per-edit costs. `composition:diffAlgorithm=1` switches
to the historical character-level `MyersDiff` fallback. All methods return
`DiffState`s computed against the **original** buffer, but the diffs must be
applied **sequentially** to intermediate buffer states. This requires adjusting
diff positions as earlier diffs shift content.

## Diff Output Contract

Diff generators return `vector<DiffState>` where each diff has:
- `beginPos`, `endPos`: positions in the **original** buffer (half-open `[begin, end)`)
- `deletedText`, `insertedText`: flattened content with `\n` for newlines

All positions reference the original buffer so `OriginalDiffMapper` can remap
each planned edit into the current intermediate buffer.

## Algorithms

- `diffAlgorithm=0`: `VimDiff::calculate`, the default Vim-costed planner. It
  searches over character positions and uses `DiffTree` only as a cost oracle,
  so tree units do not constrain diff boundaries.
- `diffAlgorithm=1`: `MyersDiff::calculate`, the historical character-level
  shortest-edit script plus local split/merge heuristics. It is useful as a fast
  baseline and fallback, but it does not model Vim command cost.

## DiffTree Cost Oracle

`DiffTree` is a fixed hierarchy over flattened text. `VimDiff` uses it to price
movement and deletion commands, not to choose legal diff boundaries:

```
Root -> Paragraph -> Line -> BigWord -> Word -> Char
```

`VimDiff` still emits ordered, non-overlapping original-buffer spans, then
converts them to `DiffState`. Because the search itself is character-granular,
`Char` leaves keep the cost oracle exact without forcing planner boundaries.

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

- **Per region:** `diffOpenPenalty` (default `1`, from
  `composition:diffOpenPenalty`) — fixed operator/mode-entry overhead.
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

`diffOpenPenalty` is ~1 (operator/mode overhead). At this value a nested
level-collapse fragments at the token seam — `((b))`->`(X)` splits into
`(b`->`X` + delete `)` instead of one `(b)`->`X`, and merge can't rejoin them
(kept `)` between). That is a known limitation pending the char-granular fix
(see Seam fragmentation); common bracket edits (`(hello)`->`(X)`,
`((hello))`->`((X))`) keep their single region.

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
`)`); at `diffOpenPenalty` 1 those fragment into two regions — a known
limitation. The fundamental fix — character-granular matching at region edges
(keep a char-prefix/suffix of the upcoming content, Myers-affix style) — is
deferred and would remove it.

### Current Examples

With default `diffOpenPenalty` and uniform debug config:

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

The tree is a cost scaffold, not an output format. It provides Vim-relevant
boundaries for movement and delete pricing while `VimDiff` still returns flat
original-buffer diffs. Coarse line/paragraph commands should compete with
smaller edits, but tree-unit boundaries should not constrain where edits begin
or end.

### Deferred Work

Not implemented yet:

- hard-split decomposition so `VimDiff` work scales with the changed region
  rather than the whole buffer
- moving the Myers-only processing-order flip into composition search as a real
  ordering choice

### Why MyersDiff is non-default (diffAlgorithm=1)

`MyersDiff` is still selectable because it is fast, familiar, and useful as a
baseline. It is not the default because its objective is shortest character edit
script, not cheapest Vim command plan:

- **Kept text is treated as free.** Myers preserves common substrings even when
  moving across them and opening another edit is more expensive than retyping
  them inside one Vim edit region.
- **Region count and command shape are absent.** It does not know about
  per-edit overhead, `dw`/`dd`/text-object deletion, join plans, or typed effort.
- **Split/merge rules are local heuristics.** Thresholds such as
  `MIN_MATCH_LENGTH` approximate a cost model, but they cannot account for
  cursor location, keyboard effort, or downstream transform search.
- **Processing order is an incidental win.** The Myers-only order flip in
  `CompositionSearchContext.cpp` can still save navigation when the cursor
  starts closer to the last diff. That should eventually become a composition
  search choice, not a reason to prefer Myers partitions.
- **Partition is final.** The generator only picks regions; A* prices the rest
  downstream and never re-partitions, so a poor partition can't be repaired.

Retiring `MyersDiff` needs processing order made a search choice (or
order-agnostic) and enough `VimDiff` end-to-end evidence that the fallback no
longer catches meaningful regressions.

## VimDiff (diffAlgorithm=0)

`VimDiff` is the default because it optimizes the thing composition actually
uses: planned edit regions. Real diffs are character-level — no tree partition,
however chosen, can express every intuitive cross-level edit. VimDiff therefore
uses `DiffTree` as a pure **cost oracle** and runs the partition search at
character granularity. The tree still supplies `moveCost`/`delCost`; it no
longer dictates region boundaries.

### Objective

A plan aligns old `A` and new `B` as kept runs interleaved with edits:

```
A = K0 D1 K1 ... Dt Kt        (Di deleted)
B = K0 I1 K1 ... It Kt        (Ii inserted, Ki kept => byte-identical in both)
```

minimizing

```
PENALTY*t + sum delCost(Di) + sum insCost(Ii) + sum moveCost(interior Ki)
```

`insCost` is the real effort model; `delCost` is a boundary-DP tiling of the flat
range into delete commands (below) and `moveCost` is a coarsest-cover tree
approximation — both keystroke oracles read off the tree. Leading `K0` and
trailing `Kt` are free (you start at the first edit, end at the last) — and free
*globally*, by construction, not per-recursive tree region.

### Inner/outer DP

Naively, the edit-start minimization is 2-D (`O(N^6)`). Hume's inner/outer split —
separate the deletion sweep from the insertion sweep — makes each a 1-D min:

```
G[a][b] = ready to START an edit at (a,b)
        = (a==b and A[0:a]==B[0:b]) ? 0 : INF                      # leading free
          OR  min_{t>=1, A[a-t:a]==B[b-t:b]} F[a-t][b-t] + moveCost(a-t, a)
D[i][b] = started, deleted A[a:i)  (a<i)  = min_{a<i} G[a][b] + delCost(a, i)
F[i][j] = just completed an edit ending at (i,j)
        = PENALTY + min( min_b   D[i][b] + insCost(b,j),           # delete + insert
                         min_{b<j} G[i][b] + insCost(b,j) )        # pure insert
answer  = min_{A[i:n]==B[j:m]} F[i][j]                             # trailing free
```

`G` is the matched-gap layer Hume's tree DP doesn't need. Because the search is
character-level there is **no refinement pass** (it is already finest-grained)
and **no merge pass** (`G` requires `t>=1`, so adjacent fragments can't form — a
straddling edit is strictly dominated by the merged one). One DP, and its
objective *is* the emitted partition's cost — no post-hoc merge/refine pass is
needed to reconcile the output to the objective.

### Top-K plans

`CostOptions::maxPlans` (default 1) generalizes the same DP to the K cheapest
*distinct* partitions, ascending by cost. Each cell keeps its `K` best sub-paths
instead of one min; merging predecessor lists + edge cost and truncating to `K`
at every cell yields the global top-K, since costs are additive and nonnegative
(the standard K-best lemma: `K` per cell suffices). Each DP path is a distinct
edit-span set (a bijection — the matched gaps pin every `(a,i,q,j)`), so the
plans need no dedup. Cost is `O(maxPlans)` over the single-plan search with a
sort/truncate per cell; reconstruction walks explicit backpointers rather than
re-deriving by cost equality. `calculate` returns `vector<Plan>` (production
takes `front()`); `calculateBreakdown` returns one breakdown per plan, the
diagnostic surface behind `tests/Approval/VimDiffApprovalTest.cpp`'s
`TopKPlans`.

### Complexity: O(N^3) exact, and no exact O(N^2)

This is the Waterman–Smith–Beyer regime (alignment with a *general* — here 2-D
region + movement — gap cost). The three nested 1-D mins (`G` over the matched
diagonal, `D` over the deletion start, `F` over the insertion start) are each
`O(N)` per cell over `O(N^2)` cells => **`O(N^3)`** with range-costs memoized.

`O(N^2)`-exact would require the cost to satisfy the quadrangle (Monge)
inequality so the sweeps become SMAWK row-minima. It does not.
`moveCost`/`delCost` violate QI extensively; `insCost` satisfies it. The witness
is `(0,1,1,k)`: `w(0,1)+w(1,k) > w(0,k)` — splitting a range mid-unit costs more
than the whole, because enclosing a full unit lets one `}`/`dap` replace the
parts (the *collapse*). Additivity would restore `O(N^2)` via running-min sweeps,
but additive movement charges per-char traversal and can never collapse a
spanned unit — it overcharges exactly the long matched gaps we want to skip
cheaply, so it is a worse model, not a faster equivalent. Verdict: `O(N^3)` exact
is the ceiling for this cost model. (Empirically: `tests/Debug/VimDiffPrototype.cpp`,
`QuadrangleInequality` — ~19k violations for move/delete, 0 for insert.)

`N` is the size of a *change region*. Hard-split (below) keeps it proportional to
the diff, not the buffer.

### Delete-cost oracle (boundary DP)

`delCost(a,i)` prices deleting the flat range `[a,i)` as the cheapest *set* of Vim
delete commands that tile it — a shortest path over a small motion menu, not a
whole-unit cover. From any position `p` the edges are:

- char `x`: `p -> p+1`, cost 1 (so `k` chars cost `k`)
- `dw`/`de`: `p -> next Word start / core-end`, cost 2 — valid mid-word
- `dW`/`dE`: `p -> next BigWord start / core-end`, cost 3
- `{n}dd`/`{n}dap`: a line/paragraph start `-> any later same-level start`, flat
  cost 2/3 (count-independent)

`de`/`dE` target the word *core* end (last non-blank + 1), not the tree span's end
(which includes trailing whitespace and coincides with the next `dw` target), so a
deletion that stops at the word gets its own cheaper edge.

Mid-word `dw`/`de` as first-class edges is the fix for the old whole-unit cover,
which fell through to a flat char cost when no whole unit was contained — pricing
a 5-char cross-word span (`bc xy` = 1) *below* an 11-char whole paragraph
(`dap` = 3), backwards. The tiling keeps whole-unit deletes cheap (a line = one
`dd`, contiguous lines = one `{n}dd`, so it never overcharges `4dd` as `4*dd`)
while pricing partial/cross-word spans honestly (`bc xy` tiles as `dw` + `2x`).
`moveCost` stays the coarsest-cover collapse (a whole paragraph = one `}`).

The buffer is fixed during search, so the full `delCost[a][i]` table is
precomputed once — one forward DAG-shortest-path per start over an
`O(1)`-out-degree graph, `O(N^2)` total — and read `O(1)` by the G/D/F search.

**Count overhead (deferred):** a `{n}` command carries mental cost beyond its
keystrokes. When added it attaches *per command* (edge-local, `f(count)`) so the
shortest path stays additive; a per-tiling-total surcharge would not be
edge-decomposable. First pass omits it — pricing a char run at 1/char already
biases tiling toward word motions over a long `{k}x`.

### Hard-split decomposition (scaling — deferred)

To make work proportional to the diff rather than the buffer, cut at common runs
no optimal edit can straddle. For a maximal common run `R`, keeping it beats
straddling it iff

```
PENALTY + moveCost(R) < insCost(R) + delCost(R)   (+ a one-bigram seam slack)
```

Evaluated *per run* (not as an asymptotic threshold `L*`), this is an
unconditional, loss-free split: it fires only where straddling is provably
dominated. Runs it doesn't fire on are exactly the ones cheap to retype, which
the DP can decide itself. **Correctness never depends on it; only region size
(hence speed) does.** Not in the initial implementation — the composition slice
already bounds region size — but it is the planned scaling layer.

### Validation

`tests/Debug/VimDiffPrototype.cpp` checks the G/D/F DP against a brute-force
partition enumerator under the same oracle: 9 handcrafted + 4000 random small
cases, zero mismatches. The DP is oracle-agnostic, so the delete-oracle
calibration above can change without re-validating the search.
`tests/Unit/DiffPlanner/VimDiffTest.cpp` covers the production planner: the diff
round-trips to the goal, and every K-best plan round-trips, is ascending by cost,
and is distinct.

### Status

Implemented: the exact G/D/F DP, the boundary-DP delete oracle, and K-best plans
(`CostOptions::maxPlans`), wired as the default `diffAlgorithm=0`. Hard-split is
the scaling follow-up (correctness-neutral). The QI verdict means we do **not**
chase an O(N^2)-exact variant for this cost model.

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
- VimDiff planner: `src/Optimizer/DiffPlanner/VimDiff.cpp`
- MyersDiff fallback and separation heuristics: `src/Optimizer/DiffPlanner/MyersDiff.cpp`; see `dev/diff-separation-rules.md`
