#include "Interpreter/SequenceDisplay.h"

#include "Interpreter/SequenceParser.h"
#include "Utils/PrettyText.h"

#include <string>

using namespace std;

namespace VF::SequenceDisplay {
namespace {

string glyphForKeyNotation(string_view special) {
  if (special == "<Space>") return string(PrettyText::SPACE);
  if (special == "<Tab>") return string(PrettyText::TAB);
  if (special == "<CR>" || special == "<Enter>" || special == "<Return>") {
    return string(PrettyText::NEWLINE);
  }
  if (special == "<lt>" || special == "<LT>") return "<";
  return string(special);
}

vector<string_view> splitKeyNotationTypedText(string_view text) {
  vector<string_view> parts;
  size_t plainStart = 0;
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] != '<') {
      i++;
      continue;
    }

    const size_t close = text.find('>', i);
    if (close == string_view::npos) {
      i++;
      continue;
    }

    if (plainStart < i) {
      parts.push_back(text.substr(plainStart, i - plainStart));
    }
    parts.push_back(text.substr(i, close - i + 1));
    i = close + 1;
    plainStart = i;
  }

  if (plainStart < text.size()) {
    parts.push_back(text.substr(plainStart));
  }
  return parts;
}

string displayTokenText(const TaggedToken& token) {
  return token.kind == TokenKind::TypedText
      ? displayTypedText(token.token)
      : displayKeyNotation(token.token);
}

struct DisplayToken {
  string text;
  TokenKind kind;
};

bool isEditStart(TokenKind kind) {
  return kind == TokenKind::Change || kind == TokenKind::Delete;
}

bool isEditContinuation(TokenKind kind) {
  return kind == TokenKind::Change ||
         kind == TokenKind::Delete ||
         kind == TokenKind::TypedText ||
         kind == TokenKind::Escape;
}

string joinTokens(const vector<DisplayToken>& tokens, bool tokenize) {
  string out;
  for (size_t i = 0; i < tokens.size(); i++) {
    if (tokenize && i > 0) out += ' ';
    out += tokens[i].text;
  }
  return out;
}

vector<vector<DisplayToken>> sectionTokens(const vector<DisplayToken>& tokens) {
  vector<vector<DisplayToken>> sections;
  vector<DisplayToken> currentMovement;

  auto flushMovement = [&]() {
    if (currentMovement.empty()) return;
    sections.push_back(std::move(currentMovement));
    currentMovement = {};
  };

  size_t i = 0;
  while (i < tokens.size()) {
    if (!isEditStart(tokens[i].kind)) {
      currentMovement.push_back(tokens[i]);
      i++;
      continue;
    }

    flushMovement();
    vector<DisplayToken> editTokens;
    while (i < tokens.size() && isEditContinuation(tokens[i].kind)) {
      editTokens.push_back(tokens[i]);
      i++;
    }
    sections.push_back(std::move(editTokens));
  }

  flushMovement();
  return sections;
}

vector<string> tokenizedLines(string_view seq, Options opts) {
  if (seq.empty()) {
    return {""};
  }

  auto parsed = parseSequence(seq);
  if (!parsed || parsed->empty()) {
    return {displayKeyNotation(seq)};
  }

  vector<DisplayToken> tokens;
  tokens.reserve(parsed->size());
  for (const TaggedToken& token : *parsed) {
    tokens.push_back(DisplayToken{
        .text = displayTokenText(token),
        .kind = token.kind,
    });
  }

  if (opts.sectionize) {
    const vector<vector<DisplayToken>> sections = sectionTokens(tokens);
    if (sections.size() > 1) {
      vector<string> out;
      out.reserve(sections.size());
      for (const auto& section : sections) {
        out.push_back(joinTokens(section, opts.tokenize));
      }
      return out;
    }
  }

  return {joinTokens(tokens, opts.tokenize)};
}

}  // namespace

string displayKeyNotation(string_view text) {
  string out;
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] != '<') {
      out += text[i];
      i++;
      continue;
    }

    const size_t close = text.find('>', i);
    if (close == string_view::npos) {
      out += text[i];
      i++;
      continue;
    }

    out += glyphForKeyNotation(text.substr(i, close - i + 1));
    i = close + 1;
  }
  return out;
}

string displayTypedText(string_view text) {
  string out;
  for (const string_view part : splitKeyNotationTypedText(text)) {
    const bool keyNotation =
        part.size() >= 2 && part.front() == '<' && part.back() == '>';
    out += keyNotation ? displayKeyNotation(part) : prettify(part);
  }
  return out;
}

string displayLiteralTypedText(string_view text) {
  return prettify(text);
}

vector<string> lines(string_view seq, Options opts) {
  if (!opts.tokenize && !opts.sectionize) {
    return {string(seq)};
  }
  return tokenizedLines(seq, opts);
}

string inlineText(string_view seq, Options opts) {
  opts.sectionize = false;
  const vector<string> rendered = tokenizedLines(seq, opts);
  return rendered.empty() ? "" : rendered.front();
}

string typedChunksInline(const vector<TypedChunk>& chunks, bool tokenize) {
  vector<DisplayToken> tokens;
  tokens.reserve(chunks.size());
  for (const TypedChunk& chunk : chunks) {
    string text = chunk.kind == "literal"
        ? displayLiteralTypedText(chunk.text)
        : displayKeyNotation(chunk.text);
    if (!text.empty()) {
      tokens.push_back(DisplayToken{
          .text = std::move(text),
          .kind = TokenKind::TypedText,
      });
    }
  }

  return joinTokens(tokens, tokenize);
}

vector<string> prefixedLines(
    string_view prefix,
    string_view seq,
    Options opts,
    string_view suffix) {
  vector<string> rendered = lines(seq, opts);
  const string first = rendered.empty() ? "" : rendered.front();
  vector<string> out;
  out.push_back(string(prefix) + first + string(suffix));

  const string continuation(prefix.size(), ' ');
  for (size_t i = 1; i < rendered.size(); i++) {
    out.push_back(continuation + rendered[i]);
  }
  return out;
}

}  // namespace VF::SequenceDisplay
