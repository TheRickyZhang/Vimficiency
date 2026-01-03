#include "EditState.h"

// TODO:
void EditState::applySingleMotion(std::string motion, const KeySequence& keySequence) {
  // applyEditMotion(motion, lines, mode);
  motionSequence += motion;
}


void EditState::updateEffort(const KeySequence& keySequence, const Config& config) {
  effort = runningEffort.append(keySequence, config);
}

void EditState::updateCost(double newCost) {
  cost = newCost;
}
