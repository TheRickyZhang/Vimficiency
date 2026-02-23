#include "MotionState.h"
#include "Interpreter/MotionInterpreter.h"
#include "Keyboard/ToKeys/CountToKeys.h"

using namespace std;

// Changes pos, mode, motionSequence
void MotionState::applySingleMotion(string_view motion, const NavContext& navContext,
                               const Lines& lines) {
  applyParsedMotion(pos, mode, ParsedMotion(motion), lines, navContext);
  motionSequence.append(motion);
}

void MotionState::applySingleMotionWithEffort(string_view motion, const NavContext& navContext,
                                              const Lines& lines, const PhysicalKeys& keys,
                                              const Config& config) {
  applyParsedMotion(pos, mode, ParsedMotion(motion), lines, navContext);
  motionSequence.append(motion);
  effort = runningEffort.append(keys, config);
}

// =============================================================================
// Public factory methods - return new state with motion applied
// =============================================================================

MotionState MotionState::afterMotion(const KeyedSequence& ks, Position endpoint,
                                     const Config& config) const {
  MotionState newState = *this;
  newState.applyMotionImpl(ks, endpoint, config);
  return newState;
}

MotionState MotionState::afterMotion(const KeyedSequence& ks, const RunningEffort& precomputed,
                                     Position endpoint, const Config& config) const {
  MotionState newState = *this;
  newState.applyMotionImpl(ks, precomputed, endpoint, config);
  return newState;
}

MotionState MotionState::afterCountedMotion(const KeyedSequence& baseMotion, int cnt,
                                            Position endpoint, const Config& config,
                                            double extraPenalty) const {
  MotionState newState = *this;
  newState.applyCountedMotionImpl(baseMotion, cnt, endpoint, config, extraPenalty);
  return newState;
}

MotionState MotionState::afterFMotion(const KeyedSequence& fMotion, int newCol,
                                      const Config& config) const {
  MotionState newState = *this;
  newState.applyFMotionImpl(fMotion, newCol, config);
  return newState;
}

// =============================================================================
// Private implementation - mutating methods
// =============================================================================

void MotionState::applyMotionImpl(const KeyedSequence& ks, Position endpoint,
                                  const Config& config) {
  pos = endpoint;
  motionSequence.append(ks.seq.view());
  effort = runningEffort.append(ks.keys, config);
}

void MotionState::applyMotionImpl(const KeyedSequence& ks, const RunningEffort& precomputed,
                                  Position endpoint, const Config& config) {
  pos = endpoint;
  motionSequence.append(ks.seq.view());
  effort = runningEffort.appendFrom(precomputed, config);
}

void MotionState::applyCountedMotionImpl(const KeyedSequence& baseMotion, int cnt,
                                         Position endpoint, const Config& config,
                                         double extraPenalty) {
  assert(abs(cnt) <= MAX_PREFIX_COUNT);
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

void MotionState::applyFMotionImpl(const KeyedSequence& fMotion, int newCol,
                                   const Config& config) {
  pos.setCol(newCol);
  motionSequence.append(fMotion.seq.view());
  effort = runningEffort.append(fMotion.keys, config);
}

void MotionState::updateEffort(const PhysicalKeys& keys, const Config& config) {
  effort = runningEffort.append(keys, config);
}
