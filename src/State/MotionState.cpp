#include "MotionState.h"
#include "Editor/Motion.h"

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

MotionState MotionState::afterCountedMotion(const KeyedSequence& baseMotion, int cnt,
                                            Position endpoint, const Config& config) const {
  MotionState newState = *this;
  newState.applyCountedMotionImpl(baseMotion, cnt, endpoint, config);
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
  motionSequence.append(ks.seq.keys);
  if (ks.hasEffort()) {
    runningEffort = RunningEffort::merge(runningEffort, ks.effort);
    effort = runningEffort.getEffort(config);
  } else {
    effort = runningEffort.append(ks.keys, config);
  }
}

void MotionState::applyCountedMotionImpl(const KeyedSequence& baseMotion, int cnt,
                                         Position endpoint, const Config& config) {
  pos = endpoint;
  if (cnt > 0) {
    motionSequence.append(to_string(cnt));
  }
  motionSequence.append(baseMotion.seq.keys);
  effort = runningEffort.append(makeCountedKeys(abs(cnt), baseMotion.keys), config);
}

void MotionState::applyFMotionImpl(const KeyedSequence& fMotion, int newCol,
                                   const Config& config) {
  pos.setCol(newCol);
  motionSequence.append(fMotion.seq.keys);
  effort = runningEffort.append(fMotion.keys, config);
}

void MotionState::updateEffort(const PhysicalKeys& keys, const Config& config) {
  effort = runningEffort.append(keys, config);
}
