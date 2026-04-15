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
`kdheepak/panvimdoc@main` action between the two shell scripts and
auto-commits `doc/vimficiency.txt` back via
`stefanzweifel/git-auto-commit-action`.

### Running it locally

panvimdoc is not packaged — it is a repo of pandoc Lua filters plus a
wrapper script. Clone it once:

```bash
git clone --depth 1 https://github.com/kdheepak/panvimdoc.git /tmp/panvimdoc
```

Prerequisites on the host: `pandoc` (Arch: `sudo pacman -S pandoc`).

Then from the repo root:

```bash
./scripts/build-vimdoc-source.sh
/tmp/panvimdoc/panvimdoc.sh \
  --project-name vimficiency \
  --input-file build/vimficiency.md \
  --vim-version "Neovim 0.11+" \
  --description "" \
  --toc true \
  --dedup-subheadings true \
  --treesitter true \
  --ignore-rawblocks true \
  --doc-mapping false \
  --doc-mapping-project-name true \
  --shift-heading-level-by 0 \
  --increment-heading-level-by 0 \
  --demojify false
./scripts/postprocess-vimdoc.sh
```

Flags must match the workflow so local output matches CI output —
update both sides together if you tune them.

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
