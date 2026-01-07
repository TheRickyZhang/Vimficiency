#include "EditState.h"
#include "Editor/Edit.h"
#include "Editor/NavContext.h"
// #include "Keyboard/CharToKeys.h"
#include "State/RunningEffort.h"

void EditState::appendSequence(const std::string& s, const PhysicalKeys& keys, const Config& config) {
  // Check if we need to start a new segment (mode changed or first segment)
  if (sequences.empty() || sequences.back().mode != mode) {
    sequences.emplace_back(mode);
  }
  sequences.back().append(s);
  effort = runningEffort.append(keys, config);
}

void EditState::applySingleMotion(const std::string& motion, const PhysicalKeys& keys, const Config& config) {
  // Append sequence with current mode BEFORE applying (mode may change after)
  appendSequence(motion, keys, config);

  // Copy lines for mutation (copy-on-write)
  Lines& mutableLines = copyLinesForMutation();

  // Apply the edit using Edit::applyEdit
  // NavContext not used for edit operations, pass dummy values
  NavContext navContext(24, 12);  // Dummy window height and scroll amount
  Edit::applyEdit(mutableLines, pos, mode, navContext, ParsedEdit(motion));
}

void EditState::addTypedSingleChar(char c, const PhysicalKeys& keys, const Config& config) {
  // Append sequence with current mode (should be Insert)
  appendSequence(std::string(1, c), keys, config);
  typedIndex++;
  didType = true;

  Lines& mutableLines = copyLinesForMutation();
  // Edit::insertText handles both regular chars and newlines
  Edit::insertText(mutableLines, pos, mode, std::string(1, c));
}

void EditState::updateCost(double newCost) {
  cost = newCost;
}

void EditState::updateDidType(bool newDidType) {
  didType = newDidType;
}

void EditState::incrementTypedIndex() {
  typedIndex++;
}
