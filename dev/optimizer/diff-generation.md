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
  searches over character positions of the flattened buffers; command
  boundaries (words, lines, paragraphs) only price spans, they never constrain
  where an edit begins or ends.
- `diffAlgorithm=1`: `MyersDiff::calculate`, the historical character-level
  shortest-edit script plus local split/merge heuristics. It is useful as a fast
  baseline and fallback, but it does not model Vim command cost.

## Flat text and unit boundaries

`VimDiff` works on `FlatText`: `Lines::flatten()` plus the line and paragraph
start positions (each list terminated by the text length, so unit `i` is
`[starts[i], starts[i+1])`). A paragraph ends after a blank (empty or
whitespace-only) line. Word/bigword runs are read from `CharMask` inside the
tiling oracle. That is the entire structural input; the per-level keystroke
constants (`MOVE_KEYS`/`DELETE_KEYS`: `l`/`x`, `w`/`dw`, `W`/`dW`, `j`/`dd`,
`}`/`dap`) are the command-cost vocabulary. `VimDiff` emits ordered,
non-overlapping original-buffer spans, then converts them to `DiffState`.

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
uses: planned edit regions. Real diffs are character-level — no fixed
word/line/paragraph partition can express every intuitive cross-level edit.
VimDiff therefore runs the partition search at character granularity and uses
command boundaries only inside the cost oracle (`moveCost`/`delCost`).

### Coordinates

- `N` / `M`: raw sizes of the initial / goal text; `ri` / `rj`: raw positions.
- `n` / `m`: pruned unit counts after matched-run collapse (below); `i` / `j`:
  pruned indices. `PositionMap` maps `i -> initialRaw(i)` and `j -> goalRaw(j)`.
- A DP step lands in cell `(i,j)` from its predecessor `(pi,pj)`; a successor
  is `(ni,nj)`. Spans are half-open `[begin,end)`.
- `k` is always a command count (`{k}dd`); the K-best bound is `maxPlans`.
- `entry` is the effort of the insert-entry keystroke (`i`), named to keep `i`
  for the index.

### Objective

A plan aligns the initial and goal text as kept runs interleaved with edits:

```
initial = K0 D1 K1 ... Dt Kt        (Di deleted)
goal    = K0 I1 K1 ... It Kt        (Ii inserted, Ki kept => byte-identical in both)
```

minimizing

```
sum delCost(Di) + sum insCost(Ii) + sum moveCost(interior Ki)
```

`insCost` is the real effort model plus `entry` + `<Esc>` whenever a region
types anything — entering and leaving insert mode, charged the same whether or
not the region deleted first. (A real delete+insert would use the change form
and save the entry key; the planner deliberately does not model that, so a
replace is overpriced by one key relative to a pure delete or insert. There is
no per-region penalty beyond these keys.) `delCost` and `moveCost` are the same
counted-tiling keystroke oracle (below) with different per-level bases —
deletion pays the operator, movement is the bare motion. Leading `K0` and
trailing `Kt` are free (you start at the first edit, end at the last) — and free
*globally*, by construction. That freeness is sound for plan *comparison*:
every optimal plan pins its first edit at the LCP boundary and its last at the
suffix boundary (starting earlier deletes and retypes matched text for
nothing), so movement to/from the session cursor is a constant across the
partitions the DP weighs.

### Out/in DP

Every cell `(i,j)` of the initial×goal grid has two tables: `out[i][j]` —
normal mode, initial `[0,i)` consumed and goal `[0,j)` produced — and
`in[i][j]` — insert mode, same coordinates. The steps are the keystrokes, each
written with the predecessor cell it pulls from:

```
move    out[pi][pj] -> out[i][j]   i-pi == j-pj >= 1, units (pi,i] match:  moveCost(pi,i)
delete  out[pi][j]  -> out[i][j]   delete initial [pi,i):                  delCost(pi,i)
enter   out[i][pj]  -> in[i][j]    pj = j-1, enter insert + type unit pj:  entry + <Esc> - cut(pj) + typed(pj)
type    in[i][pj]   -> in[i][j]    pj = j-1, type goal unit pj:            typed(pj)
exit    in[i][j]    -> out[i][j]   leave insert:                           free
out[i][j] = 0 on the diagonal within the common prefix                    # leading free
answer    = min over out[i][j] with initial[ri:] == goal[rj:]              # trailing free
```

