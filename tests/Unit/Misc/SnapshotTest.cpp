// tests/Unit/Misc/SnapshotTest.cpp
//
// Snapshot loading must fail safely on bad input (no assert-stripped UB in
// Release builds) and must never yield a zero-line buffer.
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="SnapshotTest.*"

#include <gtest/gtest.h>

#include "Session/Snapshot.h"

namespace {

// 5 header lines, no content section -> an empty buffer.
const char* kHeaderOnly = "vimficiency 1\nfile.txt\nbuf\n0 0\n0 10 11 5\n";

TEST(SnapshotTest, ParseEmptyBufferYieldsSingleEmptyLine) {
  auto parsed = parseSnapshot(kHeaderOnly);
  ASSERT_TRUE(parsed.has_value());
  ASSERT_EQ(parsed->lines.size(), 1u);
  EXPECT_EQ(parsed->lines[0], "");
}

TEST(SnapshotTest, LoadMissingFileReturnsError) {
  auto loaded = load_snapshot("/vimficiency/definitely/not/a/real/path.snap");
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().kind, SnapshotParseErrorKind::CannotRead);
}

TEST(SnapshotTest, ParseGarbageReturnsError) {
  auto parsed = parseSnapshot("not a snapshot at all");
  EXPECT_FALSE(parsed.has_value());
}

}  // namespace
