#include "VimUtils.h"

#include <algorithm>
#include <cassert>
#include <cctype>

#include "Editor/Position.h"

using namespace std;


// -----------------------------------------------------------------------------
// Classification helpers (mirror Vim semantics)
// -----------------------------------------------------------------------------

// Small "word": sequence of [A-Za-z0-9_] (approximating 'iskeyword')
// Big "WORD": sequence of any non-blank (non-space, non-tab, non-newline) chars.
//
// Newline ('\n') is treated as blank for motions.
// -----------------------------------------------------------------------------

static bool isBlank(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\n';
}

static bool isSmallWordChar(unsigned char c) {
  return std::isalnum(c) || c == '_';
}

static bool isBigWordChar(unsigned char c) {
  return c != 0 && !isBlank(c) && c != '\n';
}

static bool isBlankLineStr(const std::string& s){
  for(unsigned char c: s){
    if(c!=' ' && c!='\t') return false;
  }
  return true; // empty or whitespace-only
}

static int firstNonBlankColInLineStr(const std::string& s){
  for(int i=0;i<(int)s.size();++i){
    unsigned char c = (unsigned char)s[i];
    if(c!=' ' && c!='\t') return i;
  }
  return 0;
}


// Char at (line,col):
// - Returns 0 only if line is out of range.
// - Returns '\n' if col is outside the actual line (used as "newline/blank"
//   sentinel for word motions).
static unsigned char getChar(const std::vector<std::string> &lines,
                                int line,
                                int col) {
  int n = static_cast<int>(lines.size());
  if(line < 0 || line >= n) return 0;
  const auto &ln = lines[line];
  if(col < 0 || col >= static_cast<int>(ln.size())) return '\n';
  return static_cast<unsigned char>(ln[col]);
}

// Step forward one character in the logical buffer (across lines).
// Returns false if already at (or past) the last character.
static bool stepFwd(const std::vector<std::string> &lines, int &line, int &col) {
  int n = static_cast<int>(lines.size());
  if(line < 0 || line >= n) return false;
  const auto &ln = lines[line];
  int len = static_cast<int>(ln.size());
  if(col + 1 < len) {
    ++col;
    return true;
  }
  if(line + 1 < n) {
    ++line;
    col = 0;
    return true;
  }
  return false;
}

// Step backward one character in the logical buffer (across lines).
// Returns false if already at (or before) the first character.
static bool stepBack(const std::vector<std::string> &lines, int &line, int &col) {
  int n = static_cast<int>(lines.size());
  if(line < 0 || line >= n) return false;
  if(col > 0) {
    --col;
    return true;
  }
  if(line > 0) {
    --line;
    col = static_cast<int>(lines[line].size());
    if(col > 0) --col; // last real character of previous line (if any)
    return true;
  }
  return false;
}

// Paragraph helpers

// Returns the first line index of the paragraph containing lineIdx.
// If lineIdx is on blank lines, the "paragraph" is the contiguous blank-line run.
static int paragraphStartLine(const std::vector<std::string>& lines, int lineIdx){
  int n = (int)lines.size();
  if(n==0) return 0;
  lineIdx = std::clamp(lineIdx, 0, n-1);

  bool blank = isBlankLineStr(lines[lineIdx]);
  int i = lineIdx;
  while(i>0 && isBlankLineStr(lines[i-1])==blank) --i;
  return i;
}

// Returns the last line index of the paragraph containing lineIdx.
// If lineIdx is on blank lines, the "paragraph" is the contiguous blank-line run.
static int paragraphEndLine(const std::vector<std::string>& lines, int lineIdx){
  int n = (int)lines.size();
  if(n==0) return 0;
  lineIdx = std::clamp(lineIdx, 0, n-1);

  bool blank = isBlankLineStr(lines[lineIdx]);
  int i = lineIdx;
  while(i+1<n && isBlankLineStr(lines[i+1])==blank) ++i;
  return i;
}

void VimUtils::motion_paragraphPrev(Position& pos, const std::vector<std::string>& lines) {
  int n=(int)lines.size(); if(n==0) return;
  pos.line=std::clamp(pos.line,0,n-1);
  int s=paragraphStartLine(lines,pos.line);
  int i=s-1;
  while(i>=0 && isBlankLineStr(lines[i])) --i;
  if(i<0) return;
  pos.line=paragraphStartLine(lines,i);
  pos.setCol(firstNonBlankColInLineStr(lines[pos.line]));
}

void VimUtils::motion_paragraphNext(Position& pos, const std::vector<std::string>& lines) {
  int n=(int)lines.size(); if(n==0) return;
  pos.line=std::clamp(pos.line,0,n-1);
  int e=paragraphEndLine(lines,pos.line);
  int i=e+1;
  while(i<n && isBlankLineStr(lines[i])) ++i;
  if(i>=n) return;
  pos.line=paragraphStartLine(lines,i);
  pos.setCol(firstNonBlankColInLineStr(lines[pos.line]));
}

