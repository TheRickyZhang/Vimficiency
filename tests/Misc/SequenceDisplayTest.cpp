#include <gtest/gtest.h>

#include "Interpreter/SequenceDisplay.h"
#include "Utils/PrettyText.h"

#include <string>
#include <vector>

using namespace std;

TEST(SequenceDisplayTest, LinesTokenizeAndSectionizeByDefault) {
  EXPECT_EQ(
      VF::SequenceDisplay::lines("3wciwfoo<Esc>2j"),
      (vector<string>{"3w", "ciw foo <Esc>", "2j"}));
}

TEST(SequenceDisplayTest, LinesKeepRawSectionsWhenTokenizeFalse) {
  EXPECT_EQ(
      VF::SequenceDisplay::lines(
          "3wciwfoo<Esc>2j",
          {.tokenize = false, .sectionize = true}),
      (vector<string>{"3w", "ciwfoo<Esc>", "2j"}));
}

TEST(SequenceDisplayTest, LinesFlattenWhenSectionizeFalse) {
  EXPECT_EQ(
      VF::SequenceDisplay::lines(
          "3wciwfoo<Esc>2j",
          {.tokenize = true, .sectionize = false}),
      (vector<string>{"3w ciw foo <Esc> 2j"}));
}

TEST(SequenceDisplayTest, InlineStillFormatsKeyNotationWhenTokenizeFalse) {
  EXPECT_EQ(
      VF::SequenceDisplay::inlineText(
          "f<Space>l",
          {.tokenize = false, .sectionize = true}),
      "f" + string(VF::PrettyText::SPACE) + "l");
}

TEST(SequenceDisplayTest, PrettyPrintsTypedTextAndKeyNotation) {
  EXPECT_EQ(
      VF::SequenceDisplay::lines("cW2<Space>*<Space>i<Esc>"),
      (vector<string>{"cW 2" + string(VF::PrettyText::SPACE) + "*" +
                      string(VF::PrettyText::SPACE) + "i <Esc>"}));
  EXPECT_EQ(VF::SequenceDisplay::inlineText("f<Space>l"),
            "f" + string(VF::PrettyText::SPACE) + " l");
  EXPECT_EQ(VF::SequenceDisplay::inlineText("f<lt>l"), "f< l");
  EXPECT_EQ(VF::SequenceDisplay::inlineText("r<Tab>"),
            "r" + string(VF::PrettyText::TAB));
}

TEST(SequenceDisplayTest, TypedTextHelpersMatchLuaDisplaySemantics) {
  EXPECT_EQ(
      VF::SequenceDisplay::displayTypedText("<Tab>a<CR>b"),
      string(VF::PrettyText::TAB) + "a" +
          string(VF::PrettyText::NEWLINE) + "b");
  EXPECT_EQ(
      VF::SequenceDisplay::displayLiteralTypedText("2 * i"),
      "2" + string(VF::PrettyText::SPACE) + "*" +
          string(VF::PrettyText::SPACE) + "i");
  EXPECT_EQ(
      VF::SequenceDisplay::displayLiteralTypedText("<Space>"),
      "<Space>");
}

TEST(SequenceDisplayTest, TypedChunksJoinDisplayedParts) {
  EXPECT_EQ(
      VF::SequenceDisplay::typedChunksInline({
          {.kind = "key", .text = "<BS>"},
          {.kind = "literal", .text = " * i"},
      }),
      "<BS> " + string(VF::PrettyText::SPACE) + "*" +
          string(VF::PrettyText::SPACE) + "i");
}

TEST(SequenceDisplayTest, PrefixedLinesIndentContinuations) {
  EXPECT_EQ(
      VF::SequenceDisplay::prefixedLines("User seq: ", "3wciwfoo<Esc>2j"),
      (vector<string>{
          "User seq: 3w",
          "          ciw foo <Esc>",
          "          2j",
      }));
}
