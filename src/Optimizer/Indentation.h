#pragma once

#include "Keyboard/KeyedSequence.h"
#include "VimCore/VimOptions.h"

#include <string_view>

// Return the leading whitespace (spaces only) of a string
inline std::string_view leadingWhitespace(std::string_view s) {
  size_t i = 0;
  while (i < s.size() && s[i] == ' ') i++;
  return s.substr(0, i);
}

// Count leading spaces in a string
inline int leadingSpaceCount(std::string_view s) {
  return static_cast<int>(leadingWhitespace(s).size());
}

// Compute number of <BS> presses to reduce indent from `from` to `to` spaces.
// In autoindent context, <BS> deletes to the previous shiftwidth boundary, not
// just 1 space. Returns -1 if <BS> overshoots past `to` (can't land exactly).
inline int bsCountForIndent(int from, int to, int sw = VimOptions::shiftwidth()) {
  int count = 0;
  int pos = from;
  while (pos > to) {
    pos = ((pos - 1) / sw) * sw;
    count++;
    if (pos < to) return -1;  // overshot
  }
  return count;
}

// Compute keys to adjust autoindent from `autoindentLen` to `targetIndentLen`.
// Returns empty if already matching. Uses BS when landing on shiftwidth boundary,
// extra spaces when autoindent is too short, or C-u + retype when BS overshoots.
inline KeyedSequence computeIndentAdjustment(int autoindentLen, int targetIndentLen) {
  if (autoindentLen == targetIndentLen) return {};
  KeyedSequence ks;
  if (autoindentLen < targetIndentLen) {
    ks.appendChar(' ', targetIndentLen - autoindentLen);
    return ks;
  }
  int bsNeeded = bsCountForIndent(autoindentLen, targetIndentLen);
  if (bsNeeded >= 0) {
    ks.appendRepeated(KeyedSequence::BS, bsNeeded);
    return ks;
  }
  ks += KeyedSequence::CtrlU;
  if (targetIndentLen > 0) ks.appendChar(' ', targetIndentLen);
  return ks;
}