static bool isSentenceCloser(unsigned char c){
  return c==')' || c==']' || c=='"' || c=='\'';
}

// Sentence end at (line,col): . ! ?  then optional closers  then (EOL or space/tab)
static bool isSentenceEndAt(const std::vector<std::string>& lines,int line,int col){
  unsigned char c = getChar(lines,line,col);
  if(c==0) return false;
  if(c!='.' && c!='!' && c!='?') return false;

  int l=line, k=col;
  while(true){
    int nl=l, nk=k;
    if(!stepFwd(lines,nl,nk)) return true;     // EOF after punctuation => boundary
    if(nl!=l) return true;                     // EOL after punctuation/closers => boundary

    unsigned char d = getChar(lines,nl,nk);
    if(isSentenceCloser(d)){
      l=nl; k=nk;                              // consume closer
      continue;
    }
    return d==' ' || d=='\t';                  // boundary only if space/tab
  }
}

// Compute start of sentence containing (line,col) using a backward scan.
// Simplified: treat blank-line runs as sentence boundaries too.
static std::pair<int,int> findSentenceStart(const std::vector<std::string>& lines,int line,int col){
  int n=(int)lines.size();
  if(n==0) return {0,0};

  line = std::clamp(line, 0, n-1);
  if((int)lines[line].size()==0) col=0;
  else col = std::clamp(col, 0, (int)lines[line].size()-1);

  // If on blank line run, move up to last nonblank char before it (if any).
  while(line>0 && isBlankLineStr(lines[line])){
    --line;
    col = (int)lines[line].size();
    if(col>0) --col;
  }

  int l=line, k=col;

  // Scan backward for a boundary:
  //  - a sentence end punctuation per isSentenceEndAt
  //  - OR crossing into a blank-line run
  while(true){
    // boundary: previous char ends a sentence
    if(isSentenceEndAt(lines,l,k)){
      int sl=l, sk=k;
      // move to char after boundary
      if(!stepFwd(lines,sl,sk)) return {l,k};

      // skip closers (same line only)
      while(true){
        unsigned char c = getChar(lines,sl,sk);
        if(!isSentenceCloser(c)) break;
        int tl=sl, tk=sk;
        if(!stepFwd(lines,tl,tk)) break;
        if(tl!=sl) break;
        sl=tl; sk=tk;
      }

      // skip spaces/tabs and blank lines
      while(true){
        if(sl>=n) return {n-1,0};
        if(isBlankLineStr(lines[sl])){
          ++sl; sk=0;
          continue;
        }
        int len=(int)lines[sl].size();
        if(len==0){
          ++sl; sk=0;
          continue;
        }
        sk = std::clamp(sk, 0, len-1);
        unsigned char c=(unsigned char)lines[sl][sk];
        if(c==' ' || c=='\t'){
          if(!stepFwd(lines,sl,sk)) break;
          continue;
        }
        break;
      }

      if(sl>=n) return {n-1,0};
      return {sl, firstNonBlankColInLineStr(lines[sl])};
    }

    // boundary: if current position is on first nonblank line after blank run,
    // we'd have found a punctuation earlier; this keeps behavior simple.
    int pl=l, pk=k;
    if(!stepBack(lines,pl,pk)) break;
    l=pl; k=pk;

    // if we just stepped onto a blank line, treat that as boundary and return
    if(isBlankLineStr(lines[l])){
      // skip blank run forward to first nonblank
      while(l<n && isBlankLineStr(lines[l])) ++l;
      if(l>=n) return {n-1,0};
      return {l, firstNonBlankColInLineStr(lines[l])};
    }
  }

  // If no boundary found, start at first nonblank line in file.
  int i=0;
  while(i<n && isBlankLineStr(lines[i])) ++i;
  if(i>=n) return {n-1,0};
  return {i, firstNonBlankColInLineStr(lines[i])};
}


/*
* ------------------------------ BEGIN VimUtils ------------------------------ 
*/



// Fundamental helpers for working with position
int VimUtils::clampCol(const std::vector<std::string> &lines,
                       int col,
                       int lineIdx) {
  int n = static_cast<int>(lines.size());
  assert(lineIdx >= 0 && lineIdx < n);
  int len = static_cast<int>(lines[lineIdx].size());
  if(len == 0) return 0;
  return std::clamp(col, 0, len - 1);
}

void VimUtils::moveCol(Position &pos,
                       const std::vector<std::string> &lines,
                       int dx) {
  pos.setCol(clampCol(lines, pos.col + dx, pos.line));
}

