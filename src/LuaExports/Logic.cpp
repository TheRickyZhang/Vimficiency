#include "LuaExports/Common.h"

#include "Interpreter/SequenceParser.h"

#include <algorithm>
#include <cstdlib>

using namespace std;

namespace VF::LuaExports::logic {

namespace {

bool isCleanBoundaryMode(string_view mode) {
  if (mode.empty()) return false;
  if (mode.substr(0, min<size_t>(2, mode.size())) == "no") return false;
  return mode.front() == 'n';
}

string modePrefix(string_view mode) {
  return string(mode.substr(0, min<size_t>(2, mode.size())));
}

bool endsWithOperatorEdit(const string& sequence) {
  auto parsed = parseSequence(sequence);
  if (!parsed || parsed->empty()) return false;
  const TokenKind kind = parsed->back().kind;
  return kind == TokenKind::Change || kind == TokenKind::Delete;
}

pair<int, int> findDiffRange(const Lines& a, const Lines& b) {
  int firstDiff = -1;
  int lastDiff = -1;
  const size_t maxLen = max(a.size(), b.size());
  for (size_t i = 0; i < maxLen; ++i) {
    const string* lhs = i < a.size() ? &a[i] : nullptr;
    const string* rhs = i < b.size() ? &b[i] : nullptr;
    if (lhs == rhs) continue;
    if (!lhs || !rhs || *lhs != *rhs) {
      if (firstDiff < 0) firstDiff = static_cast<int>(i);
      lastDiff = static_cast<int>(i);
    }
  }
  return {firstDiff, lastDiff};
}

}  // namespace

Result<string> buildKeySequence(string_view encoded) {
  return payload::decodeKeyTrackingEvents(encoded).transform([](const vector<payload::KeyTrackingEvent>& events) {
    string out;
    out.reserve(events.size() * 2);

    size_t i = 0;
    while (i < events.size()) {
      const auto& curr = events[i];
      bool dominated = false;
      if (i + 1 < events.size()) {
        const auto& next = events[i + 1];
        const bool sameKey = curr.keyTyped == next.keyTyped;
        const string currMode = modePrefix(curr.mode);
        const string nextMode = modePrefix(next.mode);
        if (sameKey && currMode == "no" && nextMode == "no" &&
            endsWithOperatorEdit(out + curr.keyTyped)) {
          out += curr.keyTyped;
          i += 2;
          dominated = true;
        } else if (sameKey && currMode == "no" && nextMode != "no" &&
                   i + 2 < events.size() &&
                   endsWithOperatorEdit(out + curr.keyTyped + events[i + 2].keyTyped)) {
          out += curr.keyTyped;
          i += 2;
          dominated = true;
        }
      }
      if (!dominated) {
        out += curr.keyTyped;
        i += 1;
      }
    }

    return out;
  });
}

pair<int, int> computeSearchRegion(
    int startRow,
    int endRow,
    const Lines& startLines,
    const Lines& endLines,
    int padding) {
  int minRow = min(startRow, endRow);
  int maxRow = max(startRow, endRow);
  const auto [firstDiff, lastDiff] = findDiffRange(startLines, endLines);
  if (firstDiff >= 0) {
    minRow = min(minRow, firstDiff);
    maxRow = max(maxRow, lastDiff);
  }

  const int bufferLen = static_cast<int>(max(startLines.size(), endLines.size()));
  const int regionStart = max(0, minRow - padding);
  const int regionEnd = min(bufferLen - 1, maxRow + padding);
  return {regionStart, regionEnd};
}

int resolveRecallCutoffIndex(
    const vector<payload::RecallRecordMeta>& records,
    int64_t targetHrtime,
    int budget) {
  int lo = 0;
  int hi = static_cast<int>(records.size()) - 1;
  int best = -1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (records[mid].timeStarted <= targetHrtime) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  if (best < 0) return 0;

  int steps = 0;
  for (int i = best; i >= 0 && steps <= budget; --i, ++steps) {
    if (isCleanBoundaryMode(records[i].firstMode)) {
      return i + 1;
    }
  }
  return 0;
}

int manualEvictReason(
    int startRow,
    int cursorRow,
    int64_t lastKeyTimeNs,
    bool hasLastKey,
    int64_t nowNs,
    int maxSearchLines,
    int manualIdleTimeoutSeconds) {
  if (abs(cursorRow - startRow) + 1 > maxSearchLines) {
    return MANUAL_EVICT_DRIFT;
  }
  if (hasLastKey && nowNs - lastKeyTimeNs > static_cast<int64_t>(manualIdleTimeoutSeconds) * 1000000000LL) {
    return MANUAL_EVICT_IDLE;
  }
  return MANUAL_EVICT_NONE;
}

}  // namespace VF::LuaExports::logic
