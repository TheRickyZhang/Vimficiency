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


# Session recall model

## Unifying key-count and time-based into one recall ring

Originally there were three session categories — manual (alpha aliases),
time-based (dot aliases `.`/`..`/`...` storing finished bursts), and
key-count (rolling `1`, `2`, ...). Time was driven by idle-fire: a burst
started on first keypress, ended after `TIME_IDLE_MS` of inactivity, and
stored under rotating dot aliases.

The asymmetry was awkward. Key-count let users say "analyze the last N
keystrokes" with free N, but time-based was limited to "the last burst,"
"the one before that," and so on — no way to say "analyze the last 6
seconds." That missing symmetry was the real gap.

**Decision:** collapse to a single rolling-recall primitive indexed by
either keys or seconds:

- `:Vimfy end 3`   → 3 keystrokes ago
- `:Vimfy end 3s`  → 3 seconds ago
- `:Vimfy end <name>` (alphabetic) → manual named session

The dot alias model (`.`, `..`, `...`) is removed. Nothing in it was
worth preserving once rolling time-recall with free parameter exists.

### Implementation note: no live suffix-cost cache

Considered maintaining cost for every possible suffix window live. Not
feasible: the optimizer is A* search, and each new key shifts the goal
state, invalidating most of the explored tree. Can't incrementalize.
The primitive stays "recompute on trigger," with caching only of the
most recent result per window.

### Implementation note: time recall piggybacks on the key ring

Storing periodic buffer snapshots for arbitrary time rewind is expensive
and unnecessary. Every captured key already gets buffered with its start
state in the key-count ring. Tagging each key with a timestamp lets
`Ns` resolve to "count keys whose timestamp > now - N seconds," which
reduces to the existing key-count lookup. O(log n) or O(n) scan over a
bounded ring; cost negligible next to the optimizer run that follows.

### Implementation note: command-boundary snapping

`vim.on_key` captures every keystroke, not commands. A raw time window
may start mid-command (e.g., between `d` and `aw`), which would produce
noisy input to the optimizer. Every recall record captures the Vim
mode at creation time (`first_mode`) so we can cheaply tell whether a
given record starts on a clean normal-mode command boundary. We
resolve `:Vimfy end Ns` by first finding the youngest record whose
timestamp is at-or-before the cutoff, then snapping that index
*backward* (toward older records) to the nearest clean boundary. The
end of the window is "now" — presumed clean because the user just
typed `:Vimfy end`.

Backward, not forward: if the user started a multi-key command just
before the cutoff (e.g., the `d` of a `d{` that straddles the
boundary), including the opening of that command is what "the last N
seconds" charitably means. Forward-snapping would silently chop the
head off such commands. `SNAP_LOOKBACK_KEYS` caps how far the window
may stretch when every intervening record is mid-command.

This matters more for time recall than key recall: a user saying "last
6 keys" knows exactly which keys they mean; a user saying "last 3
seconds" means "whatever commands I ran in that window." Snapping is
the charitable reading.

### Trade-offs accepted

- Breaking change: existing `.` alias users must migrate to `Ns`. No
  back-compat shim planned; the new primitive is strictly more general.
- Time window can straddle an idle gap (3s covering 10 actual keys) or
  a fast burst (3s covering 200 keys). Documented, not prevented —
  users choose time-recall precisely when they can't estimate key count.


# Auto-suggest triggers

## Separating "suggestion surfacing" from "session category"

Originally, time-based sessions auto-fired a notification on idle; this
was *the* auto-suggestion mechanism and was tightly coupled to that
category. Collapsing the session model into pure recall broke that
coupling — "when to surface a suggestion" is a separate concern from
"how to identify a window of recent edits."

**Decision:** `auto_suggest` is a top-level config subtable whose keys
are trigger names. Presence-of-subtable = trigger enabled; absence =
disabled. No explicit `enabled = true/false` flags, no sentinel values
(`math.huge`, `0`) to mean "off."

```lua
auto_suggest = {
    idle = { ms = 3000, window = "3s" },
    -- keys = { every = 50, window = "50" },   -- omit = off
    -- cost = { m = 1.5, b = 2.0 },            -- omit = off
},
-- or: auto_suggest = false   (master off switch)
```

### Why presence-not-sentinels

Considered using sentinel values (e.g., `idle_ms = math.huge` = off).
Rejected:
- `cost` has two params (`m`, `b`). Setting `m = math.huge` leaves `b`
  semantically orphaned. Presence-semantics generalizes.
- Skim-ability: a config where "huge number = disabled" creates "why
  isn't this firing?" debug sessions later.
- The validator can reject unknown trigger keys loudly, which doubles
  as a reservation mechanism for `keys` and `cost` before they land.

### Trigger semantics

- `idle`: fire after `ms` of no user keystrokes. Analyze `window`.
- `keys`: fire every `every` user keystrokes since the previous fire.
  Analyze `window`.
