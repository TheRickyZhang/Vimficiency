// Motion.h - Movement motion parsing and application
// Handles cursor movement motions (h, j, k, l, w, b, etc.) that don't modify
// buffer content.
#pragma once

#include <iostream>
#include <string>
#include <vector>

#include "Mode.h"
#include "NavContext.h"
#include "Position.h"
#include "Utils/Lines.h"

struct MotionResult {
  Position pos;
  Mode mode;
  MotionResult(Position p, Mode mode) : pos(p), mode(mode) {}
};

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

// Parse a motion sequence into individual ParsedMotion tokens
std::vector<ParsedMotion> parseMotions(const std::string& seq);

void applyParsedMotion(Position& pos, Mode& mode, 
                       const ParsedMotion& motion, const Lines& lines,
                      const NavContext& navContext);

// Currently only to be externally called in State::applyMotion.
void applySingleMotion(Position& pos, Mode& mode, const std::string& motion,
                       const Lines& lines, const NavContext& navContext);

// Parses the motion sequence, and returns the result if they are applied to the
// current state
Position simulateMotions(Position pos, const std::string& motionSeq,
                         const Lines& lines,
                         const NavContext& navContext = NavContext());
