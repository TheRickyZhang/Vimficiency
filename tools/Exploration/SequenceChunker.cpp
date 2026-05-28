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
    switch (tok.kind) {
      case TokenKind::Movement: {
        auto& chunk = ensureChunk(SequenceChunk::Movement);
        chunk.tokens.push_back(tok.token);
        chunk.text += tok.token;
        break;
      }
      case TokenKind::Delete: {
        auto& chunk = ensureChunk(SequenceChunk::Edit);
        chunk.tokens.push_back(tok.token);
        chunk.text += tok.token;
        break;
      }
      case TokenKind::Change:
      case TokenKind::Visual: {
        auto& chunk = ensureChunk(SequenceChunk::Edit);
        chunk.tokens.push_back(tok.token);
        chunk.text += tok.token;
        break;
      }
      case TokenKind::TypedText: {
        // Must be inside an Edit chunk (follows a Change token)
        auto& chunk = ensureChunk(SequenceChunk::Edit);
        chunk.tokens.push_back(tok.token);
        // Don't add to text — extract into contents[]
        auto it = contentIndex.find(tok.token);
        if (it != contentIndex.end()) {
          chunk.contentId = it->second;
        } else {
          int id = static_cast<int>(result.contents.size());
          result.contents.push_back(tok.token);
          contentIndex[tok.token] = id;
          chunk.contentId = id;
        }
        break;
      }
      case TokenKind::Escape: {
        // Include in tokens but not in text
        if (current && current->type == SequenceChunk::Edit) {
          current->tokens.push_back(tok.token);
        } else {
          auto& chunk = ensureChunk(SequenceChunk::Edit);
          chunk.tokens.push_back(tok.token);
        }
        break;
      }
    }
  }

  return result;
}
