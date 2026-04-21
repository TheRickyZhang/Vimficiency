#include "MotionHandler.h"

#include <algorithm>
#include <set>
#include <string>

#include "Effort/RunningEffort.h"
#include "Interpreter/MotionInterpreter.h"

using namespace std;

namespace Explore::MotionHandler {

namespace {

// CUSTOM: short-motion whitelist. f/F/t/T are added dynamically against the
// current/target lines.
const vector<string> kBaseMotions = {
    "h", "j", "k", "l",
    "w", "W", "b", "B", "e", "E", "ge", "gE",
    "0", "$", "^", "_", "g_",
    "H", "M", "L",
    "gg", "G",
    "%",
    "(", ")", "{", "}",
    "n", "N",
};

void collectLineChars(const Lines& lines, int line, std::set<char>& out) {
  if (line < 0 || line >= static_cast<int>(lines.size())) return;
  for (char c : lines[static_cast<size_t>(line)]) {
    if (c == ' ' || c == '\t') continue;
    out.insert(c);
  }
}

}  // namespace

vector<Recommendation> recommendations(
    const Lines& lines,
    CursorPos cursor,
    CursorPos target,
    const MotionBoundary& boundary,
    const NavContext& navContext,
    const Config& config,
    int maxCount) {
  (void)boundary;  // reserved for future bounded search; simulateMotions doesn't use one.
  if (maxCount <= 0) return {};
  if (target == cursor) return {};

  vector<string> candidates = kBaseMotions;
  std::set<char> searchChars;
  collectLineChars(lines, cursor.line, searchChars);
  collectLineChars(lines, target.line, searchChars);
  for (char c : searchChars) {
    candidates.emplace_back(string("f") + c);
    candidates.emplace_back(string("F") + c);
    candidates.emplace_back(string("t") + c);
    candidates.emplace_back(string("T") + c);
  }

  struct Cand {
    string text;
    double motionCost;
    double total;
    int landRow;
    int landCol;
  };
  vector<Cand> cands;
  cands.reserve(candidates.size());

  for (const string& text : candidates) {
    // MIRROR: parseMotions is the gate; simulateMotions asserts on unparsed input.
    auto parsed = parseMotions(text);
    if (!parsed) continue;

    CursorPos landing = simulateMotions(cursor, text, lines, navContext);
    if (landing == cursor) continue;

    const double motionCost = getEffort(text, config);
    const double dist = static_cast<double>(std::abs(target.line - landing.line))
                      + static_cast<double>(std::abs(target.col - landing.col));
    cands.push_back({text, motionCost, motionCost + dist, landing.line, landing.col});
  }

  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
    if (a.total != b.total) return a.total < b.total;
    if (a.motionCost != b.motionCost) return a.motionCost < b.motionCost;
    return a.text < b.text;
  });

  // CUSTOM: dedup by landing cell — keep the cheapest motion reaching each.
  std::set<std::pair<int, int>> seenLandings;
  vector<Recommendation> recs;
  for (const auto& c : cands) {
    auto key = std::make_pair(c.landRow, c.landCol);
    if (!seenLandings.insert(key).second) continue;
    Recommendation rec;
    rec.text = c.text;
    rec.kind = "motion";
    rec.cost = c.motionCost;
    rec.totalPathCost = c.total;
    rec.landingRow = c.landRow;
    rec.landingCol = c.landCol;
    recs.push_back(std::move(rec));
    if (static_cast<int>(recs.size()) >= maxCount) break;
  }
  return recs;
}

MotionEffect applyMotion(
    const Lines& lines,
    CursorPos cursor,
    std::string_view text,
    const NavContext& navContext) {
  MotionEffect eff;
  if (text.empty()) {
    eff.rejectReason = "motion text must be non-empty";
    return eff;
  }
  // MIRROR: parseMotions + simulateMotions from Interpreter/MotionInterpreter.
  auto parsed = parseMotions(text);
  if (!parsed) {
    eff.rejectReason = "motion text failed to parse: " + formatMotionParseError(parsed.error());
    return eff;
  }
  eff.accepted = true;
  eff.newCursor = simulateMotions(cursor, text, lines, navContext);
  eff.appendedSeq = string(text);
  return eff;
}

MotionEffect acceptCursorMove(CursorPos newCursor, std::string_view rawKeys) {
  MotionEffect eff;
  eff.accepted = true;
  eff.newCursor = newCursor;
  // MIRROR: parseMotions gates what's safe to feed into getEffort's tokenizer.
  if (!rawKeys.empty() && parseMotions(rawKeys)) {
    eff.appendedSeq = string(rawKeys);
  }
  return eff;
}

}  // namespace Explore::MotionHandler