Typing is per-unit additive because `RunningEffort` is a monoid with a one-key
boundary: `PS` is the prefix effort of typing all of the goal text and `cut`
the bigram straddling a raw goal position, so typing raw `[begin,end)` costs
`PS(end) - PS(begin) - cut(begin)`; the `cut` is paid once, on entry. Regions
are not a DP concept: a region is a maximal stretch of delete/type steps
between two moves, read off the winning path afterwards (`Solver::walk`).
Since nothing is charged per region, the state needs no memory of where one
began — that is what makes two tables enough (charging `<Esc>` only when
something was typed, or `entry` only when nothing was deleted, would need a
third).

Deletion is not one step per cell. `delCost` is a tiling DP over the initial
text (below), and a tiling DP is multi-source for free: seed every initial
position with that column's `out` value and one left-to-right sweep over raw
initial positions yields every deletion arrival in the column at once — the
deletion is *extended* one chunk at a time, never re-priced from its start.
`Solver::sweepDeletes` runs `TilingCost::sweep` once per goal column with the
K-best lists as the propagated value, so the solver never scans deletion starts
and never prices a span from scratch. Columns are filled left to right: `in`
comes from the previous column, `out` from earlier columns (moves) and its own
(exits, then the sweep).

### Top-K plans

`CostOptions::maxPlans` (default 1) generalizes the same DP to the `maxPlans` cheapest
*distinct* partitions, ascending by cost. Each cell keeps its `maxPlans` best
candidates instead of one min; merging predecessor lists + edge cost and
truncating to `maxPlans` at every cell yields the global top-K, since costs are
additive and nonnegative (the standard K-best lemma: `maxPlans` per cell suffices).
Many paths encode one partition (different tilings of a deleted span, delete
and type steps interleaved, a move split in two), so a candidate carries a
partition key — a hash of the cells its regions opened and closed at — and a
cell keeps one candidate per key, the cheaper. Equal keys have identical
futures, so this loses nothing. The final top-K additionally compares region
lists, which also folds the one key-level artifact (a region closed by a move
into the trailing run vs. left open at its end). Cost is `O(maxPlans)` over the
single-plan search via a bounded insert per candidate. Reconstruction walks
predecessors by cell + key. `calculate` returns `vector<Plan>` (production
takes `front()`); `calculateBreakdown` returns one breakdown per plan, the
diagnostic surface behind `tests/Approval/VimDiffApprovalTest.cpp`.

### Complexity

Cells are `O(n·m)`, pruned. Per cell, `type`/`enter`/`exit` are a bounded
insert per candidate (`O(maxPlans)` candidates), and `move` pulls along the
matched diagonal — `O(run length)` in pruned units, at most
`2·MATCH_MARGIN + 1` for a collapsed run. Deletion is not per cell: one sweep
per goal column walks the raw initial text, `O(N·CAP)` chunk transitions plus
the earlier-line-start scan for `{k}dd`/`{k}dap`. So the planner is
`~O(N·m + n·m)` at the default `maxPlans = 1`: cells are diff-bound, but every
goal column still pays a raw walk — see "Exact vs diff-bound" for why an `O(1)`
cross-run oracle (which would make it `O(n·m)`) is unachieved.

Why a sweep and not a per-cell running min: `moveCost`/`delCost` violate the
quadrangle (Monge) inequality extensively (`insCost` satisfies it), so the
generic SMAWK/concave-gap speedups for a black-box span oracle do not apply.
The witness is `(0,1,1,k)`: `w(0,1)+w(1,k) > w(0,k)` — splitting a range
mid-unit costs more than the whole, because enclosing a full unit lets one
`}`/`dap` replace the parts (the *collapse*). Additivity would restore a
running-min sweep, but additive movement charges per-char traversal and can
never collapse a spanned unit — it overcharges exactly the long matched gaps we
want to skip cheaply, so it is a worse model, not a faster equivalent. The
concave count penalty (`dev/optimizer/count-penalty.md`) keeps the oracle
subadditive too. (Measured on the earlier tree-scaffold prototype: ~19k QI
violations for move/delete, 0 for insert.)

The QI argument rules out speedups on an opaque cost matrix; it does not forbid
exploiting the oracle's internal structure, which is what the solver does:

- The insert side is *exactly* per-unit additive: `RunningEffort` is a monoid
  with a one-key boundary (all metrics bigram-window; pinned by
  `tests/Unit/Misc/EffortDecompositionTest.cpp`), so `in` extends by
  `PS(goalRaw(j)) - PS(goalRaw(pj))` per unit with the boundary `cut` paid once
  on entry.
