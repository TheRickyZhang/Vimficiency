#pragma once

#include <algorithm>
#include <string_view>
#include <vector>

#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/Token.h"

class OptimizerParamOverrides;

enum class SuggestionSortMode {
  Effort,
  Distance,
  Score,
};

struct Suggestion {
  Token token;
  CursorPos landingPos{0, 0};
  double costDiff = 0.0;  // Historical name; this is immediate effort.
  double distance = 0.0;
  double score = 0.0;
};

inline double suggestionSortValue(const Suggestion& suggestion, SuggestionSortMode mode) {
  switch (mode) {
    case SuggestionSortMode::Effort:
      return suggestion.costDiff;
    case SuggestionSortMode::Distance:
      return suggestion.distance;
    case SuggestionSortMode::Score:
      return suggestion.score;
  }
  return suggestion.costDiff;
}

inline bool suggestionLess(
    const Suggestion& a,
    const Suggestion& b,
    SuggestionSortMode mode) {
  const double av = suggestionSortValue(a, mode);
  const double bv = suggestionSortValue(b, mode);
  if (av != bv) return av < bv;
  if (mode != SuggestionSortMode::Distance && a.distance != b.distance)
    return a.distance < b.distance;
  if (mode != SuggestionSortMode::Effort && a.costDiff != b.costDiff)
    return a.costDiff < b.costDiff;
  return std::string_view(a.token) < std::string_view(b.token);
}

inline void updateSuggestionMetrics(
    Suggestion& suggestion,
    double effortDiff,
    double distance,
    double effortWeight,
    double distanceWeight) {
  suggestion.costDiff = effortDiff;
  suggestion.distance = distance;
  suggestion.score = effortWeight * effortDiff + distanceWeight * distance;
}

inline double textDistanceEstimate(const Lines& lines) {
  if (lines.empty()) return 0.0;
  int total = 0;
  for (const auto& line : lines)
    total += static_cast<int>(line.size());
  total += (static_cast<int>(lines.size()) - 1) * 2;
  return static_cast<double>(total);
}

inline void sortAndCapSuggestions(
    std::vector<Suggestion>& suggestions,
    int maxCount,
    SuggestionSortMode mode) {
  std::stable_sort(suggestions.begin(), suggestions.end(),
                   [mode](const Suggestion& a, const Suggestion& b) {
                     return suggestionLess(a, b, mode);
                   });
  if (suggestions.size() > static_cast<size_t>(maxCount))
    suggestions.resize(static_cast<size_t>(maxCount));
}

struct FrontierQuery {
  const Lines& lines;
  CursorPos cursor;
  std::string_view seq = {};
  int maxCount = 0;
  SuggestionSortMode sortMode = SuggestionSortMode::Effort;
  // Null means C++ optimizer defaults.
  const OptimizerParamOverrides* overrides = nullptr;
};
