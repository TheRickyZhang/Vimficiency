#include "Motion.h"
#include "VimCore/VimUtils.h"
#include "Utils/Debug.h"

#include "Keyboard/MotionToKeys.h"

using namespace std;

// Does string motion parsing. See SequencTokenizer for the physical key parsing.
std::vector<std::string> parseMotions(const std::string& seq) {
    std::vector<std::string> result;
    size_t i = 0;
    while (i < seq.size()) {
        bool matched = false;
        // Try longest matches first
        for (size_t len = std::min(seq.size() - i, size_t{4}); len > 0; --len) {
            std::string candidate = seq.substr(i, len);
            if (ALL_MOTIONS.contains(candidate)) {
                result.push_back(candidate);
                i += len;
                matched = true;
                break;
            }
        }
        if (!matched) {
            throw std::runtime_error("Unknown motion at: " + seq.substr(i));
        }
    }
    return result;
}


/*
* Maintain this list of currently defined motions:
* Alphabet:
* bB, eE, h j k l, wW,
* 
* 
*
* Special
* G, gg
*
* Top row symbol:
* $, ^,
*
* Other Symbol:
* {}, 
*/ 
MotionResult apply_motion(Position pos, Mode mode, 
                          const std::string& motion,
                          const std::vector<std::string>& lines) {
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
  else if(motion == "{") {
    VimUtils::motion_paragraphPrev(pos, lines);
  }
  else if(motion == "}") {
    VimUtils::motion_paragraphNext(pos, lines);
  }
  else if(motion == "(") {
    VimUtils::motion_sentencePrev(pos, lines);
  }
  else if(motion == ")") {
    VimUtils::motion_sentenceNext(pos, lines);
  }
  else {
    debug("motion not supported");
  }
  return MotionResult(pos, mode);
}



// Apply a sequence of motions
MotionResult apply_motions(Position pos, Mode mode,
                                  const std::string& motionSeq,
                                  const std::vector<std::string>& lines) {
    auto motions = parseMotions(motionSeq);
    for (const auto& motion : motions) {
        auto result = apply_motion(pos, mode, motion, lines);
        pos = result.pos;
        mode = result.mode;
    }
    return MotionResult(pos, mode);
}
