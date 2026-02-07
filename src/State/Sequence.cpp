#include "Sequence.h"

#include "Editor/SequenceParser.h"
#include "Utils/StringUtils.h"

std::ostream& operator<<(std::ostream& os, const Sequence& seq) {
  if (seq.keys.empty()) return os;

  // Use SequenceParser to tokenize - it already handles all the mode tracking
  std::vector<SequenceToken> tokens = parseSequence(seq.keys);

  for (size_t i = 0; i < tokens.size(); i++) {
    os << makePrintable(tokens[i].text);

    // Add space at insert-mode boundaries (unless last token)
    if (i + 1 < tokens.size()) {
      auto cur = tokens[i].type;
      auto next = tokens[i + 1].type;
      // Space after Escape (exiting insert mode)
      // Space between Change command and TypedText (e.g. "ce" "agree")
      if (cur == TokenType::Escape ||
          (cur == TokenType::Change && next == TokenType::TypedText)) {
        os << " ";
      }
    }
  }

  return os;
}
