local M = {}

---@class VimficiencySession
---@field id string
---@field start_path string
---@field start_buf integer

---@class VimficiencyFileContents
---@field bufname string       # original buffer name (full path)
---@field filetype string      # buffer filetype
---@field row integer
---@field col integer
---@field lines string[]       # full buffer contents

---@class VimficiencyWriteDTO
---@field buf integer
---@field win integer
---@field id string
---@field path string


---@param id string
---@param start_path string
---@param start_buf integer
---@return VimficiencySession
function M.new_session(id, start_path, start_buf)
  assert(type(id) == "string" and id ~= "", "session.id must be nonempty string")
  assert(type(start_path) == "string" and start_path ~= "", "session.start_path must be nonempty string")
  assert(type(start_buf) == "integer", "session.start_buf must be integer")

  return {
    id = id,
    start_path = start_path,
    start_buf = start_buf,
  }
end


---@param bufname string
---@param filetype string
---@param row integer
---@param col integer
---@param lines string[]
---@return VimficiencyFileContents
function M.new_file_contents(bufname, filetype, row, col, lines)
  assert(type(bufname) == "string", "filecontents.bufname must be string")
  assert(type(filetype) == "string", "filecontents.filetype must be string")
  assert(type(row) == "number" and type(col) == "number", "row and col must be ints")
  assert(type(lines) == "table", "filecontents.lines must be an array of strings")

  return { bufname = bufname, filetype = filetype, row = row, col = col, lines = lines, }
end

---@param buf integer
---@param win integer
---@param id string
---@param path string
---@return VimficiencyWriteDTO
function M.new_write_dto(buf, win, id, path)
  assert(type(buf) == "integer")
  assert(type(win) == "integer")
  assert(type(id) == "string")
  assert(type(path) == "string")
  return {buf=buf, win=win, id=id, path=path}
end

return M
