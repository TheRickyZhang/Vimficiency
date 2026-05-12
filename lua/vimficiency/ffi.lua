local core = require("vimficiency.ffi.core")

local M = {
  Key = core.Key,
  Finger = core.Finger,
  Hand = core.Hand,
  CountClass = core.CountClass,
  version = core.version,
  debug_config = core.debug_config,
}

local function merge(mod)
  for k, v in pairs(mod) do
    M[k] = v
  end
end

merge(require("vimficiency.ffi.config"))
merge(require("vimficiency.ffi.session"))
merge(require("vimficiency.ffi.optimizer"))
merge(require("vimficiency.ffi.sequence"))
merge(require("vimficiency.ffi.explore"))

return M
