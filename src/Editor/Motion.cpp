#include "Motion.h"
#include "Editor/NavContext.h"
#include "Utils/Debug.h"
#include "VimCore/VimUtils.h"

#include "Keyboard/MotionToKeys.h"

using namespace std;

std::ostream& operator<<(std::ostream& os, const ParsedMotion& motion) {
  if(motion.hasCount()) {
    os << motion.effectiveCount();
  }
  os << motion.motion;
  return os;
}
// Does string motion parsing. See SequenceTokenizer for the physical key parsing.
// IMPORTANT: Returned ParsedMotions contain string_views into seq - caller must ensure seq outlives usage.

  // TODO: Once the motions we support are stable, and if it makes sense in how we implement vim's object model, consider representing motions as an enum instead of string. Then it would be no return, as harder to read/debug, but more efficient.
std::vector<ParsedMotion> parseMotions(const std::string &seq) {
  std::string_view sv(seq);  // Create view to avoid allocations
  std::vector<ParsedMotion> result;
  size_t i = 0;
  while (i < sv.size()) {
    char c = sv[i];

    int cnt = 0;
    // 0 is a valid digit except for the first one
    if(isdigit(c) && c != '0') {
      size_t start = i;
      while(i < sv.size() && isdigit(sv[i])) {
        i++;
      }
      // Parse count from substring
      cnt = 0;
      for (size_t j = start; j < i; j++) {
        cnt = cnt * 10 + (sv[j] - '0');
      }
      c = sv[i];  // Update c to char after count
    }

    // f/F/t/T consume the next character as target, then max-munch ;/,
    if ((c == 'f' || c == 'F' || c == 't' || c == 'T') && i + 1 < sv.size()) {
      size_t start = i;
      i += 2; // consume motion + target char
      // Max-munch any following ; or ,
      while (i < sv.size() && (sv[i] == ';' || sv[i] == ',')) {
        i++;
      }
      result.push_back(ParsedMotion{sv.substr(start, i - start), cnt});
      continue;
    }

    // Handle <C-_> for special bindings (e.g., <C-_>, <C-d>, <CR>)
    if (c == '<') {
      size_t close = sv.find('>', i);
      if (close != std::string_view::npos) {
        std::string_view special = sv.substr(i, close - i + 1);
        if (ALL_MOTIONS.contains(special)) {  // No allocation - transparent comparator
          result.push_back(ParsedMotion{special, cnt});
          i = close + 1;
          continue;
        }
        // If not a known motion, fall through to error
      }
      throw std::runtime_error("Unknown or malformed special key at: " + string(sv.substr(i)));
    }

    // Standard longest-match for other motions
    bool matched = false;
    for (size_t len = std::min(sv.size() - i, size_t{4}); len > 0; --len) {
      std::string_view candidate = sv.substr(i, len);
      if (ALL_MOTIONS.contains(candidate)) {  // No allocation - transparent comparator
        result.push_back({candidate, cnt});
        i += len;
        matched = true;
        break;
      }
    }
    if (!matched) {
      throw std::runtime_error("Unknown motion at: " + string(sv.substr(i)));
    }
  }
  return result;
}

/*
 * Maintain this list of currently defined motions:
 * Alphabet:
 * bB, eE, fF, h j k l, wW,
 *
 * g-prefix:
 * ge, gE, gg
 *
 * Special:
 * G
 *
 * Top row symbol:
 * $, ^,
 *
 * Other Symbol:
 * {}, (), ;,
 */
