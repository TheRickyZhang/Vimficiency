// Motion.h - Motion motion parsing and application
// Handles cursor movement motions (h, j, k, l, w, b, etc.) that don't modify
// buffer content.
#pragma once

#include <expected>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "Types/NavContext.h"
#include "Types/Mode.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

struct ParsedMovement {
private:
  // 0 -> no count, OK since it is impossible for 0 to be a count.
  // important to distinguish since 1{action} sometimes != action!
  uint32_t count;

public:
  // should be derived from a motion sequence, and applied before that lifetime
  // ends.
  std::string_view motion;

  ParsedMovement(std::string_view motion, int count)
      : motion(motion), count(count) {}
  ParsedMovement(std::string_view motion) : motion(motion), count(0) {}

  inline bool hasCount() const { return count ? true : false; }
  // IMPORTANT to call this when standard treatment.
  inline uint32_t effectiveCount() const { return count ? count : 1; }
};

std::ostream& operator<<(std::ostream& os, const ParsedMovement& motion);

enum class MovementParseErrorKind {
  UnknownMotion,
  MalformedSpecialKey,
};

struct MovementParseError {
  MovementParseErrorKind kind;
  size_t offset;
};

std::string formatMovementParseError(const MovementParseError& error);

// Parse a motion sequence into individual ParsedMovement tokens
std::expected<std::vector<ParsedMovement>, MovementParseError>
parseMovements(std::string_view seq);

void applyParsedMovement(CursorPos& pos, Mode& mode, 
                       const ParsedMovement& motion, const Lines& lines,
                      const NavContext& navContext);

// External entry point for applying a single motion in optimizer state updates.
void applySingleMovement(CursorPos& pos, Mode& mode, std::string_view motion,
                       const Lines& lines, const NavContext& navContext);

// Parses the motion sequence, and returns the result if they are applied to the
// current state
CursorPos simulateMovements(CursorPos pos, std::string_view movementSeq,
                         const Lines& lines,
                         const NavContext& navContext = NavContext());
