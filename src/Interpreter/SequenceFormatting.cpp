#include "VimTypes/Sequence.h"

#include "Interpreter/SequenceParser.h"
#include "Utils/StringUtils.h"

// Note this is defined elsewhere from Sequence.h because it relies on higher-order parsing logic
std::ostream& operator<<(std::ostream& os, const Sequence& seq) {
  if (seq.empty()) return os;

  std::vector<SequenceToken> tokens = parseSequence(seq.view());

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
