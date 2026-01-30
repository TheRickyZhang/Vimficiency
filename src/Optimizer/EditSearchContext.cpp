#include "EditSearchContext.h"
#include "Keyboard/EditToKeys.h"
#include "Keyboard/MotionToKeysPrimitives.h"

#include <cstring>

using namespace std;

EditSearchContext::EditSearchContext(const Lines& initialLines,
                                     const EditBoundary& boundary,
                                     const OptimizerParams& params,
                                     const Config& config)
    : editBoundary(boundary),
      params(params),
      config(config),
      leftColOffset(static_cast<int>(boundary.prefix().size())),
      rightColOffset(static_cast<int>(boundary.suffix().size())),
      effectiveLines(initialLines),
      effortWeight(params.effortWeight),
      distanceWeight(params.distanceWeight),
      totalPositions(0) {

  // Build effectiveLines with prefix/suffix baked in
  const auto& pre = editBoundary.prefix();
  const auto& suf = editBoundary.suffix();
  if (!pre.empty()) effectiveLines.front().insert(0, pre);
  if (!suf.empty()) effectiveLines.back() += suf;

  // Count total starting positions
  for (const auto& line : initialLines) {
    totalPositions += line.empty() ? 1 : static_cast<int>(line.size());
  }
}

bool EditSearchContext::inBoundaryRegion(const Position& pos, const Lines& lines) const {
  if (pos.line < 0 || pos.line > lines.lastLine()) return true;

  if (pos.line == 0 && pos.col < leftColOffset) return true;
  if (pos.line == lines.lastLine() && rightColOffset > 0 &&
      pos.col >= static_cast<int>(lines[pos.line].size()) - rightColOffset) {
    return true;
  }
  return false;
}

void EditSearchContext::exploreNewState(EditState&& state) {
  EditStateKey key = state.getKey();
  auto it = costMap.find(key);

  double newCost = state.getCost();
  if (it == costMap.end() || newCost <= it->second) {
    costMap[key] = newCost;
    pq.push(std::move(state));
  }
}

void EditSearchContext::initStartingPositions(const Lines& initialLines) {
  int startIndex = 0;
  // Initial priority: effortWeight * 0 + distanceWeight * distance = distanceWeight * distance
  double startPriority = computePriority(0.0, effectiveLines);

  for (int line = 0; line < static_cast<int>(initialLines.size()); line++) {
    int lineCols = initialLines[line].empty() ? 1 : static_cast<int>(initialLines[line].size());
    for (int col = 0; col < lineCols; col++) {
      int effCol = col + (line == 0 ? leftColOffset : 0);
      pq.push(EditState(effectiveLines, Position(line, effCol), startIndex, startPriority));
      startIndex++;
    }
  }
}

// Returns C++ convention [startCol, endCol) <- EXCLUSIVE END
// of where the edit region is in this line
pair<int, int> EditSearchContext::computeEditBounds(
    const Lines& lines, const Position& cursor) const {
  int rawLineLen = static_cast<int>(lines[cursor.line].size());
  int contentBegin = (cursor.line == 0) ? leftColOffset : 0;
  int contentEnd = rawLineLen;

  if (cursor.line == lines.lastLine() && rightColOffset > 0) {
    contentEnd -= rightColOffset;
  }
  if (contentEnd < 0) {
    cerr << "contentEnd < 0: lines=" << lines << " cursor=" << cursor.line << "," << cursor.col
         << " rawLineLen=" << rawLineLen << " rightColOffset=" << rightColOffset << endl;
  }
  assert(contentEnd >= 0);
  assert(contentBegin >= 0);
  assert(contentEnd >= contentBegin);
  return {contentBegin, contentEnd};
}

double EditSearchContext::distanceHeuristic(const Lines& lines) const {
  int total = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    int lineLen = static_cast<int>(lines[i].size());
    if (i == 0) lineLen -= leftColOffset;
    if (i == lines.size() - 1) lineLen -= rightColOffset;
    total += max(0, lineLen);
  }
  // Newlines count as 2 (n lines have n-1 newlines)
  total += (static_cast<int>(lines.size()) - 1) * 2;
  return static_cast<double>(total);
}

