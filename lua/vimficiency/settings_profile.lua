-- Generic sidecar persistence for per-feature UI settings.
--
-- File layout: `stdpath("data")/vimficiency/<feature>_settings.json`
-- Envelope:    `{ "version": N, "data": { ...settings... } }`
--
-- The envelope lets us migrate cleanly — future versions can detect an
-- older shape and transform it instead of discarding. For now, a version
-- mismatch is treated as "no saved state" and the user's last toggles
-- are quietly forgotten; they can re-set via the settings modal.
--
-- Best-effort throughout:
--   - missing file   → empty data (cold start)
--   - malformed JSON → empty data + one-time notify
--   - version skew   → empty data (silent; rewritten on next save)
--   - write errors   → silent (a settings save must never block the UI)
local M = {}

local SCHEMA_VERSION = 1

local function dir()
  return vim.fn.stdpath("data") .. "/vimficiency"
end

local function path(feature)
  return dir() .. "/" .. feature .. "_settings.json"
end

---Load the saved settings table for `feature`. Returns `{}` when the
---sidecar is missing, malformed, or at an incompatible schema version.
---@param feature string  e.g. "explore", "play"
---@return table
function M.load(feature)
  local fh = io.open(path(feature), "r")
  if not fh then return {} end
  local content = fh:read("*a")
  fh:close()
  if content == nil or content == "" then return {} end

  local ok, decoded = pcall(vim.json.decode, content)
  if not ok or type(decoded) ~= "table" then
    vim.schedule(function()
      vim.notify(
        "vimfy " .. feature .. ": settings JSON at " .. path(feature) ..
        " is malformed; ignoring", vim.log.levels.WARN)
    end)
    return {}
  end
  if decoded.version ~= SCHEMA_VERSION then
    return {}
  end
  return type(decoded.data) == "table" and decoded.data or {}
end

---Persist `data` as the saved settings for `feature`, wrapped in the
---versioned envelope. Overwrites any existing file; silently no-ops on
---write failure.
---@param feature string
---@param data table
function M.save(feature, data)
  vim.fn.mkdir(dir(), "p")
  local envelope = { version = SCHEMA_VERSION, data = data }
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
