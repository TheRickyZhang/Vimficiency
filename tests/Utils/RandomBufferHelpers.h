// tests/Utils/RandomBufferHelpers.h
//
// CORE random buffer generation utilities shared across all test suites.
// Uses RandomGen singleton for all randomness.

#pragma once

#include "Editor/Position.h"
#include "Utils/Lines.h"
#include "Utils/RandomGeneration.h"

#include <string>
#include <string_view>

// Character pools for random buffer generation.
// Using string_view so they satisfy Indexable concept (have .size() and operator[]).
namespace CharPools {
  constexpr std::string_view LETTERS = "abcdef"; // keyword class
  constexpr std::string_view SYMBOLS = ".,";   // non-keyword, non-space
  constexpr std::string_view SPACE = " ";      // space (others include \r, \t)
}

// Word pools for prose generation
namespace WordPools {
  constexpr std::array NOUNS = {
    "code", "function", "buffer", "line", "word", "cursor", "file", "editor",
    "motion", "command", "sequence", "position", "text", "paragraph", "sentence"
  };
  constexpr std::array VERBS = {
    "runs", "calls", "returns", "finds", "moves", "edits", "deletes", "inserts",
    "jumps", "searches", "copies", "pastes", "reads", "writes", "updates"
  };
  constexpr std::array ADJS = {
    "quick", "small", "new", "old", "fast", "slow", "simple", "complex",
    "first", "last", "next", "empty", "full", "valid", "current"
  };
  constexpr std::array ADVERBS = {
    "quickly", "slowly", "often", "rarely", "always", "never", "usually",
    "sometimes", "efficiently", "correctly"
  };
}


inline std::string randomWord(int len) {
  std::string result;
  result.resize_and_overwrite(len, [](char* s, size_t n){
    for (int i = 0; i < n; i++) {
      s[i] = RandomGen::pick(CharPools::LETTERS);
    }
    return n;
  });
  return result;
}

inline std::string randomProseLine(int len) {
  std::string line;
  line.resize_and_overwrite(len, [](char* s, int n) {
    for (int i = 0; i < n; i++) {
      s[i] = RandomGen::pick<std::string_view>({
          {80, CharPools::LETTERS},
          {16, CharPools::SPACE},
          {4, CharPools::SYMBOLS},
      });
    }
    return n;
  });
  return line;
}

inline std::string randomLine(int len) {
  std::string line;
  line.resize_and_overwrite(len, [](char* s, int n) {
    for(int i = 0; i < n; i++) {
        s[i] = RandomGen::pick<std::string_view>({
          {60, CharPools::LETTERS},
          {16, CharPools::SPACE},
          {24, CharPools::SYMBOLS},
      });
    }
    return n;
  });
  return line;
}

inline Lines randomLines(int numLines, int minLen, int maxLen) {
  Lines result;
  result.reserve(numLines);
  for (int i = 0; i < numLines; i++) {
    result.push_back(randomLine(RandomGen::range(minLen, maxLen)));
  }
  return result;
}

inline Lines randomProseLines(int numLines, int minLen, int maxLen) {
  Lines result;
  result.reserve(numLines);
  for (int i = 0; i < numLines; i++) {
    result.push_back(randomProseLine(RandomGen::range(minLen, maxLen)));
  }
  return result;
}

// =============================================================================
// Prose Buffer Generation
// =============================================================================

// Generate a random sentence (5-12 words, ends with . ! or ?)
inline std::string randomSentence() {
  int wordCount = RandomGen::range(5, 12);
  std::string sentence;

  // First word is capitalized
  auto capitalize = [](std::string s) {
    if (!s.empty()) s[0] = std::toupper(s[0]);
    return s;
  };

  // Simple sentence patterns:
  // 1. "The [adj] [noun] [verb] the [noun]."
  // 2. "[Noun] [verb] [adverb]."
  // 3. "The [noun] [verb] the [adj] [noun]."
  int pattern = RandomGen::range(0, 2);

  if (pattern == 0) {
    sentence = "The " + std::string(RandomGen::pick(WordPools::ADJS)) + " " +
               std::string(RandomGen::pick(WordPools::NOUNS)) + " " +
               std::string(RandomGen::pick(WordPools::VERBS)) + " the " +
               std::string(RandomGen::pick(WordPools::NOUNS));
  } else if (pattern == 1) {
    sentence = capitalize(std::string(RandomGen::pick(WordPools::NOUNS))) + " " +
               std::string(RandomGen::pick(WordPools::VERBS)) + " " +
               std::string(RandomGen::pick(WordPools::ADVERBS));
  } else {
    sentence = "The " + std::string(RandomGen::pick(WordPools::NOUNS)) + " " +
               std::string(RandomGen::pick(WordPools::VERBS)) + " the " +
               std::string(RandomGen::pick(WordPools::ADJS)) + " " +
               std::string(RandomGen::pick(WordPools::NOUNS));
  }

  // Add extra words to reach target word count
  int currentWords = 5; // Base patterns have ~5 words
  while (currentWords < wordCount) {
    int extraType = RandomGen::range(0, 2);
    if (extraType == 0) {
      sentence += " and " + std::string(RandomGen::pick(WordPools::VERBS));
      currentWords += 2;
    } else if (extraType == 1) {
      sentence += " with " + std::string(RandomGen::pick(WordPools::ADJS)) + " " +
                  std::string(RandomGen::pick(WordPools::NOUNS));
      currentWords += 3;
    } else {
      sentence += " " + std::string(RandomGen::pick(WordPools::ADVERBS));
      currentWords += 1;
    }
  }

  // Add sentence-ending punctuation
  constexpr std::string_view PERIODS = ".";
  constexpr std::string_view EXCLAIMS = "!";
  constexpr std::string_view QUESTIONS = "?";
  char ending = RandomGen::pick<std::string_view>({{85, PERIODS}, {10, EXCLAIMS}, {5, QUESTIONS}});
  sentence += ending;

  return sentence;
}

