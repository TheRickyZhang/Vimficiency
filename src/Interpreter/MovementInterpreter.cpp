#include "MovementInterpreter.h"

#include <cassert>
#include <limits>
#include <ostream>

#include "Types/NavContext.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimMotionUtils.h"
#include "VimCore/VimOptions.h"

#include "Interpreter/ParserChar.h"
#include "Interpreter/SequenceFormatting.h"
#include "Keyboard/ToKeys/MovementToKeys.h"

using namespace std;

namespace {

struct CharFindState {
  bool valid = false;
  char target = '\0';
  bool forward = true;
  bool till = false;
};

bool isFindCommand(char c) {
  return c == 'f' || c == 'F' || c == 't' || c == 'T';
}

bool parseFindTarget(string_view motion, char& target) {
  if (motion.size() < 2) return false;
  if (motion[1] == '<') {
    size_t close = motion.find('>', 1);
    if (close != string_view::npos) {
      if (auto parsed = parseDisplayChar(motion.substr(1, close)); parsed.has_value()) {
        target = *parsed;
        return true;
      }
    }
  }
  target = motion[1];
  return true;
}

}  // namespace

std::ostream& operator<<(std::ostream& os, const ParsedMovement& motion) {
  if(motion.hasCount()) {
    os << motion.effectiveCount();
  }
  os << motion.motion;
  return os;
}

std::string formatMovementParseError(const MovementParseError& error) {
  switch (error.kind) {
  case MovementParseErrorKind::UnknownMotion:
    return "unknown motion at offset " + std::to_string(error.offset);
  case MovementParseErrorKind::MalformedSpecialKey:
    return "malformed special key at offset " + std::to_string(error.offset);
  }
  assert(false && "Unhandled MovementParseErrorKind");
  return "unknown parse error";
}
// Does string motion parsing. See SequenceToKeys for the physical key parsing.
// IMPORTANT: Returned ParsedMovements contain string_views into seq - caller must ensure seq outlives usage.

  // TODO: Once the motions we support are stable, and if it makes sense in how we implement vim's object model, consider representing motions as an enum instead of string. Then it would be no return, as harder to read/debug, but more efficient.
