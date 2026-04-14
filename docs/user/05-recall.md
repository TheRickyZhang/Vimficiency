**[← Watch](04-watch.md)** | **[Index](./README.md)** | **[Next: Suggest →](06-suggest.md)**

---

# 5. Recall (auto start, manual end)

Recall lets you look backward without any previous marking setup by querying a continuously maintained queue. It is on by default.

```vim
:Vimfy recall on        " Start recording
:Vimfy recall off       " Stop, discard queue
:Vimfy recall toggle
```

```vim
" ... edit normally ...
:Vimfy end 6          " Analyze the last 6 keystrokes
:Vimfy end 3s         " Analyze the last 3 seconds
```

## Two ways to index the queue

| Syntax           | Meaning                             | Use when                                        |
|------------------|-------------------------------------|-------------------------------------------------|
| `:Vimfy end 6`   | Last 6 keystrokes                   | You can estimate how many keys the edit took    |
| `:Vimfy end 3s`  | Last 3 seconds of activity          | You can estimate time better than key count     |


Mixing forms (`3m`, `3ms`, `2h`, etc.) is not supported. Seconds is the
only time unit; if you want 90 seconds, use `90s`.

## Implementation Details

Each keystroke is tagged with a timestamp when captured. `:Vimfy end Ns`
resolves to `:Vimfy end K`, where the `K` most recent key
was typed within the last `N` seconds", where the window start is also snapped backward to the start of normal mode commands.

The optimizer does not run until you invoke a recall command, so recall processing and memory is typically O(1) per key.


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

If `end Ns` asks for a window older than the queue retains, the alias
fails to resolve and you get "No recall session found within 'Ns'…" —
raise either cap.

## Recall results are transient
To preserve a recall results, make sure to save it. Because the state continuously shifts, you can use `@` to refer to the recently ended recall session.

```vim
:Vimfy end 3s
:Vimfy save @ nested-refactor
```


## See also

- [6. Suggest](06-suggest.md) — surface results automatically
  without calling `end`.
- [7. Inspecting results](07-results.md) — what the output looks like
  and how to replay it.

---

**[← Watch](04-watch.md)** | **[Index](./README.md)** | **[Next: Suggest →](06-suggest.md)**
