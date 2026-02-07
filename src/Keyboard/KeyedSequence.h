#pragma once

#include "KeyboardModel.h"
#include "CharToKeys.h"
#include "State/Sequence.h"

#include <string>
#include <string_view>

struct KeyedSequence {
  Sequence seq;
  PhysicalKeys keys;

  KeyedSequence() = default;
  KeyedSequence(std::string_view s, PhysicalKeys k) : seq(std::string(s)), keys(std::move(k)) {}

  KeyedSequence& operator+=(const KeyedSequence& other) {
    seq.append(other.seq.keys);
    keys += other.keys;
    return *this;
  }

  void append(std::string_view s, const PhysicalKeys& k) {
    seq.append(s);
    keys += k;
  }

  void appendChar(char c) {
    seq.append(c);
    keys.append(CHAR_TO_KEYS.at(c));
  }

  void appendText(std::string_view text) {
    for (char c : text) {
      seq.append(c);
      keys.append(CHAR_TO_KEYS.at(c));
    }
  }

  void appendCharRepeated(char c, int count) {
    const PhysicalKeys& charKeys = CHAR_TO_KEYS.at(c);
    for (int i = 0; i < count; i++) {
      seq.append(c);
      keys.append(charKeys);
    }
  }

  void appendRepeated(const KeyedSequence& ks, int count) {
    for (int i = 0; i < count; i++) {
      seq.append(ks.seq.keys);
      keys.append(ks.keys);
    }
  }

  void appendCounted(int count, const KeyedSequence& base) {
    seq.append(std::to_string(count));
    seq.append(base.seq.keys);
    keys.append(makeCountedKeys(count, base.keys));
  }

  static const KeyedSequence BS;
  static const KeyedSequence Del;
  static const KeyedSequence Esc;
  static const KeyedSequence CR;
  static const KeyedSequence CtrlU;
};

inline const KeyedSequence KeyedSequence::BS{"<BS>", {Key::Key_Backspace}};
inline const KeyedSequence KeyedSequence::Del{"<Del>", {Key::Key_Delete}};
inline const KeyedSequence KeyedSequence::Esc{"<Esc>", {Key::Key_Esc}};
inline const KeyedSequence KeyedSequence::CR{"<CR>", {Key::Key_Enter}};
inline const KeyedSequence KeyedSequence::CtrlU{"<C-u>", {Key::Key_Ctrl, Key::Key_U}};
