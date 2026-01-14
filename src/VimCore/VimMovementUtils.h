#include <vector>
#include <string>

#include "EndpointType.h"

struct Position;

struct VimMovementUtils {
  // Fundamental helpers for working with position
  static int clampCol(const std::vector<std::string> &lines,
                      int col,
                      int lineIdx);
  static void moveCol(Position &pos,
                      const std::vector<std::string> &lines,
                      int dx);
  static void moveLine(Position &pos,
                       const std::vector<std::string> &lines,
                       int dy);

  // ==========================================================================
  // Word motions - general interface
  // ==========================================================================
  //
  // Unified word motion based on EndpointType, direction, and word type.
  // This is the fundamental building block; named motions forward to this.
  //
  // Mapping:
  //   Forward + End   -> e/E  (to end of word)
  //   Forward + Space -> w/W  (to start of next word)
  //   Backward + End  -> b/B  (to start of word)
  //   Backward + Next -> ge/gE (to end of previous word)
  //
  static void motionWord(Position &pos,
                         const std::vector<std::string> &lines,
                         bool forward,
                         EndpointType endpointType,
                         bool big);

  // Named word motion forwarders
  static void motionW(Position &pos,
                      const std::vector<std::string> &lines,
                      bool big);

  static void motionB(Position &pos,
                      const std::vector<std::string> &lines,
                      bool big);

  static void motionE(Position &pos,
                      const std::vector<std::string> &lines,
                      bool big);

  static void motionGe(Position &pos,
                       const std::vector<std::string> &lines,
                       bool big);

  // Paragraph motions
  static void moveToParagraphStart(Position& pos, const std::vector<std::string>& lines);
  static void moveToParagraphEnd(Position& pos, const std::vector<std::string>& lines);
  static void motionParagraphPrev(Position& pos, const std::vector<std::string>& lines);
  static void motionParagraphNext(Position& pos, const std::vector<std::string>& lines);

  // Sentence motions
  static void motionSentencePrev(Position& pos, const std::vector<std::string>& lines);
  static void motionSentenceNext(Position& pos, const std::vector<std::string>& lines);

  // Character find motions (f/F/t/T)
  // Returns destination column, or -1 if target not found
  // forward: true for f/t, false for F/T
  // till: true for t/T (stop one short), false for f/F (land on target)
  static int findCharInLine(char target, const std::string& line, int startCol, bool forward, bool till);

  template<bool Forward>
  static std::vector<std::tuple<char, int, int>> generateFMotions(int currCol, int targetCol, const std::string& line, int threshold);

};
