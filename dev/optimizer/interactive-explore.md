# Interactive Explore v1

Interactive explore is a scratch-buffer workflow for stepping through a fixed
goal chosen up front by the user.

It is not a generic goal-less explorer, and it is not a real-buffer live
interceptor. The scratch buffer is the only mutable surface during a session.

## Scope

v1 supports:

- one active explore session at a time
- a fixed `(goalLines, goalPos)` captured at session start
- a fixed composition edit order chosen once at session start
- tolerant motion exploration while approaching the current edit
- guarded edit execution for the current edit only
- strict insert continuation inside an accepted edit path
- session-owned undo/redo

v1 explicitly does not support:

- replanning after edit-order deviation
- multiple concurrent sessions
- arbitrary raw editing in the real buffer during the session
- accepting semantically-valid-but-unsupported post-edit cursor variants

## Buffer model

The session starts by cloning the target buffer into a scratch buffer and moving
the cursor there.

The scratch buffer:

- is the only surface mutated during explore
- has native undo disabled; session undo/redo is authoritative
- copies a fixed option set from the source buffer at session start

The real buffer remains unchanged until commit or cancel.

### Async session open

The composition plan is the heavy, blocking part of opening a session, so it is
computed on a C++ worker thread instead of synchronously across the FFI (Neovim
is single-threaded; a blocking FFI call freezes the UI). `vf_explore_start_async`
spawns the worker and returns a job id; the Lua controller opens the scratch tab
immediately with a "Computing…" placeholder and polls `vf_explore_poll` on a
`vim.uv` timer. When the worker finishes, poll builds the `View` from the
precomputed plan (the precomputed-plan `View` constructor — never a half-ready
View) and returns its id; the controller then installs the interactive handlers
and renders. Closing or cancelling mid-compute calls `vf_explore_cancel`.

The worker's search honours a `SearchControl` (cancel flag + optional deadline,
checked in the composition A* loop). Cancel powers prompt teardown; the deadline
(`compute_deadline_ms`, an explore setting; `0` = run to the natural caps) bounds
worker latency by returning best-so-far. Responsiveness comes from the search
being off-thread — the deadline only bounds how long the panel takes to fill,
not the main thread, which only ever does an O(1) poll.

### Copied options

At session start, copy these source-buffer settings into the scratch buffer:

- `shiftwidth`
- `tabstop`
- `softtabstop`
- `expandtab`
- `iskeyword`
- `matchpairs`
- `virtualedit`
- `filetype`

Copy these source-window settings into the scratch window for display parity:

- `number`
- `relativenumber`
- `cursorline`
- `wrap`

This list is intentionally explicit. Non-listed options may still cause visual
or semantic drift and can be added later if testing shows they matter.

## Commit and cancel

Commit is allowed only when `View::isCompleted()` is true.

Commit policy for v1:

- require that the original real buffer is still valid and unchanged since
  session start
- if the real buffer drifted, reject commit
- on success, replace the real buffer contents with the scratch contents in a
  single undo block
- restore the committed cursor position

Replay is not used for v1 commit. We commit the final scratch state directly.

Cancel is always allowed and discards the scratch buffer and session state
without mutating the real buffer.

## High-level model

Composition remains the planner. Interactive explore tracks one chosen edit
order and exposes local actions inside that plan.

For each session:

1. Compute composition diffs and choose the ordered edit list.
2. Build intermediate buffer states between edits.
3. While edit `i` is unfinished:
   - suggest motion actions that move toward valid entry positions for edit `i`
   - once at a valid entry position, allow only edit-start actions for edit `i`
   - if the chosen edit enters insert mode, enforce the required typed suffix
4. Once the session reaches the final fencepost and cursor goal, completion is
   true.

### Composition compatibility boundary

Explore does not consume an arbitrary optimizer result. Its compatibility
boundary is one planned edit at a time:

- current diff
- pre-edit fencepost
- valid edit-start set / post-edit cursor data
- post-edit fencepost

