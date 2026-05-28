#include "Utils/InterpreterModelReplay.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <optional>
#include <string>

#include "Interpreter/EditInterpreter.h"
#include "Interpreter/MovementInterpreter.h"
#include "Interpreter/SequenceParser.h"
#include "Keyboard/KeyNotation.h"
#include "Types/Mode.h"
#include "VimCore/VimEditUtils.h"

using namespace std;

namespace {

bool applyInsertSpecial(Lines& lines, CursorPos& cursor, Mode& mode,
                        string_view special, int* pendingAutoindentLen) {
  // `<CR>` carries autoindent semantics — route through `applyEdit` so the
  // strip-leading-RHS-and-prepend-autoindent rule fires and pendingAutoindent
  // gets set for the next `<Esc>`.
  if (special == "<CR>") {
    Edit::applyEdit(lines, cursor, mode, ParsedEdit(special), nullptr, false,
                    0, 0, false, pendingAutoindentLen);
    return true;
  }

  optional<char> display = parseDisplayChar(special);
  if (display) {
    char c = *display;
    VimCore::insertText(lines, cursor, string_view(&c, 1));
    // Any user-typed char (including space) clears the pending-strip state.
    if (pendingAutoindentLen != nullptr) *pendingAutoindentLen = 0;
    return true;
  }

  static constexpr array<string_view, 8> editSpecials = {
      "<BS>", "<Del>", "<C-u>", "<C-w>",
      "<Left>", "<Right>", "<Up>", "<Down>",
  };
  if (find(editSpecials.begin(), editSpecials.end(), special)
      != editSpecials.end()) {
    Edit::applyEdit(lines, cursor, mode, ParsedEdit(special), nullptr, false,
                    0, 0, false, pendingAutoindentLen);
    if (pendingAutoindentLen != nullptr) *pendingAutoindentLen = 0;
    return true;
  }
  return false;
}

void applyTypedText(Lines& lines, CursorPos& cursor, Mode& mode,
                    string_view text, int* pendingAutoindentLen = nullptr) {
  assert(mode == Mode::Insert);
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == '<') {
      size_t close = text.find('>', i);
      if (close != string_view::npos) {
        string_view special = text.substr(i, close - i + 1);
        if (applyInsertSpecial(lines, cursor, mode, special,
                               pendingAutoindentLen)) {
          i = close + 1;
          continue;
        }
      }
    }

    size_t nextSpecial = text.find('<', i + 1);
    string_view literal = text.substr(i, nextSpecial - i);
    VimCore::insertText(lines, cursor, literal);
    // Literal user typing clears pending strip.
    if (pendingAutoindentLen != nullptr) *pendingAutoindentLen = 0;
    i += literal.size();
  }
}

}  // namespace

InterpreterReplayResult applyUserSequence(
    Lines lines, CursorPos cursor, string_view sequence) {
  Mode mode = Mode::Normal;
  auto tokens = parseSequence(sequence);
  assert(tokens.has_value());

  string pendingMovements;
  // Tracks Vim's `did_ai` across tokens within this sequence — set by `<CR>`
  // when autoindent inserts whitespace, cleared by `<Esc>` (and by typed text
  // that isn't part of the autoindent itself).
  int pendingAutoindentLen = 0;
  // Dot-repeat storage — updated by applyEdit for buffer-modifying Normal mode
  // commands, consumed when a `.` token fires.
  DotRepeat lastEdit;
  auto flushMovements = [&] {
    if (!pendingMovements.empty()) {
      cursor = simulateMovements(cursor, pendingMovements, lines);
      pendingMovements.clear();
    }
  };

  for (const TaggedToken& token : *tokens) {
    string_view text = token.token;
    switch (token.kind) {
      case TokenKind::Movement:
        pendingMovements += text;
        pendingAutoindentLen = 0;
        break;
      case TokenKind::Delete:
      case TokenKind::Change:
      case TokenKind::Visual:
      case TokenKind::Escape: {
        flushMovements();
        // Parse the token so a count prefix like "3x" becomes (edit="x", count=3).
        auto parsed = Edit::parseEdits(text);
        if (parsed && !parsed->empty()) {
          for (const ParsedEdit& pe : *parsed) {
            Edit::applyEdit(lines, cursor, mode, pe, &lastEdit, false,
                            0, 0, false, &pendingAutoindentLen);
          }
        } else {
          Edit::applyEdit(lines, cursor, mode, ParsedEdit(text), &lastEdit,
                          false, 0, 0, false, &pendingAutoindentLen);
        }
        break;
      }
      case TokenKind::TypedText:
        flushMovements();
        applyTypedText(lines, cursor, mode, text, &pendingAutoindentLen);
        break;
    }
  }
  flushMovements();
  return {lines, cursor, mode};
}
