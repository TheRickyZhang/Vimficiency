# Count Penalty Model

Count penalties model cognitive overhead for counted commands (`{n}w`, `{n}dd`, `{n}gJ`, ...).
They are added on top of physical key effort.

## Formula

For each `CountClass`, penalty is:

`base + countSlope * countSlopeUnits(count - 1) + spanSlope * max(0, span)`

`countSlopeUnits` is concave and piecewise linear: each extra unit counts fully
for the first `COUNT_FULL_SLOPE_UNITS` (4), half for the next
`COUNT_HALF_SLOPE_UNITS` (10), and a fifth beyond. So `5dd` costs `4 * 0.5`,
`15dd` costs `9 * 0.5`, `40dd` costs `14 * 0.5`. The shape is shared by every
class; `countSlope` scales it. Piecewise linearity is load-bearing for
`VimDiff`: it lets a counted command be priced as a start cost plus a per-unit
slope (see `dev/optimizer/diff-generation.md` § Complexity).

Rules:
- `count <= 1` always returns `0.0`.
- `span < 0` is clamped to `0`.
- If runtime overrides are disabled, compile-time defaults from `CountPenaltySpec` are used.

## Where It Is Applied

### NavOptimizer

Counted motion emitters apply penalty at creation time:
- `w/e/b/ge` -> `CountClass::MovementWord`
- `W/E/B/gE` -> `CountClass::MovementBigWord`
- `{/}` -> `CountClass::MovementParagraph`
- `(/)` -> `CountClass::MovementSentence`

### TransformOptimizer

`TransformExplorer` counted emitters apply penalty into the emitted `RunningEffort`:
- `{n}dd` -> `CountClass::EditLine`
- `{n}J`, `{n}gJ` -> `CountClass::Join`
- `{n}de/{n}dw/{n}db/{n}dge` -> `CountClass::EditWord`
- `{n}dE/{n}dW/{n}dB/{n}dgE` -> `CountClass::EditBigWord`
- `{n}x` -> `CountClass::EditChar`

For full-edit mode (`ModePolicy<false>`), goal-conversion paths also preserve counted penalties when converting `d...` to change-prefixed forms.

### VimDiff

The diff planner's tiling oracle prices every counted chunk as digit keystrokes
plus `runtimeCountPenalty` for the level's class (`EditChar`/`MovementChar`
through `EditParagraph`/`MovementParagraph`), so planner and downstream search
charge counts identically.

## Runtime Overrides

Global runtime state lives in `GlobalRuntimeOptions`:
- `useCountPenaltyOverrides`
- `countPenaltyOverrides[CountClass]` (partial override per class)

`runtimeCountPenalty<C>(...)` is the canonical helper that resolves:
- override path (if enabled), or
- compile-time default spec.

## Lua/FFI Configuration

Lua config supports:
- `use_count_penalty_overrides = true|false`
- `count_penalty_overrides = { [CountClassOrName] = { base=?, count_slope=?, span_slope=? } }`

Example:

```lua
require("vimficiency").setup({
  use_count_penalty_overrides = true,
  count_penalty_overrides = {
    MotionWord = { base = 2.0, count_slope = 0.8 },
    EditLine = { base = 1.5, span_slope = 0.2 },
    Join = { base = 3.0 },
  },
})
```

If `count_penalty_overrides` is provided and `use_count_penalty_overrides` is omitted, Lua defaults it to `true`.
