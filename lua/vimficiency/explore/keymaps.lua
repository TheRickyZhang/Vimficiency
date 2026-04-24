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
---@field summary_group string?  # Adjacent matching groups collapse onto one help row.
---@field summary_desc string?   # Optional help-row description for the collapsed group.
---@field mode? string      # default "n"
---@field nowait? boolean

-- Scratch buffer is deliberately minimal: only session-flow keys stay
-- reachable in real-time (close / undo / redo). Rarer,
-- more impactful settings (display mode, dedup, recommendation count,
-- show-user-typed, result-count) live in the settings modal opened via
-- `gs`.
---@type VimficiencyExploreKeymapSpec[]
local SCRATCH_SPEC = {
  { lhs = "q",               handler = "cancel",             desc = "Close explore session",      nowait = true },
  { lhs = "u",               handler = "undo",               desc = "Undo explore step",          nowait = true,
    summary_group = "history", summary_desc = "Undo / redo explore step" },
  { lhs = "<C-r>",           handler = "redo",               desc = "Redo explore step",          nowait = true,
    summary_group = "history", summary_desc = "Undo / redo explore step" },
  { lhs = "<Leader>d",       handler = "debug_dump",         desc = "Dump explore state to :messages (debug)", mode = "n", nowait = true },
}

---@type VimficiencyExploreKeymapSpec[]
local LIST_SPEC = {
  { lhs = "q", handler = "cancel", desc = "Close explore session", nowait = true },
}

---@type VimficiencyExploreKeymapSpec[]
local HEADER_SPEC = {
  { lhs = "q", handler = "cancel", desc = "Close explore session", nowait = true },
}

local resolve

local function header_keymaps(handlers)
  return util.with_standard_ui_keymaps(
    resolve(HEADER_SPEC, handlers),
    {
      title = "Explore — Header Keys",
      docs = true,
      settings = {
        lhs = "gs",
        handler = handlers.open_settings,
        desc = "Open explore settings",
      },
    })
end

resolve = function(spec, handlers)
  local out = {}
  for _, entry in ipairs(spec) do
    local fn = handlers[entry.handler]
    assert(fn, "vimficiency explore keymaps: missing handler '" .. entry.handler .. "'")
    out[#out + 1] = {
      lhs = entry.lhs,
      handler = fn,
      desc = entry.desc,
      summary_group = entry.summary_group,
      summary_desc = entry.summary_desc,
      mode = entry.mode,
      nowait = entry.nowait,
    }
  end
  return out
end

---Install buffer-local keymaps on the explore panes.
---`handlers` maps handler-name → nullary function; callers own all side
---effects (clear_on_key_buffer wrapping, dispatching into session API,
---etc.). This module only does the lhs → handler binding.
---
---Standard summary/settings/docs keys are auto-injected per pane.
---@param header_buf integer
---@param scratch_buf integer
---@param list_buf integer
---@param handlers table<string, function>
function M.install(header_buf, scratch_buf, list_buf, handlers)
  local scratch_resolved = util.with_standard_ui_keymaps(
    resolve(SCRATCH_SPEC, handlers),
    {
      title = "Explore — Keys",
      docs = true,
      settings = {
        lhs = "gs",
        handler = handlers.open_settings,
        desc = "Open explore settings",
      },
    })
  local header_resolved = header_keymaps(handlers)
  util.set_buffer_keymaps(scratch_buf, scratch_resolved)
  util.set_buffer_keymaps(header_buf, header_resolved)
  util.set_buffer_keymaps(list_buf, resolve(LIST_SPEC, handlers))
end

---@param header_buf integer
---@param handlers table<string, function>
function M.install_header(header_buf, handlers)
  util.set_buffer_keymaps(header_buf, header_keymaps(handlers))
end

return M
