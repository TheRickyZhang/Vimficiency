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
        char c = seq[i];

        // f/F/t/T consume the next character as target, then max-munch ;/,
        if ((c == 'f' || c == 'F' || c == 't' || c == 'T') && i + 1 < seq.size()) {
            size_t start = i;
            i += 2;  // consume motion + target char
            // Max-munch any following ; or ,
            while (i < seq.size() && (seq[i] == ';' || seq[i] == ',')) {
                i++;
            }
            result.push_back(seq.substr(start, i - start));
            continue;
        }

        // Standard longest-match for other motions
        bool matched = false;
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
MotionResult applyMotion(Position pos, Mode mode,
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
    VimUtils::motionW(pos, lines, false);
  }
  else if(motion == "b") {
    VimUtils::motionB(pos, lines, false);
  }
  else if(motion == "e") {
    VimUtils::motionE(pos, lines, false);
  }
  else if(motion == "W") {
    VimUtils::motionW(pos, lines, true);
  }
  else if(motion == "B") {
    VimUtils::motionB(pos, lines, true);
  }
  else if(motion == "E") {
    VimUtils::motionE(pos, lines, true);
  }
  else if(motion == "{") {
    VimUtils::motionParagraphPrev(pos, lines);
  }
  else if(motion == "}") {
    VimUtils::motionParagraphNext(pos, lines);
  }
  else if(motion == "(") {
    VimUtils::motionSentencePrev(pos, lines);
  }
  else if(motion == ")") {
    VimUtils::motionSentenceNext(pos, lines);
  }
  // f/F/t/T motions with optional ;/, repeats (e.g., "fa;;", "Ta,")
  else if(motion.size() >= 2 && (motion[0] == 'f' || motion[0] == 'F' ||
                                  motion[0] == 't' || motion[0] == 'T')) {
    char cmd = motion[0];
    char target = motion[1];
    bool forward = (cmd == 'f' || cmd == 't');
    bool till = (cmd == 't' || cmd == 'T');
    const string& line = lines[pos.line];

    // Apply initial f/F/t/T
    int newCol = VimUtils::findCharInLine(target, line, pos.col, forward, till);
    if (newCol >= 0) {
      pos.setCol(newCol);
    }

    // Process any ; or , repeats
    for (size_t i = 2; i < motion.size(); i++) {
      char repeat = motion[i];
      bool repeatForward = (repeat == ';') ? forward : !forward;
      bool repeatTill = till;  // t/T behavior is preserved

      newCol = VimUtils::findCharInLine(target, line, pos.col, repeatForward, repeatTill);
      if (newCol >= 0) {
        pos.setCol(newCol);
      }
    }
  }
  else {
    debug("motion not supported:", motion);
  }
  return MotionResult(pos, mode);
}



// Apply a sequence of motions
MotionResult applyMotions(Position pos, Mode mode,
                          const std::string& motionSeq,
                          const std::vector<std::string>& lines) {
    auto motions = parseMotions(motionSeq);
    for (const auto& motion : motions) {
        auto result = applyMotion(pos, mode, motion, lines);
        pos = result.pos;
        mode = result.mode;
    }
    return MotionResult(pos, mode);
}
