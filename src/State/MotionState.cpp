#include "MotionState.h"
#include "Editor/Motion.h"

using namespace std;

inline bool isWordChar(unsigned char c) {
  return isalnum(c) || c == '_';
}

inline bool isBlank(unsigned char c) {
  return c == ' ' || c == '\t';
}

// Changes pos, mode, motionSequence
void MotionState::applySingleMotion(string motion, const NavContext& navContext,
                               const vector<string>& lines) {
  applyParsedMotion(pos, mode, navContext, ParsedMotion(motion), lines);
  motionSequence += motion;
}

void MotionState::applySingleMotionWithKnownColumn(string motion, int newCol) {
  setCol(newCol);
  motionSequence += motion;
}


void MotionState::applyMotionWithKnownPosition(std::string motion, int cnt, const Position& newPos) {
  pos = newPos;
  if (cnt > 0) {
    motionSequence += to_string(cnt);
  }
  motionSequence += motion;
}


void MotionState::updateEffort(const KeySequence& keySequence, const Config& config) {
  effort = runningEffort.append(keySequence, config);
}


void MotionState::updateCost(double newCost) {
  cost = newCost;
}
