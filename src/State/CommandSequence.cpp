#include "CommandSequence.h"
#include "Editor/SequenceParser.h"

std::string CommandSequence::formatted() const {
  return formatSequenceForDisplay(*this);
}

std::string formatSequenceForDisplay(std::string_view seq) {
  if (seq.empty()) {
    return "";
  }

  std::vector<std::string> tokens = parseSequenceStrings(seq);

  if (tokens.empty()) {
    return std::string(seq);  // Fallback to raw string if parsing fails
  }

  std::string result;
  for (size_t i = 0; i < tokens.size(); i++) {
    if (i > 0) {
      result += ' ';
    }
    result += tokens[i];
  }
  return result;
}
