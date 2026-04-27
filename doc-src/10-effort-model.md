---
title: "The effort model"
---

# The effort model

> **Status: placeholder.** The effort model is a substantial topic — this
> page will grow. For now, use `:Vimfy config` to inspect the live values
> and the pointers below to understand the shape.

Vimfy does not score sequences by keystroke count. It scores them by
**keyboard effort**: how hard each key is to reach, whether consecutive
keys involve the same finger, whether they alternate hands, etc. A 4-key
sequence using good rolls on the home row can be "cheaper" than a 3-key
sequence that repeats the pinky.

## What's tunable

The following knobs are accepted by `setup{}`; see their live values with
`:Vimfy config`.

### Global weights (`weights`)

```lua
require('vimficiency').setup({
  weights = {
    keyWeight        = 1.0,   -- baseline per-keystroke cost
    sameFingerWeight = 1.0,   -- penalty for consecutive same-finger
    sameKeyWeight    = 1.0,   -- penalty for the same key twice
    altHandWeight    = 1.0,   -- bonus for alternating hands
    goodRollWeight   = 1.0,   -- bonus for inward rolls
    badRollWeight    = 1.0,   -- penalty for outward rolls
  },
})
```

### Per-key base cost and finger/hand assignment (`keys`)

Each of the 61 keys on the modeled layout has `{hand, finger, base_cost}`.
Default is QWERTY; override specific keys to retune for your own layout.
(A fuller "layout file" API is planned — for now, override the key table
entries directly by index.)

### Count-penalty overrides (`count_penalty_overrides`)

A count prefix like `8j` is cheaper than eight `j`s but not free. The
penalty grows with count and motion span. Per count-class overrides let
you reshape this curve.

## Planned topics for this page

- Full field-by-field reference with defaults and sensible ranges
- Walking through a scoring example end-to-end
- Recipes for common layouts (Colemak, Dvorak, Workman)
- How per-key cost interacts with the `keys[]` finger/hand layout
- When to reach for `count_penalty_overrides` vs. `weights`

Until these are written, `:Vimfy config` is the authoritative surface, and
the C++ source (`cpp/effort_model.*`) is the spec.
