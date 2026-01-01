#include "State.h"
#include "Editor/Motion.h"

using namespace std;

inline bool isWordChar(unsigned char c) {
  return isalnum(c) || c == '_';
}

inline bool isBlank(unsigned char c) {
  return c == ' ' || c == '\t';
}

// Changes pos, mode, motionSequence
void State::applySingleMotion(string motion, const NavContext& navContext,
                               const vector<string>& lines) {
  applyParsedMotion(pos, mode, navContext, ParsedMotion(motion), lines);
  motionSequence += motion;
}

void State::applySingleMotionWithKnownColumn(string motion, int newCol) {
  setCol(newCol);
  motionSequence += motion;
}


void State::applyMotionWithKnownPosition(std::string motion, int cnt, const Position& newPos) {
  pos = newPos;
  if (cnt > 0) {
    motionSequence += to_string(cnt);
  }
  motionSequence += motion;
}


void State::updateEffort(const KeySequence& keySequence, const Config& config) {
  effort = runningEffort.append(keySequence, config);
}


void State::updateCost(double newCost) {
  cost = newCost;
}
