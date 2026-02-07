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

  // Motion constants
  static const KeyedSequence h, l, Zero, Caret, Dollar;
  static const KeyedSequence j, k;
  static const KeyedSequence w, b, e, ge, W, B, E, gE;
  static const KeyedSequence gg, G;
  static const KeyedSequence LBrace, RBrace, LParen, RParen;
  static const KeyedSequence CtrlD;
};

inline const KeyedSequence KeyedSequence::BS{"<BS>", {Key::Key_Backspace}};
inline const KeyedSequence KeyedSequence::Del{"<Del>", {Key::Key_Delete}};
inline const KeyedSequence KeyedSequence::Esc{"<Esc>", {Key::Key_Esc}};
inline const KeyedSequence KeyedSequence::CR{"<CR>", {Key::Key_Enter}};
inline const KeyedSequence KeyedSequence::CtrlU{"<C-u>", {Key::Key_Ctrl, Key::Key_U}};

// Horizontal
inline const KeyedSequence KeyedSequence::h{"h", {Key::Key_H}};
inline const KeyedSequence KeyedSequence::l{"l", {Key::Key_L}};
inline const KeyedSequence KeyedSequence::Zero{"0", {Key::Key_0}};
inline const KeyedSequence KeyedSequence::Caret{"^", {Key::Key_Shift, Key::Key_6}};
inline const KeyedSequence KeyedSequence::Dollar{"$", {Key::Key_Shift, Key::Key_4}};

// Vertical
inline const KeyedSequence KeyedSequence::j{"j", {Key::Key_J}};
inline const KeyedSequence KeyedSequence::k{"k", {Key::Key_K}};

// Word motions
inline const KeyedSequence KeyedSequence::w{"w", {Key::Key_W}};
inline const KeyedSequence KeyedSequence::b{"b", {Key::Key_B}};
inline const KeyedSequence KeyedSequence::e{"e", {Key::Key_E}};
inline const KeyedSequence KeyedSequence::ge{"ge", {Key::Key_G, Key::Key_E}};
inline const KeyedSequence KeyedSequence::W{"W", {Key::Key_Shift, Key::Key_W}};
inline const KeyedSequence KeyedSequence::B{"B", {Key::Key_Shift, Key::Key_B}};
inline const KeyedSequence KeyedSequence::E{"E", {Key::Key_Shift, Key::Key_E}};
inline const KeyedSequence KeyedSequence::gE{"gE", {Key::Key_G, Key::Key_Shift, Key::Key_E}};

// Jumps
inline const KeyedSequence KeyedSequence::gg{"gg", {Key::Key_G, Key::Key_G}};
inline const KeyedSequence KeyedSequence::G{"G", {Key::Key_Shift, Key::Key_G}};

// Paragraph/Sentence
inline const KeyedSequence KeyedSequence::LBrace{"{", {Key::Key_Shift, Key::Key_LBracket}};
inline const KeyedSequence KeyedSequence::RBrace{"}", {Key::Key_Shift, Key::Key_RBracket}};
inline const KeyedSequence KeyedSequence::LParen{"(", {Key::Key_Shift, Key::Key_9}};
inline const KeyedSequence KeyedSequence::RParen{")", {Key::Key_Shift, Key::Key_0}};

// Scroll
inline const KeyedSequence KeyedSequence::CtrlD{"<C-d>", {Key::Key_Ctrl, Key::Key_D}};
