# Design Decisions

Append-only log of non-obvious design choices and the reasoning. Add new entries
at the bottom. Keep each entry to one bullet with a short "because" — link out
to deeper docs when needed.

## Format conventions
- C++ constants use `SCREAMING_SNAKE_CASE`, not `k`-prefixed names — because the existing codebase (`LEVEL_COUNT`, `DEBUG_ENABLED`, `INF`, `LINE_OUTSIDE_BOUNDARY`) already uses it, and one style is better than two.
- Don't `static_cast<size_t>` for indexing into `std::vector`/`std::array` — implicit `int → size_t` is fine, casts add noise without safety. Use `int` for sizes/indices throughout.
- Default to no comments. Only add when the *why* is non-obvious (hidden invariant, workaround, surprising behavior). No headers, no narration, no restating function names.

## Approval tests
- When an approval-test output format changes intentionally, just overwrite `.approved.txt` with `.received.txt` — no separate confirmation step. The diff between received/approved IS the new expected output.

## Pretty text (`src/Utils/PrettyText.h`)
- Named `PrettyText` / `prettify`, not `GlyphText` / `glyphize` — because "pretty" matches the user-facing intent (make whitespace/control chars visible), while "glyph" was implementation jargon and "glyphize" is a made-up verb.
- Centralized in `Utils/PrettyText.h`, not `Interpreter/SequenceDisplay.cpp` — because VimDiff diagnostics also need this, and Optimizer→Interpreter is a backwards layering.
- `PrettyText` is a tagged struct (`struct PrettyText { std::string text; }`) built only via `explicit` constructors — because the type itself signals "rendering has happened", and the `.text` accessor marks the boundary back to untagged text.
- A free function `prettify(c/sv) → std::string` exists alongside the struct — for sites that don't need to keep the type tag (e.g. concatenating into an existing `std::string`). Without this, those sites would have to write `PrettyText(x).text`, which is friction without benefit.
- The plural-string constructor delegates to the singular-char dispatch (`appendGlyph`) — single source of truth for the char→glyph mapping.
- `SequenceDisplay.cpp` uses the raw glyph constants (`PRETTY_SPACE`, `PRETTY_TAB`, `PRETTY_NEWLINE`) directly when mapping `<Space>`/`<Tab>`/`<CR>` key notation, because that's a *different* operation (notation → glyph) from rendering text.

## VimDiff approval region format (`VimDiffApprovalTest`)
- Each case prints both buffers, a cost legend, then top-K plans. Each region uses a vertical block: deleted text, `->`, inserted text, then `del / ins / move`.
- Empty insert/delete sides stay visible as blank lines — because hidden empty sides make single-sided edits harder to audit in approval output.
- Top-K plans stay in the approval view — because VimDiff's risk is often cost calibration, not just round-trip correctness.

## Join-plan inter-group navigation uses `j0`, not `j`
- `computeJoinPlanForDiff` (`PlannedEditArtifacts.cpp`) builds multi-group join+residual plans. Between groups, it must move the cursor to the next group's first line at col 0.
- Originally emitted `j` between groups, which **preserves curswant** — so the cursor lands at the prior group's exit column, not col 0. That broke two downstream assumptions:
  - `JoinSimulation::simulate` models J-chains from slice-local `(0, 0)`. With cursor at the wrong column on entry, real-buffer joins diverged from the simulation.
  - The residual `TransformOptimizer` is called with `cursorCol = 0` for non-join groups (or with `sim.cursorCols.back()` for join groups, which is also tracked relative to col-0 entry). The residual plan assumed cursor at col 0; `j` alone didn't deliver that.
- Now emits `j0` between groups. One extra keystroke per inter-group transition; correctness across all join+residual compositions. Trip wire: `CompositionOptimizerGeneratedPropertyTest.ShrinkableBufferMutationsTopResultsReplay`.

