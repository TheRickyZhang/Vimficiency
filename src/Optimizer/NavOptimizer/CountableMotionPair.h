#pragma once

#include <string>
#include <vector>

#include "Keyboard/KeyedSequence.h"
#include "Types/LandingType.h"

struct CountableMotionPair {
  KeyedSequence forward;   // e.g., KeyedSequence::w, KeyedSequence::e
  KeyedSequence backward;  // e.g., KeyedSequence::b, KeyedSequence::ge
  LandingType type;
};

inline const std::vector<CountableMotionPair> COUNT_SEARCHABLE_MOTIONS_LINE = {
  {KeyedSequence::w,      KeyedSequence::b,  LandingType::WordBegin},
  {KeyedSequence::e,      KeyedSequence::ge, LandingType::WordEnd},
  {KeyedSequence::W,      KeyedSequence::B,  LandingType::WORDBegin},
  {KeyedSequence::E,      KeyedSequence::gE, LandingType::WORDEnd},
};

inline const std::vector<CountableMotionPair> COUNT_SEARCHABLE_MOTIONS_GLOBAL = {
  {KeyedSequence::RBrace, KeyedSequence::LBrace, LandingType::Paragraph},
  {KeyedSequence::RParen, KeyedSequence::LParen, LandingType::Sentence},
};

inline const std::vector<std::string> COUNT_SEARCHABLE_MOTIONS = [] {
  std::vector<std::string> res;
  res.reserve(2 * (COUNT_SEARCHABLE_MOTIONS_LINE.size() +
                   COUNT_SEARCHABLE_MOTIONS_GLOBAL.size()));
  for (const auto& x : COUNT_SEARCHABLE_MOTIONS_LINE) {
    res.push_back(x.forward.seq.str());
    res.push_back(x.backward.seq.str());
  }
  for (const auto& x : COUNT_SEARCHABLE_MOTIONS_GLOBAL) {
    res.push_back(x.forward.seq.str());
    res.push_back(x.backward.seq.str());
  }
  return res;
}();
