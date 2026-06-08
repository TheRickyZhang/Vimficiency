#pragma once

// Selects the composition diff planner. VimDiff is the default costed planner;
// MyersDiff is the legacy shortest-edit-script fallback.
namespace DiffAlgorithm {

inline constexpr int VimDiff = 0;
inline constexpr int MyersDiff = 1;

inline const char* name(int algorithm) {
  switch (algorithm) {
    case VimDiff:   return "vimdiff";
    case MyersDiff: return "myersdiff";
    default:        return "unknown";
  }
}

}  // namespace DiffAlgorithm