In code this boundary is `CompositionResult::plannedEditAt(i)`. Optimizer
refactors may change internal storage, search strategy, or debug surfaces
freely, but must preserve the semantics of that bundled per-edit view if
Explore is to remain compatible.

### Frontier contract

Every frontier emits exactly one immediately executable Vim action token per
recommendation. No motion prefix, no typed payload, no trailing `<Esc>`,
no multi-token macro. Duplicate tokens across emission lanes are programmer
errors and abort at runtime in both debug and release builds via the
release-active `CHECK` macro.

- **NavFrontier** — one movement token toward an executable edit region.
  Per-cell dedup is bounded by `maxResultsPerEndPos`; a global pairwise-distinct
  `CHECK` runs at the end of `rankNavFrontier`.
- **CompositionFrontier** — one direct edit-bearing token valid from the
  current cursor:
  - Pure-insertion shortcut: `i / a / I / A / o` when the cursor lies
    inside the strategy's insertion column range.
  - Bracket/quote text-object structural: `di" / da{ / ci( / ca[ / ...`
    when the cursor is at the enumerated `(line, col)` of a visible
    bracket/quote pair.
  - Bare `J` when the cursor is on the join entry line and one `J` makes
    progress on the planned diff without exactly reaching the post-edit
    fencepost (the explorer J lane in TransformFrontier owns that exact case).
- **TransformFrontier** — one Normal-mode action from a planned Transform
  start cursor: `dw`, `cl`, `rX`, bare `J` that reaches the fencepost, etc.
  The lane separation between explorer-emitted J and the progress-only J
  is structural (fencepost-equality decides), not stateful.

After an emitted token is executed, Explore re-derives phase from the
observed buffer/cursor/mode (`acceptSnapshot` / `phaseForCursor`). No hidden
continuation metadata is carried in the recommendation.

Tradeoffs and known scope holes:

- Multi-action change forms such as `dwi` are exposed as delete-first progress
  (`dw`); Explore re-queries after the observed deletion.
- The full batch `TransformOptimizer` and `CompositionOptimizer` results still
  emit multi-token sequences (`dwi...<Esc>`, full join+cleanup plans). Frontier
  vs batch result shapes intentionally differ — frontier is interactive,
  batch is the end-to-end optimization output.
- Visual-delete shortcuts (`v{motion}d`) stay in batch results only; see
  `dev/architecture/todo.md` item 6 for the threshold to surface them
  interactively.
- The programmatic `View::applyEdit` accepts single Transform and Composition
  action tokens (insertion, text-object, J) from any planned-edit-target phase.
  Full live sequences with typed payloads go through `acceptSnapshot`. Lookup
  lives in `EditHandler::applyEdit`; simulation uses the canonical Vim
  interpreter (`Edit::applyEdit`).

## Phase machine

`Navigate`

- Motion-oriented phase. With a planned edit index, the cursor is outside the
  executable edit-start set and recommendations include movements toward that
  set.
- `Navigate(totalEdits)` is the post-final-edit cursor segment. Pure-motion
  sessions start there.

`Transform(i)`

- Normal-mode edit phase for planned edit `i`.
- Edit recommendations are available only at executable edit starts reported by
  `plannedEdit.transformResult.resultsAt(...)`. The textual diff range is a
  render/target-range concept, not Transform eligibility.
- Physical snapshots can also expose delete-first replacement intermediates.
- Unsupported edits are rejected and do not mutate session state.

`Insert(i)`

- Insert-mode continuation for planned edit `i`. Entry is accepted only when
  the live cursor is still inside the expected planned edit scope.
- Leaving insert mode is accepted only when the resulting buffer matches the
  planned post-edit fencepost.

Completion is not a runtime phase. It is derived from
`Navigate(totalEdits)`, final text, and `goalPos`.

There is no runtime `Invalid` phase. User-facing failures return `Rejected`
with state unchanged; programming-invariant failures assert.


State diagram:

