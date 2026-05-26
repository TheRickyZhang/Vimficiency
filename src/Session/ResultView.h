#pragma once

#include "Interpreter/SequenceDisplay.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VF::SessionView {

struct Recommendation {
  std::string seq;
  double cost = 0.0;
};

struct Result {
  std::vector<std::string> lines;
  int startRow = 0;
  int startCol = 0;
  int endRow = 0;
  int endCol = 0;
  std::string userSeq;
  std::optional<double> userCost;
  std::string finishReason;
  std::vector<Recommendation> optimalResults;
};

std::string formatPosition(const Result& result);
std::string formatReasonSuffix(const Result& result);
std::vector<std::string> formatBody(
    const Result& result,
    SequenceDisplay::Options displayOptions = {});
std::string formatMessage(
    std::string_view title,
    const Result& result,
    SequenceDisplay::Options displayOptions = {});
std::vector<std::string> formatSavedLines(
    std::string_view name,
    const Result& result,
    SequenceDisplay::Options displayOptions = {});

}  // namespace VF::SessionView
