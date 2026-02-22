// SequenceParser.cpp - Implementation of sequence parsing for animation

#include "SequenceParser.h"
#include "Keyboard/MotionToKeys.h"
#include <unordered_set>

using namespace std;

namespace {

// Commands that enter insert mode (standalone, no motion required)
const unordered_set<string> INSERT_STANDALONE = {
    "i", "I", "a", "A", "o", "O", "s", "S", "R"
};

// Special change commands that don't need a motion
const unordered_set<string> CHANGE_SPECIAL = {
    "C",   // change to end of line
    "cc",  // change whole line
};

// Text objects (inner and around variants)
const unordered_set<string> TEXT_OBJECTS = {
    // Word
    "iw", "aw", "iW", "aW",
    // Quote/string
    "i\"", "a\"", "i'", "a'", "i`", "a`",
    // Parentheses
    "i(", "a(", "i)", "a)", "ib", "ab",
    // Braces
    "i{", "a{", "i}", "a}", "iB", "aB",
    // Brackets
    "i[", "a[", "i]", "a]",
    // Angle brackets
    "i<", "a<", "i>", "a>",
    // Paragraph and sentence
    "ip", "ap", "is", "as",
    // Tag
    "it", "at",
};

// Parse a count prefix (digits, not starting with 0)
// Returns the count (0 if no count) and advances i past the digits
int parseCount(string_view sv, size_t& i) {
  if (i >= sv.size() || !isdigit(sv[i]) || sv[i] == '0') {
    return 0;
  }
  int cnt = 0;
  while (i < sv.size() && isdigit(sv[i])) {
    cnt = cnt * 10 + (sv[i] - '0');
    i++;
  }
  return cnt;
}

// Check if position i starts a special key like <Esc>, <C-d>, etc.
// Returns the full key string if found, empty string otherwise
string tryParseSpecialKey(string_view sv, size_t i) {
  if (i >= sv.size() || sv[i] != '<') return "";
  size_t close = sv.find('>', i);
  if (close == string_view::npos) return "";
  return string(sv.substr(i, close - i + 1));
}

// Try to parse a motion at position i
// Returns the motion string (with any f/F/t/T target and ;, repeats) or empty string
string tryParseMotion(string_view sv, size_t i) {
  if (i >= sv.size()) return "";

  char c = sv[i];

  // Handle <C-...> style motions
  if (c == '<') {
    string special = tryParseSpecialKey(sv, i);
    if (!special.empty() && ALL_MOTIONS.contains(special)) {
      return special;
    }
    return "";
  }

  // f/F/t/T motions consume target char and optional ;/,
  if ((c == 'f' || c == 'F' || c == 't' || c == 'T') && i + 1 < sv.size()) {
    size_t end = i + 2;  // motion + target
    while (end < sv.size() && (sv[end] == ';' || sv[end] == ',')) {
      end++;
    }
    return string(sv.substr(i, end - i));
  }

  // Standard longest-match for other motions
  for (size_t len = min(sv.size() - i, size_t{4}); len > 0; --len) {
    string_view candidate = sv.substr(i, len);
    if (ALL_MOTIONS.contains(candidate)) {
      return string(candidate);
    }
  }

  return "";
}

// Try to parse a text object at position i (e.g., "iw", "a\"", "ib")
string tryParseTextObject(string_view sv, size_t i) {
  if (i + 1 >= sv.size()) return "";

  // Text objects are 2 chars: i/a + object type
  string candidate(sv.substr(i, 2));
  if (TEXT_OBJECTS.count(candidate)) {
    return candidate;
  }
  return "";
}

// Try to parse a delete command at position i
// Returns (command string, length consumed) or ("", 0)
pair<string, size_t> tryParseDelete(string_view sv, size_t i) {
  if (i >= sv.size()) return {"", 0};

  char c = sv[i];

  // Single-char deletes/edits that stay in normal mode
  if (c == 'x' || c == 'X' || c == 'D' || c == '.' || c == '~') {
    return {string(1, c), 1};
  }

  // r{char} (replace single char, stays in normal mode)
  if (c == 'r' && i + 1 < sv.size()) {
    return {string(sv.substr(i, 2)), 2};
  }

  // J, gJ (join lines)
  if (c == 'J') {
    return {"J", 1};
  }
  if (c == 'g' && i + 1 < sv.size() && sv[i + 1] == 'J') {
    return {"gJ", 2};
  }

  // d + motion/textobj
  if (c == 'd') {
    if (i + 1 >= sv.size()) return {"", 0};

    // dd = delete line
    if (sv[i + 1] == 'd') {
      return {"dd", 2};
    }

    // d + text object
    string textObj = tryParseTextObject(sv, i + 1);
    if (!textObj.empty()) {
      return {"d" + textObj, 1 + textObj.size()};
    }

    // d + motion
    string motion = tryParseMotion(sv, i + 1);
    if (!motion.empty()) {
      return {"d" + motion, 1 + motion.size()};
    }
  }

  return {"", 0};
}

// Try to parse a change command (enters insert mode) at position i
// Returns (command string, length consumed) or ("", 0)
pair<string, size_t> tryParseChange(string_view sv, size_t i) {
  if (i >= sv.size()) return {"", 0};

  char c = sv[i];

  // Standalone insert commands
  if (INSERT_STANDALONE.count(string(1, c))) {
    return {string(1, c), 1};
  }

  // C = change to end of line
  if (c == 'C') {
    return {"C", 1};
  }

  // c + motion/textobj
  if (c == 'c') {
    if (i + 1 >= sv.size()) return {"", 0};

    // cc = change line
    if (sv[i + 1] == 'c') {
      return {"cc", 2};
    }

    // c + text object
    string textObj = tryParseTextObject(sv, i + 1);
    if (!textObj.empty()) {
      return {"c" + textObj, 1 + textObj.size()};
    }

    // c + motion
    string motion = tryParseMotion(sv, i + 1);
    if (!motion.empty()) {
      return {"c" + motion, 1 + motion.size()};
    }
  }

  return {"", 0};
}

// Parse typed text until <Esc> is found
// Returns (typed text, length consumed including <Esc>)
// If no <Esc> found, returns all remaining text
pair<string, size_t> parseTypedText(string_view sv, size_t i) {
  string typed;
  size_t start = i;

  while (i < sv.size()) {
    // Check for <Esc>
    if (sv[i] == '<') {
      string special = tryParseSpecialKey(sv, i);
      if (special == "<Esc>") {
        // Don't include <Esc> in typed text
        return {typed, i - start};
      }
      // Other special keys become part of typed text
      typed += special;
      i += special.size();
    } else {
      typed += sv[i];
      i++;
    }
  }

  // No <Esc> found - return all remaining text
  return {typed, i - start};
}

}  // namespace

