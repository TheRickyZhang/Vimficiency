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

In code this boundary is `CompositionResult::stepAt(i)`. Optimizer refactors
may change internal storage, search strategy, or debug surfaces freely, but
must preserve the semantics of that bundled per-edit view if Explore is to
remain compatible.

## Phase machine

`Completed`

- Goal reached. Recommendations are empty. UI should offer commit/cancel.

`Invalid`

- The controller can no longer trust its state. Recommendations are an error.

`ApproachEdit(i)`

- Motions are always allowed on the scratch buffer.
- Recommendations are motion/edit candidates for the current planned edit `i`.
- Unsupported edits are rejected and do not mutate session state.

`PendingInsert(i, remainingTypedText)`

- Exactly one recommendation exists: continue the required typed text, or exit
  insert mode once `remainingTypedText` is empty.
- Insert validation is prefix-based, not keystroke-based. A chunk like `"foo"`
  is accepted if it matches the next prefix of the remaining required text.
- Leaving insert mode is accepted only when `remainingTypedText` is empty.

State diagram:

```text
Start
  -> Completed                  if initial already matches goal
  -> ApproachEdit(0)           otherwise

ApproachEdit(i)
  -- motion ------------------> ApproachEdit(i)
  -- valid edit, no insert ---> ApproachEdit(i+1) or Completed
  -- valid edit, enters ins --> PendingInsert(i, remaining)
  -- unsupported edit -------> Rejected, stay
  -- external desync --------> Invalid

PendingInsert(i, remaining)
  -- matching text prefix ---> PendingInsert(i, rest)
  -- valid mode exit --------> ApproachEdit(i+1) or Completed
  -- wrong text / early exit -> Rejected, stay
  -- external desync --------> Invalid
```

## Deviation policy

v1 is tolerant for motions and guarded for edits.

Allowed:

- a motion that moves away from the best current approach path
- a motion that overshoots and requires later correction

Rejected but non-fatal:

- an edit that does not belong to the current edit index
- an edit started from an unsupported position
- typed insert content that does not match the required continuation
- leaving insert mode before the required text is complete

Fatal (`Invalid`) only when the controller loses authority over the scratch
state or hits an internal invariant failure.

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

`ApproachEdit(i)`

- return a ranked list of motion/edit candidates for the current edit

`PendingInsert`

- return one recommendation representing the required continuation

`Completed`

- return an empty recommendation list with `Completed` phase

`Invalid`

- return an error

## Lua/controller responsibilities

The Lua scratch controller is responsible for:

- creating and placing the scratch window
- copying the explicit option set
- disabling native scratch undo
- observing insert text / insert leave events
- reverting scratch state after rejected insert-mode edits
- re-entering insert mode after an early insert exit rejection
- enforcing commit/cancel lifecycle

The C++ session core is responsible for:

- phase transitions
- accepted/rejected action semantics
- undo/redo history
- invalidation
- later, local recommendation generation

## Implementation order

1. Pure session state machine + undo/redo + tests
2. Scratch buffer controller and lifecycle in Lua
3. Motion-side local recommendation generation
4. Composition session orchestration over the ordered edit plan
5. Edit-start validation and strict insert continuation
6. Commit/cancel flow