// This is good, but I think we should be able to detect when it stops based on each condition, to let use know whether we have exhausted search (unlikely), reached result threshold, or reached search threshold
bool EditSearchContext::shouldContinue() const {
  return !pq.empty() && resultsFound < totalPositions && iterations < params.maxNodesExplored;
}

// This has potential to be inlined, but worth it for organization
// Nullopt -> there is no next valid state
optional<EditState> EditSearchContext::getNextValidState() {
  while (!pq.empty()) {
    EditState s = pq.top();
    pq.pop();

    // Skip if this state is outdated (we've found a better path)
    EditStateKey key = s.getKey();
    auto it = costMap.find(key);
    if (it != costMap.end() && it->second < s.getCost() - 1e-9)
      continue;

    return s;
  }
  return nullopt;
}

// =============================================================================
// Exploration Helper Methods
// =============================================================================

void EditSearchContext::exploreForwardWordEdits(
    const vector<Edit::ForwardWordEditSpec>& specs,
    const Position& cursor, const Lines& lines, DeletionCallback onDeletion) {
  for (const auto& spec : specs) {
    Position endpoint = VimCore::motionWordEndpoint(
        cursor, lines, true, spec.edgeType, spec.isBig,
        spec.skipCurrent, rightColOffset, editBoundary.hasLinesBelow());

    if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor)
      continue;

    // Special case for dw/dW: Vim does NOT cross lines.
    // If endpoint is on a different line, clamp to end of word on current line.
    if (spec.edgeType == EdgeType::GapEdge && endpoint.line > cursor.line) {
      // Find word end on current line instead
      Position wordEnd = VimCore::motionWordEndpoint(
          cursor, lines, true, EdgeType::WordEdge, spec.isBig,
          spec.skipCurrent, rightColOffset, editBoundary.hasLinesBelow());
      if (wordEnd == POSITION_OUTSIDE_BOUNDARY || wordEnd == cursor)
        continue;
      endpoint = wordEnd;
    }

    onDeletion(Range(cursor, endpoint), spec.cmd, spec.keys);
  }
}

void EditSearchContext::exploreBackwardWordEdits(
    const vector<Edit::BackwardWordEditSpec>& specs,
    const Position& cursor, const Lines& lines, DeletionCallback onDeletion) {
  for (const auto& spec : specs) {
    // For inclusive backward deletes (dge/dgE), the cursor is part of the
    // deletion range. Check cursor first before computing endpoint.
    if (!spec.isExclusiveAtCursor && inBoundaryRegion(cursor, lines))
      continue;

    Position endpoint = VimCore::motionWordEndpoint(
        cursor, lines, false, spec.edgeType, spec.isBig,
        spec.skipCurrent, leftColOffset, editBoundary.hasLinesAbove());

    if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor)
      continue;

    // db/dB are EXCLUSIVE at cursor: delete backward but NOT the cursor char.
    // Three cases:
    Range range;
    if (spec.isExclusiveAtCursor) {
      int cursorContentCol = cursor.col - (cursor.line == 0 ? leftColOffset : 0);
      if (cursorContentCol > 0) {
        // 1. Cursor not at content start: range ends at cursor.col - 1
        range = Range(endpoint, Position(cursor.line, cursor.col - 1));
      }
      else if (endpoint.line < cursor.line) {
        // 2. Cursor at content start AND crossed lines: ends at prev line's last char
        int prevLine = cursor.line - 1;
        range = Range(endpoint, Position(prevLine, lines[prevLine].lastCol()));
      } else {
        // 3. Cursor at content start AND same line: skip (can't delete backward)
        continue;
      }
    } else {
      range = Range(endpoint, cursor);
    }

    onDeletion(range, spec.cmd, spec.keys);
  }
}

void EditSearchContext::exploreTextObjectEdits(
    const vector<Edit::TextObjectEditSpec>& specs,
    const Position& cursor, const Lines& lines, DeletionCallback onDeletion) {
  for (const auto& spec : specs) {
    Range range = VimCore::textObjectRange(
        cursor, lines, spec.isInner, spec.isBig,
        leftColOffset, rightColOffset,
        editBoundary.hasLinesAbove(), editBoundary.hasLinesBelow());

    if (range.first == POSITION_OUTSIDE_BOUNDARY || range.last == POSITION_OUTSIDE_BOUNDARY)
      continue;

    onDeletion(range, spec.cmd, spec.keys);
  }
}

