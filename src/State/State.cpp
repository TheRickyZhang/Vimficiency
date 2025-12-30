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
void State::applyMotion(string motion, const NavContext& navContext,
                               const vector<string>& lines) {
  applySingleMotion(pos, mode, navContext, motion, lines);
  motionSequence += motion;
}
