**[← Watch](04-watch.md)** | **[Index](./README.md)** | **[Next: Suggest →](06-suggest.md)**

---

# 5. Recall (auto start, manual end)

Recall lets you look backward without any previous marking setup by querying a continuously maintained queue. It is always on — the bounded, RAM-only ring is foundational enough that there's no toggle.

```vim
" ... edit normally ...
:Vimfy recall 6       " Analyze the last 6 keystrokes
:Vimfy recall 3s      " Analyze the last 3 seconds
```

`:Vimfy end <alias>` stays reserved for manual handles (Mark / Watch);
retrospective windows go through `:Vimfy recall`.

## Window aliases: two ways to index the queue

| Syntax   | Meaning                    | Use when                                     |
|----------|----------------------------|----------------------------------------------|
| `N`      | Last N keystrokes          | You can estimate how many keys the edit took |
| `Ns`     | Last N seconds of activity | You can estimate time better than key count  |

Mixing forms (`3m`, `3ms`, `2h`, etc.) is not supported. Seconds is the
only time unit; if you want 90 seconds, use `90s`.

The same grammar is reused by [Suggest](06-suggest.md) triggers (e.g.
`window = "3s"` / `"50"`) — anywhere Vimficiency wants a slice of the
queue, `N` and `Ns` are how you name it.

## Implementation Details

Each keystroke is tagged with a timestamp. `:Vimfy recall Ns` resolves
to the `K` most recent keys typed within the last `N` seconds, with the
window start snapped backward to the nearest normal-mode command
boundary.

The optimizer runs only when you invoke a recall command, so per-key
overhead is O(1).


## Capacity

The queue is bounded by two caps, applied with **union semantics** — a
session is kept as long as EITHER cap still holds it, and only evicted
when BOTH say drop:

| Knob                       | Default | Meaning                                |
|----------------------------|---------|----------------------------------------|
| `KEY_SESSION_CAPACITY`     | 200     | Count floor: keep at least N sessions. |
| `MAX_RETENTION_SECONDS`    | 120     | Age floor: keep sessions newer than N. |

So at defaults, the queue always retains at least 200 sessions AND at
least the last 120 seconds of activity.
```lua
require('vimficiency').setup({
  KEY_SESSION_CAPACITY  = 500,   -- keep more sessions
  MAX_RETENTION_SECONDS = 300,   -- keep a longer time window
})
```

If `recall Ns` asks for a window older than the queue retains, the
alias fails to resolve and you get "No recall session found within
'Ns'…" — raise either cap.

Recall records are discarded when Neovim exits; promote with `:Vimfy save`
if you want them on disk.

## Recall results are transient

Recall windows rotate out of the queue as you type, so save promptly.
`@` refers to the most recently ended session — handy when the alias
(`3s`) is moving out from under you:

```vim
:Vimfy recall 3s
:Vimfy save @ nested-refactor
```


## See also

- [6. Suggest](06-suggest.md) — surface results automatically
  without calling `end`.
- [7. Inspecting results](07-results.md) — what the output looks like
  and how to replay it.

---

**[← Watch](04-watch.md)** | **[Index](./README.md)** | **[Next: Suggest →](06-suggest.md)**
