#include <string>
#include <string_view>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Interpreter/EditInterpreter.h"
#include "Interpreter/MovementInterpreter.h"
#include "Session/Snapshot.h"

using namespace std;

namespace {

void MovementParserRejectsOrReturnsViewsInsideInput(string sequence) {
  const string_view view(sequence);
  const auto parsed = parseMovements(view);
  if (!parsed) {
    EXPECT_LE(parsed.error().offset, view.size());
    EXPECT_FALSE(formatMovementParseError(parsed.error()).empty());
    return;
  }

  for (const ParsedMovement& movement : *parsed) {
    (void)movement.effectiveCount();
    if (movement.motion.empty()) continue;

    const auto* motionBegin = movement.motion.data();
    const auto* motionEnd = motionBegin + movement.motion.size();
    const auto* inputBegin = view.data();
    const auto* inputEnd = inputBegin + view.size();
    EXPECT_GE(motionBegin, inputBegin);
    EXPECT_LE(motionEnd, inputEnd);
  }
}

void EditParserRejectsOrReturnsViewsInsideInput(string sequence) {
  const string_view view(sequence);
  const auto parsed = Edit::parseEdits(view);
  if (!parsed) {
    EXPECT_LE(parsed.error().offset, view.size());
    EXPECT_FALSE(Edit::formatEditParseError(parsed.error()).empty());
    return;
  }

  for (const ParsedEdit& edit : *parsed) {
    (void)edit.effectiveCount();
    if (edit.edit.empty()) continue;

    const auto* editBegin = edit.edit.data();
    const auto* editEnd = editBegin + edit.edit.size();
    const auto* inputBegin = view.data();
    const auto* inputEnd = inputBegin + view.size();
    EXPECT_GE(editBegin, inputBegin);
    EXPECT_LE(editEnd, inputEnd);
  }
}

void SnapshotParserRejectsOrExposesFields(string bytes) {
  const auto parsed = parseSnapshot(bytes);
  if (!parsed) {
    EXPECT_FALSE(formatSnapshotParseError(parsed.error()).empty());
    return;
  }

  (void)parsed->bufname;
  (void)parsed->filetype;
  (void)parsed->row;
  (void)parsed->col;
  (void)parsed->topRow;
  (void)parsed->bottomRow;
  (void)parsed->windowHeight;
  (void)parsed->scrollAmount;
  (void)parsed->lines;
}

}  // namespace

FUZZ_TEST(ParserBoundaryFuzzTest, MovementParserRejectsOrReturnsViewsInsideInput)
    .WithDomains(fuzztest::String().WithMaxSize(256))
    .WithSeeds({"", "w", "3j", "f,", "<C-d>", "999999999999w", "\xffw"});

FUZZ_TEST(ParserBoundaryFuzzTest, EditParserRejectsOrReturnsViewsInsideInput)
    .WithDomains(fuzztest::String().WithMaxSize(256))
    .WithSeeds({"", "x", "dw", "ciwhello<Esc>", "999999999999dd", "\xffx"});

FUZZ_TEST(ParserBoundaryFuzzTest, SnapshotParserRejectsOrExposesFields)
    .WithDomains(fuzztest::String().WithMaxSize(1024))
    .WithSeeds({"", "not-msgpack", "\x80", "\xffsnapshot"});
