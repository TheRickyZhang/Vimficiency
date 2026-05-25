// Safety property: parser-facing boundaries must handle arbitrary bytes by
// rejecting cleanly or returning data that is valid to inspect. Movement/edit
// parsers return string_view tokens borrowed from the input; snapshot parsing
// returns an owned Snapshot whose fields must all be readable.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Interpreter/EditInterpreter.h"
#include "Interpreter/MovementInterpreter.h"
#include "Session/Snapshot.h"

using namespace std;

namespace {

// `.WithSeeds(...)` below gives deterministic smoke inputs. Longer FuzzTest
// campaigns still generate from the domain and treat those seeds as corpus
// starting points.
// Separate FUZZ_TESTs are intentional here: fuzzing varies input bytes, while
// each parser has a different API surface and success invariant.

bool viewIsInside(string_view child, string_view parent) {
  auto childBegin = reinterpret_cast<uintptr_t>(child.data());
  auto childEnd = childBegin + child.size();
  auto parentBegin = reinterpret_cast<uintptr_t>(parent.data());
  auto parentEnd = parentBegin + parent.size();
  return childBegin >= parentBegin && childEnd <= parentEnd;
}

size_t snapshotFieldFingerprint(const Snapshot& snapshot) {
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

template <typename ParseFn, typename FormatErrorFn, typename TokenFn, typename TouchFn>
void expectRejectsOrReturnsBorrowedTokens(
    string sequence,
    ParseFn parse,
    FormatErrorFn formatError,
    TokenFn tokenText,
    TouchFn touchParsedToken) {
  const string_view view(sequence);
  const auto parsed = parse(view);
  if (!parsed) {
    EXPECT_LE(parsed.error().offset, view.size());
    EXPECT_FALSE(formatError(parsed.error()).empty());
    return;
  }

  for (const auto& token : *parsed) {
    touchParsedToken(token);
    string_view text = tokenText(token);
    if (text.empty()) continue;

    EXPECT_TRUE(viewIsInside(text, view));
  }
}

void MovementParserRejectsInvalidInputOrReturnsBorrowedTokens(string sequence) {
  expectRejectsOrReturnsBorrowedTokens(
      std::move(sequence),
      [](string_view input) { return parseMovements(input); },
      [](const auto& error) { return formatMovementParseError(error); },
      [](const ParsedMovement& movement) { return movement.motion; },
      [](const ParsedMovement& movement) { (void)movement.effectiveCount(); });
}

void EditParserRejectsInvalidInputOrReturnsBorrowedTokens(string sequence) {
  expectRejectsOrReturnsBorrowedTokens(
      std::move(sequence),
      [](string_view input) { return Edit::parseEdits(input); },
      [](const auto& error) { return Edit::formatEditParseError(error); },
      [](const ParsedEdit& edit) { return edit.edit; },
      [](const ParsedEdit& edit) { (void)edit.effectiveCount(); });
}

void SnapshotParserRejectsInvalidInputOrReturnsReadableSnapshot(string bytes) {
  const auto parsed = parseSnapshot(bytes);
  if (!parsed) {
    EXPECT_FALSE(formatSnapshotParseError(parsed.error()).empty());
    return;
  }

  // The parser accepts arbitrary bytes only if it can construct a complete
  // Snapshot. Read every field so sanitizer runs catch bad ownership or
  // uninitialized-field regressions.
  [[maybe_unused]] size_t fingerprint = snapshotFieldFingerprint(*parsed);
}

}  // namespace

FUZZ_TEST(
    ParserBoundarySafetyTest,
    MovementParserRejectsInvalidInputOrReturnsBorrowedTokens)
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

        // Malformed, partial, or oversized command shapes.
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
    EditParserRejectsInvalidInputOrReturnsBorrowedTokens)
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

        // Malformed, partial, or oversized edit shapes.
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
    SnapshotParserRejectsInvalidInputOrReturnsReadableSnapshot)
    .WithDomains(fuzztest::String().WithMaxSize(1024))
    .WithSeeds({
        "",
        "not-snapshot",
        "\x80",
        "\xffsnapshot",

        // Accepted snapshots and near-misses for each required section.
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