// TODO: Support d^, d0?
// Currently contentStart, contentEnd is not used
void EditSearchContext::exploreHalfLineEdits(
    const vector<Edit::LineEditSpec>& specs,
    const Position& cursor, const Lines& lines, int contentStart, int contentEnd,
    DeletionCallback onDeletion) {
  int lastEditLine = lines.lastLine();

  for (const Edit::LineEditSpec& spec : specs) {
    // Must use strcmp since both are const char*
    if (!strcmp(spec.cmd, "D")) {
      if (cursor.line == lastEditLine && editBoundary.hasSuffix()) continue;

      int lineLen = static_cast<int>(lines[cursor.line].size());
      int lineContentEnd = lineLen;
      if (cursor.line == lastEditLine && rightColOffset > 0) {
        lineContentEnd -= rightColOffset;
      }
      if (lineContentEnd <= 0) continue;

      int endCol = lineContentEnd - 1;
      if (endCol < cursor.col) continue;
      Range range(cursor, Position(cursor.line, endCol));
      onDeletion(range, spec.cmd, spec.keys);
    } else if (!strcmp(spec.cmd, "d0")) {
      if (cursor.line == 0 && editBoundary.hasPrefix()) continue;

      int lineContentStart = (cursor.line == 0) ? leftColOffset : 0;
      if (cursor.col <= lineContentStart) continue;
      Range range(Position(cursor.line, lineContentStart),
                  Position(cursor.line, cursor.col - 1));
      onDeletion(range, spec.cmd, spec.keys);
    } else {
      assert(false && "Unexpected half-line edit command");
    }
  }
}

void EditSearchContext::exploreFullLineEdits(
  const std::vector<Edit::FullLineEditSpec>& specs,
  const Position& cursor, const Lines& lines, LinewiseCallback onLinewise
) {
  if((cursor.line == 0 && editBoundary.hasPrefix()) || 
    (cursor.line == lines.lastLine() && editBoundary.hasSuffix())
  ) {
    return;
  }
  for (const auto& spec : specs) {
    onLinewise(cursor.line, spec.cmd, spec.keys);
  }
}

void EditSearchContext::exploreCharEdits(
    const Position& cursor, const Lines& lines, int contentStart, int contentEnd,
    int editContentLen, DeletionCallback onDeletion) {
  // Note these can make the cursor end up in edit region, but we can handle inside edit region separately

  // x: delete char at cursor (must be in content region)
  if (contentStart <= cursor.col && cursor.col < contentEnd) {
    onDeletion(Range(cursor, cursor), "x", Deletion::CHAR.at("x"));
  }

  // X: delete char before cursor
  // Both cursor and the char being deleted must be in content region
  // cursor must be > contentStart AND cursor must be <= contentEnd (for char before to be valid)
  if (cursor.col > contentStart && cursor.col <= contentEnd) {
    Position before(cursor.line, cursor.col - 1);
    onDeletion(Range(before, before), "X", Deletion::CHAR.at("X"));
  }
}

void EditSearchContext::exploreParagraphEdits(
    const vector<Edit::ParagraphEditSpec>& specs,
    const Position& cursor, const Lines& lines, LinewiseCallback onLinewise) {
  int lastLine = lines.lastLine();

  for (const auto& spec : specs) {
    int endpointLine = VimCore::motionParagraphEndpoint(
        cursor.line, lines, spec.forward, LineEdgeType::NextEdge);

    // Skip if endpoint lands on edge line when bounded
    if (spec.forward) {
      if (editBoundary.hasLinesBelow() && endpointLine == lastLine) continue;
      if (editBoundary.hasSuffix() && endpointLine == lastLine) continue;
    } else {
      if (editBoundary.hasLinesAbove() && endpointLine == 0) continue;
      if (editBoundary.hasPrefix() && endpointLine == 0) continue;
    }

    // d{/d} are LINEWISE in Neovim
    if (spec.forward) {
      // d}: delete from cursor line to line before endpoint (endpoint is blank line)
      int endLine = endpointLine - 1;
      if (endLine < cursor.line) continue;
      // For now, only support single-line deletion (cursor.line == endLine)
      // Multi-line linewise deletion would need a LineRange callback
      if (endLine == cursor.line) {
        onLinewise(cursor.line, spec.cmd, spec.keys);
      }
    } else {
      // d{: delete from endpoint line to cursor line (inclusive)
      if (endpointLine > cursor.line) continue;
      // For now, only support single-line deletion
      if (endpointLine == cursor.line) {
        onLinewise(cursor.line, spec.cmd, spec.keys);
      }
    }
  }
}

