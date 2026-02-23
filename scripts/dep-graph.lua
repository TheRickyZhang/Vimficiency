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
local files = {} -- list of paths relative to root (e.g. "Optimizer/MotionOptimizer/MotionOptimizer.cpp")
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

-- Emit .dot
print("digraph deps {")
print("  rankdir=LR;")
print('  node [shape=box, style=filled, fillcolor="#e8e8e8"];')

-- Nodes
local sorted_mods = {}
for m in pairs(modules) do sorted_mods[#sorted_mods + 1] = m end
table.sort(sorted_mods)
for _, m in ipairs(sorted_mods) do
  print(string.format('  "%s";', m))
end

-- Edges
print()
local sorted_src = {}
for s in pairs(edges) do sorted_src[#sorted_src + 1] = s end
table.sort(sorted_src)
for _, src in ipairs(sorted_src) do
  local sorted_dst = {}
  for d in pairs(edges[src]) do sorted_dst[#sorted_dst + 1] = d end
  table.sort(sorted_dst)
  for _, dst in ipairs(sorted_dst) do
    local count = edges[src][dst]
    print(string.format('  "%s" -> "%s" [label="%d"];', src, dst, count))
  end
end

print("}")