```text
Start
  -> Navigate(totalEdits)  if text matches goal
  -> Navigate(0)           if text differs and cursor is outside edit 0
  -> Transform(0)          if text differs and cursor is inside edit 0

Navigate
  -- motion to goal ------> isCompleted() true
  -- other motion --------> Navigate
  -- text change ---------> Rejected

Navigate(i)
  -- motion outside range -> Navigate(i)
  -- motion inside range --> Transform(i)
  -- matching buffer -----> Navigate/Transform(i+1)
  -- in-scope insert -----> Insert(i)
  -- out-of-scope insert -> Rejected, revert scratch, stay
  -- unsupported edit ----> Rejected, stay

Transform(i)
  -- in-range motion -----> Transform(i)
  -- out-of-range snapshot -> Navigate(i)
  -- delete prefix -------> Transform(i) with partial edit span
  -- matching transform --> Navigate/Transform(i+1)
  -- enters insert -------> Insert(i)
  -- unsupported edit ----> Rejected, stay

Insert(i)
  -- matching exit -------> Navigate/Transform(i+1)
  -- abandoned insert ----> prior Navigate/Transform(i)
  -- wrong buffer --------> Rejected, revert scratch, prior phase
```

## Deviation policy

v1 is tolerant for motions and guarded for edits.

Allowed:

- a motion that moves away from the best current approach path
- a motion that overshoots and requires later correction
- while transforming, a motion that leaves the executable start and returns to
  Navigate for the same planned edit
- a physical snapshot that moves from Transform back out to Navigate
- a delete-first prefix of a replacement before entering Insert

Rejected but non-fatal:

- an edit that does not belong to the current edit index
- an edit started from an unsupported position
- insert-mode entry outside the planned edit scope
- an insert-mode exit whose buffer does not match the post-edit fencepost

Internal invariant failures assert instead of becoming a phase.

Lua also runs a defensive post-recovery invariant check against the live
scratch buffer, cursor, mode, and backend state. Normal `Rejected` actions
emit a warning notification and snap back before this fires; the large visible
warning pane is reserved for states that remain inconsistent after recovery.

## Edit acceptance and completion

`resultsAt(line, col)` is only a start-position filter. v1 remains defensive
after an accepted edit start:

- the resulting scratch state must match the expected next composition
  fencepost state
- if the post-state is semantically plausible but not representable by the
  current composition continuation, reject it for v1

This keeps later edit phases from inheriting hidden state drift.

## Undo and redo

Only user-meaningful accepted actions push a new session snapshot.

- accepted action: push old snapshot to undo and clear redo
- Insert and partial delete-prefix states: push nothing
- rejected action: push nothing and clear nothing
- invalidation: push nothing

Scratch native undo is disabled. Session undo/redo is the only supported undo
mechanism during explore. History follows Vim change boundaries: a replacement
typed as `x` then `i...<Esc>` undoes back to the pre-edit Transform state, not
the delete-prefix intermediate.

## Recommendation contract

`Navigate`

- return a ranked list of motion candidates for the final cursor goal

`Navigate(i)`

- return a ranked list of movement-oriented candidates for the current planned
  edit boundary

`Transform(i)`

- return edit candidates at the cursor, plus systematic delete-first
  replacement alternatives

`Insert`

- return the remaining typed text without trailing `<Esc>`
- completed sessions return an empty recommendation list

## Lua/controller responsibilities

The Lua scratch controller is responsible for:

- creating and placing the scratch window
- copying the explicit option set
- disabling native scratch undo
- driving the async open: polling the worker job and installing the view (plus
  interactive handlers) once the plan is ready (see [Async session open](#async-session-open))
- forwarding live buffer/cursor/mode snapshots and normalized key evidence
- reverting scratch state after rejected insert-mode edits
- enforcing commit/cancel lifecycle

The C++ session core is responsible for:

- phase transitions
- accepted/rejected action semantics
- deriving recommendations from the live snapshot state
- undo/redo history

## Implementation order

1. Pure session state machine + undo/redo + tests
2. Scratch buffer controller and lifecycle in Lua
3. Motion-side local recommendation generation
4. Composition session orchestration over the ordered edit plan
5. Edit-start validation and strict insert continuation
6. Commit/cancel flow
