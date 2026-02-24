# Keyboard

## Associating Sequence, PhysicalKeys, RunningEffort
All these three can be tied to the same "command", and necessarily tracked in sync. Putting Sequence and Physicals into a KeyedSequence is pretty straightforward, but a tension is that RunningEffort depends on config, which must be runtime.

We ideally want to unify these when possible, and use notions of Command::K to refer to {"k", Key::Key_K, effort(Key_K)}. So we keep predefined KeyedSequences, and build an array indexed by KSId (KeyedSequence ID) at each Optimizer construction. 

Then, we can use a SequenceBinding everywhere, and explicitly referring to {KeyedSequence (static), RunningEffort (indexed)} on construction. This seems to be best of all worlds

## Code organization
We completed a structural cleanup to enforce explicit module boundaries in `src/` and prevent dependency drift:

- Consolidated shared value ownership under `src/Types/`:
  `Position`, `Range`, `LineRange`, `Mode`, `Sequence`, `EdgeType`,
  `LineEdgeType`, `SentenceEdgeType`, and `LandingType`.
- Moved `NavContext` from `Editor` to `Types` as shared runtime context state.
- Migrated boundary/value data holders from `Utils` to `Types`:
  `Lines`, `BracketFlags`, `QuoteFlags`, and `NoChar`.
- Kept sequence ownership split:
  `Sequence` type stays in `Types`, while stream formatting implementation
  lives in `Interpreter` because it depends on higher-level sequence parsing helpers.
- Moved `RepeatMotionResult` to `src/Optimizer/MotionOptimizer/BufferIndex.h` because it is
  an optimizer/index query result type rather than a shared core primitive.
- Moved count-search motion pair ownership to MotionOptimizer via
  `src/Optimizer/MotionOptimizer/CountableMotionPair.h`, removing semantic
  count-landing coupling from `Keyboard`.
- Moved config type ownership to Keyboard (`src/Keyboard/Config.h`), with
  `src/Keyboard/Config.h` kept as a thin forwarding include for compatibility.
- Moved `SequenceBinding` ownership to optimizer-wide scope
  (`src/Optimizer/SequenceBinding.h`), removing it from `State`.
- Moved indentation helpers from `Utils` to `Optimizer`
  (`src/Optimizer/Indentation.h`) to avoid upward dependency from `Utils`.
- Added dependency enforcement:
  `scripts/lint-module-deps.sh` plus CI gating in `.github/workflows/bench.yml`.
- Updated dependency lint and architecture docs to treat `Types` as the
  base module and disallow new upward dependencies from `Types`.
- Replaced the mixed `Editor` layer with clearer module ownership:
  `src/Interpreter/` for arbitrary command parsing/interpreting adapters
  (`EditInterpreter`, `MotionInterpreter`, `SequenceParser`) and
  `src/Session/` for snapshot I/O (`Snapshot`).
- Moved `SequenceChunker` out of `src` into `tests/Exploration/` because it is
  currently exploration/test tooling only.
- Grouped keyboard command-to-key mapping declarations under
  `src/Keyboard/ToKeys/` (`CharToKeys`, `CommandToKeys`, `CountToKeys`,
  `MotionToKeys`, `EditToKeys`, and primitives) to separate mapping catalogs
  from keyboard value types.
- Renamed `SequenceTokenizer` to `SequenceToKeys` and moved it to
  `src/Keyboard/ToKeys/` to clarify that this layer maps command strings to
  physical key sequences for effort computation, not Vim semantic parsing.
- Folded `fingerToHand`/`sameHand` into `src/Keyboard/Finger.h` and removed
  `KeyboardUtils.h` so finger/hand relationship helpers live with the finger
  type definitions.
- Dissolved `src/State/` and moved state ownership into optimizer modules:
  `MotionState` under `src/Optimizer/MotionOptimizer/`, `EditState` under
  `src/Optimizer/EditOptimizer/`, and `CompositionState` under
  `src/Optimizer/CompositionOptimizer/`.
- Moved `EffortBank` to effort scope (`src/Effort/EffortBank.h`)
  since it is a config-scoped typed-effort cache shared across optimizers.
- Moved effort modeling to its own module (`src/Effort/RunningEffort.*`) to
  separate keyboard primitives from runtime effort aggregation.
- Moved `PosKey` to `src/Types/PosKey.h` as a shared positional value type.
- Removed the `CommandSequence` wrapper and moved sequence display formatting to
  `src/Interpreter/SequenceFormatting.*` as interpreter-level parsing/formatting
  functionality.


# EditResult

## Recording EditResult Answer
Ideally, like in MotionOptimizer, we simply record answer when a goal state is popped from the stack (guaranteed lowest cost). But we have a wrinkle with delete -> change conversions, as we would need to adjust in advance.

Several methods keeping an inverted order were tried, but in the end, guaranteed correctness is worth checking for a goal state twice. It may be possible to add a bool isGoal to trade memory in state for a faster branch check.

### Maintaining EditBoundary

### Some searches get starved (Not adequately explored)
Because of inadmissible heuristic, may not ever consider some branches

## Hashing lines in EditOpitmizer

## GoalSuffix
Beneficial to reuse results. With improvements to goal reach correctness and buffer hash, it is much faster.
