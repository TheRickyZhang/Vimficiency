# design-check

Invoke when an architectural decision is on the table — choosing between a
quick patch and a deeper structural fix, deciding where new logic belongs,
resolving a duplication vs. abstraction call, or noticing that the obvious
expedient option conflicts with the existing layering. Also invokable
explicitly by the user as a sanity-check before committing to an approach.

Vimficiency's standing preference: **the better architectural decision wins,
even when it takes more effort now.** Short-term expedience that smudges a
boundary is not a tradeoff — it is debt logged against future correctness.

## When multiple paths are visible, ask first

Before proposing an "easy" path, identify both and write them all down
briefly: if there is a clear "best structural" fix, default to that.
Otherwise if there is a genuine balance, pursue further discussion.

## Concrete shapes this takes

- **Fix the abstraction, do not add a special case.** If a new input class
  breaks an existing function's invariants, reshape the function. Do not add
  an `if (specialCase)` branch and call it done.
- **Put logic where it belongs.** If a piece of behavior is conceptually
  owned by module A but is being patched into module B because B is where
  the bug surfaced, move it. Surface site is not ownership.
- **Fix divergence at the root.** If two code paths must agree (e.g.
  `applyEdit` vs. A* state transitions) and they have drifted, unify the
  primitive both call. Do not clamp the symptom on one side.
- **Respect phase boundaries.** Transform emits edits; Navigate emits
  motions. If a recommendation wants to cross kinds, the right move is to
  reconsider the phase boundary — not to bend the rule "just this once."
- **Respect the FFI split.** Logic that ends up in Lua because "it was
  easier to reach the Neovim API from there" is the wrong kind of easier.
  Move it to C++ and expose a proper FFI entry.
- **Prefer propagating a refactor cleanly over leaving a forwarding call.**
  Half-migrated abstractions rot faster than either full state.

## What this is NOT

- Not a license to expand scope unprompted. The structural fix should still
  be the *minimal* one that actually addresses the cause — not a grand
  refactor that bundles unrelated cleanup.
- Not a reason to invent abstractions for hypothetical futures. "Better
  architecture" means matching the structure to the problem that exists,
  not preparing for problems that might, unless the user is explicitly
  planning for feature expansion.
- Not a reason to block on ambiguity. If the right structural call is
  genuinely unclear, surface the choice and let the user decide rather than
  guessing in either direction.

## How to invoke this in practice

When you notice a fork, briefly state both options to the user and your
recommendation under this skill's bias before implementing. One or two
sentences is enough — this is a sanity check, not a design doc.
