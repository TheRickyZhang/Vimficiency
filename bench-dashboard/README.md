# Bench Dashboard

Local frontend for viewing benchmark trends, exploration data, and test timing.

## Prerequisites

- Run commands from `bench-dashboard/` unless noted otherwise.
- Install dependencies once: `bun install`
- For `dev:latest`, build test binaries first from repo root:
  - `cmake --build build -j`

## Quick Start

- Deterministic UI checks (recommended first):
  - `bun run dev:fixtures`
- Local latest data checks:
  - `bun run dev:latest`

## Command Reference

| Command | Purpose | Notes |
|---|---|---|
| `bun run dev` | Start Vite dev server with current `public/` data | No reset/regen |
| `bun run build` | Production build (`dist/`) | Matches CI frontend build step |
| `bun run preview` | Preview built `dist/` locally | Run after `build` |
| `bun run fixtures:reset` | Copy tracked fixtures into `public/` | Fast, deterministic baseline |
| `bun run dev:fixtures` | Reset fixtures, then start Vite | Best for reproducible UI checks |
| `bun run data:refresh` | Regenerate local benchmark/test data into `public/` | Runs benchmarks + tests |
| `bun run dev:latest` | Refresh data, then start Vite | Best for latest local performance/timing |

## Data Sources

- `fixtures/`: tracked deterministic sample data.
- `public/`: local generated working set used by the app at runtime.
- `scripts/sync-fixtures.sh`: copies `fixtures/` into `public/`.
- `scripts/refresh-local-data.sh`: resets fixtures, runs local binaries, ingests new results into `public/`.

## Typical Workflows

1. Verify UI behavior only:
   - `bun run dev:fixtures`
2. Verify with current local optimizer/test timing output:
   - `bun run dev:latest`
3. Return to deterministic baseline after local refresh:
   - `bun run fixtures:reset`

## Related Docs

- Repo-wide CI + deployment pipeline: `docs/ci-and-benchmarks.md`
- Fixture-specific notes: `fixtures/README.md`
