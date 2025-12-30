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

// Only to be externally called in State::applyMotion! 
void applySingleMotion(Position& pos, Mode& mode, const NavContext& navContext,
                  const std::string &motion,
                  const std::vector<std::string> &lines);

// std::vector<std::string> parseMotions(const std::string& seq, const NavContext& navContext);

// Parses the motion sequence, and returns the result if they are applied to the current state
MotionResult simulateMotions(Position pos, Mode mode, const NavContext& navContext,
                          const std::string& motionSeq,
                          const std::vector<std::string>& lines);
