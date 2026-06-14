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

## DiffTree Cost Scaffold

`DiffTree` is a fixed hierarchy over flattened text:

```
Root -> Paragraph -> Line -> BigWord -> Word -> Char
```

Production `VimDiff` consumes only its flat `text` plus the Line/Paragraph node
starts (word/bigword runs are re-derived span-locally from `CharMask` inside the
tiling oracle); the per-level `levelCost`/`deleteCost` constants are the
command-cost vocabulary. The Word/BigWord levels are still built for the
prototype validator and tree approval snapshots. `VimDiff` emits ordered,
non-overlapping original-buffer spans, then converts them to `DiffState` — tree
units never constrain diff boundaries.

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
common gap.

### Design Intent

The tree is a cost scaffold, not an output format. It provides Vim-relevant
boundaries for movement and delete pricing while `VimDiff` still returns flat
original-buffer diffs. Coarse line/paragraph commands should compete with
smaller edits, but tree-unit boundaries should not constrain where edits begin
or end.

### Deferred Work

Not implemented yet:

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

`insCost` is the real effort model; `delCost` and `moveCost` are the same
counted-tiling keystroke oracle (below) with different per-level bases —
deletion pays the operator, movement is the bare motion. Leading `K0` and
trailing `Kt` are free (you start at the first edit, end at the last) — and free
*globally*, by construction, not per-recursive tree region. That freeness is
sound for plan *comparison*: every optimal plan pins its first edit at the LCP
boundary and its last at the suffix boundary (starting earlier deletes and
retypes matched text for nothing), so movement to/from the session cursor is a
constant across the partitions the DP weighs.

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
plans need no dedup. Cost is `O(maxPlans)` over the single-plan search via a
bounded insert per candidate (each cell's list stays at most K long;
materializing all predecessors per cell and sorting cost `O(N log N)` per cell
and dominated the planner even at K=1). Reconstruction walks explicit
backpointers rather than re-deriving by cost equality. `calculate` returns
`vector<Plan>` (production takes `front()`); `calculateBreakdown` returns one
breakdown per plan, the diagnostic surface behind
`tests/Approval/VimDiffApprovalTest.cpp`.

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
cheaply, so it is a worse model, not a faster equivalent. The concave count
penalty makes the production tiling oracle subadditive too, so QI stays
violated after the counted rewrite. (Empirically:
`tests/Debug/VimDiffPrototype.cpp`, `QuadrangleInequality` — ~19k violations
for move/delete, 0 for insert; the prototype carries its own surrogate oracle
copy, so it validates the DP shape independent of oracle calibration.)

**`O(N^3)` is the ceiling only for the black-box-oracle formulation.** The QI
argument rules out the generic (SMAWK/concave-gap) sweep speedups on an opaque
cost matrix; it does not forbid exploiting the oracle's internal structure:

- The insert side is *exactly* collapsible today: `RunningEffort` is a monoid
  with a one-key boundary (all metrics bigram-window; pinned by
  `tests/Unit/Misc/EffortDecompositionTest.cpp`), so
  `insCost(q,j) = PS(j) - PS(q) - cut(q)` and `F`'s split-min is a per-row
  running K-best (`solveAll` in VimDiff.cpp) — O(K) per cell, with O(m) prefix
  arrays replacing the old O(m^2) insTable.
- The delete/move sides could be fused the same way (deletion/move-in-progress
  states with O(CAP) chunk transitions), giving O(N^2·CAP·K) exact — but the
  span-local entry clamp needs per-run G-minima machinery, and fused K-best
  paths lose span-set distinctness (tilings of one span duplicate). Designed
  but deliberately not built: with hard-split (below) bounding segments to
  changed neighborhoods, measured planner times are single-digit ms at the
  500-line slice ceiling, so the remaining cubic applies only to a single huge
  contiguous rewrite — which composition's 16-edit/transform limits choke on
  anyway. Revisit only if segment-size measurements change.

`N` is the size of a *change region*. Hard-split (below) keeps it proportional to
the diff, not the buffer.

### Tiled command-cost oracle (delete + movement)

`delCost(a,i)` and `moveCost(a,i)` price the flat range `[a,i)` as the cheapest
*set* of counted Vim commands that tile it (`TilingCost` in `VimDiff.cpp`, one
class, two base tables). An END-anchored DP runs once per start `a`: for each
end `q`, the chunk ending there is the cheapest `{k}` command across levels,
`base + penalty(k)` where `penalty(k) = digits(k) + sqrt(k) - 1` (0 at `k=1`, so
an uncounted command is just `base`; concave, so each extra unit costs less):

| chunk ending at `q`        | delete (`deleteCost`) | move (`levelCost`) |
|----------------------------|-----------------------|--------------------|
| `k` chars                  | `{k}x` = 1            | `{k}l` = 1         |
| `k` alnum/_ runs           | `{k}de`/`dw` = 2      | `{k}e`/`w` = 1     |
| `k` non-blank runs         | `{k}dE`/`dW` = 3      | `{k}E`/`W` = 2     |
| `k` whole lines            | `{k}dd` = 2           | `{k}j` = 1         |
| `k` whole paragraphs       | `{k}dap` = 3          | `{k}}` = 2         |

The chunk shapes transfer to movement exactly because a delete's extent *is* its
motion's landing point (`dw` = `d`+`w`); a movement tiling is a path of motion
landings. Line/paragraph movement chunks approximate `{k}j` between unit starts
— column adjustment within a line is below the oracle's fidelity.

