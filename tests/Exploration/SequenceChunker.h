#pragma once

#include <string>
#include <string_view>
#include <vector>

// Represents a semantic chunk of a composition sequence:
// either a motion phase (cursor movement) or an edit phase (buffer modification).
struct SequenceChunk {
  enum Type { Motion, Edit };
  Type type;
  std::string text;                // Command text (edits: without typed content/Esc)
  std::vector<std::string> tokens; // Individual tokens for detail view
  int contentId = -1;              // Index into contents[] (-1 = no typed content)
};

// A composition sequence split into semantic chunks with typed content extracted.
struct ChunkedSequence {
  std::vector<SequenceChunk> chunks;
  std::vector<std::string> contents; // Typed content strings, deduplicated
};

// Parse and chunk a composition sequence into motion and edit phases.
// Groups consecutive motion tokens into one Motion chunk and edit tokens
// (Delete/Change/TypedText/Escape) into one Edit chunk.
// Typed content is extracted and deduplicated into contents[].
ChunkedSequence chunkCompositionSequence(std::string_view seq);