vector<SequenceToken> parseSequence(string_view seq) {
  vector<SequenceToken> tokens;
  string_view sv(seq);
  size_t i = 0;
  bool inInsertMode = false;

  while (i < sv.size()) {
    // Parse optional count prefix
    int count = parseCount(sv, i);
    string countStr = count > 0 ? to_string(count) : "";

    if (i >= sv.size()) break;

    if (inInsertMode) {
      // In insert mode, look for <Esc> or capture typed text
      if (sv[i] == '<') {
        string special = tryParseSpecialKey(sv, i);
        if (special == "<Esc>") {
          tokens.push_back(SequenceToken("<Esc>", TokenType::Escape));
          i += special.size();
          inInsertMode = false;
          continue;
        }
      }

      // Capture typed text until <Esc>
      auto [typed, len] = parseTypedText(sv, i);
      if (!typed.empty()) {
        tokens.push_back(SequenceToken(typed, TokenType::TypedText));
      }
      i += len;
      continue;
    }

    // Try to parse change command (enters insert mode)
    auto [changeCmd, changeLen] = tryParseChange(sv, i);
    if (!changeCmd.empty()) {
      tokens.push_back(SequenceToken(countStr + changeCmd, TokenType::Change));
      i += changeLen;
      inInsertMode = true;
      continue;
    }

    // Try to parse delete command
    auto [deleteCmd, deleteLen] = tryParseDelete(sv, i);
    if (!deleteCmd.empty()) {
      tokens.push_back(SequenceToken(countStr + deleteCmd, TokenType::Delete));
      i += deleteLen;
      continue;
    }

    // Try to parse motion
    string motion = tryParseMotion(sv, i);
    if (!motion.empty()) {
      tokens.push_back(SequenceToken(countStr + motion, TokenType::Motion));
      i += motion.size();
      continue;
    }

    // Unknown character - include it as typed text (fallback)
    tokens.push_back(SequenceToken(countStr + string(1, sv[i]), TokenType::Motion));
    i++;
  }

  return tokens;
}

vector<string> parseSequenceStrings(string_view seq) {
  auto tokens = parseSequence(seq);
  vector<string> result;
  result.reserve(tokens.size());
  for (const auto& token : tokens) {
    result.push_back(token.text);
  }
  return result;
}
