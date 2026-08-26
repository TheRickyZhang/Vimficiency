# Architectural TODO

Deferred improvements that are justified in principle but not worth the churn today. Each item lists a concrete **threshold** — the change in the codebase that tips the cost/benefit toward doing it. When you touch an area near one of these items, re-read the threshold and decide whether the condition has become true.

Re-evaluate this file:
- Whenever you add a new build-time flag or touch `CMakeLists.txt` / `src/BuildConfig.h.in`
- Whenever you modify `.github/workflows/bench.yml`
- When onboarding someone new to the repo
- At minimum once per major refactor

Completed items should be deleted, not crossed out.

---

## 1. Move the CI bench workflow onto `CMakePresets.json`

**What.** Replace the ad-hoc `-D` flags in `bench.yml` (e.g. `-DCMAKE_BUILD_TYPE=Release -DVIMF_DEBUG=OFF -DVIMF_TRACK_STATES=ON ...`) with named presets (`release-track`, `release-bench`, etc.) defined in `CMakePresets.json`. Preset names are validated against the file, so a typo fails loudly at `cmake --preset` time rather than being silently accepted as an unused cache variable.

**Why deferred.** Today the workflow has two real configurations (release-no-tracking, release-tracking) and three CMake options (`VIMF_DEBUG`, `VIMF_TRACK_STATES`, `VIMF_LEGACY_VIM`). `BuildConfig.h` + the `static_assert` in `vimfy_explore` + the `jq`-based validation step already give layered protection. Presets would add a second config file, a learning curve for contributors, and wouldn't meaningfully harden the current setup further.

**Threshold to revisit.**
- The CMake option list grows past ~5 build-time flags, **or**
- A third distinct build configuration appears (e.g. a separate ASan build, a profiling build, or a per-architecture matrix), **or**
- The workflow grows a third `cmake -B ...` invocation pattern.

Any of those makes the "one preset per config" structure pay for itself.

---

## 2. Unify `VIMF_DEBUG` through `BuildConfig.h` and fix the source-side name

**What.** `CMakeLists.txt` defines the macro `VIMF_DEBUG`, but `src/Utils/Debug.h` checks `#ifdef VIMFICIENCY_DEBUG` (different spelling). `DEBUG_ENABLED` is therefore always `false`, regardless of how the option is set. Route `VIMF_DEBUG` through `BuildConfig.h.in` (same pattern as `VIMF_TRACK_STATES`) and change `Debug.h` to read it with `#if VIMF_DEBUG`.

**Why deferred.** Fixing the wiring flips `DEBUG_ENABLED` from `false` to `true` for local developer builds (the option defaults `ON`). `AnalyzeExports.cpp:117` and every `debug(...)` call site would start producing output. Not a bug per se — that was the original intent — but it's a behavior change that deserves its own PR and a tour of the call sites, not a hidden side effect of the track-states fix.

**Threshold to revisit.** The next time someone actually needs `debug()` output to work locally, or the next time someone adds a new `#ifdef VIMFICIENCY_*` check (bundle the cleanup so the whole class goes away at once).

---

## 3. Unify `VIMF_LEGACY_VIM` through `BuildConfig.h` and fix the source-side name

**What.** Same class of bug as item 2. `CMakeLists.txt` defines `VIMF_LEGACY_VIM`, `src/VimCore/VimOptions.h` checks `VIMFICIENCY_LEGACY_VIM`. The option has never actually flipped Vim-default behavior — passing `-DVIMF_LEGACY_VIM=ON` today is a no-op.

**Why deferred.** Fixing the wiring would retroactively enable legacy-Vim option defaults (`startofline`, `joinspaces`, `Y`-yanks-line) for anyone who has ever set the flag expecting it to work. Low risk today because no current workflow sets it, but it deserves a directed audit of the `VimOptions.h` blocks + test coverage for both branches.

