#pragma once

#include <string>

#include "Mode.h"
#include "Position.h"
#include "Range.h"
#include "NavContext.h"
#include "Utils/Lines.h"

// Parsed edit (operator + motion/text-object, or single-key command)
class ParsedEdit {
  // 0 -> no count, OK since it is impossible for 0 to be a count.
  // important to distinguish since 1{action} sometimes != action!
  uint32_t count;

public:
  std::string_view edit;  // The edit string (e.g., "x", "dd", "ciw", "dfa")

  ParsedEdit(std::string_view e, int c = 0) : edit(e), count(c) {}

  bool hasCount() const { return count != 0; }
  int effectiveCount() const { return count ? count : 1; }
};

// Edit operations that modify the buffer.
// All functions modify lines and pos in place.
namespace Edit {

// -----------------------------------------------------------------------------
// Operator + Range operations (d{range}, c{range}, y{range})
// Called directly with a pre-computed Range from motion or text object.
// -----------------------------------------------------------------------------

// d{range} - delete range (Normal mode only)
void deleteRange(Lines& lines, Position& pos, Mode mode, const Range& range);

// c{range} - change range (Normal -> Insert)
void changeRange(Lines& lines, Position& pos, Mode& mode, const Range& range);

// y{range} - yank range (Normal mode only, no buffer change)
void yankRange(Lines& lines, Position& pos, Mode mode, const Range& range);

// -----------------------------------------------------------------------------
// Insert mode text insertion
// Called directly for typed characters in Insert mode.
// -----------------------------------------------------------------------------

void insertText(Lines& lines, Position& pos, Mode mode, const std::string& text);

// -----------------------------------------------------------------------------
// Edit dispatcher
// Routes parsed edit commands to appropriate operations.
// -----------------------------------------------------------------------------

void applyEdit(Lines& lines, Position& pos, Mode& mode,
               const NavContext& navContext,
               const ParsedEdit& edit);

} // namespace Edit
