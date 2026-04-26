// SequenceParser.cpp - Implementation of sequence parsing for animation

#include "SequenceParser.h"
#include "Keyboard/ToKeys/MovementToKeys.h"

#include <cassert>
#include <limits>
#include <unordered_set>

using namespace std;

string formatSequenceParseError(const SequenceParseError& error) {
  switch (error.kind) {
  case SequenceParseErrorKind::UnknownCharacter:
    return "unknown character at offset " + to_string(error.offset);
  case SequenceParseErrorKind::MalformedSpecialKey:
    return "malformed special key at offset " + to_string(error.offset);
  }
  assert(false && "Unhandled SequenceParseErrorKind");
  return "unknown parse error";
}

namespace {

// Commands that enter insert mode (standalone, no motion required)
const unordered_set<string> INSERT_STANDALONE = {
    "i", "I", "a", "A", "o", "O", "s", "S", "R"
};

// Commands that enter visual mode. `<C-v>` is handled as a special key
// in `tryParseVisual` — this set is for the simple one/two-char forms.
const unordered_set<string> VISUAL_BARE = {
    "v", "V", "gh", "gH"
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
  constexpr int INT_MAX_VALUE = std::numeric_limits<int>::max();
  int cnt = 0;
  while (i < sv.size() && isdigit(sv[i])) {
    int digit = sv[i] - '0';
    if (cnt > (INT_MAX_VALUE - digit) / 10) {
      cnt = INT_MAX_VALUE;
    } else {
      cnt = cnt * 10 + digit;
    }
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

// Whitespace/edit specials that act as motions in Normal mode but aren't
// in `ALL_MOTIONS` (the optimizer-explorable set). Recognized for
// tokenization only — the optimizer doesn't emit these, but user
// sequences and replays routinely contain them (`<Space>` as right-arrow,
// `<BS>` as left-arrow, `<CR>` as "move to first non-blank on next
// line"). Without this set, sequences containing them fell through to
// Lua's char-by-char fallback, which has no insert-mode awareness.
const unordered_set<string> REPLAY_MOTION_SPECIALS = {
  "<Space>", "<BS>", "<CR>", "<Enter>", "<Return>", "<Tab>", "<Del>",
};

// Try to parse a motion at position i
// Returns the motion string (with any f/F/t/T target and ;, repeats) or empty string
string tryParseMotion(string_view sv, size_t i) {
  if (i >= sv.size()) return "";

  char c = sv[i];

  // Handle <C-...> style motions
  if (c == '<') {
    string special = tryParseSpecialKey(sv, i);
    if (!special.empty() &&
        (ALL_MOTIONS.contains(special) || REPLAY_MOTION_SPECIALS.count(special))) {
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

// Try to parse a visual-mode-entering token at position i.
// Returns (token string, length consumed) or ("", 0).
// Recognizes `v`, `V`, `gh`, `gH`, `<C-v>`. Intentionally stateless — the
// parser doesn't track visual mode, it just emits the entering token. The
// replay layer feeds tokens to nvim one-by-one, which handles actual mode
// transitions; subsequent tokens inside the selection (motions, operators,
// text objects after `v`) can be semantically misclassified here, but they
// still feed correctly because feedkeys is tag-agnostic and the fast-path
// gate consults live `nvim_get_mode()`.
pair<string, size_t> tryParseVisual(string_view sv, size_t i) {
  if (i >= sv.size()) return {"", 0};

  char c = sv[i];

  // `<C-v>` — visual block.
  if (c == '<') {
    string special = tryParseSpecialKey(sv, i);
    if (special == "<C-v>") return {special, special.size()};
    return {"", 0};
  }

  // Two-char forms: `gh`, `gH`.
  if (c == 'g' && i + 1 < sv.size()) {
    string two(sv.substr(i, 2));
    if (VISUAL_BARE.count(two)) return {two, 2};
  }

  // Single-char forms: `v`, `V`.
  string one(1, c);
  if (VISUAL_BARE.count(one)) return {one, 1};

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
// If no <Esc> found, returns all remaining text. Malformed `<...>` (no
// closing `>`) is treated as a literal `<` character so the loop always
// advances — no sentinel/error channel.
pair<string, size_t> parseTypedText(string_view sv, size_t i) {
  string typed;
  size_t start = i;

  while (i < sv.size()) {
    if (sv[i] == '<') {
      string special = tryParseSpecialKey(sv, i);
      if (special.empty()) {
        typed += sv[i];
        i++;
        continue;
      }
      if (special == "<Esc>") {
        // Don't include <Esc> in typed text
        return {typed, i - start};
      }
      typed += special;
      i += special.size();
    } else {
      typed += sv[i];
      i++;
    }
  }

  return {typed, i - start};
}

}  // namespace

expected<vector<SequenceToken>, SequenceParseError>
parseSequence(string_view seq) {
  vector<SequenceToken> tokens;
  string_view sv(seq);
  size_t i = 0;
  bool inInsertMode = false;

  while (i < sv.size()) {
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

      auto [typed, len] = parseTypedText(sv, i);
      if (!typed.empty()) {
        tokens.push_back(SequenceToken(typed, TokenType::TypedText));
      }
      i += len;
      continue;
    }

    // Parse optional count prefix only in command mode. Once a change
    // command has entered insert mode, digits are literal typed text and
    // must not be consumed as counts.
    int count = parseCount(sv, i);
    string countStr = count > 0 ? to_string(count) : "";

    if (i >= sv.size()) break;

    // Try to parse change command (enters insert mode)
    auto [changeCmd, changeLen] = tryParseChange(sv, i);
    if (!changeCmd.empty()) {
      tokens.push_back(SequenceToken(countStr + changeCmd, TokenType::Change));
      i += changeLen;
      inInsertMode = true;
      continue;
    }

    // Try to parse visual-entering command. Stateless — see `tryParseVisual`.
    auto [visualCmd, visualLen] = tryParseVisual(sv, i);
    if (!visualCmd.empty()) {
      tokens.push_back(SequenceToken(countStr + visualCmd, TokenType::Visual));
      i += visualLen;
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
      tokens.push_back(SequenceToken(countStr + motion, TokenType::Movement));
      i += motion.size();
      continue;
    }

    // Standalone `<Esc>` in Normal context — legitimate after visual
    // selection or as a no-op. Previously this path errored; recognizing
    // it keeps sequences like `vape<Esc>` tokenizing cleanly instead of
    // falling through to Lua's char-by-char fallback.
    if (sv[i] == '<') {
      string special = tryParseSpecialKey(sv, i);
      if (special == "<Esc>") {
        tokens.push_back(SequenceToken("<Esc>", TokenType::Escape));
        i += special.size();
        continue;
      }
      return unexpected(SequenceParseError{
          .kind = SequenceParseErrorKind::MalformedSpecialKey,
          .offset = i,
      });
    }
    return unexpected(SequenceParseError{
        .kind = SequenceParseErrorKind::UnknownCharacter,
        .offset = i,
    });
  }

  return tokens;
}

expected<vector<string>, SequenceParseError>
parseSequenceStrings(string_view seq) {
  return parseSequence(seq).transform(
      [](const vector<SequenceToken>& tokens) {
        vector<string> out;
        out.reserve(tokens.size());
        for (const auto& token : tokens) {
          out.push_back(token.text);
        }
        return out;
      });
}
