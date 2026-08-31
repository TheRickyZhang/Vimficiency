# DP walkthrough animation

3blue1brown-style walkthrough of the VimDiff planner's out/in DP.

- Acts 1–5: the `aa b cc` → `xx b zz` example — grid, step types, code-order
  fill with backpointers, per-cell candidate competition, traceback (the fused
  change-form plan at 10 beats two edits at 12).
- Act 6: a larger `main()` (5-statement loop body) where a 62-char matched run
  seals mid-paragraph, splitting the DP into two blocks — 75% of cells never
  built. The elimination is shown first (code view), then the shrinking grid,
  then a magnified inset fills part of block 1 in full detail; `maxPlans=2`
  surfaces tied partitions. `trace2.json` is the slimmed export
  (example/blocks/plans + the inset's cell window).

## Data source

`trace.json` is exported by `tests/Debug/VimDiffTraceExport.cpp`. It re-runs
the K=1 recurrence with the production cost oracles while logging every
relaxation, and asserts the traced optimum and regions against
`VimDiff::calculateBreakdown` — the animation cannot drift from the real
planner.

Regenerate (from the repo root; `VIMFY_TRACE_INITIAL` / `VIMFY_TRACE_GOAL`
override the buffers, literal `\n` for newlines):

```bash
cmake --build build --target vimfy_debug
VIMFY_TRACE_OUT=anim/dp-walkthrough/trace.json \
  ./build/tests/vimfy_debug --gtest_filter='VimDiffTraceExport.*'
VIMFY_TRACE_INITIAL='int main() {\n  int x = 0;\n  for(int i = 0; i < 10; i++) {\n    int a = bar(i);\n    int b = baz(a);\n    int c = qux(b);\n    int d = quux(c);\n    int e = corge(d);\n    x += a + b + c + d + e;\n  }\n}' \
VIMFY_TRACE_GOAL='int main() {\n  int x = 0;\n  int n = 10;\n  for(int i = 0; i < n; i++) {\n    int a = bar(i);\n    int b = baz(a);\n    int c = qux(b);\n    int d = quux(c);\n    int e = corge(d);\n    x -= foo(a + b + c + d + e);\n  }\n}' \
VIMFY_TRACE_OUT=/tmp/trace2-full.json \
  ./build/tests/vimfy_debug --gtest_filter='VimDiffTraceExport.*'
jq '{example, blocks, plans,
     insetWin: {i0:24, i1:34, j0:26, j1:42},
     insetOut: (.cells.out[0][24:35] | map(.[26:43])),
     insetIn:  (.cells.in[0][24:35]  | map(.[26:43]))}' \
  /tmp/trace2-full.json > anim/dp-walkthrough/trace2.json
```

## Render

Manim Community edition, no LaTeX required (formulas are set as text). One
venv and one `manim.cfg` are shared by every animation under `anim/`, so
render from `anim/`:

```bash
cd anim
uv venv .venv --python 3.12          # once
uv pip install --python .venv/bin/python manim
.venv/bin/manim render -qh dp-walkthrough/scene.py DPWalkthrough
```

Layout, all set by `anim/manim.cfg`:

```
anim/
  manim.cfg                 shared render config
  out/<quality>/            final videos    → out/1080p60/DPWalkthrough.mp4   (ignored)
  .cache/                   partial movies, text SVGs, logs — safe to delete  (ignored)
  .venv/                    manim install                                     (ignored)
  dp-walkthrough/
    scene.py trace.json trace2.json README.md                                (tracked)
```

The "partial movie directory is full" INFO line is manim pruning its per-`play()`
segment cache; the config raises the cap so re-renders reuse segments. To
embed in the repo README, drag-drop the mp4 into the GitHub web editor so it
becomes an inline-playable `user-attachments` asset — the video is not
committed.
