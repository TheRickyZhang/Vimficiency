#include <filesystem>
#include <string>
#include <vector>
#include "Utils/Lines.h"

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

Snapshot load_snapshot(const std::filesystem::path &path);
