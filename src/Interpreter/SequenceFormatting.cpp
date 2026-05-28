#include "Interpreter/SequenceFormatting.h"

#include "Types/Sequence.h"

#include "Interpreter/SequenceParser.h"
#include "Utils/PrettyText.h"

// Note this is defined elsewhere from Sequence.h because it relies on higher-order parsing logic
std::ostream& operator<<(std::ostream& os, const Sequence& seq) {
  if (seq.empty()) return os;

  auto parsed = parseSequence(seq.view());
  if (!parsed) {
    os << VF::prettify(seq.str());
    return os;
  }
  std::vector<TaggedToken> tokens = *parsed;

  for (size_t i = 0; i < tokens.size(); i++) {
    os << VF::prettify(tokens[i].token);

    if (i + 1 < tokens.size()) {
      auto cur = tokens[i].kind;
      auto next = tokens[i + 1].kind;
      if (cur == TokenKind::Escape ||
          (cur == TokenKind::Change && next == TokenKind::TypedText)) {
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
