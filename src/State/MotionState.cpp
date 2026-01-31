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

void MotionState::applyMotion(const char* cmd, Position endpoint,
                              const PhysicalKeys& keys, const Config& config) {
  pos = endpoint;
  motionSequence += cmd;
  effort = runningEffort.append(keys, config);
}

void MotionState::applyCountedMotion(const std::string& motion, int cnt, Position endpoint,
                                     const PhysicalKeys& baseKeys, const Config& config) {
  pos = endpoint;
  if (cnt > 0) {
    motionSequence += to_string(cnt);
  }
  motionSequence += motion;
  effort = runningEffort.append(makeCountedKeys(abs(cnt), baseKeys), config);
}

void MotionState::applyFMotion(const std::string& motion, int newCol,
                               const PhysicalKeys& keys, const Config& config) {
  pos.setCol(newCol);
  motionSequence += motion;
  effort = runningEffort.append(keys, config);
}

void MotionState::updateEffort(const PhysicalKeys& keys, const Config& config) {
  effort = runningEffort.append(keys, config);
}

void MotionState::updateCost(double newCost) {
  cost = newCost;
}
