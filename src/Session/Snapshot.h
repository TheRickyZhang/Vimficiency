#include <filesystem>
#include <expected>
#include <string>
#include <string_view>
#include "Types/Lines.h"

struct Snapshot {
  std::string bufname;
  std::string filetype;
  int row;
  int col;
  int topRow;
  int bottomRow;
  int windowHeight;
  int scrollAmount;
  Lines lines;

  Snapshot(std::string bufname, std::string filetype, int row, int col,
           int topRow, int bottomRow, int windowHeight, int scrollAmount,
           Lines lines)
      : bufname(std::move(bufname)), filetype(std::move(filetype)), row(row),
        col(col), topRow(topRow), bottomRow(bottomRow),
        windowHeight(windowHeight), scrollAmount(scrollAmount),
        lines(std::move(lines)) {}
};

enum class SnapshotParseErrorKind {
  Empty,
  BadHeader,
  UnsupportedVersion,
  MissingFilename,
  MissingBufferName,
  MissingCursorPosition,
  InvalidCursorPosition,
  MissingNavContext,
  InvalidNavContext,
};

struct SnapshotParseError {
  SnapshotParseErrorKind kind;
};

std::string formatSnapshotParseError(const SnapshotParseError& error);
std::expected<Snapshot, SnapshotParseError> parseSnapshot(std::string_view bytes);
Snapshot load_snapshot(const std::filesystem::path &path);
