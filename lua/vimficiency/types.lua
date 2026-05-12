-- Shared LuaLS annotations for cross-module Vimficiency records.

---@class VF.C.ByteSlice
---@field data ffi.cdata*
---@field len ffi.cdata*

---@class VF.Optimizer.Result
---@field seq string
---@field cost number

---@class VF.OptimizerOverrides
---@field shared? table<string, number|integer|boolean>
---@field nav? table<string, number|integer|boolean>
---@field transform? table<string, number|integer|boolean>
---@field composition? table<string, number|integer|boolean>

---@class VF.Sequence.Token
---@field text string
---@field kind "movement"|"delete"|"change"|"visual"|"typed"|"escape"
---@field index integer?
---@field hl string?

---@class VF.Position
---@field row integer
---@field col integer

---@class VF.Range
---@field begin_pos VF.Position
---@field end_pos VF.Position

---@class VF.Explore.PhaseNavigate
---@field kind "Navigate"
---@field edit_index integer

---@class VF.Explore.PhaseTransform
---@field kind "Transform"
---@field edit_index integer

---@class VF.Explore.PhaseInsert
---@field kind "Insert"
---@field edit_index integer

---@alias VF.Explore.Phase
---| VF.Explore.PhaseNavigate
---| VF.Explore.PhaseTransform
---| VF.Explore.PhaseInsert

---@class VF.Explore.State
---@field phase VF.Explore.Phase
---@field is_completed boolean
---@field cursor VF.Position
---@field total_edits integer
---@field cost number
---@field seq string
---@field can_undo boolean
---@field can_redo boolean
---@field target_range VF.Range

---@class VF.Explore.AppliedResult
---@field status "Applied"

---@class VF.Explore.RejectedResult
---@field status "Rejected"
---@field reason string

---@alias VF.Explore.ApplyResult
---| VF.Explore.AppliedResult
---| VF.Explore.RejectedResult

---@class VF.Explore.Recommendation
---@field text string
---@field cost_diff number
---@field distance number
---@field score number
---@field landing VF.Position
---@field literal_text string
---@field rank? integer

---@class VF.Explore.ReconfigurePlanResult
---@field reset boolean

---@class VF.Explore.HeaderRows
---@field explored string[]
---@field optimal string[][]

---@alias VF.Session.StartKind "manual"|"auto"
---@alias VF.Session.EndKind "manual"|"auto"

return {}