std::expected<std::vector<ParsedMovement>, MovementParseError> parseMovements(std::string_view seq) {
  std::string_view sv(seq);  // Create view to avoid allocations
  std::vector<ParsedMovement> result;
  size_t i = 0;
  while (i < sv.size()) {
    char c = sv[i];

    int cnt = 0;
    // 0 is a valid digit except for the first one
    if (isAsciiDigit(c) && c != '0') {
      constexpr int INT_MAX_VALUE = std::numeric_limits<int>::max();
      while (i < sv.size() && isAsciiDigit(sv[i])) {
        int digit = sv[i] - '0';
        if (cnt > (INT_MAX_VALUE - digit) / 10) {
          cnt = INT_MAX_VALUE;
        } else {
          cnt = cnt * 10 + digit;
        }
        i++;
      }

      if (i >= sv.size()) break;
      c = sv[i];  // Update c to char after count
    }

    // f/F/t/T consume one target, including canonical forms like <Space>.
    // `;` and `,` remain separate motion tokens so replay can preserve Vim's
    // last-char-find state across intervening movement.
    if (isFindCommand(c) && i + 1 < sv.size()) {
      size_t start = i;
      size_t targetEnd = i + 2;
      if (sv[i + 1] == '<') {
        size_t close = sv.find('>', i + 1);
        if (close != std::string_view::npos) {
          std::string_view special = sv.substr(i + 1, close - i);
          if (parseDisplayChar(special).has_value()) {
            targetEnd = close + 1;
          }
        }
      }
      i = targetEnd;
      result.push_back(ParsedMovement{sv.substr(start, i - start), cnt});
      continue;
    }

    // Handle <C-_> for special bindings (e.g., <C-_>, <C-d>, <CR>)
    if (c == '<') {
      size_t close = sv.find('>', i);
      if (close != std::string_view::npos) {
        std::string_view special = sv.substr(i, close - i + 1);
        if (ALL_MOTIONS.contains(special)) {  // No allocation - transparent comparator
          result.push_back(ParsedMovement{special, cnt});
          i = close + 1;
          continue;
        }
        // If not a known motion, fall through to error
      }
      return std::unexpected(MovementParseError{
          .kind = MovementParseErrorKind::MalformedSpecialKey,
          .offset = i,
      });
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
      return std::unexpected(MovementParseError{
          .kind = MovementParseErrorKind::UnknownMotion,
          .offset = i,
      });
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
static void applyParsedMovementWithState(CursorPos& pos, Mode& mode,
                       const ParsedMovement& parsedMotion,
                       const Lines& lines,
                       const NavContext& navContext,
                       CharFindState& charFind) {
  (void)mode;  // Currently unused but kept for API consistency
  int n = static_cast<int>(lines.size());
  std::string_view motion = parsedMotion.motion;
  bool hasCount = parsedMotion.hasCount();
  int count = parsedMotion.effectiveCount();
  // Fundamental movements
  if (motion == "h") {
    VimCore::moveCol(pos, lines, -count);
  } else if (motion == "l") {
    VimCore::moveCol(pos, lines, count);
  } else if (motion == "j") {
    VimCore::moveLine(pos, lines, count);
  } else if (motion == "k") {
    VimCore::moveLine(pos, lines, -count);
  } else if (motion == "0") {
    pos.setCol(0);
  } else if (motion == "$") {
    if(hasCount) {
      int targetLine = pos.line + count - 1;
      if (targetLine >= n) {
        if (pos.line == n - 1) {
          pos.targetCol = TARGETCOL_EOL;
          return;
        }
        pos.line = n - 1;
      } else {
        pos.line = targetLine;
      }
    }
    int len = static_cast<int>(lines[pos.line].size());
    // Set col to end of line, but targetCol to TARGETCOL_EOL so subsequent
    // vertical movements keep cursor at end of each line (Vim's curswant behavior)
    pos.col = len == 0 ? 0 : len - 1;
    pos.targetCol = TARGETCOL_EOL;
  } else if (motion == "^") {
    pos.setCol(VimCore::caretTargetColInLine(lines[pos.line], pos.col));
  } else if (motion == "gg") {
    // Special: set line. Counted gg is 1-based and clamps at EOF.
    pos.line = hasCount ? min(count - 1, n - 1) : 0;
    if constexpr (VimOptions::startOfLine()) {
      pos.setCol(VimCore::firstNonBlankColInLine(lines[pos.line]));
    } else {
      pos.clampColPreservingTarget(VimCore::clampCol(lines, pos.targetCol, pos.line));
    }
  } else if (motion == "G") {
    // Special: set line. Because it's 1 based, subtract 1
    pos.line = hasCount ? min(count-1, n-1) : n-1;
    if constexpr (VimOptions::startOfLine()) {
      pos.setCol(VimCore::firstNonBlankColInLine(lines[pos.line]));
    } else {
      pos.clampColPreservingTarget(VimCore::clampCol(lines, pos.targetCol, pos.line));
    }
  }
  // Words
  // Note: motionW/motionE may return "past end" positions for delete operations.
  // For cursor movement, clamp to valid bounds.
  // TODO: Able to process faster with indices? Or at least modify underlying VimCore calls to be more efficient with multiple invocations.
  else if (motion == "w") {
    for(int i = 0; i < count; i++) VimCore::motionW(pos, lines, false);
    pos.setCol(VimCore::clampCol(lines, pos.col, pos.line));
  } else if (motion == "b") {
    for(int i = 0; i < count; i++) VimCore::motionB(pos, lines, false);
  } else if (motion == "e") {
    for(int i = 0; i < count; i++) VimCore::motionE(pos, lines, false);
    pos.setCol(VimCore::clampCol(lines, pos.col, pos.line));
  } else if (motion == "W") {
    for(int i = 0; i < count; i++) VimCore::motionW(pos, lines, true);
    pos.setCol(VimCore::clampCol(lines, pos.col, pos.line));
  } else if (motion == "B") {
    for(int i = 0; i < count; i++) VimCore::motionB(pos, lines, true);
  } else if (motion == "E") {
    for(int i = 0; i < count; i++) VimCore::motionE(pos, lines, true);
    pos.setCol(VimCore::clampCol(lines, pos.col, pos.line));
  } else if (motion == "ge") {
    for(int i = 0; i < count; i++) VimCore::motionGe(pos, lines, false);
  } else if (motion == "gE") {
    for(int i = 0; i < count; i++) VimCore::motionGe(pos, lines, true);
  }
  // Text object jumps
  else if (motion == "{") {
    for(int i = 0; i < count; i++) VimCore::motionParagraphPrev(pos, lines);
  } else if (motion == "}") {
    for(int i = 0; i < count; i++) VimCore::motionParagraphNext(pos, lines);
  } else if (motion == "(") {
    for(int i = 0; i < count; i++) VimCore::motionSentencePrev(pos, lines);
  } else if (motion == ")") {
    for(int i = 0; i < count; i++) VimCore::motionSentenceNext(pos, lines);
  }
  // f/F/t/T motions.
  else if (motion.size() >= 2 && isFindCommand(motion[0])) {
    char cmd = motion[0];
    char target = '\0';
    if (!parseFindTarget(motion, target)) return;
    bool forward = (cmd == 'f' || cmd == 't');
    bool till = (cmd == 't' || cmd == 'T');
    const string &line = lines[pos.line];
    
    int newCol = VimCore::findCharInLine(target, line, pos.col, forward, till,
                                         count, /*repeat=*/false);
    if (newCol >= 0) {
      pos.setCol(newCol);
    }
    charFind = CharFindState{
        .valid = true,
        .target = target,
        .forward = forward,
        .till = till,
    };
  }
  // Repeat the previous f/F/t/T motion.
  else if (motion == ";" || motion == ",") {
    if (!charFind.valid) return;
    bool repeatForward = motion == ";" ? charFind.forward : !charFind.forward;
    const string& line = lines[pos.line];
    int newCol = VimCore::findCharInLine(
        charFind.target, line, pos.col, repeatForward, charFind.till,
        count, /*repeat=*/true);
    if (newCol >= 0) {
      pos.setCol(newCol);
    }
  }
  // Jumps (rely on navContext)
  // Note: In Vim, count for <C-d>/<C-u> SETS the scroll amount, not repeats
  else if (motion == "<C-d>") {
    int amount = hasCount ? count : navContext.scrollAmount;
    pos.line = min(pos.line + amount, n - 1);
    if constexpr (VimOptions::startOfLine()) {
      pos.setCol(VimCore::firstNonBlankColInLine(lines[pos.line]));
    } else {
      pos.clampColPreservingTarget(VimCore::clampCol(lines, pos.targetCol, pos.line));
    }
  }
  else if (motion == "<C-u>") {
    int amount = hasCount ? count : navContext.scrollAmount;
    pos.line = max(pos.line - amount, 0);
    if constexpr (VimOptions::startOfLine()) {
      pos.setCol(VimCore::firstNonBlankColInLine(lines[pos.line]));
    } else {
      pos.clampColPreservingTarget(VimCore::clampCol(lines, pos.targetCol, pos.line));
    }
  }
  else if (motion == "<C-f>") {
    for(int i = 0; i < count; i++) {
      int jump = max(0, navContext.windowHeight - 2);
      pos.line = min(pos.line + jump, n - 1);
    }
    if constexpr (VimOptions::startOfLine()) {
      pos.setCol(VimCore::firstNonBlankColInLine(lines[pos.line]));
    } else {
      pos.clampColPreservingTarget(VimCore::clampCol(lines, pos.targetCol, pos.line));
    }
  }
  else if (motion == "<C-b>") {
    for(int i = 0; i < count; i++) {
      int jump = max(0, navContext.windowHeight - 2);
      pos.line = max(pos.line - jump, 0);
    }
    if constexpr (VimOptions::startOfLine()) {
      pos.setCol(VimCore::firstNonBlankColInLine(lines[pos.line]));
    } else {
      pos.clampColPreservingTarget(VimCore::clampCol(lines, pos.targetCol, pos.line));
    }
  }
  else {
    assert(false && "Unsupported motion");
  }
}

void applyParsedMovement(CursorPos& pos, Mode& mode,
                       const ParsedMovement& parsedMotion,
                       const Lines& lines,
                       const NavContext& navContext) {
  CharFindState charFind;
  applyParsedMovementWithState(pos, mode, parsedMotion, lines, navContext, charFind);
}

void applySingleMovement(CursorPos& pos, Mode& mode, string_view motion, const Lines& lines, const NavContext& navContext) {
  applyParsedMovement(pos, mode, ParsedMovement(motion, 0), lines, navContext);
}

// Return the result if we were to simulate movementSeq at current state
// Important that pos is passed by copy! We wouldn't want to change any state.
CursorPos simulateMovements(CursorPos pos, std::string_view movementSeq, const Lines& lines,
                         const NavContext& navContext) {
  assert(!lines.empty() && "Lines can't be empty");
  Mode mode = Mode::Normal;  // Motions don't change mode, so use dummy
  // Test-only helper: inputs are hand-authored valid motion strings, so
  // `.value()` is the right precondition check. In debug it's an assert;
  // in release it throws `std::bad_expected_access` rather than UB on a
  // bare `*motions` dereference.
  auto motions = parseMovements(movementSeq).value();
  CharFindState charFind;
  for (const auto& motion : motions) {
    applyParsedMovementWithState(pos, mode, motion, lines, navContext, charFind);
  }
  return pos;
}