void VimUtils::moveLine(Position &pos,
                        const std::vector<std::string> &lines,
                        int dy) {
  int n = static_cast<int>(lines.size());
  pos.line = std::clamp(pos.line + dy, 0, n - 1);
  pos.setCol(clampCol(lines, pos.col, pos.line));
}


// -----------------------------------------------------------------------------
// Word motions: w, b, e / W, B, E
//
// Goal: approximate Vim’s word semantics.
//
//  - w/W: move to start of next "word"/"WORD" :contentReference[oaicite:2]{index=2}
//  - b/B: move to start of current word if inside it; otherwise previous.
//  - e/E: move to end of current word if not already at end; else next.
// -----------------------------------------------------------------------------

void VimUtils::motion_w(Position &pos,
                        const std::vector<std::string> &lines,
                        bool big) {
  int line = pos.line;
  int col  = pos.col;

  auto isWord = [big](unsigned char c) {
    return big ? isBigWordChar(c) : isSmallWordChar(c);
  };

  unsigned char c0 = getChar(lines, line, col);
  if(c0 == 0) {
    // Out of buffer
    pos.line = line;
    pos.col  = col;
    return;
  }

  // Case 1: starting on blank (space/tab/newline) → skip blanks to next
  // non-blank.
  if(isBlank(c0)) {
    while(true) {
      if(!stepFwd(lines, line, col)) {
        pos.line = line;
        pos.col  = col;
        return;
      }
      unsigned char c = getChar(lines, line, col);
      if(!isBlank(c)) break;
    }
    pos.line = line;
    pos.col  = col;
    return;
  }

  // Case 2: starting on non-blank (either "keyword word" or "symbol word").
  bool inWord = isWord(c0);

  // Skip the *current* word/anti-word group.
  while(true) {
    int oldLine = line;
    int oldCol  = col;

    if(!stepFwd(lines, line, col)) {
      // Reached EOF; stay there.
      pos.line = line;
      pos.col  = col;
      return;
    }

    unsigned char c = getChar(lines, line, col);

    // Line wrap acts like hitting a newline: we consider that a boundary.
    bool wrapped = (line != oldLine);
    if(wrapped) {
      break;
    }

    // Boundary: whitespace or transition between word/non-word groups.
    if(isBlank(c)) break;
    if(isWord(c) != inWord) break;
  }

  // Now at the first character *after* the skipped group, which might be:
  //  - start of next group (word or "symbol word")
  //  - whitespace
  unsigned char c = getChar(lines, line, col);
  if(!isBlank(c)) {
    // Start of next (word|anti-word) group: this is where Vim's 'w' lands
    // when there is no intermediate whitespace.
    pos.line = line;
    pos.col  = col;
    return;
  }

  // Otherwise, we're on whitespace → skip blanks to first non-blank.
  while(true) {
    if(!stepFwd(lines, line, col)) {
      pos.line = line;
      pos.col  = col;
      return;
    }
    c = getChar(lines, line, col);
    if(!isBlank(c)) break;
  }

  pos.line = line;
  pos.col  = col;
}

void VimUtils::motion_b(Position &pos,
                        const std::vector<std::string> &lines,
                        bool big) {
  int line = pos.line;
  int col  = pos.col;

  auto isWord = [big](unsigned char c) {
    return big ? isBigWordChar(c) : isSmallWordChar(c);
  };

  unsigned char c = getChar(lines, line, col);
  if(c == 0) {
    pos.line = line;
    pos.col  = col;
    return;
  }

  // Step 1: if on blank, move backward over blanks.
  if(isBlank(c)) {
    while(true) {
      if(!stepBack(lines, line, col)) {
        pos.line = line;
        pos.col  = col;
        return;
      }
      c = getChar(lines, line, col);
      if(!isBlank(c)) break;
    }
  }

  // Now we are on a non-blank character (word or symbol group).
  c = getChar(lines, line, col);
  if(c == 0) {
    pos.line = line;
    pos.col  = col;
    return;
  }

  bool inWord = isWord(c);

  // Step 2: move left while previous character is in the same group and
  // non-blank, so we end at the *first* char of that word/anti-word.
  while(true) {
    int prevLine = line;
    int prevCol  = col;
    if(!stepBack(lines, prevLine, prevCol)) break;

    unsigned char pc = getChar(lines, prevLine, prevCol);
    if(isBlank(pc)) break;
    if(isWord(pc) != inWord) break;

    line = prevLine;
    col  = prevCol;
  }

  pos.line = line;
  pos.col  = col;
}

