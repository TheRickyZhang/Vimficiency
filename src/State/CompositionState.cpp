#include "CompositionState.h"

#include "Keyboard/MotionToKeys.h"

using namespace std;

void CompositionState::appendSequence(const string& s, const PhysicalKeys& keys,
                                      const Config& config) {
  sequence.append(s);
  effort = runningEffort.append(keys, config);
}

// =============================================================================
// Public factory methods - return new state with transition applied
// =============================================================================

CompositionState CompositionState::afterEditTransition(
    const Sequence& editSequence,
    const Position& newPos, Mode newMode,
    const Config& config) const {
  CompositionState newState = *this;
  newState.applyEditTransitionImpl(editSequence, newPos, newMode, config);
  return newState;
}

CompositionState CompositionState::afterMotionResult(
    const Sequence& moveSequence,
    const Position& newPos,
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
    const Position& newPos, Mode newMode,
    const Config& config) {
  pos = newPos;
  editsCompleted++;
  PhysicalKeys keys = globalTokenizer().tokenize(editSequence.keys);
  appendSequence(editSequence.keys, keys, config);
  mode = newMode;
}

void CompositionState::applyMotionResultImpl(
    const Sequence& moveSequence,
    const Position& newPos, const Config& config) {
  pos = newPos;
  PhysicalKeys keys = globalTokenizer().tokenize(moveSequence.keys);
  appendSequence(moveSequence.keys, keys, config);
}
