#pragma once

// =============================================================================
// Composition strategies — single source of truth
// =============================================================================
// What sequences telescope Navigate + Transform (and possibly Insert) for a
// given diff. Both consumers hand callbacks here:
//
//   - CompositionOptimizer (full A*) enqueues each strategy as a candidate
//     edit transition (with motion prefix found by an inner NavOptimizer).
//   - CompositionFrontier (depth-1) emits each strategy as a Suggestion
//     (with motion prefix found by a single NavOptimizer call).
//
// Adding a new strategy here flows to both. If you find yourself adding a
// new structural emission to one consumer, it belongs in this header
// instead. The callbacks own DISPATCH; this file owns ENUMERATION.

#include <string>
#include <string_view>

#include "Optimizer/BuildTypedCommands.h"
#include "Optimizer/CompositionOptimizer/CompositionSearchContext.h"  // BracketQuoteContext
#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Types/BracketFlags.h"
#include "Types/Lines.h"
#include "Types/QuoteFlags.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimOptions.h"

namespace CompositionStrategies {

// One pure-insertion strategy. Fire `insertCmd` (mode-entry char + typed
// payload + trailing <Esc>) when the cursor lands inside the half-open
// range [beginCol, endCol) on `targetLine`. Multiple strategies may apply
// to the same diff (e.g. `I` line-scope + `i` point-scope for FNB
// insertions); each is reported as a separate callback invocation.
struct Insertion {
  int targetLine;
  int beginCol;
  int endCol;
  std::string insertCmd;
  CursorPos goalPos;
};

// One bracket/quote shortcut. Fire `body` from the exact cursor position
// (line, col); `body` is the full structural ('d'/'c' + 'i'/'a' modifier +
// bracket-or-quote char + (typed payload + <Esc> for replacement diffs)).
// Many strategies per diff — one per (column, pair) seen by validQuoteMask
// / validBracketMask.
struct BracketQuote {
  int line;
  int col;
  std::string body;
};

// Enumerate every Insertion strategy for `diff` against `lines`. No-op
// when `diff` is not a pure insertion.
template <class OnStrategy>
void enumerateInsertions(const DiffState& diff, const Lines& lines, OnStrategy&& cb) {
  if (!diff.isPureInsertion()) return;
  const CursorPos insertPos = diff.beginPos;
  const bool isNewLineInsertion = diff.isNewLineInsertion();

  // o-style new-line insertion: command opens a new line below.
  if (isNewLineInsertion && insertPos.col == 0 && insertPos.line > 0) {
    const int targetLine = insertPos.line - 1;
    const int lineEnd = lines[targetLine].effectiveSize();
    std::string_view sourceIndent = VimOptions::autoindent()
        ? leadingWhitespace(lines[targetLine])
        : std::string_view{};
    Lines insertLines = Lines::unflatten(std::string(diff.insertedTextBody()));
    KeyedSequence typed = buildTypedCommands(insertLines, sourceIndent);
    cb(Insertion{
        targetLine, 0, lineEnd, "o" + typed.seq.str(),
        typedCommandsExitCursor(insertPos, insertLines)});
    return;
  }

  // I/A/i/a family.
  const int line = insertPos.line;
  const int fnb = VimCore::firstNonBlankColInLine(lines[line]);
  const int lineLen = static_cast<int>(lines[line].size());
  const int lineEnd = lines[line].effectiveSize();
  const int lastContentCol = lineEnd - 1;
  std::string_view lineText(lines[line].data(), lines[line].size());
  Lines insertLines = Lines::unflatten(diff.insertedText);

  if (insertPos.col == fnb) {
    std::string_view prefix = lineText.substr(0, fnb);
    std::string_view suffix = lineText.substr(fnb);
    KeyedSequence escaped = buildTypedCommands(insertLines, "",
        prefix, suffix);
    CursorPos goalPos = typedCommandsExitCursor(insertPos, insertLines, suffix);
    cb(Insertion{line, 0, lineEnd, "I" + escaped.seq.str(), goalPos});
    cb(Insertion{line, insertPos.col, insertPos.col + 1,
                 "i" + escaped.seq.str(), goalPos});
  } else if (insertPos.col == lineLen) {
    KeyedSequence escaped = buildTypedCommands(insertLines, "", lines[line]);
    CursorPos goalPos = typedCommandsExitCursor(insertPos, insertLines);
    cb(Insertion{line, 0, lineEnd, "A" + escaped.seq.str(), goalPos});
    cb(Insertion{line, lastContentCol, lastContentCol + 1,
                 "a" + escaped.seq.str(), goalPos});
  } else {
    std::string_view prefix = lineText.substr(0, insertPos.col);
    std::string_view suffix = lineText.substr(insertPos.col);
    KeyedSequence escaped = buildTypedCommands(insertLines, "",
        prefix, suffix);
    cb(Insertion{line, insertPos.col, insertPos.col + 1,
                 "i" + escaped.seq.str(),
                 typedCommandsExitCursor(insertPos, insertLines, suffix)});
  }
}

// Enumerate every BracketQuote strategy for `diff` against `bqContext`.
// No-op when `diff` is a pure insertion or `bqContext` was not computed
// for this diff's line (line < 0).
template <class OnStrategy>
void enumerateBracketQuotes(const DiffState& diff,
                            const BracketQuoteContext& bqContext,
                            OnStrategy&& cb) {
  if (diff.isPureInsertion()) return;
  if (bqContext.line < 0) return;

  const bool pureDeletion = diff.isPureDeletion();
  const char textObjOp = pureDeletion ? 'd' : 'c';
  const std::string& insertedText = diff.insertedText;

  auto bodyFor = [&](char structuralChar, char modifierCh) {
    std::string body(1, textObjOp);
    body += modifierCh;
    body += structuralChar;
    if (!pureDeletion) {
      body += insertedText;
      body += "<Esc>";
    }
    return body;
  };

  for (int col = 0; col < static_cast<int>(bqContext.validQuoteMask.size()); col++) {
    for (char q : QuoteFlags::ALL_QUOTES) {
      if (!bqContext.validQuoteMask[col].seen(q)) continue;
      cb(BracketQuote{bqContext.line, col, bodyFor(q, bqContext.quoteModifier(q))});
    }
  }
  for (int col = 0; col < static_cast<int>(bqContext.validBracketMask.size()); col++) {
    for (char b : BracketFlags::ALL_BRACKETS) {
      if (!bqContext.validBracketMask[col].seen(b)) continue;
      cb(BracketQuote{bqContext.line, col, bodyFor(b, bqContext.bracketModifier(b))});
    }
  }
}

}  // namespace CompositionStrategies
