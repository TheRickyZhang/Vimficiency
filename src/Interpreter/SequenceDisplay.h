#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace VF::SequenceDisplay {

struct Options {
  bool tokenize = true;
  bool sectionize = true;
};

struct TypedChunk {
  std::string kind;
  std::string text;
};

std::string displayKeyNotation(std::string_view text);
std::string displayTypedText(std::string_view text);
std::string displayLiteralTypedText(std::string_view text);
std::vector<std::string> lines(std::string_view seq, Options opts = {});
std::string inlineText(std::string_view seq, Options opts = {});
std::string typedChunksInline(
    const std::vector<TypedChunk>& chunks,
    bool tokenize = true);
std::vector<std::string> prefixedLines(
    std::string_view prefix,
    std::string_view seq,
    Options opts = {},
    std::string_view suffix = "");

}  // namespace VF::SequenceDisplay
