#include <bits/stdc++.h>
using namespace std;

#include "MotionToKeys.h"
#include "MotionToKeysBuildingBlocks.h"
#include "Utils/Debug.h"

vector<string> getCountableMotions(const vector<CountableMotionPair> firstVec, const vector<CountableMotionPair> secondVec) {
  vector<string> res;
  res.reserve(2 * (firstVec.size() + secondVec.size()));
  for(auto x : firstVec) {
    res.push_back(x.forward);
    res.push_back(x.backward);
  }
  for(auto x : secondVec) {
    res.push_back(x.forward);
    res.push_back(x.backward);
  }
  return res;
}

// =============================================================================
// Global Tokenizer
// =============================================================================

const SequenceTokenizer& globalTokenizer() {
  static SequenceTokenizer tok(ACTION_MOTIONS_TO_KEYS, ALL_MOTIONS);
  return tok;
}

// =============================================================================
// ACTION_MOTIONS_TO_KEYS - Physical key mappings for tokenizing raw input
// =============================================================================
// See :h key-notation, :h keytrans()

const MotionToKeys ACTION_MOTIONS_TO_KEYS = combineAll({
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
const MotionToKeys EXPLORABLE_MOTIONS = combineAll({
  cref(hjkl),
  cref(line_col),
  cref(words),
  cref(ggG),
  cref(brackets),
    cref(scrolls)
});

// =============================================================================
// EDIT_EXPLORABLE_MOTIONS - Motions directly explorable in edit optimizer search
// =============================================================================
// These motions can be applied without additional context (no target char needed)
const MotionToKeys EDIT_EXPLORABLE_MOTIONS = combineAll({
  cref(arrows)
});

// =============================================================================
// CHARACTER_FIND_MOTIONS - Motions requiring special handling
// =============================================================================
// These need target characters (f/F/t/T) or prior motion context (;/,)
// They are handled specially in the optimizer, not in the main exploration loop

static const MotionToKeys CHARACTER_FIND_MOTIONS = {
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

static MotionToKeys buildAllMotions() {
  MotionToKeys all = EXPLORABLE_MOTIONS;
  all.insert(CHARACTER_FIND_MOTIONS.begin(), CHARACTER_FIND_MOTIONS.end());
  return all;
}

const MotionToKeys ALL_MOTIONS = buildAllMotions();

// =============================================================================
// CHAR_TO_KEYS - Single character to KeySequence mapping
// =============================================================================
// Used for f/F/t/T motion targets
const CharToKeys CHAR_TO_KEYS = combineAllToCharKeySeq({
  cref(letters),
  cref(digits),
  cref(whitespace),
  cref(allSingleCharPunctuationAndSymbols),
});

const vector<CountableMotionPair> COUNT_SEARCHABLE_MOTIONS_LINE = {
  {"w",  "b",  LandingType::WordBegin},
  {"e",  "ge", LandingType::WordEnd},
  {"W",  "B",  LandingType::WORDBegin},
  {"E",  "gE", LandingType::WORDEnd},
};

const vector<CountableMotionPair> COUNT_SEARCHABLE_MOTIONS_GLOBAL = {
  {"}",  "{",  LandingType::Paragraph},
  {")",  "(",  LandingType::Sentence},
  // Note: scrolls (<C-f>, <C-b>, etc.) don't map to LandingType - handle separately
};

const vector<string> COUNT_SEARCHABLE_MOTIONS = getCountableMotions(COUNT_SEARCHABLE_MOTIONS_LINE, COUNT_SEARCHABLE_MOTIONS_GLOBAL);

// =============================================================================
// Utilities
// =============================================================================

MotionToKeys getSlicedMotionToKeys(vector<string> motions) {
  MotionToKeys res;
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
