#include "MotionState.h"
#include "Editor/Motion.h"

using namespace std;

// Changes pos, mode, motionSequence
void MotionState::applySingleMotion(string motion, const NavContext& navContext,
                               const Lines& lines) {
  applyParsedMotion(pos, mode, ParsedMotion(motion), lines, navContext);
  motionSequence += motion;
}

void MotionState::applySingleMotionWithEffort(const char* motion, const NavContext& navContext,
                                              const Lines& lines, const PhysicalKeys& keys,
                                              const Config& config) {
  applyParsedMotion(pos, mode, ParsedMotion(motion), lines, navContext);
  motionSequence += motion;
  effort = runningEffort.append(keys, config);
}

// =============================================================================
// Public factory methods - return new state with motion applied
// =============================================================================

MotionState MotionState::afterMotion(const char* cmd, Position endpoint,
                                     const PhysicalKeys& keys, const Config& config) const {
  MotionState newState = *this;
  newState.applyMotionImpl(cmd, endpoint, keys, config);
  return newState;
}

MotionState MotionState::afterCountedMotion(const string& motion, int cnt, Position endpoint,
                                            const PhysicalKeys& baseKeys, const Config& config) const {
  MotionState newState = *this;
  newState.applyCountedMotionImpl(motion, cnt, endpoint, baseKeys, config);
  return newState;
}

MotionState MotionState::afterFMotion(const string& motion, int newCol,
                                      const PhysicalKeys& keys, const Config& config) const {
  MotionState newState = *this;
  newState.applyFMotionImpl(motion, newCol, keys, config);
  return newState;
}

// =============================================================================
// Private implementation - mutating methods
// =============================================================================

void MotionState::applyMotionImpl(const char* cmd, Position endpoint,
                                  const PhysicalKeys& keys, const Config& config) {
  pos = endpoint;
  motionSequence += cmd;
  effort = runningEffort.append(keys, config);
}

void MotionState::applyCountedMotionImpl(const string& motion, int cnt, Position endpoint,
                                         const PhysicalKeys& baseKeys, const Config& config) {
  pos = endpoint;
  if (cnt > 0) {
    motionSequence += to_string(cnt);
  }
  motionSequence += motion;
  effort = runningEffort.append(makeCountedKeys(abs(cnt), baseKeys), config);
}

void MotionState::applyFMotionImpl(const string& motion, int newCol,
                                   const PhysicalKeys& keys, const Config& config) {
  pos.setCol(newCol);
  motionSequence += motion;
  effort = runningEffort.append(keys, config);
}

void MotionState::updateEffort(const PhysicalKeys& keys, const Config& config) {
  effort = runningEffort.append(keys, config);
}
