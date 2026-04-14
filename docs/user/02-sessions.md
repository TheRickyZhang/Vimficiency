**[← Installation](01-installation.md)** | **[Index](./README.md)** | **[Next: Mark →](03-mark.md)**

---

# 2. The core idea: a session

Everything in Vimficiency is organized around **sessions**. A session captures:

- The buffer content at a starting point
- The keys you typed
- The buffer content at an ending point

Vimficiency then runs an optimizer and compares your effort with the efforts of the best sequences it found.

## The 2×2 of session types

Every session is bounded by a **start** and an **end**. Each of those
can be picked manually (by you) or automatically (by Vimfy). The four
combinations are Vimficiency's four session types:

|                      | **Manual end** (`:Vimfy end`)         | **Auto end** (idle trigger)                      |
|----------------------|----------------------------------------|---------------------------------------------------|
| **Manual start**     | **Mark** — `:Vimfy start <alias>`     | **Watch** — `:Vimfy watch <alias>`                |
| **Auto start**       | **Recall** — `:Vimfy end N` / `end Ns`| **Suggest** — fires on its own while you edit     |

- **[Mark](03-mark.md)** — you decide both boundaries. Precise; up to
  five in flight at once; alphabetic aliases.
- **[Watch](04-watch.md)** — you pick the start, Vimfy picks the end
  (auto-finish after `watch.idle_ms` of no typing).
- **[Recall](05-recall.md)** — retrospective. Vimfy maintains a
  bounded queue as you type; `:Vimfy end 6` analyzes the last 6 keys,
  `:Vimfy end 3s` the last 3 seconds.
- **[Suggest](06-suggest.md)** — fully automatic on both ends. Runs the
  optimizer on a recall window when you pause, surfaces results in a
  notification.

Manual-start types (Mark, Watch) need an explicit `:Vimfy start|watch`
before the edit. Auto-start types (Recall, Suggest) need the recall
queue running — the queue is the "auto-start" mechanism both share.

Manual-end types (Mark, Recall) need an explicit `:Vimfy end`. Auto-end
types (Watch, Suggest) share a single idle-trigger engine and are
independently configurable — you can run both at once with different
`idle_ms` thresholds.

See the per-type pages for details:

- [3. Mark](03-mark.md) — manual start, manual end.
- [4. Watch](04-watch.md) — manual start, auto end.
- [5. Recall](05-recall.md) — auto start, manual end.
- [6. Suggest](06-suggest.md) — auto start, auto end.

---

**[← Installation](01-installation.md)** | **[Index](./README.md)** | **[Next: Mark →](03-mark.md)**
