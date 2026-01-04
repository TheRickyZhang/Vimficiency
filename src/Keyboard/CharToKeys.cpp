#include "CharToKeys.h"

namespace CharMappings {

const CharToKeys letters = {
  {'a', {Key::Key_A}}, {'A', {Key::Key_Shift, Key::Key_A}},
  {'b', {Key::Key_B}}, {'B', {Key::Key_Shift, Key::Key_B}},
  {'c', {Key::Key_C}}, {'C', {Key::Key_Shift, Key::Key_C}},
  {'d', {Key::Key_D}}, {'D', {Key::Key_Shift, Key::Key_D}},
  {'e', {Key::Key_E}}, {'E', {Key::Key_Shift, Key::Key_E}},
  {'f', {Key::Key_F}}, {'F', {Key::Key_Shift, Key::Key_F}},
  {'g', {Key::Key_G}}, {'G', {Key::Key_Shift, Key::Key_G}},
  {'h', {Key::Key_H}}, {'H', {Key::Key_Shift, Key::Key_H}},
  {'i', {Key::Key_I}}, {'I', {Key::Key_Shift, Key::Key_I}},
  {'j', {Key::Key_J}}, {'J', {Key::Key_Shift, Key::Key_J}},
  {'k', {Key::Key_K}}, {'K', {Key::Key_Shift, Key::Key_K}},
  {'l', {Key::Key_L}}, {'L', {Key::Key_Shift, Key::Key_L}},
  {'m', {Key::Key_M}}, {'M', {Key::Key_Shift, Key::Key_M}},
  {'n', {Key::Key_N}}, {'N', {Key::Key_Shift, Key::Key_N}},
  {'o', {Key::Key_O}}, {'O', {Key::Key_Shift, Key::Key_O}},
  {'p', {Key::Key_P}}, {'P', {Key::Key_Shift, Key::Key_P}},
  {'q', {Key::Key_Q}}, {'Q', {Key::Key_Shift, Key::Key_Q}},
  {'r', {Key::Key_R}}, {'R', {Key::Key_Shift, Key::Key_R}},
  {'s', {Key::Key_S}}, {'S', {Key::Key_Shift, Key::Key_S}},
  {'t', {Key::Key_T}}, {'T', {Key::Key_Shift, Key::Key_T}},
  {'u', {Key::Key_U}}, {'U', {Key::Key_Shift, Key::Key_U}},
  {'v', {Key::Key_V}}, {'V', {Key::Key_Shift, Key::Key_V}},
  {'w', {Key::Key_W}}, {'W', {Key::Key_Shift, Key::Key_W}},
  {'x', {Key::Key_X}}, {'X', {Key::Key_Shift, Key::Key_X}},
  {'y', {Key::Key_Y}}, {'Y', {Key::Key_Shift, Key::Key_Y}},
  {'z', {Key::Key_Z}}, {'Z', {Key::Key_Shift, Key::Key_Z}},
};

const CharToKeys digits = {
  {'0', {Key::Key_0}},
  {'1', {Key::Key_1}},
  {'2', {Key::Key_2}},
  {'3', {Key::Key_3}},
  {'4', {Key::Key_4}},
  {'5', {Key::Key_5}},
  {'6', {Key::Key_6}},
  {'7', {Key::Key_7}},
  {'8', {Key::Key_8}},
  {'9', {Key::Key_9}},
};

const CharToKeys whitespace = {
  {' ',  {Key::Key_Space}},
  {'\t', {Key::Key_Tab}},
  {'\n', {Key::Key_Enter}},
  {'\r', {Key::Key_Enter}},
};

const CharToKeys topPunctuation = {
  {'`', {Key::Key_Grave}},      {'~', {Key::Key_Shift, Key::Key_Grave}},
  {'-', {Key::Key_Minus}},      {'_', {Key::Key_Shift, Key::Key_Minus}},
  {'=', {Key::Key_Equal}},      {'+', {Key::Key_Shift, Key::Key_Equal}},
  {'[', {Key::Key_LBracket}},   {'{', {Key::Key_Shift, Key::Key_LBracket}},
  {']', {Key::Key_RBracket}},   {'}', {Key::Key_Shift, Key::Key_RBracket}},
  {'\\', {Key::Key_Backslash}}, {'|', {Key::Key_Shift, Key::Key_Backslash}},
};

const CharToKeys mainPunctuation = {
  {';', {Key::Key_Semicolon}},   {':', {Key::Key_Shift, Key::Key_Semicolon}},
  {'\'', {Key::Key_Apostrophe}}, {'"', {Key::Key_Shift, Key::Key_Apostrophe}},
  {',', {Key::Key_Comma}},       {'<', {Key::Key_Shift, Key::Key_Comma}},
  {'.', {Key::Key_Period}},      {'>', {Key::Key_Shift, Key::Key_Period}},
  {'/', {Key::Key_Slash}},       {'?', {Key::Key_Shift, Key::Key_Slash}},
};

const CharToKeys digitSymbols = {
  {'!', {Key::Key_Shift, Key::Key_1}},
  {'@', {Key::Key_Shift, Key::Key_2}},
  {'#', {Key::Key_Shift, Key::Key_3}},
  {'$', {Key::Key_Shift, Key::Key_4}},
  {'%', {Key::Key_Shift, Key::Key_5}},
  {'^', {Key::Key_Shift, Key::Key_6}},
  {'&', {Key::Key_Shift, Key::Key_7}},
  {'*', {Key::Key_Shift, Key::Key_8}},
  {'(', {Key::Key_Shift, Key::Key_9}},
  {')', {Key::Key_Shift, Key::Key_0}},
};

// Helper to merge CharToKeys maps
static CharToKeys merge(std::initializer_list<const CharToKeys*> maps) {
  CharToKeys result;
  for (const auto* m : maps) {
    result.insert(m->begin(), m->end());
  }
  return result;
}

const CharToKeys allPunctuationAndSymbols = merge({
  &topPunctuation,
  &mainPunctuation,
  &digitSymbols,
});

} // namespace CharMappings

// Global CHAR_TO_KEYS combining all character categories
const CharToKeys CHAR_TO_KEYS = CharMappings::merge({
  &CharMappings::letters,
  &CharMappings::digits,
  &CharMappings::whitespace,
  &CharMappings::allPunctuationAndSymbols,
});
