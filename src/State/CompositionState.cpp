#include "CompositionState.h"

#include "Keyboard/MotionToKeys.h"

using namespace std;

void CompositionState::appendSequence(const string& s, const PhysicalKeys& keys,
                                      const Config& config) {
  if (sequences.empty() || sequences.back().mode != mode) {
    sequences.emplace_back(mode);
  }
  sequences.back().append(s);
  effort = runningEffort.append(keys, config);
}

// =============================================================================
// Public factory methods - return new state with transition applied
// =============================================================================

CompositionState CompositionState::afterEditTransition(
    const vector<Sequence>& editSequences,
    const Position& newPos, Mode newMode,
    const Config& config) const {
  CompositionState newState = *this;
  newState.applyEditTransitionImpl(editSequences, newPos, newMode, config);
  return newState;
}

CompositionState CompositionState::afterMotionResult(
    const vector<Sequence>& moveSequences,
    const Position& newPos,
    const Config& config) const {
  CompositionState newState = *this;
  newState.applyMotionResultImpl(moveSequences, newPos, config);
  return newState;
}

// =============================================================================
// Private implementation - mutating methods
// =============================================================================

void CompositionState::applyEditTransitionImpl(
    const vector<Sequence>& editSequences,
    const Position& newPos, Mode newMode,
    const Config& config) {
  pos = newPos;
  editsCompleted++;
  // Merge edit sequences into our sequences
  for (const auto& seq : editSequences) {
    // Set mode to match each segment's mode before appending
    mode = seq.mode;
    PhysicalKeys keys = globalTokenizer().tokenize(seq.keys);
    appendSequence(seq.keys, keys, config);
  }
  mode = newMode;
}

void CompositionState::applyMotionResultImpl(
    const vector<Sequence>& moveSequences,
    const Position& newPos, const Config& config) {
  pos = newPos;
  for (const auto& seq : moveSequences) {
    PhysicalKeys keys = globalTokenizer().tokenize(seq.keys);
    appendSequence(seq.keys, keys, config);
  }
}
