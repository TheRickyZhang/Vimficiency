# vimficiency

https://github.com/user-attachments/assets/d32cb577-8b72-4301-85ea-dc2c3d706a04

One of the biggest challenges with Vim is knowing which of the many ways to perform an edit is the most efficient — or what all the applicable motions even are.

vimficiency watches how you edit and surfaces shorter keystroke sequences that would have produced the same result, with awareness of customizable per-key effort and the algorithmically-tractable subset of Vim's grammar. You keep editing the way you already do; the plugin runs in the background and lets you replay suggestions side-by-side to learn.

The benchmark dashboard with details on the search process is here:
https://therickyzhang.github.io/Vimfy/

For the two algorithms underneath — how a buffer change is partitioned into
planned edits, and how each plan is turned into keystrokes by shortest-path
search — see [How it works](#how-it-works).

## Requirements

- Neovim 0.11+
- A C++23 compiler (GCC 13+ or Clang 16+) and CMake 4.1+ to build the native
  library. **Linux and macOS only** at the moment — Windows is not yet
  supported. Prebuilt binaries are not yet shipped; the plugin is currently
  built from source on install.

## Installation

vimficiency ships a native library (`libvimficiency.{so,dylib}`) that the
Lua side loads via LuaJIT FFI. Plugin managers can compile it as part of the
install step.

### lazy.nvim

```lua
{
  "therickyzhang/vimficiency",
  build = "cmake -B build && cmake --build build",
  config = function()
    require("vimficiency").setup()
  end,
}
```

If you update the plugin and start seeing an "ABI mismatch" error on load,
the prebuilt artifact is stale against the new FFI bindings — run
`:Lazy build vimficiency` to recompile.

### packer.nvim

```lua
use {
  "therickyzhang/vimficiency",
  run = "cmake -B build && cmake --build build",
  config = function() require("vimficiency").setup() end,
}
```

### vim-plug

```vim
Plug 'therickyzhang/vimficiency', { 'do': 'cmake -B build && cmake --build build' }
```

…then in your Lua config: `require("vimficiency").setup()`.

### Manual build

```bash
git clone https://github.com/therickyzhang/vimficiency
cd vimficiency
cmake -B build
cmake --build build
```

Then either place the plugin on your `runtimepath` or point at the library
explicitly:

```bash
export VIMFICIENCY_LIB_PATH=/path/to/vimficiency/build/libvimficiency.so
```

For a full setup with configuration and keymaps, see
[`examples/config.lua`](examples/config.lua).

## Usage

vimficiency organizes work around **sessions** — captures of (start state, keys typed, end state) that the optimizer scores. Sessions form a 2×2 over how they start and end:

|                   | **Manual end**                | **Auto end** (idle)         |
|-------------------|-------------------------------|-----------------------------|
| **Manual start**  | **Mark** — `:Vimfy start a` … `:Vimfy finish a` | **Watch** — `:Vimfy watch a`            |
| **Auto start**    | **Recall** — `:Vimfy recall 6` / `recall 3s` | **Suggest** — fires while you edit      |

Recall is always on, so the lowest-friction entry point is:

```vim
" ... edit something ...
:Vimfy recall 6     " analyze the last 6 keystrokes (or 'recall 3s' for time)
:Vimfy play 6       " animate the suggested sequence
```

For the full set of commands, save/store/fetch flow, configuration, and the effort model, see the user guide.

## Documentation

- **User guide:** [`doc-src/README.md`](doc-src/README.md) — installation, the four session types, keymap contract, configuration, effort model, troubleshooting.
- **In-editor:** `:help vimficiency` (generated from `doc-src/`).
- **Internals:** `dev/` — implementation notes on the optimizer, FFI conventions, replay precompute, etc.

## How it works

A session gives the optimizer an initial buffer $A$, a goal buffer $B$, the cursor, and what the user typed. Finding something shorter is two problems:
- a **partition** of the change into planned edits, priced by a coarse keystroke heuristic
- a **shortest-path search** over editor states that realizes each plan with exact Vim semantics

### 1. Diff breakdown: partitioning a change into planned edits

Consider transforming `aa b cc` → `xx b zz`. Two reasonable plans:

```
two edits   aa→xx, move over b, cc→zz    cwxx<Esc> ww cwzz<Esc>   12 keystrokes, 4 chars changed
one edit    delete everything, retype     Cxx b zz<Esc>             9 keystrokes, 7 chars changed
```

https://github.com/user-attachments/assets/d214130e-4026-4ea6-994d-fe85c3503277

Pure edit distance always prefers the first. Moving between regions and re-entering insert mode cost keystrokes too, which is what makes the second cheaper here.

**Objective.** Write both buffers as kept runs interleaved with edits, $A = K_0 D_1 K_1 \cdots D_t K_t$ and $B = K_0 I_1 K_1 \cdots I_t K_t$, and minimize

```math
\sum_{k=1}^{t} \operatorname{del}(D_k) \;+\; \sum_{k=1}^{t} \operatorname{ins}(I_k) \;+\; \sum_{k=1}^{t-1} \operatorname{move}(K_k)
```

$K_0$ and $K_t$ are free: every optimal plan starts at the first differing character and ends at the last, so reaching them from the cursor costs the same for every plan.

**Pricing a step.** With `i` characters of `A` consumed and `j` characters of `B` produced, there are four kinds of step:

| step   | commands                          | cost                                          | `(i, j) →`    |
|--------|-----------------------------------|-----------------------------------------------|---------------|
| insert | typed text                        | insert-mode entry + typed characters + `<Esc>` | `(i, j+x)`    |
| move   | `l` `w` `W` `j` `}` `$`           | counted-motion tiling                          | `(i+x, j+x)`  |
| delete | `x` `dw` `dW` `dd` `dap` `D` `d0` | counted-operator tiling                        | `(i+x, j)`    |
| change | delete, then insert               | delete + typed characters + `<Esc>` — the `c` form saves the entry key | `(i+x, j+y)` |

Counted commands cost their digit keystrokes plus a concave cognitive penalty (roughly `sqrt(k)`), shared with the downstream search.

**Tiling.** A span's move or delete cost comes from one left-to-right pass over its characters:

```
best[r] = min over chunks [s, r) ending at r  of  best[s] + cost(chunk)
          chunks: {k} chars · {k} words · {k} lines · {k} paragraphs · D · d0
```

- one pass from a start prices every end, so a whole table row costs one walk
- seeding `best` with a whole DP column makes the pass multi-source: a deletion's remaining cost does not depend on where it began, so one pass prices every deletion arriving in that column

**Sealing long matches.** A long kept run should never be edited into — the goal still contains it, so cutting through it means retyping it. A matched run with core `R` is sealed when

```
type(R) > move(R) + cost(i) + cost(Esc) + slack_stop + slack_start
```

- a plan that deletes through `R` pays at least `type(R)` to put it back
- a plan that moves over `R` pays `move(R)`, at most one extra insert-mode round trip, and the *slack* of stopping a delete tiling at the run's edge and starting another after it (a `{k}dd` cut mid-line, a `{k}dap` cut mid-paragraph)
- every term is read from the cost tables, not hand-picked; retyping grows per character while the slack is bounded per edge, so edits may still reach a few characters *into* a run — those margins stay in the blocks

What remains is a list of **blocks**, character-level sub-problems between consecutive seals:

```
initial   [ block 1: edit · short kept run · edit ]═══ sealed core ═══[ block 2: edit ]
                                                     one CROSS move
```

A seal is crossed by one `CROSS` move priced over the whole core, and no deletion crosses a seal. Every sweep therefore stays inside its block, and the planner's work is bounded by the size of the diff, not the buffer.

Runs too short to seal stay in their block as ordinary characters, and the DP decides:

```
initial   the quick brown fox jumps over the lazy dog
goal      the quick red fox leaps over the lazy dog
                    ^^^^^     ^^^^                        one block, two regions
plan      "brown"→"red"  ·  move  ·  " jum"→" lea"
```

Here the DP moves over `" fox "` rather than retyping it. The second region starts on the space: command boundaries price spans, they never constrain where an edit begins or ends.

**The DP.** Two tables per block, `out[i][j]` (normal mode) and `in[i][j]` (insert mode), both meaning initial `[0, i)` consumed and goal `[0, j)` produced:

```
move    out[pi][pj] → out[i][j]   i-pi = j-pj ≥ 1, chars match          moveCost(pi, i)
delete  out[pi][j]  → out[i][j]   delete initial [pi, i)                 delCost(pi, i)
change  out[pi][j-1] → in[i][j]   delete initial [pi, i), enter insert   delCost(pi, i) + esc + typed
enter   out[i][j-1] → in[i][j]    enter insert, type goal char j-1       entry + esc + typed
type    in[i][j-1]  → in[i][j]    type goal char j-1                     typed
exit    in[i][j]    → out[i][j]   leave insert                           0
cross   previous block's trailing diagonal → this block's leading one   moveCost(seal)
```

- a *region* is a maximal run of delete/type steps between two moves, read off the winning path afterwards
- nothing is charged per region, so the state needs no memory of where one began — that is why two tables suffice
- typing is additive per character because effort is a monoid (below): with `PS` the prefix effort of typing all of `B`, typing `[b, e)` costs `PS(e) − PS(b) − cut(b)`
- for the `maxPlans` cheapest *distinct* partitions, each cell keeps up to `maxPlans` candidates, deduplicated by a partition key

**Complexity.** Per block of $n \times m$ cells over $N_k$ raw characters with count cap $c$: $n$ move sweeps of $O(N_k c)$, an $O(nm)$ DP, and one $O(N_k c)$ deletion sweep per goal column.

```math
O\Big(\sum_k (n_k + m_k)\, N_k\, c \;+\; n_k m_k\Big) \;+\; \text{one crossing sweep per seal}
```

Generic concave-gap speedups (SMAWK) do not apply: move and delete violate the quadrangle inequality, since one `dap` can replace the deletions it encloses.

### 2. Shortest-path search: from a plan to keystrokes

The partition fixes *what* changes; the search finds the cheapest keystrokes that do it. Nodes are editor states, edges are Vim commands applied by our own simulator (verified against a headless Neovim), and edge weights are typing effort.

**Cost model.** Keys map onto a configurable keyboard and are priced per key plus bigram adjustments: same finger, same key, hand alternation, inward/outward rolls. Every term has a one-key window, so effort is a monoid — merging two sequences is $O(1)$, and the planner's prefix-sum typing cost is exact.

**Three searches.**

```
NavOptimizer          node = cursor                        edge = motion
TransformOptimizer    node = (buffer hash, cursor, mode)   edge = delete · change · join · .
CompositionOptimizer  node = (edits done, cursor)          edge = planned edit · motion toward next edit
```

**NavOptimizer** — the buffer is fixed, so one $O(N)$ pass indexes word and paragraph landings, and `{k}w` for any `k` becomes a lookup. Motions are pruned by direction toward the goal, roughly halving what is explored per state.

- heuristic: `sqrt(|Δline|) + sqrt(|Δcol|)`
- concave because one `}` or `G` covers arbitrary distance for a fixed cost; linear Manhattan overestimated true effort ~6× and railroaded the search into counted-`j` chains

**TransformOptimizer** — models an edit as deleting the old text, then typing the new. The final deletion becomes its change form (`de`→`ce`, `dd`→`cc`, `x`→`s`) so the delete and the insert-mode entry are one command.

- dot-repeat: a suffix cache keyed on (buffer hash, cursor, mode) stores the remaining suffix at every intermediate state of a goal-reaching path
- heuristic: characters left to delete, plus 2 per extra line

**CompositionOptimizer** — precomputes a Transform result from every valid start position of every planned edit, so an edit is a single edge. The remaining edges are motions toward the next region, plus fused commands like `ci"`, `da(`, and `J` where the text-object context allows.

- heuristic: `Σ cost(remaining edits) + dist(next region)`, with overshoot penalized asymmetrically — moving past a region and coming back is expensive

**Inadmissibility.** No admissible heuristic exists for Vim short of a full search: `G` or `}` moves an arbitrary distance for a fixed cost, so any distance-based `h` can overestimate. A* loses its optimality guarantee, and we compensate:

- `f = effortWeight·g + distanceWeight·h`; `distanceWeight = 0` is pure-effort Dijkstra — exact, slow, and the ground truth we calibrate against
- the concave distances above shrink the overestimate exactly where long-range motions exist
- costs are recorded at pop time, not push time, so a cheaper path found later to a queued state still wins
- each search continues past the first goal and returns up to `maxResults` distinct results

Harnesses in `tests/Debug/` (`NavQuality`, `EditCompQuality`) measure the gap against Dijkstra: the sqrt heuristic took Nav from 59% to 9% above optimal. Better heuristics remain an open area.

**Bounds.** Every search stops at `maxNodesPopped` pops (50 000 by default) and never enqueues a state costing more than `exploreFactor` × what the user typed (2× by default). With `P` pops and branching factor `b`, a search is $O(P\, b \log(P b))$ on a binary heap; Composition additionally runs one Transform search per (planned edit, start position) before its own A*.

## License

