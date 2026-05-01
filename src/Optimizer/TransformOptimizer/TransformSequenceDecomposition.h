#pragma once

#include <string>
#include <string_view>

#include "Types/Token.h"

struct TransformSequenceDecomposition {
  Token token;
  std::string typedText;
};

TransformSequenceDecomposition decomposeEditSequence(std::string_view fullSequence);
