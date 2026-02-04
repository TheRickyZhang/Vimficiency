#include "Sequence.h"

#include "Editor/SequenceParser.h"
#include "Utils/StringUtils.h"

std::ostream& operator<<(std::ostream& os, const Sequence& seq) {
  if (seq.keys.empty()) return os;

  // Use SequenceParser to tokenize - it already handles all the mode tracking
  std::vector<SequenceToken> tokens = parseSequence(seq.keys);

  for (size_t i = 0; i < tokens.size(); i++) {
    os << makePrintable(tokens[i].text);

    // Add space after Escape (mode transition back to normal) unless last token
    if (tokens[i].type == TokenType::Escape && i + 1 < tokens.size()) {
      os << " ";
    }
  }

  return os;
}
