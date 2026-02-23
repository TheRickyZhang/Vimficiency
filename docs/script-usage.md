See internal dependency graph:
prerequesisites: install
  sudo pacman -S graphviz
  luarocks install luafilesystem

```
lua scripts/dep-graph.lua > deps.dot
  dot -Tsvg deps.dot -o deps.svg
```
