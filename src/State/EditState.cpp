#include "EditState.h"
#include "Editor/Edit.h"
#include "Editor/NavContext.h"

void EditState::applySingleMotion(std::string motion, const KeySequence& keySequence) {
  motionSequence += motion;

  // Copy lines for mutation (copy-on-write)
  Lines& mutableLines = copyLinesForMutation();

  // Apply the edit using Edit::applyEdit
  // NavContext not used for edit operations, pass dummy values
  NavContext navContext(24, 12);  // Dummy window height and scroll amount
  Edit::applyEdit(mutableLines, pos, mode, navContext, ParsedEdit(motion));
}

void EditState::updateEffort(const KeySequence& keySequence, const Config& config) {
  effort = runningEffort.append(keySequence, config);
}

void EditState::updateCost(double newCost) {
  cost = newCost;
}
