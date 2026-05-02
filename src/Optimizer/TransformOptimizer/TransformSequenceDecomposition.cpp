#include "TransformSequenceDecomposition.h"

#include "Interpreter/SequenceParser.h"

using namespace std;

Token extractStructuralToken(string_view fullSequence) {
  Token fallback{fullSequence};

  auto tokens = parseSequence(fullSequence);
  if (!tokens || tokens->empty()) return fallback;

  const TaggedToken& first = tokens->front();
  if (first.kind == TokenKind::TypedText || first.kind == TokenKind::Escape)
    return fallback;

  return first.token;
}
