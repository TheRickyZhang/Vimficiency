local core = require("vimficiency.ffi.core")

local M = {}

local lib = core.lib

---@param key_seq VF.KeyEvent[]
---@return string
function M.build_sequence(key_seq)
  local parts = {}
  for i = 1, #key_seq do
    local ev = key_seq[i]
    assert(not ev.key_typed:find("[\x1e\x1f]"),
      "build_sequence: key_typed contains a record/field separator; " ..
      "callers must pass keytrans'd strings")
    parts[#parts + 1] = ev.mode
    parts[#parts + 1] = core.EVENT_FIELD_SEP
    parts[#parts + 1] = ev.key_typed
    parts[#parts + 1] = core.EVENT_RECORD_SEP
  end
  local payload = table.concat(parts)
  local result = core.slice_to_string(lib.vf_build_sequence(payload, #payload))
  if result:sub(1, 7) == "ERROR: " then
    error("vf_build_sequence failed: " .. result, 0)
  end
  return result
end

---@param start_lines string[]
---@param end_lines string[]
---@param start_row integer
---@param end_row integer
---@param padding integer
---@return integer
---@return integer
function M.compute_search_region(start_lines, end_lines, start_row, end_row, padding)
  local start_payload = core.encode_line_array(start_lines, "start_lines")
  local end_payload = core.encode_line_array(end_lines, "end_lines")
  local result = core.slice_to_string(lib.vf_compute_search_region(
    start_payload,
    #start_payload,
    end_payload,
    #end_payload,
    start_row,
    end_row,
    padding
  ))
  if result:sub(1, 7) == "ERROR: " then
    error("vf_compute_search_region failed: " .. result, 0)
  end
  local parts = vim.split(result, core.EVENT_FIELD_SEP, { plain = true, trimempty = true })
  assert(#parts == 2, "vf_compute_search_region returned malformed payload")
  local a = assert(tonumber(parts[1]), "vf_compute_search_region: non-numeric region start")
  local b = assert(tonumber(parts[2]), "vf_compute_search_region: non-numeric region end")
  return a, b
end

---@param records table<string, { time_started: integer, first_mode: string|nil }>
---@param order string[]
---@param target_hrtime integer
---@param budget integer
---@return integer|nil
function M.resolve_recall_cutoff(records, order, target_hrtime, budget)
  local parts = {}
  for i = 1, #order do
    local rec = records[order[i]]
    assert(rec, "resolve_recall_cutoff: missing record for order index " .. i)
    parts[#parts + 1] = core.encode_int64(rec.time_started)
    parts[#parts + 1] = rec.first_mode or ""
  end
  local encoded = core.encode_string_list(parts)
  local index = lib.vf_resolve_recall_cutoff(
    encoded,
    #encoded,
    target_hrtime,
    budget
  )
  if index == 0 then
    return nil
  end
  return index
end

---@param start_row integer
---@param cursor_row integer
---@param last_key_time_ns integer|nil
---@param now_ns integer
---@param max_search_lines integer
---@param manual_idle_timeout_seconds integer
---@return string|nil
function M.manual_evict_reason(
  start_row,
  cursor_row,
  last_key_time_ns,
  now_ns,
  max_search_lines,
  manual_idle_timeout_seconds
)
  local has_last_key = last_key_time_ns ~= nil
  local code = lib.vf_manual_evict_reason(
    start_row,
    cursor_row,
    last_key_time_ns or 0,
    has_last_key,
    now_ns,
    max_search_lines,
    manual_idle_timeout_seconds
  )
  if code == 1 then
    return "cursor drifted beyond MAX_SEARCH_LINES (" .. max_search_lines .. ")"
  end
  if code == 2 then
    return "idle for more than " .. manual_idle_timeout_seconds .. "s"
  end
  return nil
end

return M
