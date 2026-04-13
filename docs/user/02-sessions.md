**[← Installation](01-installation.md)** | **[Index](./README.md)** | **[Next: Manual sessions →](03-manual.md)**

---

# 2. The core idea: a session

Everything in Vimficiency is organized around **sessions**. A session captures:

- The buffer content at a starting point
- The keys you typed
- The buffer content at an ending point

Vimficiency then runs an optimizer and reports: "you used cost X; the best
sequence would have been cost Y (here it is)".

## Two ways to bound a session

Sessions differ only in **how their start is picked**:

| Kind    | Alias shape  | How the start is chosen                                         | How it ends                  |
|---------|--------------|-----------------------------------------------------------------|------------------------------|
| Manual  | alphabetic   | You mark it: `:Vimfy start a`                                   | `:Vimfy end a`               |
| Recall  | `N` or `Ns`  | Rolling ring; start = N keys ago (`N`) or N seconds ago (`Ns`)  | `:Vimfy end N` / `end Ns`    |

- **Manual** — when you know *in advance* that an edit is worth auditing.
  Precise boundaries; up to 5 in flight at once (`a`–`e`).
- **Recall** — retrospective. "I just did something; analyze the last 6
  keys" (`:Vimfy end 6`) or "the last 3 seconds" (`:Vimfy end 3s`). The
  ring is populated passively as you type, so it's ready whenever you
  call `end`.

Separately, **auto-suggest** can run the optimizer on its own and surface
results without you calling `end` — see
[5. Auto-suggest](05-auto-suggest.md). That's orthogonal to which kind
of session you use.

See the per-kind pages:

- [3. Manual sessions](03-manual.md)
- [4. Recall](04-recall.md) — retrospective recall by keys or seconds

---

**[← Installation](01-installation.md)** | **[Index](./README.md)** | **[Next: Manual sessions →](03-manual.md)**