- `cost`: fire only when `user_cost > m * optimal_cost + b` for the
  most recent window. Requires running the optimizer to check.

Multiple triggers = OR. Any trigger firing surfaces the result.

## Cost trigger: concurrency and scheduling

Cost is the most interesting trigger — "surface only when you were
wasteful" is far better signal than "surface every pause." But checking
cost requires running the optimizer, which is ~50-200ms per call on
reasonable inputs. Can't run synchronously on every keystroke (100ms
keystroke latency = unusable).

### Rejected: live suffix-cost cache

Same reason as above: A* search doesn't incrementalize.

### Scheduling options

| Mode              | Behavior                                                      | When it misses                |
|-------------------|---------------------------------------------------------------|-------------------------------|
| Sync on idle only | Only on pause, blocks                                         | Misses mid-activity signal    |
| Debounced async   | Each key schedules a check in ~300ms; cancel if new key       | Sweet spot                    |
| Sampled async     | At most one check every T ms; drop requests during in-flight  | Coarser; graceful under load  |
| Sync per-key      | Block until result                                            | Rejected — 100ms latency      |

**Decision for cost trigger (future):** debounced async at ~300ms. This
fires naturally at micro-pauses between commands without firing on
every keystroke.

### Concurrency options for cost checks

| Approach                  | What it takes                       | Trade-offs                                        |
|---------------------------|-------------------------------------|---------------------------------------------------|
| `vim.uv.new_work()`       | LuaJIT FFI from a work thread       | Fragile — LuaJIT/libuv/cdata has sharp edges     |
| Subprocess via `vim.system()` | CLI harness reading JSON stdin / writing stdout | Cleanest. Cancellable by kill. ~2ms fork. |
| C++ thread + Lua-side poll | `analyze_async` + completion flag   | Fast but grows shared-state surface               |

**Decision when we get there:** subprocess first. 95% of the
performance at 20% of the complexity, and Neovim tooling standardizes
on this pattern. Revisit C++ thread-pool only if subprocess overhead
shows up as a measured bottleneck.

### Pre-filter considered and rejected

Early draft proposed gating cost checks on a cheap lower-bound
(`user_cost <= m * raw_key_count + b` → skip the optimizer). Dropped:
raw keystroke count is not a provable lower bound on optimal cost
under the effort model. A *safe* skip would need a bound comparable
in cost to running the optimizer itself. Not worth being clever here
— ship correct, revisit if debounced subprocess calls ever show up
as measured overhead.

## Dedup and cooldown

Idle triggers will re-fire on every pause. Without suppression, a user
who idles twice in the same spot gets the same recommendation notified
twice. Spec:

```
fingerprint = hash(user_sequence, start_buf_hash, best_result_hashes)
cooldown_ms = 5000   -- configurable via auto_suggest.cooldown_ms
```

Suppress iff `fingerprint == last_fire.fingerprint` *or*
`now - last_fire.time < cooldown_ms`. Fingerprint catches "same thing
again"; cooldown catches rapid re-triggers from adjacent pauses.

Applies to all triggers uniformly; not a per-trigger option.

## Staging: ship `idle` first

For the first implementation pass:
- `idle` trigger only. Sync on fire (we're already idle, blocking is
  fine).
- Config validator rejects `keys` and `cost` with an explicit "not yet
  implemented" message. Reserves the names loudly so they can't be
  confused with typos or silently ignored.
- `cost` async infrastructure (subprocess harness, pre-filter) lands
  when the feature lands. Not worth building ahead of need.


# Session menu (future)

Planned as a first-class UI over `session_store`: shows active manual
sessions, recent finished results, and lets the user pick one to view
or simulate.

## Session summary view-model

Active sessions and finished results have different shapes in storage.
Direct rendering from either forces UI code to branch on type. Fix
with a normalized summary record *before* any UI work starts:

```lua
---@class SessionSummary
---@field id          string
---@field kind        "manual" | "recall"
---@field alias       string|nil       -- current alias if any
---@field status      "active" | "finished"
---@field start_time  number
---@field end_time    number|nil
---@field key_count   integer
---@field preview     string           -- first ~20 chars of user_seq
---@field result      ResultSession|nil
```

`session_store.summarize(id) -> SessionSummary` is the single API the
menu and `:Vimfy list` consume. New session shapes (future recall
sub-kinds, etc.) update this record in one place.

## UI abstraction prerequisite

Before building the session menu, extract common floating-window
primitives from `util.lua` / `simulate.lua` into a `ui.lua` module.
Three consumers (results display, simulate animation, session menu)
all want: floating-window creation with a managed buffer, a small
keymap set, close-on-leave handlers. Doing this once avoids a third
copy of the same scaffolding.

Not built yet. Mentioned here to prevent the session menu from landing
as a fourth copy.
