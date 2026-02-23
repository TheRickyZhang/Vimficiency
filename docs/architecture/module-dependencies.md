# Module Dependencies

This document defines the allowed `src` module dependency graph and the intent of each module.

## Module Intent

- `VimTypes`: Shared representation primitives and semantic enums only (e.g. `Position`, `Range`, `Mode`, `NavContext`, `Sequence`, `Lines`, `BracketFlags`, `QuoteFlags`, `NoChar`, `EdgeType`, `LandingType`). No optimizer policy or execution logic.
- `Keyboard`: Physical keys, key maps, tokenization, and keyboard effort-related data definitions.
- `Effort`: Typing-effort accumulation/modeling built on keyboard primitives/config.
- `VimCore`: Vim semantic behavior (motions, edit semantics, endpoints, options).
- `Interpreter`: Arbitrary command parsing/interpreting adapters (`parse*`, `apply*`, `simulate*`) built on `VimCore` and keyboard motion vocab.
- `Session`: Snapshot/session file I/O types and loaders.
- `Boundary`: Boundary metadata and conversion helpers.
- `Utils`: Generic helpers.
- `Optimizer`: Search algorithms and optimizer-specific logic.

## Allowed Dependencies

- `VimTypes` -> (none)
- `Utils` -> `VimTypes`
- `Keyboard` -> `VimTypes`, `Utils`
- `Effort` -> `Keyboard`
- `VimCore` -> `VimTypes`, `Utils`, `Boundary`
- `Interpreter` -> `VimTypes`, `VimCore`, `Keyboard`, `Utils`
- `Session` -> `VimTypes`
- `Boundary` -> `VimTypes`, `Utils`
- `Optimizer` -> `VimTypes`, `Utils`, `Keyboard`, `VimCore`, `Boundary`, `Interpreter`, `Effort`

## Placement Rules

- Put a type in `VimTypes` when it is shared value/state representation with no execution/parsing policy.
  Examples: `Position`, `Range`, `Lines`, `Mode`, boundary/landing enums and flags.
- Put Vim semantic execution in `VimCore`, and arbitrary command string interpretation in `Interpreter`.
  Examples: endpoint logic in `VimCore`; `parse*`/`apply*` adapters in `Interpreter`.
- Keep foundational buffer/cursor types out of `Interpreter`; lower layers (`VimCore`, `Boundary`, `Optimizer`) share them via `VimTypes`.
- `Sequence` follows split ownership:
  type declaration in `VimTypes`, formatting/parsing-aware stream implementation in a higher layer (`Interpreter`) to keep `VimTypes` dependency-free.

## Enforcement

Run dependency lint locally:

```bash
./scripts/lint-module-deps.sh
```

CI runs the same lint before build/tests.