void EditSearchContext::exploreSentenceEdits(
    const vector<Edit::SentenceEditSpec>& specs,
    const Position& cursor, const Lines& lines, DeletionCallback onDeletion) {
  int lastLine = lines.lastLine();

  for (const auto& spec : specs) {
    Position endpoint = VimCore::motionSentenceEndpoint(
        cursor, lines, spec.forward, SentenceEdgeType::NextEdge);

    if (endpoint == POSITION_OUTSIDE_BOUNDARY) continue;

    // Skip if endpoint lands on edge line when bounded (may have been clamped)
    if (spec.forward) {
      if (editBoundary.hasLinesBelow() && endpoint.line == lastLine) continue;
      // Check if endpoint is in suffix region
      if (editBoundary.hasSuffix() && endpoint.line == lastLine &&
          endpoint.col >= static_cast<int>(lines[lastLine].size()) - rightColOffset) continue;
    } else {
      if (editBoundary.hasLinesAbove() && endpoint.line == 0) continue;
      // Check if endpoint is in prefix region
      if (editBoundary.hasPrefix() && endpoint.line == 0 &&
          endpoint.col < leftColOffset) continue;
    }

    if (spec.forward) {
      // d): delete from cursor to just before endpoint (exclusive)
      if (endpoint <= cursor) continue;
      Position endPos = lines.getPrevPos(endpoint);
      if (endPos == POSITION_OUTSIDE_BOUNDARY) continue;
      onDeletion(Range(cursor, endPos), spec.cmd, spec.keys);
    } else {
      // d(: delete from endpoint to just before cursor (exclusive)
      if (endpoint >= cursor) continue;

      // Skip when cursor is on trailing whitespace at EOL and endpoint is col 0
      // In this case, Vim converts to linewise deletion (exclusive-linewise rule)
      // which we don't handle correctly
      if (endpoint.col == 0 && cursor.line > endpoint.line) {
        // Check if cursor is on trailing whitespace at end of line
        bool cursorOnTrailingWs = false;
        if (cursor.col > 0 && cursor.col < static_cast<int>(lines[cursor.line].size())) {
          char cursorChar = lines[cursor.line][cursor.col];
          if (cursorChar == ' ' || cursorChar == '\t') {
            // Check if all characters after cursor are also whitespace
            cursorOnTrailingWs = true;
            for (int c = cursor.col + 1; c < static_cast<int>(lines[cursor.line].size()); c++) {
              char ch = lines[cursor.line][c];
              if (ch != ' ' && ch != '\t') {
                cursorOnTrailingWs = false;
                break;
              }
            }
          }
        }
        if (cursorOnTrailingWs) continue;
      }

      Position endPos = Position(cursor.line, cursor.col - 1);
      if (cursor.col == 0) {
        if (cursor.line == 0) continue;
        endPos = Position(cursor.line - 1, lines[cursor.line - 1].lastCol());
      }
      onDeletion(Range(endpoint, endPos), spec.cmd, spec.keys);
    }
  }
}


// =============================================================================
// Main Exploration Entry Point
// =============================================================================

void EditSearchContext::exploreJoinCommands(
    const Position& cursor, const Lines& lines, JoinCallback onJoin) {
  if (!onJoin) return;

  // J/gJ require a next line to join with
  if (cursor.line >= lines.lastLine()) return;

  int nextLine = cursor.line + 1;

  // Don't allow J if there are lines below the edit region
  // (would pull content from outside the edit region)
  // Note: J with suffix line IS allowed - after J, the suffix moves to end of
  // the merged line, which becomes the new lastLine(), so boundary checks still work
  if (nextLine == lines.lastLine() && editBoundary.hasLinesBelow()) return;

  // Explore both J (with space) and gJ (without space)
  for (const auto& [cmd, keys] : Deletion::JOIN) {
    bool addSpace = (cmd == "J");
    onJoin(addSpace, cmd.c_str(), keys);
  }
}

