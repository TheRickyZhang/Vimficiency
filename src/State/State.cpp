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
void State::apply_normal_motion(string motion,
                                const vector<string>& lines) {
  MotionResult result = apply_motion(pos, mode, motion, lines);
  pos = result.pos;
  mode = result.mode;
  motionSequence += motion;
}
