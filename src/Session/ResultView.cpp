#include "Session/ResultView.h"

#include "Utils/PrettyText.h"

#include <iomanip>
#include <iterator>
#include <sstream>

using namespace std;

namespace VF::SessionView {
namespace {

string fixedCost(double value, int precision) {
  ostringstream out;
  out << fixed << setprecision(precision) << value;
  return out.str();
}

string costSuffix(double value, string_view prefix = "") {
  return " (" + string(prefix) + fixedCost(value, 2) + ")";
}

void appendAll(vector<string>& dest, vector<string> src) {
  dest.insert(
      dest.end(),
      make_move_iterator(src.begin()),
      make_move_iterator(src.end()));
}

void appendRecommendation(
    vector<string>& lines,
    size_t index,
    const Recommendation& recommendation,
    SequenceDisplay::Options displayOptions,
    string_view costPrefix = "") {
  appendAll(lines, SequenceDisplay::prefixedLines(
      "  " + to_string(index + 1) + ". ",
      recommendation.seq,
      displayOptions,
      costSuffix(recommendation.cost, costPrefix)));
}

}  // namespace

string formatPosition(const Result& result) {
  ostringstream out;
  out << "(" << result.startRow << "," << result.startCol << ") "
      << PrettyText::ARROW << " ("
      << result.endRow << "," << result.endCol << ")";
  return out.str();
}

string formatReasonSuffix(const Result& result) {
  if (result.finishReason.empty()) return "";
  return " [" + result.finishReason + "]";
}

vector<string> formatBody(
    const Result& result,
    SequenceDisplay::Options displayOptions) {
  vector<string> lines;
  if (!result.userSeq.empty()) {
    const string cost =
        result.userCost ? costSuffix(*result.userCost) : "";
    appendAll(lines, SequenceDisplay::prefixedLines(
        "  user: ",
        result.userSeq,
        displayOptions,
        cost));
  }

  for (size_t i = 0; i < result.optimalResults.size(); i++) {
    appendRecommendation(lines, i, result.optimalResults[i], displayOptions);
  }
  return lines;
}

string formatMessage(
    string_view title,
    const Result& result,
    SequenceDisplay::Options displayOptions) {
  const vector<string> body = formatBody(result, displayOptions);
  ostringstream out;
  out << title << " " << formatPosition(result) << formatReasonSuffix(result)
      << "\n";
  for (size_t i = 0; i < body.size(); i++) {
    if (i > 0) out << "\n";
    out << body[i];
  }
  return out.str();
}

vector<string> formatSavedLines(
    string_view name,
    const Result& result,
    SequenceDisplay::Options displayOptions) {
  const string userCost =
      result.userCost ? costSuffix(*result.userCost, "cost: ") : "";

  vector<string> outputLines{
      "=== " + string(name) + " ===",
      "",
      "Position: (" + to_string(result.startRow) + ", " +
          to_string(result.startCol) + ") -> (" +
          to_string(result.endRow) + ", " +
          to_string(result.endCol) + ")",
      "",
  };

  if (!result.userSeq.empty()) {
    appendAll(outputLines, SequenceDisplay::prefixedLines(
        "User sequence: ",
        result.userSeq,
        displayOptions,
        userCost));
  } else {
    outputLines.push_back("User sequence: (none)" + userCost);
  }

  outputLines.push_back("");
  outputLines.push_back("Recommendations:");
  for (size_t i = 0; i < result.optimalResults.size(); i++) {
    appendRecommendation(
        outputLines,
        i,
        result.optimalResults[i],
        displayOptions,
        "cost: ");
  }

  if (result.optimalResults.empty()) {
    outputLines.push_back("  (no results)");
  }

  outputLines.push_back("");
  outputLines.push_back("Buffer context:");
  for (int row = 0; row < ssize(result.lines); row++) {
    string prefix = "  ";
    if (row == result.startRow) {
      prefix = "> ";
    } else if (row == result.endRow) {
      prefix = "< ";
    }
    outputLines.push_back(prefix + result.lines[row]);
  }

  return outputLines;
}

}  // namespace VF::SessionView
