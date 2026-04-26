#include "SequenceChunker.h"
#include "Interpreter/SequenceParser.h"

#include <unordered_map>

using namespace std;

ChunkedSequence chunkCompositionSequence(string_view seq) {
  // Hand-authored valid sequences in test scaffolding: assert on malformed.
  auto tokens = parseSequence(seq).value();

  ChunkedSequence result;
  unordered_map<string, int> contentIndex; // dedup typed content

  SequenceChunk* current = nullptr;

  auto finalize = [&]() { current = nullptr; };

  auto ensureChunk = [&](SequenceChunk::Type type) -> SequenceChunk& {
    if (current && current->type == type) return *current;
    finalize();
    result.chunks.push_back({type, "", {}, -1});
    current = &result.chunks.back();
    return *current;
  };

  for (const auto& tok : tokens) {
    switch (tok.type) {
      case TokenType::Movement: {
        auto& chunk = ensureChunk(SequenceChunk::Movement);
        chunk.tokens.push_back(tok.text);
        chunk.text += tok.text;
        break;
      }
      case TokenType::Delete: {
        auto& chunk = ensureChunk(SequenceChunk::Edit);
        chunk.tokens.push_back(tok.text);
        chunk.text += tok.text;
        break;
      }
      case TokenType::Change: {
        auto& chunk = ensureChunk(SequenceChunk::Edit);
        chunk.tokens.push_back(tok.text);
        chunk.text += tok.text;
        break;
      }
      case TokenType::TypedText: {
        // Must be inside an Edit chunk (follows a Change token)
        auto& chunk = ensureChunk(SequenceChunk::Edit);
        chunk.tokens.push_back(tok.text);
        // Don't add to text — extract into contents[]
        auto it = contentIndex.find(tok.text);
        if (it != contentIndex.end()) {
          chunk.contentId = it->second;
        } else {
          int id = static_cast<int>(result.contents.size());
          result.contents.push_back(tok.text);
          contentIndex[tok.text] = id;
          chunk.contentId = id;
        }
        break;
      }
      case TokenType::Escape: {
        // Include in tokens but not in text
        if (current && current->type == SequenceChunk::Edit) {
          current->tokens.push_back(tok.text);
        } else {
          auto& chunk = ensureChunk(SequenceChunk::Edit);
          chunk.tokens.push_back(tok.text);
        }
        break;
      }
    }
  }

  return result;
}
