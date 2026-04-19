# Replay precompute

How the simulate/replay UI faithfully replays a user-captured key sequence
on top of the original buffer content, and how that precompute stays
correct under Neovim's async input loop.

Code of record: `lua/vimficiency/simulate/init.lua`.

---

## The goal

The user captures a sequence of motion keystrokes (`jf;i<BS>2<Esc><Space>ve`)
against a buffer slice. The replay UI needs to show, at every intermediate
step `k` in that sequence:

- the buffer contents after `k` tokens have been applied;
- the cursor position after `k` tokens;
- the mode after `k` tokens (so `i` colors the cursor "insert",
  `v`/`V`/`<C-v>` colors it "visual", etc.).

Two constraints drive the design:

1. **Scrubbing is `O(1)`.** The user can `<Left>`/`<Right>`/`[b`/`]b`
   arbitrarily; we cannot re-simulate from step 0 on every key. So every
   step's `{ lines, cursor, mode }` triple is precomputed up front and
   indexed by step.
2. **Mode-faithful.** `:normal` flattens insert/visual into a single
   atomic operation; that throws away the very mid-stream states the
   replay exists to surface. So we can't use `:normal` as the oracle.

## The oracle: a hidden probe window

The oracle is Neovim itself, driven through `vim.api.nvim_feedkeys`. For
each sequence we:

1. Create an off-screen 1×1 floating window (`probe_win`) over a scratch
   buffer seeded with the user's line snapshot.
2. Set the cursor to the captured start position.
3. Drive tokens through the probe one at a time, sampling
   `{ lines, cursor, mode }` after each token.
4. Tear down the probe (window + buffer) when done.

Key reasons for the 1×1 float rather than `nvim_buf_call`:

- `nvim_feedkeys` queues keys to the input queue to drain *later*; at
  drain time, whichever window is **actually current** receives them. If
  we used `nvim_buf_call` to run the feedkeys inside a callback that
  merely swaps current-buffer for its duration, keys queued inside that
  callback could drain into the user's real buffer after it returns —
  leaking input.
- The float must be focusable and really current, not just
  "temporarily" current. Making `probe_win` the live current window for
  the entire precompute avoids the race entirely.

`noautocmd = true` and `buftype = nofile` / `bufhidden = wipe` keep the
probe invisible to user autocmds and plugin machinery.

## Sequential, not concurrent

`M.simulate_compare` is called with N sequences (user seq + top-M
optimals). Each gets its own probe, one at a time, via
`precompute_next(idx)` which calls `precompute_states(...)` and only
starts the next sequence from its `on_done` callback. This is deliberate:

- Only one probe is ever current at a time — no cross-talk.
- A cancellation token (`precompute_gen`) invalidates stale callbacks if
  the user fires a new `simulate_compare` before the previous finishes.

## Tokenization (`tokenize_for_animation`)

The raw captured sequence is a single string. To render one token per
step we first tokenize it. Three tiers:

1. **C++ `tokenize_sequence`** — the canonical tokenizer the optimizer
   already uses. Understands counts, operators, text objects, named-key
   forms (`<BS>`, `<Esc>`, etc.), and motion combinators.
