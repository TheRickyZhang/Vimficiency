-- Generic sidecar persistence for per-feature UI settings.
--
-- File layout: `stdpath("data")/vimficiency/<feature>_settings.json`
-- Envelope:    `{ "version": N, "data": { ...settings... } }`
--
-- Each feature owns its own version contract. `load` returns the parsed
-- envelope so the caller can detect the version and migrate or accept
-- as-is. `save` requires an explicit version so each feature can bump
-- independently of the others.
--
-- Best-effort throughout:
--   - missing file   → version=nil, data={} (cold start)
--   - malformed JSON → version=nil, data={} + one-time notify
--   - write errors   → silent (a settings save must never block the UI)
local M = {}

local function dir()
  return vim.fn.stdpath("data") .. "/vimficiency"
end

local function path(feature)
  return dir() .. "/" .. feature .. "_settings.json"
end

---@class VF.SettingsProfile.Envelope
---@field version integer|nil  schema version stored on disk; nil = no/malformed sidecar
---@field data table

---Load the saved envelope for `feature`. Returns `{ version = nil, data = {} }`
---when the sidecar is missing or malformed. The caller inspects
---`version` to decide between accept-as-is and migration.
---@param feature string  e.g. "explore", "play"
---@return VF.SettingsProfile.Envelope
function M.load(feature)
  local empty = { version = nil, data = {} }
  local fh = io.open(path(feature), "r")
  if not fh then return empty end
  local content = fh:read("*a")
  fh:close()
  if content == nil or content == "" then return empty end

  local ok, decoded = pcall(vim.json.decode, content)
  if not ok or type(decoded) ~= "table" then
    vim.schedule(function()
      vim.notify(
        "vimfy " .. feature .. ": settings JSON at " .. path(feature) ..
        " is malformed; ignoring", vim.log.levels.WARN)
    end)
    return empty
  end
  return {
    version = type(decoded.version) == "number" and decoded.version or nil,
    data = type(decoded.data) == "table" and decoded.data or {},
  }
end

---Persist `data` as the saved settings for `feature`, wrapped in the
---versioned envelope. Overwrites any existing file; silently no-ops on
---write failure.
---@param feature string
---@param version integer
---@param data table
function M.save(feature, version, data)
  vim.fn.mkdir(dir(), "p")
  local envelope = { version = version, data = data }
  local ok, encoded = pcall(vim.json.encode, envelope)
  if not ok then return end
  local fh = io.open(path(feature), "w")
  if not fh then return end
  fh:write(encoded)
  fh:close()
end

---Delete the sidecar for `feature`, reverting to the layered defaults.
---@param feature string
function M.clear(feature)
  pcall(os.remove, path(feature))
end

return M
