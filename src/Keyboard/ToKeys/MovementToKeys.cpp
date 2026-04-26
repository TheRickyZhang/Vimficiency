#include "MovementToKeys.h"

#include <string>
#include <vector>

using namespace std;

#include "MovementToKeysPrimitives.h"
#include "Utils/Debug.h"

// =============================================================================
// Global Tokenizer
// =============================================================================

const SequenceToKeys& globalSequenceToKeys() {
  static SequenceToKeys tok(ACTION_MOTIONS_TO_KEYS, ALL_MOTIONS);
  return tok;
}

// =============================================================================
// ACTION_MOTIONS_TO_KEYS - Physical key mappings for tokenizing raw input
// =============================================================================
// See :h key-notation, :h keytrans()

const MovementToKeys ACTION_MOTIONS_TO_KEYS = combineAll({
  cref(letters),
  cref(digits), 
  cref(whitespace), 
  cref(topPunctuation),
  cref(mainPunctuation),
  cref(digitSymbols),
  cref(specialWithBracket),
  cref(ctrlCombinations),
});

// =============================================================================
// EXPLORABLE_MOTIONS - Motions directly explorable in optimizer search
// =============================================================================
// These motions can be applied without additional context (no target char needed)
const MovementToKeys EXPLORABLE_MOTIONS = combineAll({
  cref(hjkl),
  cref(line_col),
  cref(words),
  cref(ggG),
  cref(brackets),
    cref(scrolls)
});


// =============================================================================
// CHARACTER_FIND_MOTIONS - Motions requiring special handling
// =============================================================================
// These need target characters (f/F/t/T) or prior motion context (;/,)
// They are handled specially in the optimizer, not in the main exploration loop

static const MovementToKeys CHARACTER_FIND_MOTIONS = {
  {"f",  {Key::Key_F}},                         // find char forward
  {"F",  {Key::Key_Shift, Key::Key_F}},         // find char backward
  {"t",  {Key::Key_T}},                         // till char forward
  {"T",  {Key::Key_Shift, Key::Key_T}},         // till char backward
  {";",  {Key::Key_Semicolon}},                 // repeat f/F/t/T same direction
  {",",  {Key::Key_Comma}},                     // repeat f/F/t/T opposite direction
};

// =============================================================================
// ALL_MOTIONS - Union of all supported vim motions (for parsing/validation)
// =============================================================================

static MovementToKeys buildAllMotions() {
  MovementToKeys all = EXPLORABLE_MOTIONS;
  all.insert(CHARACTER_FIND_MOTIONS.begin(), CHARACTER_FIND_MOTIONS.end());
  return all;
}

const MovementToKeys ALL_MOTIONS = buildAllMotions();

// CHAR_TO_KEYS is now defined in CharToKeys.cpp

// =============================================================================
// Utilities
// =============================================================================

MovementToKeys getSlicedMovementToKeys(vector<string> motions) {
  MovementToKeys res;
  for(const string& m : motions) {
    auto it = ALL_MOTIONS.find(m);
    if(it == ALL_MOTIONS.end()) {
      debug("cannot find", m, "in ALL_MOTIONS");
    } else {
      res[m] = it->second;
    }
  }
  return res;
}
