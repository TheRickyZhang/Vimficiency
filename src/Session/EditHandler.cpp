#include "EditHandler.h"

#include <algorithm>
#include <span>
#include <unordered_map>

#include "Interpreter/SequenceParser.h"
#include "Optimizer/Result.h"

using namespace std;

namespace Explore::EditHandler {

string extractAtom(string_view fullSequence) {
  // MIRROR: parseSequence's TokenType::Change/TypedText/Escape split defines
  // what counts as "normal-mode prefix". Update together.
  auto tokens = parseSequence(fullSequence);
  if (!tokens) return string(fullSequence);
  string atom;
  for (const auto& tok : *tokens) {
    if (tok.type == TokenType::TypedText || tok.type == TokenType::Escape) break;
    atom += tok.text;
  }
  return atom.empty() ? string(fullSequence) : atom;
}

vector<Recommendation> recommendations(
    const EditResult& editResult,
    CursorPos cursor,
    int maxCount) {
  if (maxCount <= 0) return {};

  // MIRROR: EditResult::resultsAt + goalPosAt define the per-start set.
  std::span<const ::Result> starts = editResult.resultsAt(cursor.line, cursor.col);
  if (starts.empty()) return {};

  const CursorPos postEditCursor = editResult.goalPosAt(cursor.line, cursor.col);

  // CUSTOM: dedup by atom (normal-mode prefix), keeping cheapest cost per atom.
  struct AtomEntry {
    string atom;
    double cost;
  };
  unordered_map<string, AtomEntry> byAtom;
  for (const ::Result& r : starts) {
    if (!r.isValid() || r.getSequence().empty()) continue;
    string atom = extractAtom(r.getSequence().view());
    if (atom.empty()) continue;
    auto it = byAtom.find(atom);
    if (it == byAtom.end() || r.getCost() < it->second.cost) {
      byAtom[atom] = AtomEntry{atom, r.getCost()};
    }
  }

  vector<Recommendation> recs;
  recs.reserve(byAtom.size());
  for (auto& [_, entry] : byAtom) {
    Recommendation rec;
    rec.text = std::move(entry.atom);
    rec.kind = "edit";
    rec.cost = entry.cost;
    rec.totalPathCost = entry.cost;
    rec.landingRow = postEditCursor.line;
    rec.landingCol = postEditCursor.col;
    recs.push_back(std::move(rec));
  }

  std::sort(recs.begin(), recs.end(),
      [](const Recommendation& a, const Recommendation& b) {
        if (a.totalPathCost != b.totalPathCost) return a.totalPathCost < b.totalPathCost;
        return a.text < b.text;
      });
  if (static_cast<int>(recs.size()) > maxCount) recs.resize(maxCount);
  return recs;
}

BufferStateEffect validateBufferState(
    const Lines& newLines,
    const Lines& currentFencepost,
    const Lines& nextFencepost) {
  BufferStateEffect eff;
  if (newLines == nextFencepost) {
    eff.accepted = true;
    eff.advance = true;
    return eff;
  }
  if (newLines == currentFencepost) {
    eff.accepted = true;
    eff.advance = false;
    return eff;
  }
  eff.rejectReason = "buffer state drifted outside the planned edit scope";
  return eff;
}

ApplyEditEffect applyEdit(
    const EditResult& editResult,
    CursorPos cursor,
    std::string_view text) {
  ApplyEditEffect eff;
  if (text.empty()) {
    eff.rejectReason = "edit text must be non-empty";
    return eff;
  }
  // MIRROR: match against EditResult::resultsAt — the planned edit-start set.
  std::span<const ::Result> starts = editResult.resultsAt(cursor.line, cursor.col);
  for (const ::Result& r : starts) {
    if (r.isValid() && r.getSequence().view() == text) {
      eff.accepted = true;
      eff.postCursor = editResult.goalPosAt(cursor.line, cursor.col);
      return eff;
    }
  }
  eff.rejectReason = "edit command is not part of the planned edit scope at this cursor";
  return eff;
}

}  // namespace Explore::EditHandler
