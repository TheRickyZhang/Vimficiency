#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "Interpreter/SequenceParser.h"

using namespace std;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const string_view sequence(reinterpret_cast<const char*>(data), size);

  const auto parsed = parseSequence(sequence);
  if (!parsed) {
    if (parsed.error().offset > sequence.size()) __builtin_trap();
    (void)formatSequenceParseError(parsed.error());
    return 0;
  }

  const auto parsedStrings = parseSequenceStrings(sequence);
  if (!parsedStrings) {
    (void)formatSequenceParseError(parsedStrings.error());
    __builtin_trap();
  }
  if (parsedStrings->size() != parsed->size()) __builtin_trap();

  for (size_t i = 0; i < parsed->size(); i++) {
    if ((*parsedStrings)[i] != string((*parsed)[i].token)) __builtin_trap();
  }

  return 0;
}
