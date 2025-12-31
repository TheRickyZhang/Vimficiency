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
void State::applyMotion(string motion, int cnt /* = 0 */, const NavContext& navContext,
                               const vector<string>& lines) {
  applyParsedMotion(pos, mode, navContext, ParsedMotion(motion, cnt), lines);
  if (cnt > 0) {
    motionSequence += to_string(cnt);
  }
  motionSequence += motion;
}
