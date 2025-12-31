// MotionLogic.h
#pragma once

#include <string>
#include <vector>

#include "Position.h"
#include "Mode.h"
#include "NavContext.h"

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
  // should be derived from a motion sequence, and applied before that lifetime ends.
  std::string_view motion;

  ParsedMotion(std::string_view motion, int count) : motion(motion), count(count) {}
  ParsedMotion(std::string_view motion) : motion(motion), count(0) {}

  inline bool hasCount() const {
    return count ? true : false;
  }
  // IMPORTANT to call this when standard treatment.
  inline uint32_t effectiveCount() const {
    return count ? count : 1;
  }
};

/* Implementation details


  std::vector<std::string> parsemotions(const std::string& seq, const NavContext& navContext);
*/

// Currently only to be externally called in State::applyMotion.
void applySingleMotion(Position& pos, Mode& mode, const NavContext& navContext,
                  const std::string &motion,
                  const std::vector<std::string> &lines);

void applyParsedMotion(Position& pos, Mode& mode, const NavContext& navContext,
                  const ParsedMotion& motion,
                  const std::vector<std::string> &lines);

// Parses the motion sequence, and returns the result if they are applied to the current state
MotionResult simulateMotions(Position pos, Mode mode, const NavContext& navContext,
                          const std::string& motionSeq,
                          const std::vector<std::string>& lines);
