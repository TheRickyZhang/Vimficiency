#include "Utils/InterpreterModelReplay.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <optional>
#include <string>

#include "Interpreter/EditInterpreter.h"
#include "Interpreter/MovementInterpreter.h"
#include "Interpreter/SequenceFormatting.h"
#include "Interpreter/SequenceParser.h"
#include "Types/Mode.h"
#include "VimCore/VimEditUtils.h"

using namespace std;

namespace {

bool applyInsertSpecial(Lines& lines, CursorPos& cursor, Mode& mode,
                        string_view special) {
  optional<char> display = parseDisplayChar(special);
  if (display) {
    char c = *display;
    VimCore::insertText(lines, cursor, string_view(&c, 1));
    return true;
  }

  static constexpr array<string_view, 8> editSpecials = {
      "<BS>", "<Del>", "<C-u>", "<C-w>",
      "<Left>", "<Right>", "<Up>", "<Down>",
  };
  if (find(editSpecials.begin(), editSpecials.end(), special)
      != editSpecials.end()) {
    Edit::applyEdit(lines, cursor, mode, ParsedEdit(special));
    return true;
  }
  return false;
}

void applyTypedText(Lines& lines, CursorPos& cursor, Mode& mode,
                    string_view text) {
  assert(mode == Mode::Insert);
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == '<') {
      size_t close = text.find('>', i);
      if (close != string_view::npos) {
        string_view special = text.substr(i, close - i + 1);
        if (applyInsertSpecial(lines, cursor, mode, special)) {
          i = close + 1;
          continue;
        }
      }
    }

    size_t nextSpecial = text.find('<', i + 1);
    string_view literal = text.substr(i, nextSpecial - i);
    VimCore::insertText(lines, cursor, literal);
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
        break;
      case TokenKind::Delete:
      case TokenKind::Change:
      case TokenKind::Visual:
      case TokenKind::Escape:
        flushMovements();
        Edit::applyEdit(lines, cursor, mode, ParsedEdit(text));
        break;
      case TokenKind::TypedText:
        flushMovements();
        applyTypedText(lines, cursor, mode, text);
        break;
    }
  }
  flushMovements();
  return {lines, cursor, mode};
}
