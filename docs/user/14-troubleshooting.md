**[← Limitations](13-limitations.md)** | **[Index](./README.md)** | **[Next: FAQ →](15-faq.md)**

---

# 14. Troubleshooting

## `:Vimfy` command isn't defined

You forgot `require('vimficiency').setup()` in your config, or `setup`
errored (likely couldn't load the library — check `VIMFICIENCY_LIB_PATH`).

## Suggest never fires

- Is it configured? `auto_suggest = { idle = { ms = N, window = "..." } }`
  in `setup{}`.
- Is it enabled at runtime? `:Vimfy suggest on`.
- Is the queue recording? Suggest analyzes the recall queue — it
  needs `:Vimfy recall on` (or the default, which enables recall when
  `auto_suggest` is configured).

## Watch never fires

- Is it configured? `watch = { idle = { ms = N }, cooldown_ms = N }` in
  `setup{}`. Without this, `:Vimfy watch <alias>` errors out at call
  time.
- Does the session still exist? Check `:Vimfy list`. Window-change
  aborts, `:Vimfy close`, and the 5-minute idle / 500-line drift
  guards all tear down Watch sessions silently.

## Suggestions look wrong or worse than what I did

- Some motions we can't see (text-object last character, screen-line
  motions, certain plugin-provided mappings).
- Check `:Vimfy config` — a mismatched `shiftwidth` or keyboard-effort
  setting can skew the cost.
- See [13. Limitations](13-limitations.md) for the full list.

## A key I bound to Vimfy is showing up as motion

Your mapping isn't announcing itself as admin activity. Route it through
`<Plug>` or `wrap()` — see [8. Keymaps](08-keymaps.md).

## "unknown config keys ignored" warning

Typo in a `setup{}` key. The warning names them. Cross-check against
[9. Configuration](09-configuration.md) or `:Vimfy config`.

## I want to start over cleanly

- `:Vimfy recall off` then `:Vimfy recall on` discards the queue.
- `:Vimfy close <alias>` throws away any active session.
- Restarting Neovim clears all in-memory state; saved-to-disk results
  survive.

## Still stuck?

Run `:Vimfy config` and capture the output along with the sequence you
typed — most triage starts there.

---

**[← Limitations](13-limitations.md)** | **[Index](./README.md)** | **[Next: FAQ →](15-faq.md)**
