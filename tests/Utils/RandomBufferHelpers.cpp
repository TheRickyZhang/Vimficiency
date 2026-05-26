#include "Utils/RandomBufferHelpers.h"

#include <algorithm>

#include "Utils/RandomGeneration.h"

using namespace std;

namespace {

char randomLetter() {
  return RandomGen::pick(RANDOM_LETTERS);
}

string randomPatternWord(int len) {
  if (len <= 0) return "";

  char a = randomLetter();
  char b = randomLetter();
  string word(len, a);

  switch (RandomGen::range(0, 3)) {
    case 0:
      return randomWord(len);
    case 1:
      return word;
    case 2:
      for (int i = 0; i < len; i++) word[i] = (i % 2 == 0) ? a : b;
      return word;
    default:
      for (int i = len / 2; i < len; i++) word[i] = b;
      return word;
  }
}

string randomSentence() {
  int wordCount = RandomGen::range(3, 8);
  string sentence;
  for (int i = 0; i < wordCount; i++) {
    if (!sentence.empty()) sentence += ' ';
    sentence += randomPatternWord(RandomGen::range(1, 8));
  }

  sentence += RandomGen::pick<string_view>({
      {85, "."},
      {10, "!"},
      {5, "?"},
  });
  return sentence;
}

}  // namespace

string randomWord(int len) {
  string result;
  result.resize_and_overwrite(len, [](char* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
      s[i] = RandomGen::pick(RANDOM_LETTERS);
    }
    return n;
  });
  return result;
}

string randomHighSpaceLine(int len) {
  string line;
  line.resize_and_overwrite(len, [](char* s, int n) {
    for (int i = 0; i < n; i++) {
      s[i] = RandomGen::pick<string_view>({
          {50, RANDOM_SPACE},
          {30, RANDOM_LETTERS},
          {20, RANDOM_SYMBOLS},
      });
    }
    return n;
  });
  return line;
}

string randomProseLine(int len) {
  string line;
  line.resize_and_overwrite(len, [](char* s, int n) {
    for (int i = 0; i < n; i++) {
      s[i] = RandomGen::pick<string_view>({
          {80, RANDOM_LETTERS},
          {16, RANDOM_SPACE},
          {4, RANDOM_SYMBOLS},
      });
    }
    return n;
  });
  return line;
}

string randomLine(int len) {
  string line;
  line.resize_and_overwrite(len, [](char* s, int n) {
    for (int i = 0; i < n; i++) {
      s[i] = RandomGen::pick<string_view>({
          {60, RANDOM_LETTERS},
          {16, RANDOM_SPACE},
          {24, RANDOM_SYMBOLS},
      });
    }
    return n;
  });
  return line;
}

Lines randomLines(int numLines, int minLen, int maxLen) {
  Lines result;
  result.reserve(numLines);
  for (int i = 0; i < numLines; i++) {
    result.push_back(randomLine(RandomGen::range(minLen, maxLen)));
  }
  return result;
}

Lines randomProseLines(int numLines, int minLen, int maxLen) {
  Lines result;
  result.reserve(numLines);
  for (int i = 0; i < numLines; i++) {
    result.push_back(randomProseLine(RandomGen::range(minLen, maxLen)));
  }
  return result;
}

Lines randomProseBuffer(int targetLines) {
  Lines result;
  result.reserve(targetLines);

  int sentencesInParagraph = 0;
  int sentencesForBreak = RandomGen::range(3, 6);
  bool atParagraphStart = true;

  while (result.size() < static_cast<size_t>(targetLines)) {
    string line;
    if (atParagraphStart) {
      line = "  ";
      atParagraphStart = false;
    }

    while (line.size() < 60 || (line.size() < 100 && RandomGen::chance(1, 2))) {
      if (!line.empty() && line.back() != ' ' && line.size() > 2) {
        line += ' ';
      }
      line += randomSentence();
      sentencesInParagraph++;
      if (sentencesInParagraph >= sentencesForBreak) break;
    }

    result.push_back(line);

    if (sentencesInParagraph >= sentencesForBreak &&
        result.size() < static_cast<size_t>(targetLines - 1)) {
      result.push_back("");
      sentencesInParagraph = 0;
      sentencesForBreak = RandomGen::range(3, 6);
      atParagraphStart = true;
    }
  }

  return result;
}

Lines randomCodeBuffer(int numLines, int avgLineLen) {
  Lines result;
  result.reserve(numLines);

  int currentIndent = 0;
  constexpr int INDENT_SIZE = 2;

  for (int i = 0; i < numLines; i++) {
    if (RandomGen::chance(1, 10)) {
      result.push_back("");
      continue;
    }

    if (RandomGen::chance(1, 20)) {
      string comment = string(currentIndent, ' ') + "// ";
      int commentLen = RandomGen::range(10, avgLineLen);
      for (int j = 0; j < commentLen; j++) {
        comment += RandomGen::pick<string_view>({
            {4, RANDOM_LETTERS},
            {1, RANDOM_SPACE},
        });
      }
      result.push_back(comment);
      continue;
    }

    string line = string(currentIndent, ' ');

    int contentLen = RandomGen::range(avgLineLen / 2, avgLineLen * 3 / 2);
    for (int j = 0; j < contentLen; j++) {
      line += RandomGen::pick<string_view>({
          {3, RANDOM_LETTERS},
          {1, RANDOM_SYMBOLS},
          {1, RANDOM_SPACE},
      });
    }

    int endPattern = RandomGen::range(0, 19);
    if (endPattern < 3) {
      line += RandomGen::chance(1, 2) ? " {" : " (";
      currentIndent = min(currentIndent + INDENT_SIZE, 8);
    } else if (endPattern < 5 && currentIndent > 0) {
      currentIndent = max(currentIndent - INDENT_SIZE, 0);
      line = string(currentIndent, ' ');
      line += RandomGen::chance(1, 2) ? "}" : ")";
      if (RandomGen::chance(1, 2)) line += ";";
    } else if (endPattern < 8) {
      line += ";";
    } else if (endPattern < 10) {
      line += ",";
    }

    result.push_back(line);
  }

  return result;
}

int randomLineIndex(const Lines& lines) {
  return RandomGen::range(0, lines.lastLine());
}

int randomCol(string_view line) {
  return line.empty() ? 0 : RandomGen::range(0, static_cast<int>(line.size()) - 1);
}

int randomInsertCol(string_view line) {
  return RandomGen::range(0, static_cast<int>(line.size()));
}

CursorPos randomPos(const Lines& lines) {
  int line = randomLineIndex(lines);
  return CursorPos(line, randomCol(lines[line]));
}

CursorPos randomFirstPos(const Lines& lines) {
  return CursorPos(0, RandomGen::range(0, lines.front().effectiveSize() - 1));
}

CursorPos randomLastPos(const Lines& lines) {
  int lastLine = lines.lastLine();
  return CursorPos(lastLine, RandomGen::range(0, lines[lastLine].effectiveSize() - 1));
}
