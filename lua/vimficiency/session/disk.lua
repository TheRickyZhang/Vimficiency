local alias_mod = require("vimficiency.session.alias")

local M = {}

local EMPTY_ARRAY_MT = { __jsontype = "array" }

function M.empty_array()
  return setmetatable({}, EMPTY_ARRAY_MT)
end

function M.save_dir()
  return vim.fn.stdpath("data") .. "/vimficiency/saved"
end

---@param value any
---@param level integer|nil
---@return string
local function encode_pretty(value, level)
  if type(value) ~= "table" then
    return vim.json.encode(value)
  end
  level = level or 0
  local pad = string.rep("  ", level)
  local inner = string.rep("  ", level + 1)
  if next(value) == nil then
    local mt = getmetatable(value)
    if mt and mt.__jsontype == "array" then return "[]" end
    if not vim.islist(value) then return "{}" end
    error(
      "encode_pretty: bare empty table is shape-ambiguous; " ..
      "use vim.empty_dict() for objects or session.empty_array() for arrays"
    )
  end
  if vim.islist(value) then
    local parts = {}
    for _, item in ipairs(value) do
      parts[#parts + 1] = inner .. encode_pretty(item, level + 1)
    end
    return "[\n" .. table.concat(parts, ",\n") .. "\n" .. pad .. "]"
  end
  local keys = {}
  for k in pairs(value) do keys[#keys + 1] = k end
  table.sort(keys)
  local parts = {}
  for _, k in ipairs(keys) do
    parts[#parts + 1] = inner
      .. vim.json.encode(tostring(k))
      .. ": "
      .. encode_pretty(value[k], level + 1)
  end
  return "{\n" .. table.concat(parts, ",\n") .. "\n" .. pad .. "}"
end

---@param name string
---@param data table
---@return string|nil path
---@return string|nil error
function M.save(name, data)
  local save_dir = M.save_dir()
  vim.fn.mkdir(save_dir, "p")
  local path = save_dir .. "/" .. name .. ".json"
  local lines = vim.split(encode_pretty(data), "\n")
  local ok, err = pcall(vim.fn.writefile, lines, path)
  if not ok then
    return nil, err
  end
  return path, nil
end

---@param data any
---@return string|nil err
function M.validate(data)
  if type(data) ~= "table" then
    return "top-level must be a JSON object"
  end
  if type(data.lines) ~= "table" or #data.lines == 0 then
    return "missing or empty 'lines' field"
  end
  if data.goal_lines ~= nil and type(data.goal_lines) ~= "table" then
    return "non-array 'goal_lines' field"
  end
  if type(data.start_row) ~= "number" then
    return "missing or non-numeric 'start_row' field"
  end
  if type(data.start_col) ~= "number" then
    return "missing or non-numeric 'start_col' field"
  end
  if data.has_lines_above ~= nil and type(data.has_lines_above) ~= "boolean" then
    return "non-boolean 'has_lines_above' field"
  end
  if data.has_lines_below ~= nil and type(data.has_lines_below) ~= "boolean" then
    return "non-boolean 'has_lines_below' field"
  end
  if data.window_height ~= nil and type(data.window_height) ~= "number" then
    return "non-numeric 'window_height' field"
  end
  if data.scroll_amount ~= nil and type(data.scroll_amount) ~= "number" then
    return "non-numeric 'scroll_amount' field"
  end
  if type(data.optimal_results) ~= "table" then
    return "missing or non-array 'optimal_results' field"
  end
  for i, r in ipairs(data.optimal_results) do
    if type(r) ~= "table" then
      return string.format("optimal_results[%d] is not an object", i)
    end
    if type(r.seq) ~= "string" then
      return string.format("optimal_results[%d] missing or non-string 'seq'", i)
    end
    if type(r.cost) ~= "number" then
      return string.format("optimal_results[%d] missing or non-numeric 'cost'", i)
    end
  end
  return nil
end

---@param name string
---@return table|nil data
---@return string|nil error
---@return boolean is_missing
function M.load(name)
  local path = M.save_dir() .. "/" .. name .. ".json"
  if vim.fn.filereadable(path) == 0 then
    return nil, "File not found: " .. path, true
  end
  local lines = vim.fn.readfile(path)
  local json_str = table.concat(lines, "\n")
  local ok, data = pcall(vim.json.decode, json_str)
  if not ok then
    return nil, "Failed to parse JSON: " .. tostring(data), false
  end
  local schema_err = M.validate(data)
  if schema_err then
    return nil, "Malformed result file: " .. schema_err, false
  end
  return data, nil, false
end

---@param name string
---@param result VF.Session.Result
---@return string|nil path
---@return string|nil err
function M.write_with_overwrite_warn(name, result)
  local dest_path = M.save_dir() .. "/" .. name .. ".json"
  local existed = vim.fn.filereadable(dest_path) == 1
  local path, err = M.save(name, result)
  if not path then return nil, err end
  if existed then
    vim.notify("vimfy: overwrote existing saved result [" .. name .. "]",
      vim.log.levels.WARN)
  end
  return path, nil
end

---@return string[]
function M.list()
  local save_dir = M.save_dir()
  if vim.fn.isdirectory(save_dir) == 0 then
    return {}
  end
  local files = vim.fn.glob(save_dir .. "/*.json", false, true)
  local names = {}
  for _, path in ipairs(files) do
    local name = vim.fn.fnamemodify(path, ":t:r")
    table.insert(names, name)
  end
  return names
end

---@param name string
---@return boolean ok
---@return string|nil err
function M.remove(name)
  local path = M.save_dir() .. "/" .. name .. ".json"
  if vim.fn.filereadable(path) == 0 then
    return false, "No saved result '" .. name .. "' at " .. path
  end
  if vim.fn.delete(path) ~= 0 then
    return false, "vimfy rm failed for " .. path
  end
  return true, path
end

---@param old_name string
---@param new_name string
---@return boolean ok
---@return string|nil err
function M.rename(old_name, new_name)
  if not alias_mod.is_valid_saved_name(old_name) then
    return false, "Invalid source name '" .. tostring(old_name) .. "'."
  end
  if not alias_mod.is_valid_saved_name(new_name) then
    return false, "Invalid target name '" .. tostring(new_name) .. "'."
  end
  if old_name == new_name then
    return false, "Source and target names are identical."
  end
  local src = M.save_dir() .. "/" .. old_name .. ".json"
  local dst = M.save_dir() .. "/" .. new_name .. ".json"
  if vim.fn.filereadable(src) == 0 then
    return false, "No saved result '" .. old_name .. "' at " .. src
  end
  if vim.fn.filereadable(dst) == 1 then
    return false, "Target '" .. new_name .. "' already exists."
  end
  local ok, err = vim.uv.fs_rename(src, dst)
  if not ok then
    return false, "rename failed: " .. tostring(err)
  end
  return true, nil
end

---@param src_name string
---@param dst_name string
---@return boolean ok
---@return string|nil err
function M.duplicate(src_name, dst_name)
  if not alias_mod.is_valid_saved_name(src_name) then
    return false, "Invalid source name '" .. tostring(src_name) .. "'."
  end
  if not alias_mod.is_valid_saved_name(dst_name) then
    return false, "Invalid target name '" .. tostring(dst_name) .. "'."
  end
  if src_name == dst_name then
    return false, "Source and target names are identical."
  end
  local src = M.save_dir() .. "/" .. src_name .. ".json"
  local dst = M.save_dir() .. "/" .. dst_name .. ".json"
  if vim.fn.filereadable(src) == 0 then
    return false, "No saved result '" .. src_name .. "' at " .. src
  end
  if vim.fn.filereadable(dst) == 1 then
    return false, "Target '" .. dst_name .. "' already exists."
  end
  local ok, err = vim.uv.fs_copyfile(src, dst, nil)
  if not ok then
    return false, "copy failed: " .. tostring(err)
  end
  return true, nil
end

return M
