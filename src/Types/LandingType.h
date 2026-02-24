#pragma once

#include <cstddef>

// Motion landing categories used by count-search indexing.
// These are Vim semantic classes (word/WORD/paragraph/sentence boundaries).
enum class LandingType : size_t {
  WordBegin = 0,  // w, b
  WordEnd = 1,    // e, ge
  WORDBegin = 2,  // W, B
  WORDEnd = 3,    // E, gE
  Paragraph = 4,  // {, }
  Sentence = 5,   // (, )
  COUNT = 6
};
