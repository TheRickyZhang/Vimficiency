#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

enum class CountClass {
  MovementChar,
  MovementWord,
  MovementBigWord,
  MovementLine,
  MovementParagraph,
  MovementSentence,
  MovementJump,
  EditChar,
  EditWord,
  EditBigWord,
  EditLine,
  EditParagraph,
  EditSentence,
  Join,
  COUNT
};

constexpr size_t toIndex(CountClass c) {
  return static_cast<size_t>(c);
}

inline constexpr size_t CountClassCOUNT = toIndex(CountClass::COUNT);

struct CountPenaltyInput {
  int count = 0;
  int span = 0;
};

struct CountPenaltyParams {
  double base = 0.0;
  double countSlope = 0.0;
  double spanSlope = 0.0;
};

// Concave count cost, in units of a class's countSlope: each extra unit costs the
// full slope for the first COUNT_FULL_SLOPE_UNITS, half for the next
// COUNT_HALF_SLOPE_UNITS, a fifth beyond. Piecewise-linear so a counted command
// decomposes into a start cost plus a per-unit slope, which is what lets VimDiff
// price counts incrementally.
inline constexpr int COUNT_FULL_SLOPE_UNITS = 4;
inline constexpr int COUNT_HALF_SLOPE_UNITS = 10;
inline constexpr double COUNT_HALF_SLOPE = 0.5;
inline constexpr double COUNT_TAIL_SLOPE = 0.2;

constexpr double countSlopeUnits(int extra) {
  const int full = std::min(extra, COUNT_FULL_SLOPE_UNITS);
  const int half = std::clamp(extra - COUNT_FULL_SLOPE_UNITS, 0, COUNT_HALF_SLOPE_UNITS);
  const int tail = std::max(extra - COUNT_FULL_SLOPE_UNITS - COUNT_HALF_SLOPE_UNITS, 0);
  return full + COUNT_HALF_SLOPE * half + COUNT_TAIL_SLOPE * tail;
}

template<CountClass C>
inline constexpr CountPenaltyParams CountPenaltySpec{0.0, 0.0, 0.0};

template<>
inline constexpr CountPenaltyParams CountPenaltySpec<CountClass::MovementChar>{0.0, 0.5, 0.0};
template<>
inline constexpr CountPenaltyParams CountPenaltySpec<CountClass::MovementWord>{1.0, 0.5, 0.1};
template<>
inline constexpr CountPenaltyParams CountPenaltySpec<CountClass::MovementBigWord>{1.0, 0.5, 0.1};
template<>
inline constexpr CountPenaltyParams CountPenaltySpec<CountClass::MovementLine>{0.0, 0.5, 0.0};
template<>
inline constexpr CountPenaltyParams CountPenaltySpec<CountClass::MovementParagraph>{1.0, 0.5, 0.1};
template<>
inline constexpr CountPenaltyParams CountPenaltySpec<CountClass::MovementSentence>{1.0, 0.5, 0.1};
template<>
inline constexpr CountPenaltyParams CountPenaltySpec<CountClass::MovementJump>{1.0, 3.0, 0.1};
template<>
inline constexpr CountPenaltyParams CountPenaltySpec<CountClass::EditChar>{0.0, 0.5, 0.0};
template<>
inline constexpr CountPenaltyParams CountPenaltySpec<CountClass::EditWord>{1.0, 0.5, 0.1};
template<>
inline constexpr CountPenaltyParams CountPenaltySpec<CountClass::EditBigWord>{1.0, 0.5, 0.1};
template<>
inline constexpr CountPenaltyParams CountPenaltySpec<CountClass::EditLine>{0.0, 0.5, 0.0};
template<>
inline constexpr CountPenaltyParams CountPenaltySpec<CountClass::EditParagraph>{1.0, 0.5, 0.1};
template<>
inline constexpr CountPenaltyParams CountPenaltySpec<CountClass::EditSentence>{1.0, 0.5, 0.1};
template<>
inline constexpr CountPenaltyParams CountPenaltySpec<CountClass::Join>{0.0, 0.5, 0.0};

// BEGIN override logic
struct PartialCountPenaltyParams {
  std::optional<double> base;
  std::optional<double> countSlope;
  std::optional<double> spanSlope;
};

using CountPenaltyOverrideTable =
    std::array<std::optional<PartialCountPenaltyParams>, CountClassCOUNT>;

// END override logic

// Encapsulate implementation details to not be callable from other files
struct CountPenalty final {
  template<CountClass C>
  static inline double value(const CountPenaltyInput& in) {
    return eval(CountPenaltySpec<C>, in);
  }

  template<CountClass C>
  static inline double value(const CountPenaltyInput& in,
                             const CountPenaltyOverrideTable& overrides) {
    CountPenaltyParams p = CountPenaltySpec<C>;
    const auto& maybeOverride = overrides[toIndex(C)];
    if (maybeOverride) {
      p = applyOverrides(p, *maybeOverride);
    }
    return eval(p, in);
  }

private:
  static inline CountPenaltyParams applyOverrides(CountPenaltyParams p,
                                                  const PartialCountPenaltyParams& o) {
    if (o.base) p.base = *o.base;
    if (o.countSlope) p.countSlope = *o.countSlope;
    if (o.spanSlope) p.spanSlope = *o.spanSlope;
    return p;
  }

  static inline double eval(const CountPenaltyParams& p,
                            const CountPenaltyInput& in) {
    if (in.count <= 1) return 0.0;

    int span = std::max(0, in.span);
    return p.base +
           p.countSlope * countSlopeUnits(in.count - 1) +
           p.spanSlope * static_cast<double>(span);
  }
};

template<CountClass C>
inline double countPenalty(const CountPenaltyInput& in) {
  return CountPenalty::template value<C>(in);
}

template<CountClass C>
inline double countPenalty(const CountPenaltyInput& in,
                           const CountPenaltyOverrideTable& overrides) {
  return CountPenalty::template value<C>(in, overrides);
}
