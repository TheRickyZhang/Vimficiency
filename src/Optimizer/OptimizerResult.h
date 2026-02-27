#pragma once

#include <concepts>
#include <ostream>
#include <vector>
#include "Result.h"

template<typename ElementType = Result>
struct BaseOptimizerResult {
  const std::vector<ElementType>& getResults() const { return results_; }
  size_t resultCount() const { return results_.size(); }

protected:
  BaseOptimizerResult() = default;
  BaseOptimizerResult(std::vector<ElementType> results)
    : results_(std::move(results)) {}

  // Only available when ElementType is streamable (e.g. Result, RangeResult).
  // Subtypes with non-streamable elements (e.g. EditResult) provide their own operator<<.
  friend std::ostream& operator<<(std::ostream& os, const BaseOptimizerResult& res)
    requires requires(std::ostream& o, const ElementType& e) { o << e; }
  {
    for (size_t i = 0; i < res.results_.size(); i++) {
      os << "  [" << i << "] " << res.results_[i] << "\n";
    }
    return os;
  }

  std::vector<ElementType> results_;
};
