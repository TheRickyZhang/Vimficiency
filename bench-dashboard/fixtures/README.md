# Dashboard Fixtures

Tracked, deterministic fixture data for local dashboard development.

## Purpose

- `fixtures/` is committed and stable.
- `public/` is generated local state (gitignored for `data.json` / `explore.json`).

Use fixtures when you want reproducible UI behavior across machines.
Use latest mode when you want dashboard visuals from freshly generated local results.

## Layout

- `edit/data.json`
- `edit/explore.json`
- `motion/data.json`
- `motion/explore.json`
- `composition/data.json`
- `composition/explore.json`
- `tests/data.json`

## Commands

From `bench-dashboard/`:

```bash
bun run fixtures:reset  # copy fixtures -> public
bun run dev:fixtures    # reset fixtures and run dev server
bun run dev:latest      # regenerate local benchmark/test data and run dev server
```
