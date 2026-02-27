#pragma once

#include <vector>
#include "Result.h"

struct BaseOptimizerResult {
  const std::vector<Result>& getResults() const { return results_; }
  size_t resultCount() const { return results_.size(); }

protected:
  BaseOptimizerResult() = default;
  BaseOptimizerResult(std::vector<Result> results)
    : results_(std::move(results)) {}

  std::vector<Result> results_;
};
