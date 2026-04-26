#!/usr/bin/env lua
-- Emit a Graphviz .dot file of directory-level include dependencies.
-- Usage: lua scripts/dep-graph.lua > deps.dot

local lfs = require("lfs")

local script_dir = arg[0]:match("(.+)/[^/]+$") or "."
local root = script_dir .. "/../src"

-- Normalize a path: collapse "foo/../bar" and strip trailing /
local function normalize(path)
  local parts = {}
  for seg in path:gmatch("[^/]+") do
    if seg == ".." and #parts > 0 and parts[#parts] ~= ".." then
      parts[#parts] = nil
    elseif seg ~= "." then
      parts[#parts + 1] = seg
    end
  end
  return table.concat(parts, "/")
end

-- Collect all .h and .cpp files under src/
local files = {} -- list of paths relative to root (e.g. "Optimizer/NavOptimizer/NavOptimizer.cpp")
local function walk(dir, prefix)
  for entry in lfs.dir(dir) do
    if entry ~= "." and entry ~= ".." then
      local full = dir .. "/" .. entry
      local attr = lfs.attributes(full)
      if attr and attr.mode == "directory" then
        walk(full, prefix .. entry .. "/")
      elseif entry:match("%.[ch]pp$") or entry:match("%.h$") or entry:match("%.inc$") then
        files[#files + 1] = prefix .. entry
      end
    end
  end
end
walk(root, "")

-- Build a set of known files for resolution
local known = {}
for _, f in ipairs(files) do
  known[f] = true
end

-- Map a file path (relative to src/) to its module name
local function to_module(rel)
  local dir = rel:match("^(.+)/[^/]+$")
  if not dir then return "root" end
  return dir
end

-- Resolve an include path from a given source file
local function resolve(include_path, source_file)
  -- Try relative to src/ first
  if known[include_path] then
    return include_path
  end
  -- Try relative to the source file's directory
  local dir = source_file:match("^(.+)/") or ""
  local candidate = dir ~= "" and (dir .. "/" .. include_path) or include_path
  candidate = normalize(candidate)
  if known[candidate] then
    return candidate
  end
  return nil
end

-- Parse includes and collect edges
-- edges[src_mod][dst_mod] = count
local edges = {}

for _, src_file in ipairs(files) do
  local full_path = root .. "/" .. src_file
  local fh = io.open(full_path, "r")
  if fh then
    local src_mod = to_module(src_file)
    for line in fh:lines() do
      local inc = line:match('^%s*#include%s+"([^"]+)"')
      if inc then
        local resolved = resolve(inc, src_file)
        if resolved then
          local dst_mod = to_module(resolved)
          if dst_mod ~= src_mod then
            if not edges[src_mod] then edges[src_mod] = {} end
            edges[src_mod][dst_mod] = (edges[src_mod][dst_mod] or 0) + 1
          end
        else
          io.stderr:write("warning: unresolved include \"" .. inc .. "\" in " .. src_file .. "\n")
        end
      end
    end
    fh:close()
  end
end

-- Collect all modules that appear as source or destination
local modules = {}
for src, dsts in pairs(edges) do
  modules[src] = true
  for dst in pairs(dsts) do
    modules[dst] = true
  end
end

-- Cluster definitions: { name, label, fill_color, edge_color, members }
local clusters = {
  { name = "optimizer", label = "Optimizer", color = "#fff3e0", edge = "#d84315",
    members = {"Optimizer", "Optimizer/CompositionOptimizer",
               "Optimizer/TransformOptimizer", "Optimizer/NavOptimizer"} },
  { name = "keyboard",  label = "Input",     color = "#e8f5e9", edge = "#2e7d32",
    members = {"Keyboard", "Keyboard/ToKeys", "Effort"} },
  { name = "vim",       label = "Vim Model",  color = "#e3f2fd", edge = "#1565c0",
    members = {"VimCore", "Boundary"} },
}

-- Map module -> its cluster's edge color
local mod_edge_color = {}
local clustered = {}
for _, cl in ipairs(clusters) do
  for _, m in ipairs(cl.members) do
    clustered[m] = true
    mod_edge_color[m] = cl.edge
  end
end

-- Display label: use leaf directory name for nested modules
local function display_label(mod)
  if mod == "root" then return "root" end
  return mod:match("([^/]+)$") or mod
end

local default_edge_color = "#777777"

-- Emit .dot
print("digraph deps {")
print("  rankdir=TB;")
print("  newrank=true;")
print("  compound=true;")
print("  nodesep=0.5;")
print("  ranksep=0.7;")
print('  node [shape=box, style="filled,rounded", fillcolor="#e8e8e8",')
print('        fontname="Helvetica", fontsize=11, margin="0.12,0.06"];')
print('  edge [fontname="Helvetica", fontsize=8, arrowsize=0.6];')
print()

-- Clustered nodes
for _, cl in ipairs(clusters) do
  print(string.format("  subgraph cluster_%s {", cl.name))
  print(string.format('    label="%s";', cl.label))
  print(string.format('    style="filled,rounded"; fillcolor="%s";', cl.color))
  print('    fontname="Helvetica"; fontsize=12;')
  for _, m in ipairs(cl.members) do
    if modules[m] then
      print(string.format('    "%s" [label="%s"];', m, display_label(m)))
    end
  end
  print("  }")
  print()
end

-- Unclustered nodes (skip entry points: root, Session)
local skip_modules = { root = true, Session = true }
for m in pairs(modules) do
  if not clustered[m] and not skip_modules[m] then
    print(string.format('  "%s" [label="%s"];', m, display_label(m)))
  end
end
print()

-- Layout hints
print("  // Force Optimizer sub-modules side-by-side")
print('  { rank=same; "Optimizer/CompositionOptimizer";')
print('               "Optimizer/TransformOptimizer";')
print('               "Optimizer/NavOptimizer"; }')
print("  // Middle layer")
print('  { rank=same; "Interpreter"; "Effort"; "Keyboard";')
print('               "Keyboard/ToKeys"; }')
print("  // Lower layer")
print('  { rank=same; "VimCore"; "Boundary"; }')
print("  // Sinks")
print('  { rank=same; "Types"; "Utils"; }')
print()

-- Edges: color by source cluster, penwidth by count
local sorted_src = {}
for s in pairs(edges) do sorted_src[#sorted_src + 1] = s end
table.sort(sorted_src)
for _, src in ipairs(sorted_src) do
  if not skip_modules[src] then
    local sorted_dst = {}
    for d in pairs(edges[src]) do sorted_dst[#sorted_dst + 1] = d end
    table.sort(sorted_dst)
    local ec = mod_edge_color[src] or default_edge_color
    for _, dst in ipairs(sorted_dst) do
      if not skip_modules[dst] then
        local count = edges[src][dst]
        local pw = 0.5 + math.log(count + 1) * 0.7
        print(string.format(
          '  "%s" -> "%s" [penwidth=%.1f, color="%s", tooltip="%d includes"];',
          src, dst, pw, ec, count))
      end
    end
  end
end

print("}")