void VimUtils::motion_e(Position &pos,
                        const std::vector<std::string> &lines,
                        bool big) {
  int line = pos.line;
  int col  = pos.col;

  auto isWord = [big](unsigned char c) {
    return big ? isBigWordChar(c) : isSmallWordChar(c);
  };

  // Step 1: move forward one character.
  // This aligns with Vim's "current-or-next word end" semantics. :contentReference[oaicite:3]{index=3}
  if(!stepFwd(lines, line, col)) {
    pos.line = line;
    pos.col  = col;
    return;
  }

  unsigned char c = getChar(lines, line, col);

  // Step 2: skip blanks (spaces, tabs, logical newlines/empty lines).
  while(isBlank(c)) {
    if(!stepFwd(lines, line, col)) {
      pos.line = line;
      pos.col  = col;
      return;
    }
    c = getChar(lines, line, col);
  }

  if(c == 0) {
    pos.line = line;
    pos.col  = col;
    return;
  }

  bool inWord = isWord(c);

  // Step 3: now at first char of the target word/anti-word.
  // Advance until the last character of that group.
  while(true) {
    int nextLine = line;
    int nextCol  = col;
    if(!stepFwd(lines, nextLine, nextCol)) {
      // EOF → current position is already last char.
      break;
    }

    unsigned char nc = getChar(lines, nextLine, nextCol);
    if(isBlank(nc)) break;
    if(isWord(nc) != inWord) break;

    line = nextLine;
    col  = nextCol;
  }

  pos.line = line;
  pos.col  = col;
}

// Move to the "top edge" (start) of the current paragraph.
// If currently on blank lines, goes to first blank line in that run.
void VimUtils::moveToParagraphStart(Position& pos, const std::vector<std::string>& lines){
  int n = (int)lines.size();
  if(n==0) return;
  pos.line = std::clamp(pos.line, 0, n-1);
  pos.line = paragraphStartLine(lines, pos.line);

  // For non-blank paragraph, go to first non-blank column on that line.
  if(!isBlankLineStr(lines[pos.line])) pos.setCol(firstNonBlankColInLineStr(lines[pos.line]));
  else pos.setCol(0);
}

// Move to the "bottom edge" (end) of the current paragraph.
// If currently on blank lines, goes to last blank line in that run.
void VimUtils::moveToParagraphEnd(Position& pos, const std::vector<std::string>& lines){
  int n = (int)lines.size();
  if(n==0) return;
  pos.line = std::clamp(pos.line, 0, n-1);
  pos.line = paragraphEndLine(lines, pos.line);

  // Keep column in bounds.
  pos.setCol(clampCol(lines, pos.col, pos.line));
}



void VimUtils::motion_sentenceNext(Position& pos,const std::vector<std::string>& lines){
  int n=(int)lines.size();
  if(n==0) return;

  int line = std::clamp(pos.line, 0, n-1);
  int col  = (int)lines[line].size()==0 ? 0 : std::clamp(pos.col, 0, (int)lines[line].size()-1);

  // If currently on blank run: jump to next nonblank line start.
  if(isBlankLineStr(lines[line])){
    while(line<n && isBlankLineStr(lines[line])) ++line;
    if(line>=n) return;
    pos.line=line;
    pos.setCol(firstNonBlankColInLineStr(lines[line]));
    return;
  }

  int l=line, k=col;
  while(true){
    if(isSentenceEndAt(lines,l,k)){
      // move to start of next sentence
      if(!stepFwd(lines,l,k)) return;

      // skip closers (same line only)
      while(true){
        unsigned char c = getChar(lines,l,k);
        if(!isSentenceCloser(c)) break;
        int tl=l, tk=k;
        if(!stepFwd(lines,tl,tk)) break;
        if(tl!=l) break;
        l=tl; k=tk;
      }

      // skip spaces/tabs and blank lines
      while(true){
        if(l>=n) return;
        if(isBlankLineStr(lines[l])){
          ++l; k=0;
          continue;
        }
        int len=(int)lines[l].size();
        if(len==0){
          ++l; k=0;
          continue;
        }
        k = std::clamp(k, 0, len-1);
        unsigned char c=(unsigned char)lines[l][k];
        if(c==' ' || c=='\t'){
          if(!stepFwd(lines,l,k)) return;
          continue;
        }
        break;
      }

      if(l>=n) return;
      pos.line=l;
      pos.setCol(firstNonBlankColInLineStr(lines[l]));
      return;
    }

    if(!stepFwd(lines,l,k)) return;
  }
}

void VimUtils::motion_sentencePrev(Position& pos,const std::vector<std::string>& lines){
  int n=(int)lines.size();
  if(n==0) return;

  auto [sl, sc] = findSentenceStart(lines, pos.line, pos.col);

  // If already at sentence start, go to previous sentence start.
  if(sl==pos.line && sc==pos.col){
    int l=sl, k=sc;
    if(stepBack(lines,l,k)){
      auto [psl, psc] = findSentenceStart(lines, l, k);
      pos.line=psl;
      pos.setCol(psc);
      return;
    }
  }

  pos.line=sl;
  pos.setCol(sc);
}