- The delete side is fused into the tiling: the deletion-in-progress value is
  the tiling DP's own reach value, so one multi-source sweep per goal column
  prices every deletion arrival. The span-local entry clamp is handled by
  per-run source minima within the sweep; K-best distinctness is the partition
  key. Measured on `VimDiffPlan/*` (debug build): 8–14× over the old per-cell
  start scan with memoized span queries.
- The move side still prices each matched run with a single-source `query`
  (memoized); runs are few and short in pruned units, so it is not the
  bottleneck. Fusing it is the same sweep along the matched diagonal.

### Tiled command-cost oracle (delete + movement)

`delCost(begin,end)` and `moveCost(begin,end)` price the raw initial span `[begin,end)` as the cheapest
*set* of counted Vim commands that tile it (`TilingCost` in `VimDiff.cpp`, one
class, two base tables). An end-anchored DP runs from `begin`: for each
end `ri`, the chunk ending there is the cheapest `{k}` command across levels,
`base + penalty(k)` where `penalty(k)` is the digit keystrokes plus the shared
cognitive count penalty for the level's class (`CountPenalty.h`, honouring
runtime overrides), 0 at `k=1` so an uncounted command is just `base`:

| chunk ending at `ri`       | delete (`deleteCost`) | move (`levelCost`) |
|----------------------------|-----------------------|--------------------|
| `k` chars                  | `{k}x` = 1            | `{k}l` = 1         |
| `k` alnum/_ runs           | `{k}de`/`dw` = 2      | `{k}e`/`w` = 1     |
| `k` non-blank runs         | `{k}dE`/`dW` = 3      | `{k}E`/`W` = 2     |
| `k` whole lines            | `{k}dd` = 2           | `{k}j` = 1         |
| `k` whole paragraphs       | `{k}dap` = 3          | `{k}}` = 2         |
| rest of the line           | `D` = 2               | `$` = 2            |
| line start to here         | `d0` = 2              | —                  |

The chunk shapes transfer to movement exactly because a delete's extent *is* its
motion's landing point (`dw` = `d`+`w`); a movement tiling is a path of motion
landings. Deletion line/paragraph chunks start on a unit start (`dd` takes whole
lines); movement chunks start anywhere in their first unit, since `{k}j` works
from any column — the column adjustment on landing is below the oracle's
fidelity. The to-boundary rows are what make a partial line cheap: without
them, cutting a `{k}dd` mid-line re-tiles the partial lines word by word, which
both overprices surgical edits (measured as a systematic preference for fewer,
larger regions in `PlanRegret`) and makes the cost of cutting a command grow
with line length instead of staying bounded.

Word/big runs are read from character class (`CharMask`) and are **span-local**:
a run start is clamped to `begin`, so a contiguous run counts as one word even
inside a larger global word (deleting `bbbbbbb` out of `abbbbbbba` = one `de` =
2, not 7 chars). A chunk may swallow whitespace on one side at count `k` — `dw`
trailing, or `de` from a leading space — and on both sides at count `k+1`
(`d{k+1}w` from the leading space). The clamp also lets a partial-word delete (a
run prefix) cost `base`; that over-credits vs a real `de`, but keeps the
partitioner from over-pricing mid-word cuts.

Counted tiling keeps whole-unit commands cheap (contiguous lines = one `{k}dd`,
a kept multi-word gap = one `{k}w`) while pricing partial/cross-word spans
honestly (`bc xy` tiles as `5x` ≈ 3.24). Movement priced as counted motions
rather than a per-unit sum is what keeps surgical splits competitive: the split
pays one cheap counted hop across each kept gap instead of a linear traversal,
so short kept gaps no longer get folded into one big retyping REPLACE.

Words/bigwords/chars cap the count scan at `CAP = 9` (single-digit counts;
longer runs chain chunks); lines/paragraphs scan all earlier starts. The tiling
is one left-to-right sweep (`TilingCost::sweep`) that is multi-source by
construction: any position may seed a "start here" value and a chunk ending at
`ri` relaxes from the value reached at its start. `query(begin,end)` is the
single-source scalar instance (movement pricing, diagnostics); the solver seeds
it with a whole `out` column (see Out/in DP).

### Coordinate collapse + raw-span oracle (implemented)

