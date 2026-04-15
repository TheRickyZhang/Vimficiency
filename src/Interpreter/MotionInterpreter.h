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

struct ParsedMotion {
private:
  // 0 -> no count, OK since it is impossible for 0 to be a count.
  // important to distinguish since 1{action} sometimes != action!
  uint32_t count;

public:
  // should be derived from a motion sequence, and applied before that lifetime
  // ends.
  std::string_view motion;

  ParsedMotion(std::string_view motion, int count)
      : motion(motion), count(count) {}
  ParsedMotion(std::string_view motion) : motion(motion), count(0) {}

  inline bool hasCount() const { return count ? true : false; }
  // IMPORTANT to call this when standard treatment.
  inline uint32_t effectiveCount() const { return count ? count : 1; }
};

std::ostream& operator<<(std::ostream& os, const ParsedMotion& motion);

enum class MotionParseErrorKind {
  UnknownMotion,
  MalformedSpecialKey,
};

struct MotionParseError {
  MotionParseErrorKind kind;
  size_t offset;
};

std::string formatMotionParseError(const MotionParseError& error);

// Parse a motion sequence into individual ParsedMotion tokens
std::expected<std::vector<ParsedMotion>, MotionParseError>
parseMotions(std::string_view seq);

void applyParsedMotion(CursorPos& pos, Mode& mode, 
                       const ParsedMotion& motion, const Lines& lines,
                      const NavContext& navContext);

// External entry point for applying a single motion in optimizer state updates.
void applySingleMotion(CursorPos& pos, Mode& mode, std::string_view motion,
                       const Lines& lines, const NavContext& navContext);

// Parses the motion sequence, and returns the result if they are applied to the
// current state
CursorPos simulateMotions(CursorPos pos, std::string_view motionSeq,
                         const Lines& lines,
                         const NavContext& navContext = NavContext());
