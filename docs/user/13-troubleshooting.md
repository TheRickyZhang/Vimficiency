**[← Limitations](12-limitations.md)** | **[Index](./README.md)** | **[Next: FAQ →](14-faq.md)**

---

# 13. Troubleshooting

## `:Vimfy` command isn't defined

You forgot `require('vimficiency').setup()` in your config, or `setup`
errored (likely couldn't load the library — check `VIMFICIENCY_LIB_PATH`).

## Auto-suggest never fires

- Is it configured? `auto_suggest = { idle = { ms = N, window = "..." } }`
  in `setup{}`.
- Is it enabled at runtime? `:Vimfy suggest on`.
- Is the ring recording? Auto-suggest analyzes the recall ring — it
  needs `:Vimfy recall on` (or the default, which enables recall when
  auto-suggest is configured).

## Suggestions look wrong or worse than what I did

- Some motions we can't see (text-object last character, screen-line
  motions, certain plugin-provided mappings).
- Check `:Vimfy config` — a mismatched `shiftwidth` or keyboard-effort
  setting can skew the cost.
- See [12. Limitations](12-limitations.md) for the full list.

## A key I bound to Vimfy is showing up as motion

Your mapping isn't announcing itself as admin activity. Route it through
`<Plug>` or `wrap()` — see [7. Keymaps](07-keymaps.md).

## "unknown config keys ignored" warning

Typo in a `setup{}` key. The warning names them. Cross-check against
[8. Configuration](08-configuration.md) or `:Vimfy config`.

## I want to start over cleanly

- `:Vimfy recall off` then `:Vimfy recall on` discards the ring.
- `:Vimfy close <alias>` throws away any active session.
- Restarting Neovim clears all in-memory state; saved-to-disk results
  survive.

## Still stuck?

Run `:Vimfy config` and capture the output along with the sequence you
typed — most triage starts there.

---

**[← Limitations](12-limitations.md)** | **[Index](./README.md)** | **[Next: FAQ →](14-faq.md)**
