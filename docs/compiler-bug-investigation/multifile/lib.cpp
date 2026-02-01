// Library code - simulates vimficiency_core
#include "params.h"
#include <iostream>

// Function that uses params - compiled into library
void processWithParams(const Params& p) {
  std::cout << "  [lib] useFeature=" << p.useFeature
            << " threshold=" << p.threshold << std::endl;
}
