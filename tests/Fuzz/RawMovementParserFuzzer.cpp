#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Interpreter/MovementInterpreter.h"

using namespace std;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const string_view sequence(reinterpret_cast<const char*>(data), size);

  const auto parsed = parseMovements(sequence);
  if (!parsed) {
    if (parsed.error().offset > sequence.size()) __builtin_trap();
    (void)formatMovementParseError(parsed.error());
    return 0;
  }

  for (const ParsedMovement& movement : *parsed) {
    (void)movement.effectiveCount();

    if (movement.motion.empty()) continue;
    const auto* motionBegin = movement.motion.data();
    const auto* motionEnd = motionBegin + movement.motion.size();
    const auto* sequenceBegin = sequence.data();
    const auto* sequenceEnd = sequenceBegin + sequence.size();
    if (motionBegin < sequenceBegin || motionEnd > sequenceEnd) __builtin_trap();
  }

  return 0;
}
