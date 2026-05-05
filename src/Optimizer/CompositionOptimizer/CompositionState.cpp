#include "CompositionState.h"

#include "Keyboard/ToKeys/MovementToKeys.h"

using namespace std;

void CompositionState::appendSequence(string_view s, const PhysicalKeys& keys,
                                      const Config& config) {
  sequence.append(s);
  effort = runningEffort.append(keys, config);
}

void CompositionState::applyEditTransitionImpl(
    const Sequence& editSequence,
    const CursorPos& newPos, Mode newMode,
    const Config& config) {
  pos = newPos;
  editsCompleted++;
  PhysicalKeys keys = globalSequenceToKeys().tokenize(editSequence.view());
  appendSequence(editSequence.view(), keys, config);
  mode = newMode;
}

void CompositionState::applyNavResultImpl(
    const Sequence& moveSequence,
    const CursorPos& newPos, const Config& config) {
  pos = newPos;
  PhysicalKeys keys = globalSequenceToKeys().tokenize(moveSequence.view());
  appendSequence(moveSequence.view(), keys, config);
}