// Directly modifies the position and mode passed in. 
// We may think of passing in BufferIndex to improve simulating certain count motions.
// However, I am not sure if it is worth it, as count > 1 is called only in simulateMotion(). It is helpful to compare vs repeated applications as well.
void applyParsedMotion(Position& pos, Mode& mode,
                       const NavContext& navContext,
                  const ParsedMotion& parsedMotion,
                  const std::vector<std::string> &lines) {
  int n = static_cast<int>(lines.size());
  std::string_view motion = parsedMotion.motion;
  bool hasCount = parsedMotion.hasCount();
  int count = parsedMotion.effectiveCount();
  // Fundamental movements
  if (motion == "h") {
    VimUtils::moveCol(pos, lines, -count);
  } else if (motion == "l") {
    VimUtils::moveCol(pos, lines, count);
  } else if (motion == "j") {
    VimUtils::moveLine(pos, lines, count);
  } else if (motion == "k") {
    VimUtils::moveLine(pos, lines, -count);
  } else if (motion == "0") {
    pos.setCol(0);
  } else if (motion == "$") {
    // Special: {cnt}$ moves cursor down
    if(hasCount) {
      VimUtils::moveLine(pos, lines, count-1);
    }
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
    // Special: set line. Because it's 1 based, subtract 1
    pos.line = hasCount ? count-1 : 0;
    pos.col = VimUtils::clampCol(lines, pos.col, pos.line);
  } else if (motion == "G") {
    // Special: set line. Because it's 1 based, subtract 1
    pos.line = hasCount ? min(count-1, n-1) : n-1;
    pos.col = VimUtils::clampCol(lines, pos.col, pos.line);
  }
  // Words
  // TODO: Able to process faster with indices? Or at least modify underlying VimUtils calls to be more efficient with multiple invocations.
  else if (motion == "w") {
    for(int i = 0; i < count; i++) VimUtils::motionW(pos, lines, false);
  } else if (motion == "b") {
    for(int i = 0; i < count; i++) VimUtils::motionB(pos, lines, false);
  } else if (motion == "e") {
    for(int i = 0; i < count; i++) VimUtils::motionE(pos, lines, false);
  } else if (motion == "W") {
    for(int i = 0; i < count; i++) VimUtils::motionW(pos, lines, true);
  } else if (motion == "B") {
    for(int i = 0; i < count; i++) VimUtils::motionB(pos, lines, true);
  } else if (motion == "E") {
    for(int i = 0; i < count; i++) VimUtils::motionE(pos, lines, true);
  } else if (motion == "ge") {
    for(int i = 0; i < count; i++) VimUtils::motionGe(pos, lines, false);
  } else if (motion == "gE") {
    for(int i = 0; i < count; i++) VimUtils::motionGe(pos, lines, true);
  }
  // Text object jumps
  else if (motion == "{") {
    for(int i = 0; i < count; i++) VimUtils::motionParagraphPrev(pos, lines);
  } else if (motion == "}") {
    for(int i = 0; i < count; i++) VimUtils::motionParagraphNext(pos, lines);
  } else if (motion == "(") {
    for(int i = 0; i < count; i++) VimUtils::motionSentencePrev(pos, lines);
  } else if (motion == ")") {
    for(int i = 0; i < count; i++) VimUtils::motionSentenceNext(pos, lines);
  }
  // f/F/t/T motions with optional ;/, repeats (e.g., "fa;;", "Ta,")
  else if (motion.size() >= 2 && (motion[0] == 'f' || motion[0] == 'F' ||
                                  motion[0] == 't' || motion[0] == 'T')) {
    char cmd = motion[0];
    char target = motion[1];
    bool forward = (cmd == 'f' || cmd == 't');
    bool till = (cmd == 't' || cmd == 'T');
    const string &line = lines[pos.line];
    
    for(int i = 0; i < count; i++) {
      int newCol = VimUtils::findCharInLine(target, line, pos.col, forward, till);
      if (newCol >= 0) {
        pos.setCol(newCol);
      }
    }
    // Process any ; or , repeats
    for (size_t i = 2; i < motion.size(); i++) {
      char repeat = motion[i];
      bool repeatForward = (repeat == ';') ? forward : !forward;
      bool repeatTill = till; // t/T behavior is preserved
      int newCol = VimUtils::findCharInLine(target, line, pos.col, repeatForward, repeatTill);
      if (newCol >= 0) {
        pos.setCol(newCol);
      }
    }
  }
  // Jumps (rely on navContext)
  // Note: In Vim, count for <C-d>/<C-u> SETS the scroll amount, not repeats
  else if (motion == "<C-d>") {
    int amount = hasCount ? count : navContext.scrollAmount;
    pos.line = min(pos.line + amount, n - 1);
    pos.col = VimUtils::clampCol(lines, pos.col, pos.line);
  }
  else if (motion == "<C-u>") {
    int amount = hasCount ? count : navContext.scrollAmount;
    pos.line = max(pos.line - amount, 0);
    pos.col = VimUtils::clampCol(lines, pos.col, pos.line);
  }
  else if (motion == "<C-f>") {
    for(int i = 0; i < count; i++) {
      int jump = max(0, navContext.windowHeight - 2);
      pos.line = min(pos.line + jump, n - 1);
      pos.col = VimUtils::clampCol(lines, pos.col, pos.line);
    }
  }
  else if (motion == "<C-b>") {
    for(int i = 0; i < count; i++) {
      int jump = max(0, navContext.windowHeight - 2);
      pos.line = max(pos.line - jump, 0);
      pos.col = VimUtils::clampCol(lines, pos.col, pos.line);
    }
  }
  else {
    throw std::runtime_error("Unsupported motion: " + std::string(motion));
  }
}


void applySingleMotion(Position& pos, Mode& mode, const NavContext& navContext, const string& motion, const vector<string>& lines) {
  applyParsedMotion(pos, mode, navContext, ParsedMotion(motion, 0), lines);
}

// Return the result if we were to simulate motionSeq at current state
// Important that pos and mode are passed by copy! We wouldn't want to change any state.
MotionResult simulateMotions(Position pos, Mode mode, const NavContext& navContext,
                          const std::string &motionSeq,
                          const std::vector<std::string> &lines) {
  auto motions = parseMotions(motionSeq);
  for (const auto &motion : motions) {
    applyParsedMotion(pos, mode, navContext, motion, lines);
  }
  return MotionResult(pos, mode);
}
