#include "Sequence.h"
#include "Utils/StringUtils.h"

#include <string_view>

namespace {

// Check if character is a digit
bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

// Check if position starts a special key like <Esc>, <CR>, etc.
// Returns the length of the special key, or 0 if not a special key
size_t specialKeyLength(std::string_view s, size_t pos) {
  if (pos >= s.size() || s[pos] != '<') return 0;

  size_t end = s.find('>', pos);
  if (end == std::string_view::npos) return 0;

  return end - pos + 1;
}

// Check if position is at <Esc>
bool isEsc(std::string_view s, size_t pos) {
  if (pos + 4 >= s.size()) return false;
  return s.substr(pos, 5) == "<Esc>";
}

// Skip over a count prefix (digits)
size_t skipCount(std::string_view s, size_t pos) {
  while (pos < s.size() && isDigit(s[pos])) {
    pos++;
  }
  return pos;
}

// Simple insert-mode entry commands (single char that enters insert mode directly)
bool isSimpleInsertEntry(char c) {
  return c == 'i' || c == 'I' || c == 'a' || c == 'A' ||
         c == 'o' || c == 'O' || c == 's' || c == 'S' ||
         c == 'C' || c == 'R';
}

// Skip a motion after c/d operator (simplified - handles common cases)
// Returns position after the motion
size_t skipMotion(std::string_view s, size_t pos) {
  if (pos >= s.size()) return pos;

  // Skip count prefix
  pos = skipCount(s, pos);
  if (pos >= s.size()) return pos;

  char c = s[pos];

  // Single-char motions
  if (c == 'w' || c == 'W' || c == 'e' || c == 'E' ||
      c == 'b' || c == 'B' || c == 'h' || c == 'l' ||
      c == 'j' || c == 'k' || c == '0' || c == '^' ||
      c == '$' || c == 'G' || c == '{' || c == '}' ||
      c == '(' || c == ')' || c == '%') {
    return pos + 1;
  }

  // f/F/t/T + char
  if (c == 'f' || c == 'F' || c == 't' || c == 'T') {
    return pos + 2;
  }

  // Text objects: iw, aw, i", a", etc.
  if (c == 'i' || c == 'a') {
    if (pos + 1 < s.size()) {
      return pos + 2;
    }
  }

  // gg motion
  if (c == 'g' && pos + 1 < s.size() && s[pos + 1] == 'g') {
    return pos + 2;
  }

  // ge/gE motion
  if (c == 'g' && pos + 1 < s.size() && (s[pos + 1] == 'e' || s[pos + 1] == 'E')) {
    return pos + 2;
  }

  // Special keys like <Home>, <End>
  size_t specLen = specialKeyLength(s, pos);
  if (specLen > 0) {
    return pos + specLen;
  }

  // Fallback: single char
  return pos + 1;
}

// Parse sequence and output with mode-separated spacing
void outputWithModeSpacing(std::ostream& os, std::string_view s) {
  if (s.empty()) return;

  size_t pos = 0;
  bool inInsert = false;
  bool firstSegment = true;
  size_t segmentStart = 0;

  auto flushSegment = [&](size_t end) {
    if (end > segmentStart) {
      std::string segment(s.substr(segmentStart, end - segmentStart));
      if (!firstSegment) {
        os << " ";
      }
      os << makePrintable(segment);
      firstSegment = false;
      segmentStart = end;
    }
  };

  while (pos < s.size()) {
    // Skip count prefix
    size_t afterCount = skipCount(s, pos);

    if (afterCount >= s.size()) {
      pos = afterCount;
      break;
    }

    char c = s[afterCount];

    if (!inInsert) {
      // In normal mode, look for insert-mode entry
      if (isSimpleInsertEntry(c)) {
        inInsert = true;
        pos = afterCount + 1;
        continue;
      }

      // c + motion enters insert mode
      if (c == 'c' && afterCount + 1 < s.size()) {
        size_t motionEnd = skipMotion(s, afterCount + 1);
        inInsert = true;
        pos = motionEnd;
        continue;
      }

      // Handle special keys
      size_t specLen = specialKeyLength(s, afterCount);
      if (specLen > 0) {
        pos = afterCount + specLen;
        continue;
      }

      // Regular normal-mode command
      pos = afterCount + 1;
    } else {
      // In insert mode, look for <Esc>
      if (isEsc(s, pos)) {
        // Include <Esc> in the insert segment, then flush
        pos += 5;
        flushSegment(pos);
        inInsert = false;
        continue;
      }

      // Handle special keys in insert mode
      size_t specLen = specialKeyLength(s, pos);
      if (specLen > 0) {
        pos += specLen;
        continue;
      }

      // Regular typed character
      pos++;
    }
  }

  // Flush any remaining content
  flushSegment(s.size());
}

} // anonymous namespace

std::ostream& operator<<(std::ostream& os, const Sequence& seq) {
  outputWithModeSpacing(os, seq.keys);
  return os;
}
