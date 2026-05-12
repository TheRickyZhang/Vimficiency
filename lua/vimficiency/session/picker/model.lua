local uv = vim.uv

local session = require("vimficiency.session")
local session_store = require("vimficiency.session.store")

local M = {}

local DEFAULT_DIRECTIONS = {
  alpha = "asc",
  category = "asc",
  created = "desc",
}

local STATUS_SYMBOLS = { ongoing = "●", saved = "✓", blank = " " }

M.DEFAULT_DIRECTIONS = DEFAULT_DIRECTIONS

function M.save_dir()
  return vim.fn.stdpath("data") .. "/vimficiency/saved"
end

function M.format_age_ns(t_ns)
  if not t_ns or t_ns == 0 then return "?" end
  local diff = (uv.hrtime() - t_ns) / 1e9
  if diff < 1 then return "just now" end
  if diff < 60 then return string.format("%ds ago", math.floor(diff)) end
  if diff < 3600 then return string.format("%dm ago", math.floor(diff / 60)) end
  if diff < 86400 then return string.format("%dh ago", math.floor(diff / 3600)) end
  return string.format("%dd ago", math.floor(diff / 86400))
end

function M.format_epoch(sec)
  if not sec or sec == 0 then return "?" end
  return os.date("%Y-%m-%d %H:%M", sec)
end

function M.fuzzy_match(haystack, needle)
  if not needle or needle == "" then return true end
  local h = haystack:lower()
  local n = needle:lower()
  local hi, ni = 1, 1
  while hi <= #h and ni <= #n do
    if h:sub(hi, hi) == n:sub(ni, ni) then ni = ni + 1 end
    hi = hi + 1
  end
  return ni > #n
end

function M.mark_key(item)
  return item.pane .. ":" .. item.key
end

function M.build_active_items()
  local summaries = session_store.summarize_all()
  local items = {}
  local recall_ring = {}
  for _, s in ipairs(summaries) do
    if s.type == "recall" and s.status == "active" then
      table.insert(recall_ring, s)
    else
      table.insert(items, {
        pane = "active",
        key = s.id,
        name = s.display_alias or s.id,
        category = s.type,
        status = s.status,
        start_time_ns = s.start_time,
        end_time_ns = s.end_time,
        key_count = s.key_count,
        preview_seq = s.preview,
        result = s.result,
        summary = s,
      })
    end
  end
  if #recall_ring > 0 then
    local oldest_ns, newest_ns = math.huge, -math.huge
    local total_keys = 0
    for _, s in ipairs(recall_ring) do
      if s.start_time < oldest_ns then oldest_ns = s.start_time end
      if s.start_time > newest_ns then newest_ns = s.start_time end
      total_keys = total_keys + (s.key_count or 0)
    end
    table.insert(items, {
      pane = "active",
      key = "__recall_ring__",
      name = "recall ring",
      category = "recall",
      status = "active",
      is_synthetic = true,
      start_time_ns = newest_ns,
      ring_count = #recall_ring,
      ring_oldest_ns = oldest_ns,
      ring_newest_ns = newest_ns,
      ring_total_keys = total_keys,
    })
  end
  return items
end

function M.build_saved_items()
  local names = session.list_saved()
  local items = {}
  for _, name in ipairs(names) do
    local path = M.save_dir() .. "/" .. name .. ".json"
    local stat = uv.fs_stat(path)
    local mtime_sec = stat and stat.mtime.sec or 0
    table.insert(items, {
      pane = "saved",
      key = name,
      name = name,
      category = "saved",
      status = "finished",
      mtime_sec = mtime_sec,
      path = path,
    })
  end
  return items
end

function M.sort_items(items, mode, direction)
  local function cmp_asc(a, b)
    if mode == "alpha" then
      return a.name < b.name
    elseif mode == "created" then
      local at = a.start_time_ns or (a.mtime_sec or 0) * 1e9
      local bt = b.start_time_ns or (b.mtime_sec or 0) * 1e9
      return at < bt
    else
      if (a.category or "") ~= (b.category or "") then
        return (a.category or "") < (b.category or "")
      end
      return a.name < b.name
    end
  end
  table.sort(items, cmp_asc)
  if direction == "desc" then
    local n = #items
    for i = 1, math.floor(n / 2) do
      items[i], items[n - i + 1] = items[n - i + 1], items[i]
    end
  end
end

function M.group_into_sections(items, mode, direction)
  local ongoing, rest = {}, {}
  for _, it in ipairs(items) do
    if it.pane == "active" and it.status == "active" then
      table.insert(ongoing, it)
    else
      table.insert(rest, it)
    end
  end
  M.sort_items(ongoing, mode, direction)
  M.sort_items(rest, mode, direction)

  local sections = {}
  if #ongoing > 0 then
    table.insert(sections, { title = "Ongoing", items = ongoing })
  end
  if mode == "category" then
    local by_cat = {}
    local order = {}
    for _, it in ipairs(rest) do
      local c = it.category or "other"
      if not by_cat[c] then
        by_cat[c] = {}
        table.insert(order, c)
      end
      table.insert(by_cat[c], it)
    end
    table.sort(order)
    for _, c in ipairs(order) do
      table.insert(sections, { title = c, items = by_cat[c] })
    end
  elseif #rest > 0 then
    table.insert(sections, { title = "All", items = rest })
  end
  return sections
end

function M.row_for_item(item, marked)
  local sym
  if item.pane == "active" and item.status == "active" then
    sym = STATUS_SYMBOLS.ongoing
  elseif item.pane == "saved" then
    sym = STATUS_SYMBOLS.saved
  else
    sym = STATUS_SYMBOLS.blank
  end
  local mark = item.is_synthetic and " " or (marked[M.mark_key(item)] and "*" or " ")
  local cat = item.category or ""
  if item.is_synthetic then
    local name = string.format("%s (%d)", item.name, item.ring_count)
    local age = string.format("oldest %s", M.format_age_ns(item.ring_oldest_ns))
    return string.format("  %s %s %-16s  %-8s  %s", mark, sym, name, cat, age)
  end
  local age
  if item.pane == "active" then
    age = M.format_age_ns(item.start_time_ns)
  else
    age = M.format_epoch(item.mtime_sec)
  end
  return string.format("  %s %s %-16s  %-8s  %s", mark, sym, item.name, cat, age)
end

return M
