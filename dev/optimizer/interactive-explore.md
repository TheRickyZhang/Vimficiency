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

Commit is allowed only when the session is `Completed`.

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
4. Once the session reaches the final fencepost state, mark it `Completed`.

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

## Phase machine

`Navigate`

- Motion-oriented phase. With a planned edit index, the cursor is outside the
  current edit range and recommendations include movements toward that range.
- For a pure motion goal, `Navigate` carries no edit index and points directly
  at the final cursor goal.

`Transform(i)`

- Normal-mode edit phase for planned edit `i`.
- Edit recommendations are available at the current cursor. Movements are still
  accepted, but only while they stay inside the current edit range.
- Unsupported edits are rejected and do not mutate session state.

`Insert(i)`

- Insert-mode continuation for planned edit `i`.
- C++ stores only the planned edit index. Lua owns the live typed-text suffix
  because it is derivable from the scratch buffer and the matched edit
  recommendation's `typed_text`.
- Leaving insert mode is accepted only when the resulting buffer matches the
  planned post-edit fencepost.

`Completed`

- Goal reached. Recommendations are empty. UI should offer commit/cancel.

There is no runtime `Invalid` phase. User-facing failures return `Rejected`
with state unchanged; programming-invariant failures assert.


State diagram:

```text
Start
  -> Completed             if text and cursor already match goal
  -> Navigate              if text matches but cursor differs
  -> Navigate(0)           if text differs and cursor is outside edit 0
  -> Transform(0)          if text differs and cursor is inside edit 0

Navigate
  -- motion to goal ------> Completed
  -- other motion --------> Navigate
  -- text change ---------> Rejected

Navigate(i)
  -- motion outside range -> Navigate(i)
  -- motion inside range --> Transform(i)
  -- matching buffer -----> Navigate/Transform(i+1) or Completed
  -- unsupported edit ----> Rejected, stay

Transform(i)
  -- in-range motion -----> Transform(i)
  -- out-of-range motion -> Rejected, stay
  -- matching transform --> Navigate/Transform(i+1) or Completed
  -- enters insert -------> Insert(i)
  -- unsupported edit ----> Rejected, stay

Insert(i)
  -- matching exit -------> Navigate/Transform(i+1) or Completed
  -- abandoned insert ----> prior Navigate/Transform(i)
  -- wrong buffer --------> Rejected, revert scratch, prior phase
```

## Deviation policy

v1 is tolerant for motions and guarded for edits.

Allowed:

- a motion that moves away from the best current approach path
- a motion that overshoots and requires later correction
- while transforming, a motion that stays inside the current edit range

Rejected but non-fatal:

- an edit that does not belong to the current edit index
- an edit started from an unsupported position
- a transform-phase motion that leaves the current edit range
- an insert-mode exit whose buffer does not match the post-edit fencepost

Internal invariant failures assert instead of becoming a phase.

## Edit acceptance and completion

`resultsAt(line, col)` is only a start-position filter. v1 remains defensive
after an accepted edit start:

- the resulting scratch state must match the expected next composition
  fencepost state
- if the post-state is semantically plausible but not representable by the
  current composition continuation, reject it for v1

This keeps later edit phases from inheriting hidden state drift.

## Undo and redo

Only accepted actions push a new session snapshot.

- accepted action: push old snapshot to undo and clear redo
- rejected action: push nothing and clear nothing
- invalidation: push nothing

Scratch native undo is disabled. Session undo/redo is the only supported undo
mechanism during explore.

## Recommendation contract

`Navigate`

- return a ranked list of motion candidates for the final cursor goal

`Navigate(i)`

- return a ranked list of motion/edit candidates for the current planned edit

`Transform(i)`

- return edit candidates at the cursor, with motion backfill only inside the
  current edit range

`Insert`

- return an empty recommendation list; Lua renders the live typed-text suffix

`Completed`

- return an empty recommendation list

## Lua/controller responsibilities

The Lua scratch controller is responsible for:

- creating and placing the scratch window
- copying the explicit option set
- disabling native scratch undo
- observing insert text / insert leave events
- reverting scratch state after rejected insert-mode edits
- enforcing commit/cancel lifecycle

The C++ session core is responsible for:

- phase transitions
- accepted/rejected action semantics
- undo/redo history
- later, local recommendation generation

## Implementation order

1. Pure session state machine + undo/redo + tests
2. Scratch buffer controller and lifecycle in Lua
3. Motion-side local recommendation generation
4. Composition session orchestration over the ordered edit plan
5. Edit-start validation and strict insert continuation
6. Commit/cancel flow
