#include "Interpreter/SequenceFormatting.h"

#include "Types/Sequence.h"

#include "Interpreter/SequenceParser.h"
#include "Utils/StringUtils.h"

// Note this is defined elsewhere from Sequence.h because it relies on higher-order parsing logic
std::ostream& operator<<(std::ostream& os, const Sequence& seq) {
  if (seq.empty()) return os;

  auto parsed = parseSequence(seq.view());
  if (!parsed) {
    os << makePrintable(seq.str());
    return os;
  }
  std::vector<SequenceToken> tokens = *parsed;

  for (size_t i = 0; i < tokens.size(); i++) {
    os << makePrintable(tokens[i].text);

    if (i + 1 < tokens.size()) {
      auto cur = tokens[i].type;
      auto next = tokens[i + 1].type;
      if (cur == TokenType::Escape ||
          (cur == TokenType::Change && next == TokenType::TypedText)) {
        os << " ";
      }
    }
  }

  return os;
}

std::string formatSequenceForDisplay(std::string_view seq) {
  if (seq.empty()) {
    return "";
  }

  // Best-effort display: Lua's format_sequence passes captured user_seq
  // here, so unparseable input is expected. Fall back to the raw string
  // rather than erroring — the user still sees their keystrokes.
  auto parsed = parseSequenceStrings(seq);
  if (!parsed || parsed->empty()) {
    return std::string(seq);
  }

  std::string result;
  for (size_t i = 0; i < parsed->size(); i++) {
    if (i > 0) {
      result += ' ';
    }
    result += (*parsed)[i];
  }
  return result;
}
