# Script usage

## Dependency graph

See internal dependency graph:

Prerequisites:
```bash
sudo pacman -S graphviz
luarocks install luafilesystem
```

```bash
lua scripts/dep-graph.lua > deps.dot
dot -Tsvg deps.dot -o deps.svg
```

## Generating `:help vimficiency` (vimdoc)

The in-editor help shown by `:Vimfy help` / `:help vimficiency` is a
vimdoc file (`doc/vimficiency.txt`) generated from the user-facing
markdown chapters in `doc-src/`. The markdown is the single source of
truth; the vimdoc is derived and regenerated in CI.

### Pipeline

```
doc-src/NN-*.md                (edit these)
        │
        ▼ scripts/build-vimdoc-source.sh
        │   - strips nav lines, demotes headings, flattens *.md links
        │
build/vimficiency.md           (intermediate; build/ is gitignored)
        │
        ▼ panvimdoc (pandoc custom writer)
        │
doc/vimficiency.txt            (committed)
        │
        ▼ scripts/postprocess-vimdoc.sh
        │   - adds a bare `*vimficiency*` tag so `:help vimficiency`
        │     resolves without needing `:help vimficiency.txt`
        │
doc/vimficiency.txt            (committed, final)
```

### CI

`.github/workflows/vimdoc.yml` runs on pushes to `main` that touch
`doc-src/**` or the two scripts. It invokes the
`kdheepak/panvimdoc@v4.0.1` action between the two shell scripts and
auto-commits `doc/vimficiency.txt` back via
`stefanzweifel/git-auto-commit-action`. The pinned tag must stay in
sync with `.github/workflows/vimdoc-check.yml` (same tag, same output)
— bumping one without the other produces divergent vimdoc and a flaky
CI diff.

### Running it locally

panvimdoc is not packaged — it is a repo of pandoc Lua filters plus a
wrapper script. Clone it once at the same tag CI pins to (`v4.0.1`) so
local runs produce byte-identical output and the `vimdoc-check` job
doesn't fail on a drift-only diff:

```bash
git clone --depth 1 --branch v4.0.1 https://github.com/kdheepak/panvimdoc.git /tmp/panvimdoc
```

Prerequisites on the host: `pandoc` (Arch: `sudo pacman -S pandoc`).

Then from the repo root:

```bash
./scripts/build-vimdoc.sh
```

This wraps all three steps (source concat → panvimdoc → postprocess).
Override the panvimdoc clone location with `PANVIMDOC_DIR=...` if
you've put it somewhere other than `/tmp/panvimdoc`. Flags inside the
script must match the workflow so local output matches CI output —
update both sides together if you tune them.

### Why a single output file?

Neovim supports multiple help files per plugin (tag lookup is global
across `doc/` on `runtimepath`), so we could split into e.g.
`doc/vimficiency-config.txt`, `doc/vimficiency-commands.txt`, etc.
Today we don't — the generated file is ~1150 lines, which is well
within single-file territory (fugitive ~1500, plenary ~800). The
build pipeline assumes one output.

Revisit if either of these becomes true:
- Total vimdoc size passes ~2000 lines and single-file navigation
  gets awkward.
- One topic (e.g. effort-model internals) grows large enough to
  stand alone as `doc/vimficiency-<topic>.txt`.

Splitting would mean: grouping `doc-src/` chapters into buckets in
`scripts/build-vimdoc-source.sh`, running panvimdoc per bucket in
`scripts/build-vimdoc.sh`, and applying the `*vimficiency*` tag only
to the primary file in `scripts/postprocess-vimdoc.sh`.

### Editing the user docs

- Edit `doc-src/NN-*.md` normally. GitHub renders them as a
  navigable guide; CI regenerates the vimdoc on merge.
- The intermediate `build/vimficiency.md` is inside the gitignored
  `build/` tree — don't commit it and don't edit it by hand; it's
  overwritten on every build.
- If you add a chapter, just drop in a new `doc-src/NN-title.md`
  file following the existing numbering. The build script globs
  `[0-9][0-9]-*.md` in order.
- Navigation lines at the top/bottom of each chapter (`**[← Prev]** |
  **[Index]** | **[Next →]**`) are matched and stripped by the
  generator (the `**[Index]` pattern is the anchor). Keep them intact;
  breaking the pattern will leak navigation into `:help`.
- Cross-chapter links written as `[text](08-keymaps.md)` get reduced to
  plain `text` in vimdoc. That's fine for flat help; if you ever want
  real vimdoc tag links, add them as `|vimficiency-keymaps|` directly
  alongside and the Markdown renderer will still show the text.
