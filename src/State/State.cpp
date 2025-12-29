#include "State.h"
#include "Editor/Motion.h"

using namespace std;

inline bool isWordChar(unsigned char c) {
  return isalnum(c) || c == '_';
}

inline bool isBlank(unsigned char c) {
  return c == ' ' || c == '\t';
}

// Changes pos, mode, motion
void State::applyNormalMotion(string motion,
                              const vector<string>& lines) {
  MotionResult result = applyMotion(pos, mode, motion, lines);
  pos = result.pos;
  mode = result.mode;
  motionSequence += motion;
}
