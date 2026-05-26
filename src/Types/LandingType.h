#pragma once

#include <cstddef>

// Motion landing categories used by count-search indexing.
enum class LandingType : size_t {
  WordBegin = 0,  // w, b
  WordEnd = 1,    // e, ge
  BigWordBegin = 2,  // W, B
  BigWordEnd = 3,    // E, gE
  Paragraph = 4,  // {, }
  COUNT = 5
};
