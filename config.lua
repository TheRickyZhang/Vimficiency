local M = {}

M.motions = {
  "b", "B",
  "e", "E",
  "h", "j", "k", "l",
  "w", "W",
  "{", "}",
  "(", ")",
  "gg", "G",
  "M"
}

M.characterMotions = {"t", "T", "f", "F", "r"}
M.counts = {"2", "3", "4", "5", "6", "7", "8", "9", "12"}

M.insideMotions = {
  "iw",
  "aw",
  "iW",
  "aW",
}
M.actions = {
  "c", "d", "s", "y", "v",
}

M.insertModeKeys = {"a", "A", "C", "i", "I", "o", "O", "s", "S"}
M.visualModeKeys = {"v", "V"}
M.deleteKeys = {"d", "D", "x", "X"}
M.stateKeys = {"p", "P", "y", "Y", "q", "@", "u"}
M.historyKeys = {"u", "C-r"}
M.markKeys = {"m", "`"}
M.specialKeys = {"g", "z"}
M.specialActionKeys = {"J"}
M.searchKeys = {"/", "n", "N"}

return M;
