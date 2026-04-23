#pragma once

#include <string>
#include <string_view>

struct EditSequenceDecomposition {
  std::string molecule;
  std::string typedText;
};

EditSequenceDecomposition decomposeEditSequence(std::string_view fullSequence);
