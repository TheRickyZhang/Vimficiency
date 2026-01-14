# X Macros and Data Values

The codebase uses **X macros** in `XMacroKeyDefinitions.h` to define keys/hands/fingers:

```cpp
#define VIMFICIENCY_KEYS(X) \
  X(Key_A, "a") \
  X(Key_B, "b") \
  // ...
```

This single definition generates:
- Enum values: `enum class Key { Key_A, Key_B, ... }`
- Name arrays: `const char* g_key_names[] = {"a", "b", ...}`
- FFI exports: `vimficiency_key_name(int index)`

## Supported Commands
You can find supported commands in:
- `src/Keyboard/CharToKey` - Character to key mapping
- `src/Keyboard/MotionToKeys` - Motion command key sequences
- `src/Keyboard/EditToKeys` - Edit command key sequences

Note we group commands by role in searching. For instance, f-commands are separate in motions, and edits are grouped based on EditBoundary level.

## Sequence to Keys Example
```
Sequence "l3wfD;" -> {ParsedMotion("l", 0), ParsedMotion("w", 3), ParsedMotion("fD;", 0)}
                  -> {Key(l), Key(3), Key(w), Key(f), Key(Shift), Key(d), Key(Semicolon)}
```
