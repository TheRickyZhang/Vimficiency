**[← Installation](01-installation.md)** | **[Index](./README.md)** | **[Next: Mark →](03-mark.md)**

---

# 2. The core idea: a session

Everything in Vimficiency is organized around **sessions**. A session captures:

- The buffer content at a starting point
- The keys you typed
- The buffer content at an ending point

Vimficiency then runs an optimizer and compares your effort with the efforts of the best sequences it found.

## The 2×2 matrix of session types

Every session is bounded by a **start** and an **end**. Each of those
can be picked manually (by you) or automatically (by triggers from your configuration). The four types have distinct names, and manual invoking is outlined here:


|                      | **Manual end**                                         | **Auto end** (idle trigger)                      |
|----------------------|--------------------------------------------------------|---------------------------------------------------|
| **Manual start**     | **Mark** — `:Vimfy start <alias>`, `:Vimfy end <alias>` | **Watch** — `:Vimfy watch <alias>`                |
| **Auto start**       | **Recall** — `:Vimfy recall N` / `recall Ns`           | **Suggest** — fires on its own while you edit     |



See the per-type pages for details:

- [3. Mark](03-mark.md) — manual start, manual end.
- [4. Watch](04-watch.md) — manual start, auto end.
- [5. Recall](05-recall.md) — auto start, manual end.
- [6. Suggest](06-suggest.md) — auto start, auto end.

## Where a session lives

Once finished, a session lives in **session memory** (the workspace),
keyed by its alias. Memory rotates: Mark slots cap at 5, Recall entries
age out of the rolling ring. For anything durable, **save** a session
to disk — or **store** it (save + remove from workspace), **fetch** it
back later, and **sim** it whether it's in memory or on disk. See
[7a. Session storage](07a-session-storage.md) for the full model.

---

**[← Installation](01-installation.md)** | **[Index](./README.md)** | **[Next: Mark →](03-mark.md)**
