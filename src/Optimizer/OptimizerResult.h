#pragma once

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

  friend std::ostream& operator<<(std::ostream& os, const BaseOptimizerResult& res) {
    for (size_t i = 0; i < res.results_.size(); i++) {
      os << "  [" << i << "] " << res.results_[i] << "\n";
    }
    return os;
  }

  std::vector<ElementType> results_;
};