2. **C++ `tokenize_motions`** — fallback for sequences the sequence
   tokenizer rejects (e.g., when the buffer context doesn't match).
3. **Per-character Lua fallback** — last resort; keeps `<Key>` forms
   intact.

Two post-processing passes matter for replay:

- **Merge "needs-following-key" tokens.** `f`, `F`, `t`, `T`, `r`, `m`,
  `'`, `` ` ``, `@` are all commands that consume exactly one more
  keystroke. If we fed them as separate tokens and sampled in between,
  Neovim would wait for the next key — blocking the event loop. So we
  glue them: `f` + `;` → `"f;"`.
- **Chunk insert-mode text into 4-char segments** for nicer animation
  (`"hello world"` → `["hell", "o wo", "rld"]`). Purely cosmetic.

Both passes are visible via `M._debug_tokenize_for_animation` and
exercised by `tests/lua/simulate/tokenize.lua`.

## Feeding tokens: the drain problem

Given a tokenized sequence, the naïve loop is:

```lua
for _, token in ipairs(tokens) do
  nvim_feedkeys(token, "n", false)     -- queue the key
  -- sample here
end
```

The problem: `nvim_feedkeys(..., "n", false)` **queues** `token` into the
typeahead buffer and returns immediately. Neovim's input loop drains
typeahead on some later tick. If we sample before the drain, we capture
stale state. If we sample after the drain, we capture correct state. So
we need to *wait for the drain*.

### Previous strategy: coroutine yields

The precompute originally ran inside a `coroutine.create(...)`, with a
helper:

```lua
local function feed_and_yield(keys)
  nvim_set_current_win(probe_win)
  nvim_feedkeys(keys, "n", false)
  coroutine.yield()       -- give Neovim one tick to drain typeahead
  coroutine.yield()       -- give it another tick to stabilize nvim_get_mode()
end
```

The driver (`step()`) resumes the coroutine, and when the coroutine
yields, schedules its next resume via `vim.defer_fn(step, 0)` — a libuv
0ms timer. Two yields = two libuv-timer-callback gaps.

Empirically, the original author found one yield was enough for the
typeahead drain in most cases, but `nvim_get_mode()` sometimes lagged
one tick behind a mode transition (notably `i` → `<Esc>`). Two yields
stabilized the mode query. Comment from the code:

> Empirically, a single defer tick is enough for the input queue to
> drain, but `nvim_get_mode()` sometimes lags one more tick behind a
> mode transition (observed reliably for `i` → `<Esc>` with only one
> intermediate feed). Two yields makes the mode query stable across
> all mode transitions we care about.

### Why the two-yield strategy was probabilistic, not deterministic

The assumption "1 libuv tick ≥ 1 typeahead drain" holds *most of the
time* but not *all of the time*. `vim.defer_fn(cb, 0)` schedules a
libuv timer; the libuv loop fires its timer-callback phase at specific
points. Neovim's input-loop pass (the thing that actually drains
typeahead and dispatches keys through `vgetc`) is a separate pass.
Whether an input-drain pass runs *between* our two timer callbacks is
not guaranteed. Sometimes they're interleaved (good); sometimes two
timer callbacks fire back-to-back with no input-drain pass in between
(bad, the race).

The bug surfaced as a **first-token dropout** under live load: running
the capture under a live Neovim with autocmds, UI, and other event-loop
competitors, the first `$` of a precompute would occasionally produce
a snapshot where the cursor hadn't moved. Per-token trace (`D` in the
replay UI) confirmed:

```
window[2] token "$":
  before_feed      cursor=(2,2) mode=n
  after_feedkeys   cursor=(2,2) mode=n   +0.00ms
  after_yield_1    cursor=(2,2) mode=n   +0.08ms
  after_yield_2    cursor=(2,2) mode=n   +0.58ms   ← NEVER drained

window[3] token "$" (same starting state, next sequence):
  before_feed      cursor=(2,2) mode=n
  after_feedkeys   cursor=(2,2) mode=n   +0.00ms
  after_yield_1    cursor=(2,2) mode=n   +0.01ms
  after_yield_2    cursor=(2,7) mode=n   +0.59ms   ← drained
```

Identical inputs, identical time budgets (~0.5ms between yields),
different outcomes. That rules out wall-clock-insufficiency and points
squarely at nvim's input-loop scheduling.

### The fix: `nx` for normal-mode-only tokens

Neovim's `nvim_feedkeys` mode string has an `x` flag documented as
"execute commands until typeahead is empty". That's a synchronous drain:
by the time feedkeys returns, the queued key has been processed by the
same code path `vgetc` uses. No more racing with the event loop.

We can't blanket-switch to `x` because the precompute needs to
**observe** intermediate modal states. Tokens like `i`, `v`, `c{motion}`
enter modes the oracle needs to sample — and `x` drains until typeahead
is empty, which for a pending `i` is the moment nvim becomes ready to
accept the next insert-mode character, past the point where we'd sample
the plain "just entered insert" state.

So the fix is narrow:

```lua
local curr_mode = nvim_get_mode().mode
if curr_mode:sub(1, 1) == "n" and not enters_modal_state(token) then
  nvim_feedkeys(keys, "nx", false)     -- synchronous drain
else
  nvim_feedkeys(keys, "n", false)      -- fall through to yield path
end
coroutine.yield()
coroutine.yield()
```

`enters_modal_state(token)` returns true for insert-entering commands
(`is_change_command`) and visual-entering commands (`v`, `V`, `<C-v>`,
`gh`, `gH`). The check `curr_mode == "n"` filters out tokens fed *while*
the oracle is in insert/visual/operator-pending — those are either
typed characters or intra-mode motions that still benefit from the
yield path for reasons the original comment describes.

The two yields still run for every token, including the `nx` path, for
three reasons:

1. The existing mode-query lag concern (`i` → `<Esc>` stabilization) is
   untouched and still covered.
2. The yields are zero-cost when there's nothing left to drain —
   `vim.defer_fn(_, 0)` is a single event-loop tick.
3. Future tokens in the same sequence may enter modal state; keeping
   the yield scaffold in place means we don't have to re-introduce it.

In practice this means pure-Normal motions (`j`, `w`, `$`, `f;`, `de`)
are deterministically drained, which is exactly the class of tokens
the bug was dropping.

## Per-token telemetry

`probe_debug(label)` samples `{ cursor, mode, current_win, t_ns }`
around each `feed_and_yield` at four points: `before_feed`,
`after_feedkeys`, `after_yield_1`, `after_yield_2`. The trace is
attached to the post-token snapshot alongside `lines`/`cursor`/`mode`.

Two consumers:

- **Live debug (`D` keybind in replay)** surfaces the trace for the
  snapshot at the current step. Lets us isolate *which transition*
  dropped a token without having to reproduce the race synthetically.
- **Test failure messages** include the trace, so a regression reports
  exactly where the precompute diverged from expectation.

The cost is trivial — four `nvim_win_get_cursor` / `nvim_get_mode`
calls per token, all cheap. No runtime flag gates it; it's always on.

## Tail `<Esc>`

After the last user token, the coroutine feeds one final `<Esc>` to
flush any residual mode (the sequence may end mid-insert or mid-visual
if the user didn't exit explicitly). That keeps stray keystrokes from
leaking into the user's real buffer when the probe is torn down — the
next `feed_and_yield` is guaranteed to start from Normal mode.

This `<Esc>` goes through the same branch logic: `curr_mode != "n"` (we
just ended in insert/visual), so it takes the yield-based path.

## Replay layer vs. precompute

Once precompute finishes, the replay UI in `M.simulate_compare` /
`build_sim_ui` just indexes into the precomputed `states[seq_idx][step
+ 1]` arrays and blits snapshots onto display buffers via
`apply_state`. The UI is a pure index lookup over precomputed state
— stepping, cycling (`[b`/`]b`), and focus (`<CR>` / `:Vimfy focus N`
/ `:Vimfy escape`) all share this lookup.

Because the UI is decoupled from the oracle, a precompute bug
manifests as a **wrong snapshot stored**, not a wrong *rendering*. The
`D` dump's juxtaposition of `rendered cursor` vs. `snapshot[k]` cursor
vs. `trace` makes this decomposition explicit, which is how the
drain-race diagnosis was nailed down.

## Tests that guard this

- `tests/lua/simulate/tokenize.lua` — tokenization invariants.
- `tests/lua/simulate/integration.lua`
  - `simulate replays insert and visual sequence with virtual header`
    — exercises the yield path on modal transitions (`i`, `<BS>`,
    typed chars, `<Esc>`, `<Space>`, `v`, `e`).
  - `simulate handles leading space and append-at-eol` — `A`+typed
    via yield path.
  - `simulate precompute drains first normal-mode token before
    snapshot` — repeats the `j` / `$` / `$` three-sequence case from
    the live bug dump 20 times. Regression guard for the drain fix.
- `tests/lua/capture/on_key_mapping_probe.lua` — characterizes what
  `vim.on_key` emits for different mapping shapes. Adjacent concern,
  not precompute per se, but informs whether a captured sequence can
  contain a bound LHS (which would confuse the oracle).

## When to revisit

If the first-token regression test ever goes intermittent again, the
`nx` path is no longer sufficient — likely because a future Neovim
version changes feedkeys-`x` semantics or the token set grew a new
mode-entering command we didn't tag in `enters_modal_state`. Extend
`VISUAL_ENTER_COMMANDS` / `INSERT_COMMANDS` to cover it, or widen the
fast-path guard.

If modal snapshots start drifting (insert/visual sampling captures the
wrong mode or cursor), the culprit is the yield path, not `nx`. Either
add a third yield or introduce a bounded poll (`vim.wait(N, pred)`)
keyed on mode/cursor stability.
