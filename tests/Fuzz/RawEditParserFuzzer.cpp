#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Interpreter/EditInterpreter.h"

using namespace std;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const string_view sequence(reinterpret_cast<const char*>(data), size);

  const auto parsed = Edit::parseEdits(sequence);
  if (!parsed) {
    if (parsed.error().offset > sequence.size()) __builtin_trap();
    (void)Edit::formatEditParseError(parsed.error());
    return 0;
  }

  for (const ParsedEdit& edit : *parsed) {
    (void)edit.effectiveCount();

    if (edit.edit.empty()) continue;
    const auto* editBegin = edit.edit.data();
    const auto* editEnd = editBegin + edit.edit.size();
    const auto* sequenceBegin = sequence.data();
    const auto* sequenceEnd = sequenceBegin + sequence.size();
    if (editBegin < sequenceBegin || editEnd > sequenceEnd) __builtin_trap();
  }

  return 0;
}
