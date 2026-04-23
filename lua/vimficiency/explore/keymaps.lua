-- Buffer-local keymaps for the explore scratch buffer + its companion
-- recommendation list buffer. Declarative: each spec entry names a
-- `handler` key that the caller's `handlers` table must provide — so this
-- module stays ignorant of session state, forward-declared locals, etc.
local util = require("vimficiency.util")

local M = {}

---@class VimficiencyExploreKeymapSpec
---@field lhs string        # key sequence
---@field handler string    # handler name — resolved against the `handlers` table passed to `install`
---@field desc string       # human-readable description (shown by :map etc.)
---@field mode? string      # default "n"
---@field nowait? boolean

-- Scratch buffer is deliberately minimal: only session-flow keys stay
-- reachable in real-time (close / undo / redo / layout toggle). Rarer,
-- more impactful settings (display mode, dedup, recommendation count,
-- show-user-typed, result-count) live in the settings modal opened via
-- `<LocalLeader>s`.
---@type VimficiencyExploreKeymapSpec[]
local SCRATCH_SPEC = {
  { lhs = "q",               handler = "cancel",             desc = "Close explore session",      nowait = true },
  { lhs = "u",               handler = "undo",               desc = "Undo explore step",          nowait = true },
  { lhs = "<C-r>",           handler = "redo",               desc = "Redo explore step",          nowait = true },
  { lhs = "<Tab>",           handler = "toggle_staged_mode", desc = "Toggle flat / staged header layout", mode = "n", nowait = true },
  { lhs = "gs",              handler = "open_settings",      desc = "Open explore settings",      mode = "n", nowait = true },
  { lhs = "<Leader>d",       handler = "debug_dump",         desc = "Dump explore state to :messages (debug)", mode = "n", nowait = true },
  { lhs = "?",               handler = "show_help",          desc = "Show this keymap summary",   mode = "n", nowait = true },
}

---@type VimficiencyExploreKeymapSpec[]
local LIST_SPEC = {
  { lhs = "q", handler = "cancel", desc = "Close explore session", nowait = true },
}

local function resolve(spec, handlers)
  local out = {}
  for _, entry in ipairs(spec) do
    local fn = handlers[entry.handler]
    assert(fn, "vimficiency explore keymaps: missing handler '" .. entry.handler .. "'")
    out[#out + 1] = {
      lhs = entry.lhs,
      handler = fn,
      desc = entry.desc,
      mode = entry.mode,
      nowait = entry.nowait,
    }
  end
  return out
end

---Install buffer-local keymaps on both the scratch and list buffers.
---`handlers` maps handler-name → nullary function; callers own all side
---effects (clear_on_key_buffer wrapping, dispatching into session API,
---etc.). This module only does the lhs → handler binding.
---
---The `show_help` handler is auto-injected: it pops a floating window
---listing every scratch-buffer binding and its `desc`. Callers do not
---need to supply it (but may override it if they want custom help UI).
---@param scratch_buf integer
---@param list_buf integer
---@param handlers table<string, function>
function M.install(scratch_buf, list_buf, handlers)
  -- Forward-referenced so the injected show_help handler can render the
  -- final resolved list (including the `?` entry itself — otherwise the
  -- popup would fail to list its own binding).
  local scratch_resolved
  local augmented = vim.tbl_extend("keep", handlers, {
    show_help = function()
      util.show_keymap_help("Explore — Keys", scratch_resolved)
    end,
  })
  scratch_resolved = resolve(SCRATCH_SPEC, augmented)
  util.set_buffer_keymaps(scratch_buf, scratch_resolved)
  util.set_buffer_keymaps(list_buf, resolve(LIST_SPEC, augmented))
end

return M
