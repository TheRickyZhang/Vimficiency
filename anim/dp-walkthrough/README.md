# DP walkthrough animation

3blue1brown-style walkthrough of the VimDiff planner's out/in DP.

- Acts 1–5: the `aa b cc` → `xx b zz` example — grid, step types, code-order
  fill with backpointers, per-cell candidate competition, traceback (the fused
  change-form plan at 10 beats two edits at 12).
- Act 6: a two-function C edit where a sealed matched run splits the DP into
  blocks (68% of cells pruned) and `maxPlans=2` returns two tied partitions.
  `trace2.json` is the slimmed (example/blocks/plans) export of that example.

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
VIMFY_TRACE_INITIAL='int add(int a, int b) {\n  int s = a + b;\n  log("add", s);\n  return s;\n}\n\nint main() {\n  int x = 0;\n  for(int i = 0; i < 10; i++) {\n    x += i;\n  }\n}' \
VIMFY_TRACE_GOAL='int add3(int a, int b, int c) {\n  int s = a + b;\n  log("add", s);\n  return s;\n}\n\nint main() {\n  int x = 0;\n  int n = 10;\n  for(int i = 0; i < n; i++) {\n    x -= foo(i);\n  }\n}' \
VIMFY_TRACE_OUT=/tmp/trace2-full.json \
  ./build/tests/vimfy_debug --gtest_filter='VimDiffTraceExport.*'
jq '{example, blocks, plans}' /tmp/trace2-full.json > anim/dp-walkthrough/trace2.json
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