void EditSearchContext::exploreAllDeletions(const EditState& state,
                                            DeletionCallback onDeletion,
                                            LinewiseCallback onLinewise,
                                            MotionCallback onMotion,
                                            JoinCallback onJoin) {
  const Lines& lines = state.getLines();
  Position cursor = state.getPos();

  // Right boundary (suffix region): cursor on last line, in suffix columns
  if (cursor.line == lines.lastLine() && rightColOffset > 0 &&
      cursor.col + rightColOffset >= static_cast<int>(lines.getSize(cursor.line))) {
    // Can still do backward deletions that don't touch suffix
    exploreBackwardWordEdits(Edit::EXCLUSIVE_BACKWARD_WORD_EDITS, cursor, lines, onDeletion);

    if (onMotion) {
      // h: move left within line (away from suffix) - horizontal updates targetCol
      if (cursor.col > 0) { onMotion(Position(cursor.line, cursor.col - 1), "h", hjkl.at("h")); }
      // k: move up to previous line (escape suffix line entirely) - vertical preserves targetCol
      if (cursor.line > 0) {
        int newCol = min(cursor.targetCol, lines[cursor.line - 1].lastCol());
        onMotion(Position(cursor.line - 1, newCol, cursor.targetCol), "k", hjkl.at("k"));
      }
    }
    return;
  }

  // Left boundary (prefix region): cursor on line 0, in prefix columns
  if (cursor.line == 0 && cursor.col < leftColOffset) {
    if (onMotion) {
      // l: move right within line (away from prefix) - horizontal updates targetCol
      if (cursor.col < static_cast<int>(lines[0].size()) - 1) {
        onMotion(Position(0, cursor.col + 1), "l", hjkl.at("l"));
      }
      // j: move down to next line (escape prefix line entirely) - vertical preserves targetCol
      if (lines.lastLine() > 0) {
        int newCol = min(cursor.targetCol, lines[1].lastCol());
        onMotion(Position(1, newCol, cursor.targetCol), "j", hjkl.at("j"));
      }
    }
    return;
  }

  auto [contentBegin, contentEnd] = computeEditBounds(lines, cursor);
  int editContentLen = contentEnd - contentBegin;

  // Otherwise if no content we must have empty line -> Explore limited set, since many commands are equivalent
  if (editContentLen <= 0) {
    assert(lines[cursor.line].size() == 0);

    // Limited exploration:
    // (dd == dw == dW)
    exploreFullLineEdits(Edit::EMPTYLINE_FULL_LINE_EDITS, cursor, lines, onLinewise);

    // de/dE
    exploreForwardWordEdits(Edit::EMPTYLINE_FORWARD_WORD_EDITS, cursor, lines, onDeletion);
    // db/dB/dge
    exploreBackwardWordEdits(Edit::EMPTYLINE_BACKWARD_WORD_EDITS, cursor, lines, onDeletion);
    // J/gJ - can join even from empty lines
    exploreJoinCommands(cursor, lines, onJoin);
    return;
  }

  exploreForwardWordEdits(Edit::FORWARD_WORD_EDITS, cursor, lines, onDeletion);
  exploreBackwardWordEdits(Edit::BACKWARD_WORD_EDITS, cursor, lines, onDeletion);
  exploreTextObjectEdits(Edit::TEXT_OBJECT_EDITS, cursor, lines, onDeletion);
  exploreHalfLineEdits(Edit::HALF_LINE_EDITS, cursor, lines, contentBegin, contentEnd, onDeletion);
  exploreFullLineEdits(Edit::FULL_LINE_EDITS, cursor, lines, onLinewise);
  exploreCharEdits(cursor, lines, contentBegin, contentEnd, editContentLen, onDeletion);
  exploreParagraphEdits(Edit::PARAGRAPH_EDITS, cursor, lines, onLinewise);
  exploreSentenceEdits(Edit::SENTENCE_EDITS, cursor, lines, onDeletion);
  exploreJoinCommands(cursor, lines, onJoin);
}
