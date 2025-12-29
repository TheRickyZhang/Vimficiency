// MotionLogic.h
#pragma once

#include <string>
#include <vector>

#include "Position.h"
#include "Mode.h"

struct MotionResult {
    Position pos;
    Mode mode;
    MotionResult(Position p, Mode mode) : pos(p), mode(mode) {}
};

std::vector<std::string> parseMotions(const std::string& seq);

MotionResult applyMotion(Position pos, Mode mode,
                         const std::string& motion,
                         const std::vector<std::string>& lines);

MotionResult applyMotions(Position pos, Mode mode,
                          const std::string& motionSeq,
                          const std::vector<std::string>& lines);
