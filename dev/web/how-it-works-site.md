# "How it works" on the docs site

The algorithm explainer lives on the Starlight site (`web/docs/`) as a chapter
group, not in the README. The README keeps a summary, the two walkthrough
videos, and a link. Each chapter leads with a concept diagram or a live widget
and puts the precise statement after it.

The widgets run the **real planner**, compiled to WebAssembly from the same
`src/Optimizer/DiffPlanner/` sources the plugin ships. There is no TypeScript
reimplementation to drift.

## Layout

| piece | path |
|---|---|
| chapters (MDX, site-only) | `doc-src/how-it-works/*.mdx` |
| widgets | `web/docs/src/components/{Plan,Grid,Tiling,Seal}Widget.astro` |
| wasm loader + typings | `web/docs/src/scripts/planner.ts` |
| shared styles + palette | `web/docs/src/styles/how-it-works.css` |
| concept diagrams | `web/docs/src/assets/how-it-works/*.svg` |
| C ABI | `src/Wasm/PlannerExports.cpp` |
| read-only planner surfaces | `src/Optimizer/DiffPlanner/VimDiffTrace.h`, `TilingCost::cover` |
| built module (CI only, ignored) | `web/docs/public/wasm/vimfy_planner.{js,wasm}` |

Chapters sit under `doc-src/` so the content collection stays one glob
(`**/*.{md,mdx}`), but the vimdoc and doc-lint scripts glob top-level `*.md`
only and never see them. Each chapter sets `sidebar: { hidden: true }` and is
listed explicitly in the "How it works" sidebar group in `astro.config.mjs`;
the Guide group autogenerates from `directory: '../../doc-src'` (the glob
loader records root-relative paths, so `'.'` matched nothing and left that
group empty before this change).

Chapters import components by relative path
(`../../web/docs/src/components/PlanWidget.astro`). SVG diagrams are imported
the same way and rendered inline as Astro SVG components so the CSS palette
applies to them.

| chapter | widget | export |
|---|---|---|
| `planning-a-change` | PlanWidget | `vf_plan` → `calculateBreakdown` |
| `the-grid` | GridWidget | `vf_grid` → `VimDiff::trace` |
| `pricing-spans` | TilingWidget | `vf_tiling` → `TilingCost::cover` + `query` + `chainCost` |
| `sealing` | SealWidget | `vf_seal` → `sealMatchedRuns` |

## The wasm module

`CMakeLists.txt` has a `VIMF_WASM` option that builds `vimfy_planner_wasm`
from an explicit source list (the planner closure: DiffPlanner, Boundary,
Effort, Keyboard) plus `src/Wasm/PlannerExports.cpp`, and returns before the
native targets. `src/Wasm/` is excluded from the `vimfy_core` glob.

```bash
emcmake cmake -B build-wasm -DVIMF_WASM=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --target vimfy_planner_wasm
mkdir -p web/docs/public/wasm
cp build-wasm/wasm/vimfy_planner.js build-wasm/wasm/vimfy_planner.wasm web/docs/public/wasm/
cd web/docs && bun run dev
```

Emscripten comes from `brew install emscripten`, `pacman -S emscripten` (then
`source /etc/profile.d/emscripten.sh`, or open a new login shell), or the
emsdk. Both CI pipelines install the emsdk release in `.emscripten-version`
through `.github/actions/setup-emscripten`, so bump the version there only.

Contract of the exports:

- `extern "C"` functions taking UTF-8 C strings and numbers, returning a
  `malloc`'d JSON string the caller frees with `_free`. `planner.ts` wraps
  this (`callJson`) and parses `{ "error": ... }` into a thrown `Error`.
- Input is **sanitized, not rejected**: any byte with no `CHAR_TO_KEYS` entry
  becomes `?`. `KeyedSequence::append` uses `CHAR_TO_KEYS.at()`, the only
  throwing call in the closure, and the module is built without exception
  catching (a throw would abort it).
- Inputs are capped (`MAX_TEXT`, and 200 chars a side for the grid dump) and
  the planner runs with a small `maxPlannerCells`, so a pasted file cannot grow
  memory without bound.
- `INF` costs serialize as `null`.
- Layout strings are `qwerty`, `colemak_dh`, `uniform`.

The ES6 glue is loaded at runtime from `public/wasm/` by dynamic import
(`import.meta.env.BASE_URL + '/wasm/vimfy_planner.js'`), not bundled by Vite:
the glue locates the `.wasm` via `import.meta.url`, and a missing module (a
local docs build without the wasm step) must not fail the build. Widgets then
show a one-line "not available" message instead of a blank box.

## Deploy

Both pipelines run `scripts/build-docs-site.sh`, which builds the wasm and the
docs, checks the output pages, and calls every export through
`scripts/smoke-planner-wasm.ts`. The smoke test loads the module in Bun, so it
does not cover the browser fetch path in `planner.ts`.

- PR check: `.github/workflows/vimdoc-check.yml` runs on any `src/**` change,
  because the module compiles sources from several modules.
- Main: `scripts/bench-local-run.sh` runs it on main only. A failure keeps the
  previously published docs, still publishes the benchmark point, and fails the
  run at the end.

## Palette

`how-it-works.css` defines `--vf-delete`, `--vf-type`, `--vf-kept`/`--vf-move`,
`--vf-opt`, `--vf-alt` with the colors of `anim/dp-walkthrough/scene.py`
(`STEP_COLOR`), so the videos and the widgets read the same: deletions red,
typed text orange, kept/moved-over text grey, the optimum yellow. Keep the two
in sync when either changes.

## Trace surfaces in core

Two read-only additions exist for the widgets and are not used by `calculate`:

- `TilingCost::cover(begin, end)` returns the chunks of the cheapest tiling.
  `Arrival` carries the position it sits at, the position it came from, and
  the chunk kind/count; the sweep's aggregation sites copy arrivals
  (`lower`) while chunk extensions set the backpointer (`relax`).
- `VimDiff::trace` runs the normal pipeline and copies the DP tables, the
  optimal path, and the sealed blocks out of the private `Planned` state.
  `walk` takes a visitor so reconstruction and tracing share one loop.