## op_delete result-is-blank uses `nostartofline` cursor (Neovim default)
- `:help startofline`: with `nostartofline` (Neovim default), the `d` operator does NOT jump to first non-blank; cursor keeps the same column.
- Vim's `op_delete` calls `beginline(BL_WHITE | BL_FIX)` after linewise delete, which *would* go to first non-blank — but `nostartofline` overrides that for the `d` operator.
- Two linewise-promotion rules with different cursor handling:
  - **Exclusive-linewise** (`:help exclusive-linewise`, e.g. `d)` `d}` `db` when end at col 0 of another line): cursor goes to first non-blank regardless of `'startofline'`.
  - **Result-is-blank** (`op_delete` ops.c:741-757, e.g. `dge` when join would be blank): cursor honors `'startofline'`. With `nostartofline`, preserves targetCol.
- In our resolvers: result-is-blank promotion in both `resolveBackwardInclusiveWordEndDeleteRange` (`dge`/`dgE`) and `resolveForwardInclusiveWordEndDeleteRange` (`de`/`dE`) uses `LinewiseDeleteCursorPolicy::LinewiseCommand`, which routes to `deleteLineRangeAndUpdatePos` — that function already branches on `VimOptions::startOfLine()`. Exclusive-linewise paths continue using `OperatorMotion` policy.
- Trip wires: `VerifyWordOperator.WordOperatorMatchesOracle` (e.g. `dge` on `[" ", "", " ,"]` cursor (1, 0) — Vim lands at (0, 0), not (0, 1)). `TransformOptimizerGeneratedPropertyTest.MultiLineFullBufferChangeTopResultsReplay` (de from blank-line start over `["", "E", "      ^"]`: the OperatorMotion cursor placement at (0, 6) caused the optimizer to emit a downstream `d{` that no longer matched Neovim's cursor; LinewiseCommand keeps cursor at (0, 0) and the planner converges).

## Exclusive-linewise forward cursor on blank line honors `'nostartofline'`
- `applyExclusiveLinewiseCursorPolicy` (`src/VimCore/VimEditUtils.cpp`) previously did `pos.setCol(0)` whenever forward exclusive-linewise delete (`d}`, `d)`) left the cursor on a blank line. That's the `'startofline'`-on behavior: Vim's `d` operator goes to first-non-blank (which on a blank line means col 0).
- With `'nostartofline'` (Neovim default), the cursor stays at the targetCol clamped to lastCol — exactly what `deleteOperatorLineRangeAndUpdatePos` produces via its `VimOptions::startOfLine()` branch. The forward-exclusive override was clobbering that.
- Now the unconditional `setCol(0)` is wrapped in `if constexpr (VimOptions::startOfLine())`. The backward-exclusive branch was already curswant-aware (`min(originalPos.col, lastCol)`) and stays as-is.
- Trip wire: `TransformOptimizerGeneratedPropertyTest.MultiLineFullBufferTopResultsReplay` (`d}D` from `(1, 2)` over `["  ", "   K6~ ", "zg~"]` — Vim leaves cursor at `(0, 1)`, then `D` deletes only `[1, EOL)`; old behavior placed cursor at `(0, 0)` and `D` deleted the whole line).

## Same-line delete clearing an off-cursor line does not remove the line
- `applyCharDeletionToBuffer` (`src/VimCore/VimEditUtils.cpp`) used to remove the entire line when (a) the delete was same-line, (b) it cleared the line, (c) `begin.col == 0`, (d) the cursor was on a *different* line. The branch was added in early `db`/`dB` development; the actual `db`/`dB` cases that needed it are now multi-line and go through a different path.
- The branch was firing incorrectly for text-object deletes (e.g., `da]` over `[]` on a line away from the cursor), removing the empty line where Vim leaves it. Removed entirely. All 470 unit tests still pass.
- Trip wire: `VerifyBracketObjects.BracketObjectsMatchOracle` (`da]` over `["(", "[]", "["]` from `(0, 0)` — Vim leaves `["(", "", "["]`; old behavior produced `["(", "["]`).

## TransformExplorer skips sentence ops when findsent's decl-prelude can't see surrounding context
- `findsent`'s opening scan walks BACKWARD over closing chars / end-punct / whitespace from the cursor to find the anchor it starts its forward scan from. On the boundary slice the scan terminates early because it can't see `linesAbove`; the resulting endpoint diverges from what the same `)` would compute on the full buffer. Symmetric problem for `(` with `linesBelow`.
- The explorer can't compute the right answer without that context, so it gates emission: `Forward && hasLinesAbove && cursor.line == 0`, or `!Forward && hasLinesBelow && cursor.line == lastLine`, are treated as ambiguous and not emitted. Lose some valid emissions, never emit an unsafe one.
- Trip wire: `TransformExplorerBoundaryPropertyTest.EmittedDeletesPreserveBoundaries` (`d)` from `(0, 0)` over editRegion=`[".", "ccb."]` with linesAbove=`["aa,."]`, suffix=`"V"` — full-buffer `)` overshoots through `"ccb.V"` because no whitespace after the `.`).

## op_delete result-is-blank linewise promotion
- Vim's `op_delete` (ops.c:741-757) promotes a multi-line char-wise OP_DELETE to linewise when the join-result would be a blank line — i.e., `hasOnlyBlankPrefix(begin.line, begin.col) AND hasOnlyBlankSuffix(end.line, end.col)`. Without this promotion, you delete only a slice of two non-blank lines and the residual is a partially-blank line, which doesn't match Vim's user-visible behavior.
- Two resolvers must apply this rule:
  - `exclusiveDeletePromotesToLinewise` — for `d)`/`d(`/`d}`/`d{` and similar exclusive motions.
  - `resolveBackwardInclusiveWordEndDeleteRange` — for `dge`/`dgE`.
- Both were earlier using STRICTER ad-hoc checks (e.g. `end.col >= line.size()` or per-line "isBlankLine" combos) added as patches for specific test failures. Unified to Vim's actual rule.
- Trip wire: `VerifySentenceCommands.SentenceCommandsMatchOracle` (d() and `TransformOptimizerGeneratedPropertyTest.MultiLineEmbeddedTopResultsReplay` (dgE).

## J/gJ comment-leader stripping (NOT modeled)
- `VimCore::doJoin` does not implement Vim's comment-leader stripping (the `'comments'` option + `formatoptions+=j` behavior that drops `#`/`%`/`>`/`*` etc. from the joined-from line). Faithful porting requires `get_leader_len`, `get_last_leader_offset`, `skip_comment`, and a `'comments'` format parser — ~300 lines plus configuration plumbing — and the gain for the optimizer is small (J/gJ is a minor emission strategy).
- A naive "always strip these chars" version would silently strip when Vim wouldn't (Vim only strips when the joined-from line is also a comment, tracked via `prev_was_comment`), so it would introduce a new failure mode rather than reduce divergence.
- `NeovimOracle` sets `comments=` (empty) so the test oracle agrees with our model. Real users with default `'comments'` and `formatoptions` may see optimizer-emitted J/gJ sequences that diverge from actual Vim output when the buffer joins onto a comment-leader line. Documented as a known gap; revisit if user feedback specifically calls it out.

## TRUE exclusive-linewise forward motion uses motion-endpoint curswant on shifted-in cursor
- `:help exclusive-linewise` promotion (begin blank prefix + endAtLineStart or consumesBufferTail): Vim's motion sets curswant to the endpoint's column BEFORE the operator runs. Our optimizer collapses motion + operator into one apply, so we must explicitly propagate the motion-endpoint column to the post-delete cursor's `targetCol`.
- Distinct from `op_delete` result-is-blank promotion (begin blank prefix + end blank suffix, both sides non-zero): that's a pseudo-linewise cleanup inside `op_delete` that preserves the ORIGINAL cursor's curswant. We tag the two cases with `classifyExclusiveDeleteLinewisePromotion` and only populate `ResolvedDeleteRange::exclusiveMotionEndCol` for the TRUE-exclusive case.
- Cursor placement rule: when the delete leaves lines AFTER the deleted range (cursor lands on a shifted-in line), apply `pos.targetCol = exclusiveMotionEndCol`. When the delete consumes the buffer tail (cursor falls back to the line ABOVE the deleted range), preserve the original cursor's `targetCol`.
- Trip wires: `TransformOptimizerGeneratedPropertyTest.MultiLineEmbeddedTopResultsReplay` (`d)` from `(1, 1)` over `[">y~ $", " I| ", "~\"M~!", "^~[ ]^_"]` — cursor (1, 0), motion endpoint col was 0). `TransformOptimizer_ManualTest.ExclusiveLineAdjust_ForwardSentence_Linewise` (`d)` on `["dfacbfab", "  ", "eaed"]` from `(1, 1)` — cursor (0, 1), tail-consumed so original curswant preserved).

## Dot repeat uses a structured `DotRepeat`, not a re-parsed text command
- `Edit::applyEdit` previously took `std::string* lastEditCmd` holding a formatted text command like `"5de"`. On every `.`, it called `parseEdits(*lastEditCmd)` to split count and base again. The optimizer side already carried `(lastEditCount_, lastEditBase_)` as separate fields, so the two sides described the same concept in two incompatible shapes — and `formatCountedCommand` + `matchesCountedCmd` existed solely to bridge them via text.
- Now there's one shape: `struct DotRepeat { std::string base; int count; }` in `EditInterpreter.h` (next to `ParsedEdit`). `applyEdit` takes `DotRepeat* lastEdit`; on `.`, it constructs `ParsedEdit{base, count}` directly via `asEdit()` — no parse round-trip. `TransformState`/`TransformPathStep` store a `DotRepeat lastEdit_`. `ChangeGoalHandler::isDotRepeat`, `SuffixCache::canUseDot`, `TransformTransitionDispatcher::continueWithEdit`, and `afterCommandWithLastEdit` all consume `DotRepeat` instead of `(int, string_view)` pairs.
- `formatCountedCommand` stays — `SuffixCache::matchesCountedCmd` still compares against an existing text token inside the program. That's a text↔text comparison, not a parse round-trip.
- Verified by `Verify_DotRepeat.cpp` (oracle-pinned `.` after each of `x/dw/dW/de/dE/db/dB/dge/dgE` + controlled motion).

## Composition join plan picks J vs gJ per group via match score
- `computeJoinPlanForDiff` (`PlannedEditArtifacts.cpp`) used to hardcode `J` (addSpace=true) for every join in every partition group. For pure-newline-deletion goals (e.g. `["abc","def"] → ["abcdef"]`) this forced a residual edit to delete the inserted space — and on top of TransformOptimizer's start-position iteration skipping the diff's `(L, lineEnd)` slot, the plan settled on heavy `J + ciw` cleanup instead of a bare `gJ`.
- Now `JoinSimulation::simulate` takes an `addSpace` flag, and both the partition-feasibility loop and the emission loop simulate J and gJ separately and pick the variant whose joined line has higher `joinSimMatchScore` against the target (exact match wins decisively). The chosen variant is emitted uniformly for that group's `numJoins`.
- Trip wire: `CompositionOptimizer_ManualTest.GJEmittedForPureNewlineDeletion` (`["abc","def"] → ["abcdef"]` must surface a `gJ` result).
- The bare-`J` shortcut in `CompositionFrontier::emitJoinAction` is unaffected — it's the Explore navigate-phase single-action emitter, not the composition optimizer's plan, and its existing disjointness comment about TransformFrontier's J lane still applies.

## Main-branch bench moved to a self-hosted runner (spare MacBook)
- Pre-push hook on the dev machine only fires on local `git push origin` — PR merges via the GitHub web UI silently bypass it, so the resulting main SHA was absent from the chart. An earlier fix (`scripts/bench-reconcile.sh`) tried to copy prior feature-branch entries under the new main SHA by matching tree hashes; that worked for exact tree matches but missed docs-only commits, depended on local git keeping all source SHAs reachable, and the committer-date-vs-author-date subtlety produced clustered/incorrect chart timestamps on the first push.
- Replaced with `.github/workflows/bench-main.yml` on `runs-on: [self-hosted, macOS, vimficiency-bench]` — every `push: branches: [main]` (including PR merges) bench-runs on the spare machine. Concurrency group `bench-main` serializes runs; `CI=true` in the workflow env skips the `flock` single-flight inside the script. The pre-push hook now `continue`s on `main` so the two triggers don't race.
- Trade-off (later removed — see *Benches went main-only* below): feature branches still bench on the dev machine while main benches on the spare, so the two regimes have different absolute numbers — cross-machine baseline comparisons (`bench-compare` against a parent SHA from the other machine) are apples-to-oranges. Within-regime comparisons (main↔main, feature↔feature) are stable. Documented in `dev/ci-and-benchmarks.md`.
- Reconcile machinery deleted: `scripts/bench-reconcile.sh` and the `reconcile-add` subcommand of `bench-data.ts`. Once the runner is live, every main SHA gets a fresh measurement; the back-fill workaround is no longer load-bearing.

## Benches went main-only on the macOS runner (feature-branch hook removed)
- The cross-machine mix above (feature branches on the Linux dev machine, main on the macOS spare) put two hardware regimes on one timeline, so the chart carried an unavoidable apples-to-oranges discontinuity. We removed the non-main half rather than keep annotating it.
- `.githooks/pre-push` (the dev-machine feature-branch bench trigger) and the `core.hooksPath=.githooks` install block in the top-level `CMakeLists.txt` were deleted. `bench-main.yml` on the self-hosted runner is now the only automatic trigger, so every timeline point is a main SHA measured on the one consistent machine.
- `scripts/bench-local-run.sh` is unchanged and still ingests+pushes when invoked by hand; a manual run on a non-main SHA is now the only way an off-main point can land. For throwaway local perf intuition, run `./build/tests/vimfy_benchmarks` directly — it doesn't touch gh-pages.

## No `KeyNotation` tagged type — `std::string`/`std::string_view` IS notation by convention
- Considered introducing a `KeyNotation` tagged string type to catch "raw text leaks into a `Sequence`" at compile time. After auditing, didn't ship it.
- The convention this codebase already follows: any `std::string`/`std::string_view` holding a Vim sequence is in notation form (`<Space>`, `<CR>`, `<lt>`, etc.). Lua normalises raw `vim.on_key` bytes via `vim.fn.keytrans` before they cross the FFI; internal C++ helpers (`displayChar`, `CountToKeys::textForCount`, sequence concatenation) produce notation by construction. The only string-category that needs distinguishing is buffer text — and `Line`/`Lines` already covers that.
- The actual bug class (raw buffer text leaking where notation was expected) is therefore already guarded by the `Lines` boundary. The remaining "raw text → Sequence" path was essentially hypothetical given current data flow.
- Tradeoff: introducing a `KeyNotation` type would have required wrapping every literal at every call site (`KeyNotation("j0")` or a `"j0"_kn` literal), plus updating ~67 test sites and every notation-producing helper to return the typed form. The compile-time safety it adds is asymmetric with peer types (`Sequence`, `Line`, `Token` don't enforce this), and the FFI/test churn is high for a small actual safety gain.
- The header `src/Types/KeyNotation.h` survives, but only as a home for `displayChar(char) → std::string` and `parseDisplayChar(string_view) → optional<char>` free functions. No tagged type, no `_kn` literal, no `Sequence::notation()` accessor.
- If a future case introduces *another* string category that needs distinguishing from notation, type **that** new category, not notation — notation is the default in this codebase.

## Property/safety suites are not timed on the dashboard (coverage, not duration)
- The test-timing dashboard charts **Unit + Approval** wall-time per commit; **Property + Safety are shown as coverage** (suite/case listing + counts), never as a duration trend.
- Why: property/safety run FuzzTest in unit mode, whose wall-time is dominated by fixed per-test overhead, not real work, so a duration trend is pure noise. A *meaningful* signal would require the hardcoded 10000-iteration cap (fixed seed, coverage-free generation — see below) which is minutes-slow per commit, or a source patch to dial the count down. Optimizer performance is already tracked by the dedicated `EditOpt`/`MotionOpt`/`CompositionOpt` benchmarks, so a property-timing chart would at best duplicate and at worst mislead.
- Note for future iterations (so this isn't relitigated): FuzzTest unit mode (`runtime.cc` `RunInUnitTestMode`) DOES honor `FUZZTEST_FUZZ_FOR` and loops `min(10000 iterations, duration)`; generation is pure seeded-PRNG mutation with **no** coverage feedback, so a fixed seed + a duration long enough to hit the 10000 cap yields an identical, workload-stable run across commits. It's just too slow per-commit to be worth charting. `FUZZTEST_MAX_FUZZING_RUNS` is ignored in unit mode (fuzzing-path only); `--fuzz` campaign mode needs sancov instrumentation the normal test build lacks (it aborts).

## FuzzTest unit-mode watchdog patched out via `PATCH_COMMAND`
- We carry `tests/patches/fuzztest-watchdog.patch` applied to fetched FuzzTest (`tests/CMakeLists.txt` `FetchContent_Declare(fuzztest ...)`), pinned to `GIT_TAG 2026-02-19`.
- Why: FuzzTest's unit-mode watchdog (`runtime.cc` `Runtime::Watchdog`) polls with `absl::SleepFor(absl::Seconds(1))`, and `~Watchdog` *joins* that sleeping thread — so every unit-mode test blocks up to ~1s at teardown. Measured: with the generated loop fully disabled (`FUZZTEST_FUZZ_FOR=0`, zero iterations) the property binary took **38.72s/38 = 1.019s/case** and safety **6.01s/6 = 1.002s/case**. It is unconditional (`CreateWatchdog()` always returns a real watchdog unless compiled with Centipede). This floor was also why `FUZZTEST_FUZZ_FOR` couldn't shorten runs below ~1s.
- The patch replaces the single 1s sleep with a poll of `stop_requested_` in 10ms steps, preserving the ~1s `CheckWatchdogLimits` cadence (the real per-test timeout guard) while letting `join()` return in ~10ms. After the patch: property full `FUZZ_FOR=0` dropped 38.72s → **1.15s**, safety 6.01s → **0.075s**, and `FUZZ_FOR` became a real two-way knob (0.1s→0.128s, 0.5s→0.551s). All 44 property+safety tests still pass.
- `PATCH_COMMAND` is idempotent (`git apply -R --check … || git apply …`) since the build dir persists across configures. Trip wire: re-time the property binary at `FUZZTEST_FUZZ_FOR=0` — must be ~1s total, not ~38s. Revisit the patch on any FuzzTest `GIT_TAG` bump (context lines may shift).

## Scroll commands: search `<C-d>`/`<C-u>` (oracle-exact), exclude `<C-f>`/`<C-b>` (viewport-dependent)
- The NavOptimizer search emits only `<C-d>`/`<C-u>` (`MovementToSpec.cpp` `SCROLL_MOTIONS`). Their cursor lands at `min/max(cursor ± 'scroll', buffer-edge)` — a fixed offset independent of the window's topline — so they match Vim exactly. `Verify_ScrollMotions.cpp` oracle-fuzzes them (small buffers, counts, varying scroll) and passes.
- `<C-f>`/`<C-b>` are deliberately NOT searched: full-page scroll places the cursor relative to the window's **topline** (e.g. with the whole buffer visible, `<C-b>` doesn't move), which the minimal-state model doesn't track. The interpreter still *parses* and *approximates* them (jump ~one height, clamp) for replaying captured sessions — fine because the captured cursor is ground truth — but the approximation is not exact (caveat comment in `MovementInterpreter.cpp`).
- Why not model topline to make all four exact: `line('w0')` is already captured in Lua (`util.lua`) but intentionally not plumbed to `NavContext` (its `topRow`/`bottomRow` fields stay commented out). A nowrap topline port (`onepage`/`halfpage`/`update_topline`) is tractable, but Neovim's default `'wrap'` makes scroll operate on *screen* lines, requiring window width + `plines_win` (tabs, double-width, number/sign/fold columns) — effectively Vim's display engine, which contradicts the minimal-state design. Scroll suggestions are also low-value (any vertical jump has a view-independent alternative like `NG`/`Nj`), so the cost/benefit didn't justify it.
- Oracle harness: `NeovimOracle::simulateScroll` sets window-local `'scroll'` (clamped to height in the lua) for one call; `windowHeight()` queries the real height. Headless nvim **can't resize its sole window** (`nvim_win_set_height` is a no-op there), so the property test uses the fixed headless height and mirrors it into `NavContext` rather than varying it. Trip wire: `Verify_ScrollMotions.ScrollMotionsMatchOracle`.
- Migration note: this replaced the hand-written `CountedMiscMotions`/`ScrollMotions` unit tests (removed). `EXPLORABLE_MOTIONS` was found to be vestigial for search (only feeds `ALL_MOTIONS` parsing); doc corrected in `dev/core/keyboard.md`.

## VimDiff collapse decisions are derived, not tuned (2026-08-30)
- `MATCH_MARGIN = 8` and the `collapseRuns` verification knob are gone. Margins and the collapse gate are value comparisons under the cost model: retype (`ins(d)` minus one bigram seam correction) vs the computed per-edge structural slack (`TilingCost::stopSlack`/`startSlack`) plus the exact edge move sweep.
- The slack is a case max over what can cross an edge: counted-chunk split gaps read from the pen tables, `{k}dd`/`{k}dap` splits plus a cover bound of the edge's partial line, `D`/`d0` pieces. It is per-edge because the `D`-through saving grows with the edge's line width — no constant bounds it, which is why the hand-picked margin was unsound on wide lines (the small-buffer A/B could never produce that shape).
- Rejected: `{k}|`/`{k}G` absolute-target oracle pieces — they would make the slack a small constant, but price commands users don't reach for in normal editing.
- Validated at retirement time: 11.9k-case collapse-vs-dense A/B (small alphabets, mutations, wide lines, tall paragraphs), zero plan-1 cost mismatches; the dense baseline and `tests/Debug/SparseVsDense.cpp` were then deleted with the knob. `tests/Debug/VimDiffCostCorpus.cpp` (single-mode format since this change) remains the before/after tool.

## VimDiff deletions are a per-column multi-source sweep, not a table (2026-08-30)
- `relaxDeletes` prices every delete arrival of a DP column in one `O(N·cap)` sweep: an in-progress deletion's value is independent of where it started, so one carried K-best list serves all starts. Replaces the `O(n²·m)` per-cell start pull (and the `del[pi][i]` table) that the readability restructure had introduced; verified cost-identical on the corpus. The breakdown re-prices regions with single-span `query` calls instead of the table.

## VimDiff counted-command cap is the search's own knob (2026-08-30)
- The oracle caps counted chunks at every level — including `{k}dd`/`{k}dap`, previously unbounded — at `CostOptions::maxPrefixCount`, wired live from the same `maxPrefixCount` param the Nav/Transform searches obey; the shared default lives in `CountPrefixLimits::DEFAULT_MAX_PREFIX_COUNT`. One variable on purpose: the old private `CAP = 9` underpriced counts the search can emit (10..16) while pricing line counts (`{150}dd`) nothing downstream can produce.

## VimDiff seals kept runs instead of collapsing them (2026-08-31)
- A matched run the gate proves no optimal plan edits into is a separator, not a cell: every optimal path crosses it with one move, so the alignment splits into independent char-level blocks (`sealMatchedRuns` → `Block{aBegin,aEnd,bBegin,bEnd}`). This deleted the pruned↔raw coordinate layer (`PrunedUnits`, unit hashes, `prunedAt`) entirely.
- Crossing a seal is `CROSS` transitions from the previous block's trailing matched diagonal to the next block's leading one, each priced by one raw sweep — so a region-to-region move costs exactly one `move` query over the whole gap, as before; no artificial split at margin boundaries.
- No deletion crosses a seal (that would retype the core, which the gate excluded), so deletion sweeps are block-local and the planner is diff-bound. This closes the "exact vs diff-bound" tension without a cross-run oracle. Release `VimDiffPlan/BufferSize/100`: 292 → 14 ms.
- Verified: plan-1 costs identical on all 3,325 corpus lines; the only K-best plans lost are those retyping an entire sealed core (dominated by construction).
