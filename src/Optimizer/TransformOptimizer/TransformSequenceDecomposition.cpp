#include "TransformSequenceDecomposition.h"

#include "Interpreter/SequenceParser.h"

using namespace std;

TransformSequenceDecomposition decomposeEditSequence(string_view fullSequence) {
  TransformSequenceDecomposition decomposition{
      .molecule = string(fullSequence),
      .typedText = {},
  };

  auto tokens = parseSequence(fullSequence);
  if (!tokens || tokens->empty()) return decomposition;

  const SequenceToken& first = tokens->front();
  if (first.type != TokenType::TypedText && first.type != TokenType::Escape) {
    decomposition.molecule = first.text;
  }

  for (const SequenceToken& token : *tokens) {
    if (token.type == TokenType::Escape) break;
    if (token.type == TokenType::TypedText) decomposition.typedText += token.text;
  }

  return decomposition;
}
