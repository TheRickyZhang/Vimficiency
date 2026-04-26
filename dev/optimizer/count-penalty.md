# Count Penalty Model

Count penalties model cognitive overhead for counted commands (`{n}w`, `{n}dd`, `{n}gJ`, ...).
They are added on top of physical key effort.

## Formula

For each `CountClass`, penalty is:

`base + countSlope * (count - 1) + spanSlope * max(0, span)`

Rules:
- `count <= 1` always returns `0.0`.
- `span < 0` is clamped to `0`.
- If runtime overrides are disabled, compile-time defaults from `CountPenaltySpec` are used.

## Where It Is Applied

### NavOptimizer

Counted motion emitters apply penalty at creation time:
- `w/e/b/ge` -> `CountClass::MovementWord`
- `W/E/B/gE` -> `CountClass::MovementWORD`
- `{/}` -> `CountClass::MovementParagraph`
- `(/)` -> `CountClass::MovementSentence`

### TransformOptimizer

`TransformExplorer` counted emitters apply penalty into the emitted `RunningEffort`:
- `{n}dd` -> `CountClass::EditLine`
- `{n}J`, `{n}gJ` -> `CountClass::Join`
- `{n}de/{n}dw/{n}db/{n}dge` -> `CountClass::EditWord`
- `{n}dE/{n}dW/{n}dB/{n}dgE` -> `CountClass::EditWORD`
- `{n}x` -> `CountClass::EditChar`

For full-edit mode (`ModePolicy<false>`), goal-conversion paths also preserve counted penalties when converting `d...` to change-prefixed forms.

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
