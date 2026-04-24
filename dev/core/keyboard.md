# Keyboard Module

The `src/Keyboard/` module handles physical keyboard representation, key mappings, and sequence-to-keys conversion. This is the foundation for effort-based cost calculation in the optimizer.

## Key Concepts

| Concept | Description | Source |
|---------|-------------|--------|
| `Key` | Physical key enum (61 keys) | `Key.h` |
| `PhysicalKeys` | Ordered list of keys representing keystrokes | `PhysicalKeys.h` |
| `Hand`, `Finger` | Ergonomic metadata for effort calculation | `Hand.h`, `Finger.h` |
| `CharToKeys` | Single character → key sequence | `ToKeys/CharToKeys.h` |
| `CommandToKeys` | Token string → key sequence map | `ToKeys/CommandToKeys.h` |
| `CountToKeys` | Prebuilt count-prefix keys/text | `ToKeys/CountToKeys.h` |
| `SequenceToKeys` | Vim sequence → physical keys | `ToKeys/SequenceToKeys.h` |

## X Macros (`XMacroKey.inc`, `XMacroHand.inc`, `XMacroFinger.inc`, `XMacroKeyedSequence.h`)

**Single source of truth** for key/hand/finger definitions. Uses X-macro include files to generate enums, name arrays, and FFI tables.

```cpp
enum class Key : int {
#define X(name, str) name,
#include "XMacroKey.inc"
#undef X
  None
};
```

This generates:
- **Enum values**: `enum class Key { Key_A, Key_B, ..., None }`
- **Name arrays**: Used for FFI and debug output
- **Static assertions**: Verify counts match at compile time

### Supported Keys (61 total)
- Letters: `Q-P`, `A-L`, `Z-M` (26)
- Digits: `0-9` (10)
- Punctuation: `Semicolon`, `Comma`, `Period`, `Slash`, `Grave`, `Minus`, `Equal`, `LBracket`, `RBracket`, `Backslash`, `Apostrophe` (11)
- Modifiers: `Ctrl`, `Shift` (2)
- Special: `Esc`, `Tab`, `Enter`, `Backspace`, `Space`, `Delete` (6)
- Navigation: `Home`, `End`, `Left`, `Down`, `Up`, `Right` (6)

### Hands and Fingers

```cpp
enum class Hand : int8_t {
#define X(name, str) name,
#include "XMacroHand.inc"
#undef X
  None
};

enum class Finger : int8_t {
#define X(name, str) name,
#include "XMacroFinger.inc"
#undef X
  None
};
```

Mapping: `L`/`R` = hand, `p`/`r`/`m`/`i`/`t` = pinky/ring/middle/index/thumb.

## PhysicalKeys

Represents a sequence of physical key presses. Used as the input to effort calculation.

```cpp
PhysicalKeys keys;
keys.push_back(Key::Key_3);
keys.push_back(Key::Key_W);
// Represents typing "3w"
```

**Key methods:**
- `append(other, count)` - append keys, optionally repeated
- `makeCountedKeys(count, motionKeys)` - prepend digit keys for counted motions

## Character and String Mappings

### CharToKeys (`ToKeys/CharToKeys.h`)

Maps single printable characters to their physical key sequences:

```cpp
// Organized by category in CharMappings namespace:
CharMappings::letters      // 'a' -> {Key_A}, 'A' -> {Key_Shift, Key_A}
CharMappings::digits       // '5' -> {Key_5}
CharMappings::whitespace   // ' ' -> {Key_Space}
CharMappings::digitSymbols // '!' -> {Key_Shift, Key_1}
```

### CommandToKeys (`ToKeys/CommandToKeys.h`)

Maps multi-character command tokens (like `<C-d>`, `<Esc>`, `gJ`) to key sequences. Uses transparent hashing for efficient `string_view` lookups without allocation.

## Motion and Edit Mappings

### MotionToKeys (`ToKeys/MotionToKeys.h`)

Several maps for different purposes:

| Map | Purpose |
|-----|---------|
| `ACTION_MOTIONS_TO_KEYS` | Physical actions for tokenizing raw input |
| `ALL_MOTIONS` | All supported vim motions for parsing/validation |
| `EXPLORABLE_MOTIONS` | Motions directly usable in optimizer search (excludes f/F/t/T which need target char) |

Count-search motion pair specs (`w/b`, `e/ge`, paragraph/sentence categories) are owned by NavOptimizer (`Optimizer/NavOptimizer/CountableMotionPair.h`), not Keyboard.

### EditToKeys (`ToKeys/EditToKeys.h`)

Similar structure for edit commands. Organized by `EditBoundary` level (see `boundary-logic.md`).

## Sequence Tokenization

The `SequenceToKeys` tokenizer converts a Vim command sequence string into `PhysicalKeys`:

```
Sequence "l3wfD;"
  -> Tokens: ["l", "3", "w", "f", "D", ";"]
  -> Keys: {Key_L, Key_3, Key_W, Key_F, Key_Shift, Key_D, Key_Semicolon}
```

**Important:** This is for physical effort calculation only. For semantic parsing (understanding what motions/edits are being executed), see `Interpreter/SequenceParser.h`.

## Effort Model

The `RunningEffort` class (`Effort/RunningEffort.h`) computes typing cost from `PhysicalKeys`:

**Metrics tracked:**
- Stroke count
- Base key cost (some keys harder than others)
- Same-finger bigrams (penalty)
- Same-key repeats (extra penalty)
- Hand alternation (bonus)
- Long same-hand runs (penalty beyond threshold)
- Good/bad rolls (inward vs outward finger sequences)

The weighted sum of these metrics produces the final effort score used by the optimizer's A* heuristic.

## Adding New Keys or Commands

1. **New physical key**: Add to `XMacroKey.inc`, update any related key metadata in `Config`
2. **New character mapping**: Add to appropriate `CharMappings` category
3. **New motion**: Add to `ToKeys/MotionToKeysPrimitives.h` and relevant motion maps
4. **New edit**: Add to `ToKeys/EditToKeysPrimitives.cpp` and relevant edit maps

## Semantics
Note that append(thing, count) -> append count copies of thing, while append(count, thing) -> append count as chars literally, then thing
