#include "TransformSequenceDecomposition.h"

#include "Interpreter/SequenceParser.h"

using namespace std;

TransformSequenceDecomposition decomposeEditSequence(string_view fullSequence) {
  TransformSequenceDecomposition decomposition{
      .token = Token{fullSequence},
      .typedText = {},
  };

  auto tokens = parseSequence(fullSequence);
  if (!tokens || tokens->empty()) return decomposition;

  const TaggedToken& first = tokens->front();
  if (first.kind != TokenKind::TypedText && first.kind != TokenKind::Escape) {
    decomposition.token = first.token;
  }

  for (const TaggedToken& tagged : *tokens) {
    if (tagged.kind == TokenKind::Escape) break;
    if (tagged.kind == TokenKind::TypedText) {
      decomposition.typedText += tagged.token;
    }
  }

  return decomposition;
}