// Generate prose buffer with natural sentence/paragraph breaks
inline Lines randomProseBuffer(int targetLines) {
  Lines result;
  result.reserve(targetLines);

  int sentencesInParagraph = 0;
  int sentencesForBreak = RandomGen::range(3, 6);
  bool atParagraphStart = true;

  while (static_cast<int>(result.size()) < targetLines) {
    std::string line;

    // Indent at paragraph start
    if (atParagraphStart) {
      line = "  ";
      atParagraphStart = false;
    }

    // Add sentences until line is reasonably long (60-100 chars)
    while (line.size() < 60 || (line.size() < 100 && RandomGen::chance(1, 2))) {
      if (!line.empty() && line.back() != ' ' && line.size() > 2) {
        line += " ";
      }
      line += randomSentence();
      sentencesInParagraph++;

      // Check for paragraph break
      if (sentencesInParagraph >= sentencesForBreak) {
        break;
      }
    }

    result.push_back(line);

    // Add paragraph break (blank line)
    if (sentencesInParagraph >= sentencesForBreak &&
        static_cast<int>(result.size()) < targetLines - 1) {
      result.push_back("");
      sentencesInParagraph = 0;
      sentencesForBreak = RandomGen::range(3, 6);
      atParagraphStart = true;
    }
  }

  return result;
}

// =============================================================================
// Code-Like Buffer Generation
// =============================================================================

inline Lines randomCodeBuffer(int numLines, int avgLineLen) {
  Lines result;
  result.reserve(numLines);

  int currentIndent = 0;  // Logical indent level (0, 2, 4, ...)
  constexpr int INDENT_SIZE = 2;

  for (int i = 0; i < numLines; i++) {
    // 10% chance of blank line
    if (RandomGen::chance(1, 10)) {
      result.push_back("");
      continue;
    }

    // 5% chance of comment line
    if (RandomGen::chance(1, 20)) {
      std::string comment = std::string(currentIndent, ' ') + "// ";
      int commentLen = RandomGen::range(10, avgLineLen);
      for (int j = 0; j < commentLen; j++) {
        comment += RandomGen::pick<std::string_view>({
            {4, CharPools::LETTERS},
            {1, CharPools::SPACE},
        });
      }
      result.push_back(comment);
      continue;
    }

    std::string line = std::string(currentIndent, ' ');

    // Generate line content
    int contentLen = RandomGen::range(avgLineLen / 2, avgLineLen * 3 / 2);
    for (int j = 0; j < contentLen; j++) {
      line += RandomGen::pick<std::string_view>({
          {3, CharPools::LETTERS},
          {1, CharPools::SYMBOLS},
          {1, CharPools::SPACE},
      });
    }

    // Line ending patterns
    int endPattern = RandomGen::range(0, 19);
    if (endPattern < 3) {
      // 15% end with opening brace/paren - increase indent
      line += RandomGen::chance(1, 2) ? " {" : " (";
      currentIndent = std::min(currentIndent + INDENT_SIZE, 8);
    } else if (endPattern < 5 && currentIndent > 0) {
      // 10% end with closing brace/paren - decrease indent
      currentIndent = std::max(currentIndent - INDENT_SIZE, 0);
      line = std::string(currentIndent, ' ');  // Rebuild with new indent
      line += RandomGen::chance(1, 2) ? "}" : ")";
      if (RandomGen::chance(1, 2)) line += ";";
    } else if (endPattern < 8) {
      // 15% end with semicolon
      line += ";";
    } else if (endPattern < 10) {
      // 10% end with comma
      line += ",";
    }
    // 50% end naturally (no special ending)

    result.push_back(line);
  }

  return result;
}

// =============================================================================
// Random Position Generation
// =============================================================================

// Generate a random valid position within the given buffer
inline Position randomPosition(const Lines& lines) {
  int line = RandomGen::range(0, static_cast<int>(lines.size()) - 1);
  int col = lines[line].empty() ? 0 : RandomGen::range(0, static_cast<int>(lines[line].size()) - 1);
  return Position(line, col);
}

inline Position randomFirstPos(const Lines& lines) {
  return Position(0, RandomGen::range(0, lines.front().effectiveSize() - 1));
}

inline Position randomLastPos(const Lines& lines) {
  int lastLine = lines.lastLine();
  return Position(lastLine, RandomGen::range(0, lines[lastLine].effectiveSize() - 1));
}
