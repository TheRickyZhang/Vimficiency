#include "CompositionState.h"

#include "Keyboard/ToKeys/MotionToKeys.h"

using namespace std;

void CompositionState::appendSequence(string_view s, const PhysicalKeys& keys,
                                      const Config& config) {
  sequence.append(s);
  effort = runningEffort.append(keys, config);
}

// =============================================================================
// Public factory methods - return new state with transition applied
// =============================================================================

CompositionState CompositionState::afterEditTransition(
    const Sequence& editSequence,
    const CursorPos& newPos, Mode newMode,
    const Config& config) const {
  CompositionState newState = *this;
  newState.applyEditTransitionImpl(editSequence, newPos, newMode, config);
  return newState;
}

CompositionState CompositionState::afterMotionResult(
    const Sequence& moveSequence,
    const CursorPos& newPos,
    const Config& config) const {
  CompositionState newState = *this;
  newState.applyMotionResultImpl(moveSequence, newPos, config);
  return newState;
}

// =============================================================================
// Private implementation - mutating methods
// =============================================================================

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

void CompositionState::applyMotionResultImpl(
    const Sequence& moveSequence,
    const CursorPos& newPos, const Config& config) {
  pos = newPos;
  PhysicalKeys keys = globalSequenceToKeys().tokenize(moveSequence.view());
  appendSequence(moveSequence.view(), keys, config);
}