**Threshold to revisit.**
- A user or test case actually needs legacy-Vim defaults (right now it's pure latent infrastructure), **or**
- Bundle with item 2 — one PR that migrates both flags through `BuildConfig.h` is simpler than two.

---

## 4. Retire or use `DebugTracking.h`'s `Maybe<T>` pattern

**What.** `src/Optimizer/DebugTracking.h` defines a zero-overhead-when-disabled `Maybe<T>` template gated on `kDebugTrackingEnabled` (now driven by `VIMF_TRACK_STATES` via `BuildConfig.h`). It has no users. `SearchStats::exploredStates_` is just a raw `std::vector<ExploredState>` that costs a 24-byte empty-vector member per `BaseSearchStats` even with tracking off.

**Why deferred.** The empty-vector cost is negligible and `Maybe<T>` would require rethreading accessor call sites. Not worth it for one field.

**Threshold to revisit.** A second `if constexpr (SEARCH_TRACE_STATS_ENABLED)` data field lands (e.g. per-pop timing, queue-size history). At two users, `Maybe<T>` starts earning its keep — either adopt it or delete it.

---

## 5. Migrate `VIMF_DEBUG` / `VIMF_LEGACY_VIM` into `BuildConfig.h.in`

**What.** Both flags are still surfaced via `target_compile_definitions(... $<$<BOOL:${VIMF_DEBUG}>:VIMF_DEBUG>)` rather than `#cmakedefine01` in `BuildConfig.h.in`. The `target_compile_definitions` form uses `#ifdef` in source, which is the same "typo silently takes the disabled branch" footgun that caused the original track-states bug.

**Why deferred.** Entangled with items 2 and 3 — moving the flag into `BuildConfig.h.in` requires changing the source-side checks at the same time, and those changes carry real behavior deltas (see 2, 3). Do all three together.

**Threshold to revisit.** Do this as part of item 2 and/or item 3.

---

## 6. Re-introduce visual-delete shortcuts to TransformFrontier

**What.** `tryVisualDelete` is still invoked by the batch TransformOptimizer
(`TransformOptimizer.cpp` in the `PureDeletion` finalization block) but the
TransformFrontier comment at the `emitReplaceCharAction` call site
explicitly skips it because visual-delete sequences are multi-token
structural macros (`v{motion}d`) that don't fit the single-action invariant
the rest of the frontier now enforces.

**Why deferred.** The frontier contract is "one immediately executable Vim
action per recommendation, and Explore re-derives phase from observed
state". `v)hd` is three structural tokens; supporting it in the frontier
requires a continuation/macro abstraction Explore doesn't have today.
Composition's text-object lane already covers the most common cases that
visual-delete used to surface.

**Threshold to revisit.** Either (a) Explore gains a `Macro` phase that can
track a multi-step structural token across `acceptSnapshot` calls, or
(b) a benchmark / activity-log case demonstrates that a real session is
losing ground because the visual-delete shortcut isn't surfaced
interactively. Pointer to the current skip site:
`src/Optimizer/TransformOptimizer/TransformFrontier.cpp` ("Visual deletion
is a multi-token structural macro" comment).

---

## 7. Rebalance diff generation against composition search

**What.** A three-step investigation, in order:

1. **Simplify diff generation.** `VimDiff` is an exact K-best DP over a hand-built
   surrogate cost model. Reduce it to the simplest thing that still ranks
   partitions well. The exactness is with respect to the surrogate, not to real
   Vim cost, so it may be buying nothing.
2. **Measure.** (a) Time split between diff generation and composition search on
   realistic slices — `VIMFY_TIME_PHASES` already prints per-phase and per-diff
   timing; a healthy split is something like 10/90. (b) How well the K plan
   rankings correspond to the best sequence actually findable from each
   (`tests/Debug/PlanRegret.cpp` via the `forcedDiffs` seam). (c) Whether the two
   stages can be combined into one globally optimal search rather than
   partition-then-optimize.
3. **Complexity.** Derive the time complexity of both stages and check that
   measured scaling matches. A mismatch means either the model is wrong or a cap
   is binding.

**Why it matters.** Production consumes `plans.front()` with `maxPlans` at its
default of 1 (`CompositionSearchContext.cpp:78`), so plan 1 must be right.
Exactness costs an `O(N)` oracle walk per span query — the only reason planner
runtime is buffer-bound rather than diff-bound. Whether that price buys better
partitions has never been measured: `SparseVsDense` measures agreement with the
surrogate's own value, while `PlanRegret` measures rank inversion, which is the
thing that actually decides plan quality.

**Complexity as currently understood** (`M` = differing chars, `N` = buffer
chars, `E` = planned edits, `P` = pops, `B` = branching per pop):

| stage | cost | note |
|---|---|---|
| VimDiff search | `O(M³)` | `O(M²)` cells, `O(M)` sweep each |
| VimDiff oracle | `O(M²·N·CAP)` | `O(M²)` distinct spans, `O(N)` walk each; dominates |
| Composition construction | `O(E · P_t · B_t)` | `E` Transform A* runs; dominates the `O(E·N)` buffer rebuild and `O(E·L²)` text-object scan |
| Composition search | `O(P_c · P_n · B_n)` | **nested**: each pop can fire up to ~9 NavOptimizer A* runs (8 ranked start positions + the diff-range search) |

The headline is that composition is not one A* but two levels — a nav search
sits inside the expansion step. All three searches are hard-capped
(`maxNodesPopped` 50000), so wall time is governed by *where the caps bind*, not
by the asymptotics; that is exactly why step 2 has to be empirical.

**Concrete candidate found while deriving the above.** The `starts` loop in
`CompositionOptimizer.cpp:578` issues up to 8 single-point NavOptimizer searches
from the same cursor, each rebuilding its own `Lines` slice and `BufferIndex`.
`startPositions()` is clipped to the diff range by `restrictValidStartRegion`
(`PlannedEditArtifacts.cpp:243`), so it is a holey subset of that range — the
gaps come from start starvation under the shared Transform queue, which is why it
cannot be one `CharInterval`. But the *bounding* range is just the diff range,
which the fallback search twenty lines below already targets. Replacing the loop
with that single range search plus a filter of landings against
`startPositions()` gives one slice, one `BufferIndex`, one A*, and the same
reachable set.

Caveat: not a pure win. Today the 8-cap plus the `costToGoal` sort bounds
successors per pop at 8 ranked landings; the range search returns whatever nav's
`maxResults` / `maxResultsPerEndPos` allow, so composition branching — and hence
pop counts and results — can change. Measure before and after, same as the rest
of this item.

---

**Decision rule.** If diff generation is a small share of end-to-end time, stop —
the current design is fine and the effort belongs in composition. If it is a
large share, run `PlanRegret` with a cheap approximate oracle against the exact
one; equal regret means exactness can be dropped and the `N` factor disappears.

---

## Noted alternative: item 1 vs. the current approach

The defense layers chosen for the track-states fix (`BuildConfig.h` + `static_assert` + workflow `jq` check + differentiated dashboard message) are cheaper and more localized than `CMakePresets.json`. Item 1 becomes the better path only when configuration count or CI complexity makes per-flag discipline hard to maintain — otherwise the current three-layer defense subsumes what presets would give us.
