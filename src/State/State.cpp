#include "State.h"
#include "VimCore/VimUtils.h"

using namespace std;

inline bool isWordChar(unsigned char c) {
  return isalnum(c) || c == '_';
}

inline bool isBlank(unsigned char c) {
  return c == ' ' || c == '\t';
}

void State::apply_normal_motion(string motion,
                                const vector<string>& lines) {
  sequence += motion;
  int n = static_cast<int>(lines.size());

  if(motion == "h") {
    VimUtils::moveCol(pos, lines, -1);
  }
  else if(motion == "l") {
    VimUtils::moveCol(pos, lines, 1);
  }
  else if(motion == "j") {
    VimUtils::moveLine(pos, lines, 1);
  }
  else if(motion == "k") {
    VimUtils::moveLine(pos, lines, -1);
  }
  else if(motion == "0") {
    pos.setCol(0);
  }
  else if(motion == "$") {
    int len = static_cast<int>(lines[pos.line].size());
    pos.setCol(len == 0 ? 0 : len - 1);
  }
  else if(motion == "^") {
    int len = static_cast<int>(lines[pos.line].size());
    int col = 0;
    const string &line = lines[pos.line];
    while(col < len && isspace(static_cast<unsigned char>(line[col]))) ++col;
    pos.setCol(col);
  }
  else if(motion == "gg") {
    pos.line = 0;
    pos.col  = VimUtils::clampCol(lines, pos.col, pos.line);
  }
  else if(motion == "G") {
    pos.line = n - 1;
    pos.col  = VimUtils::clampCol(lines, pos.col, pos.line);
  }
  else if(motion == "w") {
    VimUtils::motion_w(pos, lines, false);
  }
  else if(motion == "b") {
    VimUtils::motion_b(pos, lines, false);
  }
  else if(motion == "e") {
    VimUtils::motion_e(pos, lines, false);
  }
  else if(motion == "W") {
    VimUtils::motion_w(pos, lines, true);
  }
  else if(motion == "B") {
    VimUtils::motion_b(pos, lines, true);
  }
  else if(motion == "E") {
    VimUtils::motion_e(pos, lines, true);
  }
  else {
    cout<<"motion not supported"<<endl;
  }
}
