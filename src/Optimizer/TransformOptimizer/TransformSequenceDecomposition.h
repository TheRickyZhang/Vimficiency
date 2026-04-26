#pragma once

#include <string>
#include <string_view>

struct TransformSequenceDecomposition {
  std::string molecule;
  std::string typedText;
};

TransformSequenceDecomposition decomposeEditSequence(std::string_view fullSequence);
