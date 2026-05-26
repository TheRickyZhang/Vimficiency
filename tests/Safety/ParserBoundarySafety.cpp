// Safety property: parser-facing boundaries reject arbitrary bytes cleanly or
// return readable data. Movement/edit tokens must borrow from the input.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Interpreter/EditInterpreter.h"
#include "Interpreter/MovementInterpreter.h"
#include "Session/Snapshot.h"

using namespace std;

namespace {

pair<uintptr_t, uintptr_t> addressRange(string_view view) {
  const uintptr_t begin = reinterpret_cast<uintptr_t>(view.data());
  return {begin, begin + view.size()};
}

bool viewIsInside(string_view view, string_view parent) {
  const auto [begin, end] = addressRange(view);
  const auto [parentBegin, parentEnd] = addressRange(parent);
  return begin >= parentBegin && end <= parentEnd;
}

size_t snapshotFingerprint(const Snapshot& snapshot) {
  size_t value = snapshot.bufname.size() ^ snapshot.filetype.size();
  value ^= static_cast<size_t>(snapshot.row);
  value ^= static_cast<size_t>(snapshot.col);
  value ^= static_cast<size_t>(snapshot.topRow);
  value ^= static_cast<size_t>(snapshot.bottomRow);
  value ^= static_cast<size_t>(snapshot.windowHeight);
  value ^= static_cast<size_t>(snapshot.scrollAmount);
  for (const auto& line : snapshot.lines) {
    value ^= line.size();
  }
  return value;
}

template <typename ParseFn, typename FormatErrorFn, typename TokenTextFn,
          typename TouchFn>
void expectBorrowedTokensOrError(
    string sequence,
    ParseFn parse,
    FormatErrorFn formatError,
    TokenTextFn tokenText,
    TouchFn touchToken) {
  const string_view view(sequence);
  const auto parsed = parse(view);
  if (!parsed) {
    EXPECT_LE(parsed.error().offset, view.size());
    EXPECT_FALSE(formatError(parsed.error()).empty());
    return;
  }

  for (const auto& token : *parsed) {
    touchToken(token);
    const string_view text = tokenText(token);
    if (text.empty()) continue;

    EXPECT_TRUE(viewIsInside(text, view));
  }
}

void MovementTokensBorrowInput(string sequence) {
  expectBorrowedTokensOrError(
      std::move(sequence),
      [](string_view input) { return parseMovements(input); },
      [](const auto& error) { return formatMovementParseError(error); },
      [](const ParsedMovement& movement) { return movement.motion; },
      [](const ParsedMovement& movement) { (void)movement.effectiveCount(); });
}

void EditTokensBorrowInput(string sequence) {
  expectBorrowedTokensOrError(
      std::move(sequence),
      [](string_view input) { return Edit::parseEdits(input); },
      [](const auto& error) { return Edit::formatEditParseError(error); },
      [](const ParsedEdit& edit) { return edit.edit; },
      [](const ParsedEdit& edit) { (void)edit.effectiveCount(); });
}

void SnapshotsAreReadable(string bytes) {
  const auto parsed = parseSnapshot(bytes);
  if (!parsed) {
    EXPECT_FALSE(formatSnapshotParseError(parsed.error()).empty());
    return;
  }

  [[maybe_unused]] const size_t fingerprint = snapshotFingerprint(*parsed);
}

}  // namespace

FUZZ_TEST(
    ParserBoundarySafetyTest,
    MovementTokensBorrowInput)
    .WithDomains(fuzztest::String().WithMaxSize(256))
    .WithSeeds({
        "",
        "w",
        "3j",
        "gg",
        "10G",
        "ge",
        "gE",
        "f,",
        "t<",
        ";",
        "2;",
        "<C-d>",

        // Malformed commands.
        "f",
        "2f",
        "<",
        "<C-",
        "<Bad>",
        "999999999999w",
        "\xffw",
    });

FUZZ_TEST(
    ParserBoundarySafetyTest,
    EditTokensBorrowInput)
    .WithDomains(fuzztest::String().WithMaxSize(256))
    .WithSeeds({
        "",
        "x",
        "X",
        "dw",
        "d$",
        "dd",
        "ciwhello<Esc>",
        "sabc<Esc>",
        "vwd",
        "<Esc>",

        // Malformed edits.
        "d",
        "c",
        "r",
        "r<",
        "<",
        "<C-",
        "999999999999dd",
        "\xffx",
    });

FUZZ_TEST(
    ParserBoundarySafetyTest,
    SnapshotsAreReadable)
    .WithDomains(fuzztest::String().WithMaxSize(1024))
    .WithSeeds({
        "",
        "not-snapshot",
        "\x80",
        "\xffsnapshot",

        // Accepted snapshots and near-misses.
        "vimficiency 1\nfile.cpp\nbuffer\n0 0\n0 0 24 0\nline",
        "vimficiency 1\nfile.cpp\nbuffer\n0 0\n0 0 24 0\nline1\nline2",
        "vimficiency",
        "vimficiency 2\nfile.cpp\nbuffer\n0 0\n0 0 24 0\nline",
        "other 1\nfile.cpp\nbuffer\n0 0\n0 0 24 0\nline",
        "vimficiency 1",
        "vimficiency 1\nfile.cpp",
        "vimficiency 1\nfile.cpp\nbuffer",
        "vimficiency 1\nfile.cpp\nbuffer\nx y",
        "vimficiency 1\nfile.cpp\nbuffer\n0 0",
        "vimficiency 1\nfile.cpp\nbuffer\n0 0\nbad nav",
    });
