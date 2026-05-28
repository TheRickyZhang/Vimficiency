#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Types/NavContext.h"
#include "Types/CharLineRange.h"
#include "Types/LineCharRange.h"
#include "Types/Mode.h"
#include "Types/CursorPos.h"
#include "Types/CharRange.h"
#include "Types/Lines.h"

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
  int rawCount() const { return count; }
};

// Dot-repeat state. `base` is the command stripped of its count prefix
// (e.g. "de"); `count` is the count prefix (0 == uncounted). Empty `base`
// means "nothing to repeat" — `.` no-ops in that case. Owns its storage so a
// `ParsedEdit` view built via `asEdit()` is valid as long as the DotRepeat is.
struct DotRepeat {
  std::string base;
  int count = 0;

  bool empty() const { return base.empty(); }
  ParsedEdit asEdit() const { return ParsedEdit{base, count}; }

  bool operator==(const DotRepeat&) const = default;
};

// Edit operations that modify the buffer.
// All functions modify lines and pos in place.
namespace Edit {

// -----------------------------------------------------------------------------
// Operator + CharRange operations (d{range}, c{range}, y{range})
// Called directly with a pre-computed CharRange from motion or text object.
// -----------------------------------------------------------------------------

// d{range} - delete range (Normal mode only)
void deleteRange(Lines& lines, CursorPos& pos, Mode mode, const CharRange& range);
void deleteRange(Lines& lines, CursorPos& pos, Mode mode, const CharLineRange& range);
void deleteRange(Lines& lines, CursorPos& pos, Mode mode, const LineCharRange& range);

// c{range} - change range (Normal -> Insert)
void changeRange(Lines& lines, CursorPos& pos, Mode& mode, const CharRange& range);
void changeRange(Lines& lines, CursorPos& pos, Mode& mode, const CharLineRange& range);
void changeRange(Lines& lines, CursorPos& pos, Mode& mode, const LineCharRange& range);

// y{range} - yank range (Normal mode only, no buffer change)
void yankRange(Lines& lines, CursorPos& pos, Mode mode, const CharRange& range);
void yankRange(Lines& lines, CursorPos& pos, Mode mode, const CharLineRange& range);
void yankRange(Lines& lines, CursorPos& pos, Mode mode, const LineCharRange& range);

// Edit dispatcher
// Routes parsed edit commands to appropriate operations.
// -----------------------------------------------------------------------------

bool updatesDotRepeat(Mode mode, std::string_view edit);
bool isValidNormalEditOnEmptyLine(std::string_view edit);
std::optional<DotRepeat> dotRepeatFor(Mode mode, const ParsedEdit& edit);

// Apply a single parsed edit. If lastEdit is non-null, '.' repeats the
// last buffer-modifying command stored there, and buffer-modifying commands
// update it. Callers that don't need dot repeat can omit the parameter.
// hasLinesBelow: when true, linewise deletions (dd, dj, dk) that remove the
// last lines leave pos.line past end instead of clamping. This models the
// edit-region context where the real buffer has lines below.
// leftColOffset/rightColOffset + hasLinesAbove/hasLinesBelow can be provided to
// make word text objects boundary-aware (used by TransformOptimizer replay).
void applyEdit(Lines& lines, CursorPos& pos, Mode& mode, const ParsedEdit& edit,
               DotRepeat* lastEdit = nullptr, bool hasLinesBelow = false,
               int leftColOffset = 0, int rightColOffset = 0,
               bool hasLinesAbove = false,
               // Vim's `did_ai` flag for the autoindent strip on `<Esc>`.
               // Set by `<CR>` to the length of autoindent inserted; cleared
               // by any user typing other than the autoindent itself or `<BS>`
               // that wipes it. Caller threads this across token applies so
               // multi-token sequences (`i<CR><Esc>`, `iX<CR><Esc>`) see
               // consistent state.
               int* pendingAutoindentLen = nullptr);

enum class EditParseErrorKind {
  MalformedSpecialKey,
};

struct EditParseError {
  EditParseErrorKind kind;
  size_t offset;
};

std::string formatEditParseError(const EditParseError& error);

// Parse an edit sequence string into individual ParsedEdit tokens.
// Handles operators (d, c) + motions/text objects, special keys (<Esc>, <CR>, etc.),
// and counts.
// IMPORTANT: Returned ParsedEdits contain string_views into seq - caller must ensure seq outlives usage.
std::expected<std::vector<ParsedEdit>, EditParseError> parseEdits(std::string_view seq);

void simulateEdits(CursorPos& pos, Mode& mode, const NavContext& navContext,
                          std::string_view movementSeq,
                          const Lines& lines);
} // namespace Edit
