#include "Motion.h"
#include "Editor/NavContext.h"
#include "Utils/Debug.h"
#include "VimCore/VimUtils.h"

#include "Keyboard/MotionToKeys.h"

using namespace std;

// Does string motion parsing. See SequenceTokenizer for the physical key parsing.
std::vector<std::string> parseMotions(const std::string &seq, const NavContext& navContext) {
  std::vector<std::string> result;
  size_t i = 0;
  while (i < seq.size()) {
    char c = seq[i];

    // f/F/t/T consume the next character as target, then max-munch ;/,
    if ((c == 'f' || c == 'F' || c == 't' || c == 'T') && i + 1 < seq.size()) {
      size_t start = i;
      i += 2; // consume motion + target char
      // Max-munch any following ; or ,
      while (i < seq.size() && (seq[i] == ';' || seq[i] == ',')) {
        i++;
      }
      result.push_back(seq.substr(start, i - start));
      continue;
    }
  
    // Handle <C-_> for special bindings (e.g., <C-_>, <C-d>, <CR>)
    if (c == '<') {
      size_t close = seq.find('>', i);
      if (close != std::string::npos) {
        std::string special = seq.substr(i, close - i + 1);
        if (ALL_MOTIONS.contains(special)) {
          result.push_back(special);
          i = close + 1;
          continue;
        }
        // If not a known motion, fall through to error
      }
      throw std::runtime_error("Unknown or malformed special key at: " + seq.substr(i));
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
 * bB, eE, fF, h j k l, wW,
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
 * {}, ;,, 
 */
// Directly modifies the position and mode passed in. 
void applySingleMotion(Position& pos, Mode& mode, const NavContext& navContext,
                  const std::string &motion,
                  const std::vector<std::string> &lines) {
  int n = static_cast<int>(lines.size());
  // Fundamental movements
  if (motion == "h") {
    VimUtils::moveCol(pos, lines, -1);
  } else if (motion == "l") {
    VimUtils::moveCol(pos, lines, 1);
  } else if (motion == "j") {
    VimUtils::moveLine(pos, lines, 1);
  } else if (motion == "k") {
    VimUtils::moveLine(pos, lines, -1);
  } else if (motion == "0") {
    pos.setCol(0);
  } else if (motion == "$") {
    int len = static_cast<int>(lines[pos.line].size());
    pos.setCol(len == 0 ? 0 : len - 1);
  } else if (motion == "^") {
    int len = static_cast<int>(lines[pos.line].size());
    int col = 0;
    const string &line = lines[pos.line];
    while (col < len && isspace(static_cast<unsigned char>(line[col])))
      ++col;
    pos.setCol(col);
  } else if (motion == "gg") {
    pos.line = 0;
    pos.col = VimUtils::clampCol(lines, pos.col, pos.line);
  } else if (motion == "G") {
    pos.line = n - 1;
    pos.col = VimUtils::clampCol(lines, pos.col, pos.line);
  }
  // Words
  else if (motion == "w") {
    VimUtils::motionW(pos, lines, false);
  } else if (motion == "b") {
    VimUtils::motionB(pos, lines, false);
  } else if (motion == "e") {
    VimUtils::motionE(pos, lines, false);
  } else if (motion == "W") {
    VimUtils::motionW(pos, lines, true);
  } else if (motion == "B") {
    VimUtils::motionB(pos, lines, true);
  } else if (motion == "E") {
    VimUtils::motionE(pos, lines, true);
  }
  // Text object jumps
  else if (motion == "{") {
    VimUtils::motionParagraphPrev(pos, lines);
  } else if (motion == "}") {
    VimUtils::motionParagraphNext(pos, lines);
  } else if (motion == "(") {
    VimUtils::motionSentencePrev(pos, lines);
  } else if (motion == ")") {
    VimUtils::motionSentenceNext(pos, lines);
  }
  // f/F/t/T motions with optional ;/, repeats (e.g., "fa;;", "Ta,")
  else if (motion.size() >= 2 && (motion[0] == 'f' || motion[0] == 'F' ||
                                  motion[0] == 't' || motion[0] == 'T')) {
    char cmd = motion[0];
    char target = motion[1];
    bool forward = (cmd == 'f' || cmd == 't');
    bool till = (cmd == 't' || cmd == 'T');
    const string &line = lines[pos.line];

    int newCol = VimUtils::findCharInLine(target, line, pos.col, forward, till);
    if (newCol >= 0) {
      pos.setCol(newCol);
    }
    // Process any ; or , repeats
    for (size_t i = 2; i < motion.size(); i++) {
      char repeat = motion[i];
      bool repeatForward = (repeat == ';') ? forward : !forward;
      bool repeatTill = till; // t/T behavior is preserved
      newCol = VimUtils::findCharInLine(target, line, pos.col, repeatForward, repeatTill);
      if (newCol >= 0) {
        pos.setCol(newCol);
      }
    }
  }
  // Jumps (rely on navContext)
  else if (motion == "<C-d>") {
    pos.line = min(pos.line + navContext.scrollAmount, n - 1);
    pos.col = VimUtils::clampCol(lines, pos.col, pos.line);
  }
  else if (motion == "<C-u>") {
    int jump = navContext.scrollAmount;
    pos.line = max(pos.line - navContext.scrollAmount, 0);
    pos.col = VimUtils::clampCol(lines, pos.col, pos.line);
  }
  else if (motion == "<C-f>") {
    int jump = max(0, navContext.windowHeight - 2);
    pos.line = min(pos.line + jump, n - 1);
    pos.col = VimUtils::clampCol(lines, pos.col, pos.line);
  }
  else if (motion == "<C-b>") {
    int jump = max(0, navContext.windowHeight - 2);
    pos.line = max(pos.line - jump, 0);
    pos.col = VimUtils::clampCol(lines, pos.col, pos.line);
  }
  else {
    debug("motion not supported:", motion);
  }
}

// Return the result if we were to simulate motionSeq at current state
// Important that pos and mode are passed by copy! We wouldn't want to change any state.
MotionResult simulateMotions(Position pos, Mode mode, const NavContext& navContext,
                          const std::string &motionSeq,
                          const std::vector<std::string> &lines) {
  auto motions = parseMotions(motionSeq, navContext);
  for (const auto &motion : motions) {
    applySingleMotion(pos, mode, navContext, motion, lines);
  }
  return MotionResult(pos, mode);
}
