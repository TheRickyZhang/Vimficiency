local model = require("vimficiency.session.picker.model")
local sequence_display = require("vimficiency.sequence_display")

local M = {}

local function lines_for_active(item)
  if item.is_synthetic then
    return {
      "Recall ring (auto-generated key/time windows)",
      "",
      string.format("Entries:    %d", item.ring_count),
      string.format("Newest:     %s", model.format_age_ns(item.ring_newest_ns)),
      string.format("Oldest:     %s", model.format_age_ns(item.ring_oldest_ns)),
      string.format("Total keys: %d", item.ring_total_keys),
      "",
      "The ring is populated on every keystroke; entries are not directly",
      "actionable from this picker. Resolve a specific window via:",
      "  :Vimfy recall N    — N keys ago",
      "  :Vimfy recall Ns   — N seconds ago",
    }
  end

  local lines = {
    string.format("Alias:    %s", item.name),
    string.format("Type:     %s", item.category or "?"),
    string.format("Status:   %s", item.status),
    string.format("Started:  %s", model.format_age_ns(item.start_time_ns)),
  }
  if item.end_time_ns then
    table.insert(lines, string.format("Finished: %s", model.format_age_ns(item.end_time_ns)))
  end
  table.insert(lines, string.format("Keys:     %d", item.key_count or 0))
  if item.preview_seq and item.preview_seq ~= "" then
    table.insert(lines, "")
    table.insert(lines, "User seq:")
    table.insert(lines, "  " .. item.preview_seq)
  end
  local result = item.result
  if result then
    table.insert(lines, "")
    local user_cost = result.user_cost and string.format(" (cost: %.2f)", result.user_cost) or ""
    vim.list_extend(lines,
      sequence_display.prefixed_lines("User full: ", result.user_seq or "", nil, user_cost))
    table.insert(lines, "")
    table.insert(lines, "Optimal:")
    for i, r in ipairs(result.optimal_results or {}) do
      if i > 5 then break end
      vim.list_extend(lines,
        sequence_display.prefixed_lines(string.format("  %d. ", i), r.seq or "", nil,
          string.format("  (cost: %.2f)", r.cost or 0)))
    end
  end
  return lines
end

local function lines_for_saved(item)
  local lines = {
    string.format("Name:  %s", item.name),
    string.format("Path:  %s", vim.fn.fnamemodify(item.path, ":~")),
    string.format("Saved: %s", model.format_epoch(item.mtime_sec)),
    "",
  }
  local raw = vim.fn.readfile(item.path)
  local ok, data = pcall(vim.json.decode, table.concat(raw, "\n"))
  if not ok or type(data) ~= "table" then
    table.insert(lines, "(unable to parse file)")
    return lines
  end
  table.insert(lines, string.format("Position: (%d,%d) -> (%d,%d)",
    data.start_row or 0, data.start_col or 0, data.end_row or 0, data.end_col or 0))
  local user_cost = data.user_cost and string.format(" (cost: %.2f)", data.user_cost) or ""
  if data.user_seq and data.user_seq ~= "" then
    vim.list_extend(lines,
      sequence_display.prefixed_lines("User seq: ", data.user_seq, nil, user_cost))
  else
    table.insert(lines, "User seq: (none)" .. user_cost)
  end
  table.insert(lines, "")
  table.insert(lines, "Optimal:")
  for i, r in ipairs(data.optimal_results or {}) do
    if i > 5 then break end
    vim.list_extend(lines,
      sequence_display.prefixed_lines(string.format("  %d. ", i), r.seq or "", nil,
        string.format("  (cost: %.2f)", r.cost or 0)))
  end
  return lines
end

function M.lines_for_item(item)
  if item.pane == "active" then
    return lines_for_active(item)
  end
  return lines_for_saved(item)
end

return M