`PositionMap` (VimDiff.cpp) shrinks the K-best DP by collapsing the interior of
matched runs the optimum provably keeps — the Myers gaps where
`type(run) > move(run) + entry + <Esc>` — into single units, while keeping
`MATCH_MARGIN` char-level cells at each run edge. The margin is load-bearing: an
optimal edit boundary can slide a few chars into a kept run for an alignment
saving (inserting `a` before `ad` attaches at col 0 or 1 at *different* cost),
but the slide is bounded — sliding `k` chars costs ~`k` to retype against a
sub-linear navigation gain — so a small char margin keeps every cost-optimal
boundary representable. This is what makes `n`,`m` diff-sized rather than `N`,`M`.

The oracle stays **exact** by pricing `delCost`/`moveCost` over the *raw* span
(`TilingCost::query`, memoized): a counted command tiles across a collapsed run's
interior using the real characters, so a collapsed run and full char-level
coordinates give identical costs. Verified against the char-level baseline by
`tests/Debug/SparseVsDense.cpp` (collapse on vs off; 11.5k adversarial
small-alphabet + mutated cases; zero cost mismatches).
`CostOptions::collapseRuns=false` is the exact char-level escape (used by
`calculateBreakdown` and the A/B reference side).

### Exact vs diff-bound: a real tension (the oracle is O(span), not O(1))

Collapse makes the DP *cells* `O(n·m)`, but the tiling still walks raw initial
positions — one `O(N)` sweep per goal column for deletion, an `O(span)` walk
per matched run for movement — so runtime is `~O(N·m)`, not `O(n·m)`.
Making
the oracle O(1) (a pruned-coordinate table pricing counted commands across a
collapsed block from precomputed line/char counts) was tried three ways —
char-margin blocks, whole-line-core blocks with a severing table, and
whole-line-core blocks with a cross-block table — and **every reduced oracle is
inexact** (misprices by ≤ ~3 keystrokes on ~7–13% of mutated cases, measured by
SparseVsDense). The cause is structural: an exact tiling of a span that crosses a
kept run can place counted-command boundaries on positions *inside* the run
(e.g. `{k}dd` over the run's line starts), and exactness also needs the
char-level margin cells for the boundary slide — a reduced O(1) oracle that drops
interior positions loses some of those tilings.

> Exact pricing forces an O(span) oracle, which is incompatible with `O(n·m)`
> runtime. Both cannot hold for this cost model. This is not a proven theorem,
> but it is unachieved after the variants above and the obstacle is concrete.

We choose **exact**: composition consumes `plans.front()`, so a wrong plan-1 cost
mis-ranks the partition. Collapse still buys a large constant-factor win over the
char-level baseline (`VimDiffPlan/BufferSize/100` ≈168s → ≈8s debug) by removing
most DP cells; only the oracle walk keeps it from being asymptotically
diff-bound. Composition runs VimDiff on edit *slices* (usually small), so this is
acceptable in practice; a genuinely huge slice with few edits is the remaining
slow case. A correct O(1) cross-block oracle (or a proof none exists) is the open
follow-up.

### Validation

`tests/Unit/DiffPlanner/VimDiffTest.cpp` covers the production planner: the diff
round-trips to the goal, every K-best plan round-trips, is ascending by cost,
and is distinct, and each plan's breakdown total (regions re-priced with the
single-source `query`) equals its DP cost — the pin that the multi-source
deletion sweep agrees with the single-span oracle. Cost-model behavior is pinned
per case by the markdown approval fixtures behind
`tests/Approval/VimDiffApprovalTest.cpp` (one case per file).
`tests/Debug/VimDiffCostCorpus.cpp` dumps plan costs + span signatures over a
deterministic corpus; run it (with `VIMFY_SEED_MODE=fixed`) before and after a solver refactor and diff.
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

Implemented: the exact two-table (`out`/`in`) DP with the per-column multi-source
deletion sweep, the shared counted-tiling oracle for delete and movement
(span-local runs, digit keystrokes plus the shared `CountPenalty` model),
insert-mode overhead (`entry` + `<Esc>` per typed region, no other per-region
charge), K-best plans (`CostOptions::maxPlans`), and matched-run collapse —
wired as the default `diffAlgorithm=0`. Remaining cost is the raw-position walk in the sweep
(`O(N)` per goal column) and the earlier-line-start scan for counted line
chunks; a per-piece sliding-window min over the concave count penalty would make
the latter O(pieces), and a per-seal crossing table would make the former
diff-bound (see "Exact vs diff-bound").

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
