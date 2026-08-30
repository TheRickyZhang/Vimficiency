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
- a **partition** of the change into planned edits, priced by a coarse heuristic
- a **shortest-path search** over editor states that realizes each plan with exact Vim semantics

### 1. Diff breakdown: partitioning a change into planned edits

Consider transforming `aa b cc` → `xx b zz`. Two reasonable plans:
- two edits: `aa`→`xx`, move over `b`, `cc`→`zz`
- one edit: delete everything, type `xx b zz`

While pure edit distance would always prefer the first, we must consider the practical cost of movement and transitioning between modes, so my heuristic I want to minimize is:
$$
\sum_k \mathrm{del}(D_k) + \sum_k \mathrm{ins}(I_k) + \sum_{0<k<t} \mathrm{move}(K_k)
$$

for $A = K_0 D_1 K_1 \cdots D_t K_t$ and $B = K_0 I_1 K_1 \cdots I_t K_t$ ($K_k$ kept, $D_k$ deleted, $I_k$ inserted).

**Costing a span.** $\mathrm{ins}$ is real typing effort plus `i` and `<Esc>`. $\mathrm{del}$ and $\mathrm{move}$ are the cheapest tiling of the span by counted commands:
- `{k}x` / `{k}dw` / `{k}dW` / `{k}dd` / `{k}dap` (for moves `{k}l` / `{k}w` / `{k}W` / `{k}j` / `{k}}`), plus `D`, `$`, `d0`
- a count costs its digits plus a concave cognitive penalty in $k$, capped at the same limit the searches use
- one left-to-right sweep prices every span from a start, and it is multi-source: one sweep can price many starts at once

**The DP.** Two tables, $\mathrm{out}[i][j]$ (normal mode) and $\mathrm{in}[i][j]$ (insert mode), where index i means covering 0, i) in the starting buffer, and j means getting [0, j) in the ending buffer.
- *move* over a matched diagonal; *delete* a span of $A$ (one multi-source sweep per column)
- *enter* insert and type a unit; *type* another unit; *exit* for free

Regions are not state: they are read off the winning path as the stretches between moves. For the $K$ cheapest plans each cell keeps $K$ candidates, one per partition.

**Sealing kept text.** A char-level grid is $O(N \cdot M)$. A long matched run that no optimal plan edits into is a separator: every optimal path crosses it with one move, so the problem splits into independent blocks joined by seals. A run is sealed when:
- gate: $\mathrm{type}(\text{core}) > \mathrm{move}(\text{core}) + \text{entry} + \text{Esc} + \text{slack}_L + \text{slack}_R$
- margin at each edge: keep depth $d$ while $\mathrm{ins}(d) \le \text{slack}(\text{edge}) + \mathrm{move}(d)$ — those chars stay in the adjacent block

The slack is the merge bonus of deleting straight through an edge instead of stopping there, bounded by chopping the crossing command and repairing it. It is computed per edge from the text: `D` deletes to end-of-line for 2 keys, so on a wide line stopping mid-line is expensive and the margin widens. Retype costs a key per character, so the crossover is quick. No deletion crosses a seal, so every sweep stays inside its block and the planner is diff-bound — $O\big(\sum_k (n_k + m_k) \cdot N_k \cdot \text{cap} + n_k \cdot m_k\big)$ over blocks of $n_k \times m_k$ cells — with the same optimum as the full grid.

The plan's cost is a surrogate. A regret harness measures how often plan 1 is not the plan the real search realizes most cheaply.

### 2. Shortest-path search: from a plan to keystrokes

A state is $(\text{buffer}, \text{cursor}, \text{mode})$; a transition is one Vim command, simulated by `VimCore` (motions, operators, text objects, autoindent, exclusive/linewise rules) on a bounded slice. Three A\* searches compose:

- **NavOptimizer** — cursor only. $f = w_e \cdot \mathrm{effort} + w_d \cdot \mathrm{dist}$, with distance concave per axis ($\sqrt{\cdot}$) because counted motions cross long gaps cheaply; counted motions come from a precomputed landing index.
- **TransformOptimizer** — buffer changes, mostly "delete old, type new". The goal test runs *before* a deletion commits, so the last delete becomes its change form (`dw`→`dwi`, `dd`→`cc`, `x`→`s`) with the right autoindent keys; counted edits keep true Vim semantics (`3dd` ≠ `dd` three times at buffer edges). States dedup on (buffer hash, cursor, mode); a suffix cache and dot-repeat (`.`) reuse finished work.
- **CompositionOptimizer** — sequences the planned edits. State = (edits done, cursor, buffer); steps alternate applying an edit with a nested Nav search to the next region, plus fused forms (`ci"`, `J` plans). $h = \sum_{\text{remaining}} \mathrm{cost}(E_k) + \mathrm{dist}(\text{next region})$, penalizing overshoot.

**Effort.** Cost is not key count. Keys map to a configurable keyboard and are priced per key plus bigram terms (same finger, same key, alternation, rolls). Every term has a one-key window, so effort is a monoid: merging two sequences is $O(1)$, which is what lets both the DP and the searches accumulate cost incrementally.

**Admissibility.** The heuristics overestimate on purpose, trading A\*'s guarantee for a narrow frontier. Correctness comes back structurally:
- costs are recorded at pop time, and each search returns its $K$ best distinct results
- exploration stops at a node cap and at a budget relative to what the user typed
- $w_d = 0$ is plain Dijkstra (a preset); results are certified by replay through a live Neovim oracle

Composition nests a Nav search inside each expansion, so wall time is set by where the caps bind, not by the asymptotics alone.

## License

MIT
