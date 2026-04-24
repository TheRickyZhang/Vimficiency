#include "NavState.h"
#include "Interpreter/MotionInterpreter.h"
#include "Keyboard/ToKeys/CountToKeys.h"
#include "Types/CountPrefixLimits.h"

using namespace std;

// Changes pos, mode, motionSequence
void NavState::applySingleMotion(string_view motion, const NavContext& navContext,
                               const Lines& lines) {
  applyParsedMotion(pos, mode, ParsedMotion(motion), lines, navContext);
  motionSequence.append(motion);
}

void NavState::applySingleMotionWithEffort(string_view motion, const NavContext& navContext,
                                              const Lines& lines, const PhysicalKeys& keys,
                                              const Config& config) {
  applyParsedMotion(pos, mode, ParsedMotion(motion), lines, navContext);
  motionSequence.append(motion);
  effort = runningEffort.append(keys, config);
}

// =============================================================================
// Private implementation - mutating methods
// =============================================================================

void NavState::applyMotionImpl(const KeyedSequence& ks, CursorPos endpoint,
                                  const Config& config) {
  pos = endpoint;
  motionSequence.append(ks.seq.view());
  effort = runningEffort.append(ks.keys, config);
}

void NavState::applyMotionImpl(const KeyedSequence& ks, const RunningEffort& precomputed,
                                  CursorPos endpoint, const Config& config) {
  pos = endpoint;
  motionSequence.append(ks.seq.view());
  effort = runningEffort.appendFrom(precomputed, config);
}

void NavState::applyCountedMotionImpl(const KeyedSequence& baseMotion, int cnt,
                                         CursorPos endpoint, const Config& config,
                                         double extraPenalty) {
  assert(abs(cnt) <= CountPrefixLimits::MAX_PREFIX_COUNT);
  pos = endpoint;
  if (cnt > 0) {
    motionSequence.append(to_string(cnt));
  }
  motionSequence.append(baseMotion.seq.view());
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
  motionSequence.append(fMotion.seq.view());
  effort = runningEffort.append(fMotion.keys, config);
}

void NavState::updateEffort(const PhysicalKeys& keys, const Config& config) {
  effort = runningEffort.append(keys, config);
}
