#include "NavState.h"
#include "Interpreter/MovementInterpreter.h"
#include "Keyboard/ToKeys/CountToKeys.h"
#include "Types/CountPrefixLimits.h"

using namespace std;

// Changes pos, mode, movementSequence
void NavState::applySingleMovement(string_view motion, const NavContext& navContext,
                               const Lines& lines) {
  applyParsedMovement(pos, mode, ParsedMovement(motion), lines, navContext);
  movementSequence.append(motion);
}

void NavState::applySingleMovementWithEffort(string_view motion, const NavContext& navContext,
                                              const Lines& lines, const PhysicalKeys& keys,
                                              const Config& config) {
  applyParsedMovement(pos, mode, ParsedMovement(motion), lines, navContext);
  movementSequence.append(motion);
  effort = runningEffort.append(keys, config);
}

// =============================================================================
// Private implementation - mutating methods
// =============================================================================

void NavState::applyMotionImpl(const KeyedSequence& ks, CursorPos endpoint,
                                  const Config& config) {
  pos = endpoint;
  movementSequence.append(ks.seq.view());
  effort = runningEffort.append(ks.keys, config);
}

void NavState::applyMotionImpl(const KeyedSequence& ks, const RunningEffort& precomputed,
                                  CursorPos endpoint, const Config& config) {
  pos = endpoint;
  movementSequence.append(ks.seq.view());
  effort = runningEffort.appendFrom(precomputed, config);
}

void NavState::applyCountedMotionImpl(const KeyedSequence& baseMotion, int cnt,
                                         CursorPos endpoint, const Config& config,
                                         double extraPenalty) {
  assert(abs(cnt) <= CountPrefixLimits::MAX_PREFIX_COUNT);
  pos = endpoint;
  if (cnt > 0) {
    movementSequence.append(to_string(cnt));
  }
  movementSequence.append(baseMotion.seq.view());
  if (abs(cnt) <= 1) {
    effort = runningEffort.append(baseMotion.keys, config);
  } else {
    PhysicalKeys keys;
    keys.append(CountToKeys::keysForCount(abs(cnt)));
    keys.append(baseMotion.keys);
    effort = runningEffort.append(keys, config);
  }
  if (extraPenalty > 0.0) {
    runningEffort.addPenalty(extraPenalty);
    effort = runningEffort.getEffort(config);
  }
}

void NavState::applyFMotionImpl(const KeyedSequence& fMotion, int newCol,
                                   const Config& config) {
  pos.setCol(newCol);
  movementSequence.append(fMotion.seq.view());
  effort = runningEffort.append(fMotion.keys, config);
}

void NavState::updateEffort(const PhysicalKeys& keys, const Config& config) {
  effort = runningEffort.append(keys, config);
}