Word/big runs are read from character class (`CharMask`) and are **span-local**:
a run start is clamped to `a`, so a contiguous run counts as one word even
inside a larger global word (deleting `bbbbbbb` out of `abbbbbbba` = one `de` =
2, not 7 chars). A chunk may swallow whitespace on one side at count `k` — `dw`
trailing, or `de` from a leading space — and on both sides at count `k+1`
(`d{k+1}w` from the leading space). The clamp also lets a partial-word delete (a
run prefix) cost `base`; that over-credits vs a real `de`, but keeps the
partitioner from over-pricing mid-word cuts.

Counted tiling keeps whole-unit commands cheap (contiguous lines = one `{n}dd`,
a kept multi-word gap = one `{n}w`) while pricing partial/cross-word spans
honestly (`bc xy` tiles as `5x` ≈ 3.24). Movement priced as counted motions
rather than a per-unit sum is what keeps surgical splits competitive: the split
pays one cheap counted hop across each kept gap instead of a linear traversal,
so short kept gaps no longer get folded into one big retyping REPLACE.

Words/bigwords/chars cap the count scan at `CAP = 9` (single-digit counts;
longer runs chain chunks); lines/paragraphs scan all earlier starts. The buffer
is fixed during search, so both full `[a][i]` tables are precomputed once —
`O(CAP·N²)` each — and read `O(1)` by the G/D/F search.

### Hard-split decomposition (implemented)

Planner work scales with changed neighborhoods, not the slice: `computeSegments`
(VimDiff.cpp) cuts at long kept whole-line blocks no optimal plan straddles,
solves the line-aligned segments independently, and recombines (cross-sum top-K
per segment + plan-independent crossing-move constants). A cut fires only where
keeping the block dominates any straddle by a slack margin:

```
PENALTY + moveUB(R) + SPLIT_SLACK < insCost(R)     (ins <= del + ins)
```

with `moveUB` a valid counted-motion tiling of the block and `SPLIT_SLACK`
bounding what a cut can lose (counted-chunk concavity coupling across each
seam plus the closed-form crossing approximation). Blocks are whole lines whose
aligning newline is strictly inside the matched gap (from the Myers alignment),
so both buffers stay line-aligned at every cut and segment slices keep faithful
line structure; a segment's first line can fake a paragraph start — bounded
oracle wobble within the slack. Leading/trailing kept blocks are cut
unconditionally (free zones). Interior segments charge movement over their
leading/trailing kept runs (`Solver` chargeLeading/chargeTrailing) so the
inter-edit gaps crossing a cut stay priced. Pure insertions at the buffer end
sit exactly at the final piece's end — `diffsWithin` is end-inclusive there,
and a `CHECK` guarantees no diff is ever dropped by segmentation.

`CostOptions::hardSplit=false` is the measurement/testing escape hatch.
Equivalence is pinned by `VimDiffTest.HardSplit*` (split vs unsplit plan-1
cost within the seam slack, identical regions on separated edits) and the
fixed-seed `PlanRegret` corpus. Measured effect: `VimDiffPlan/BufferSize`
40 lines 14.4s → 5ms, 100 lines ≈ 7ms (flat in buffer size);
`CompositionOpt/BufferSizeLarge/500` — the plugin's `MAX_SEARCH_LINES`
ceiling — runs end-to-end in ~70ms where the unsplit solver needed
multi-GB tables.

### Validation

`tests/Debug/VimDiffPrototype.cpp` checks the G/D/F DP against a brute-force
partition enumerator under the same oracle: 9 handcrafted + 4000 random small
cases, zero mismatches. The DP is oracle-agnostic, so the delete-oracle
calibration above can change without re-validating the search.
`tests/Unit/DiffPlanner/VimDiffTest.cpp` covers the production planner: the diff
round-trips to the goal, and every K-best plan round-trips, is ascending by cost,
and is distinct. Cost-model behavior is pinned per case by the markdown approval
fixtures behind `tests/Approval/VimDiffApprovalTest.cpp` (one case per file).
Use `./build/tests/vimfy_diff_debug <initial> <goal>` for side-by-side Myers and
VimDiff output.

Calibration ground truth comes from the plan-regret harness
(`tests/Debug/PlanRegret.cpp`): for each catalog case it takes the planner's
top-K partitions, runs the real `CompositionOptimizer` over each via the
`forcedDiffs` seam, and reports inversions (plan 1 not the real winner), regret,
and rank concordance. K-best plans are filtered for identical-replace regions
(`deleted == inserted`) at reconstruction — those are strictly dominated in the
optimum and rejected by the transform layer, so they are noise for top-K
consumers. `VimDiffPlan/*` in `vimfy_benchmarks` times the planner alone
(ad hoc, not a dashboard suite).

### Status

Implemented: the exact G/D/F DP with the collapsed insert-split min, the shared
counted-tiling oracle for delete and movement (span-local runs,
`digits(k)+sqrt(k)-1` count penalty), K-best plans (`CostOptions::maxPlans`),
and hard-split decomposition — wired as the default `diffAlgorithm=0`. The
remaining cubic applies per segment; the fused O(N^2·CAP) rewrite is designed
(see the complexity section) but parked until segment-size measurements demand
it.

Deliberately not modeled yet: dot-repeat credit for identical repeated regions —
composition itself does not exploit `.` across planned edits yet, so a planner
that predicted dot savings would prefer partitions whose savings downstream
never realizes. Add it downstream-first, then predict it here.

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
