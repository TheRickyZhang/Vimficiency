#include <filesystem>
#include <string>
#include <vector>

struct Snapshot {
  std::string bufname;
  std::string filetype;
  int row;
  int col;
  std::vector<std::string> lines;

  Snapshot(std::string bufname, std::string filetype, int row, int col,
           std::vector<std::string> lines)
      : bufname(std::move(bufname)), filetype(std::move(filetype)), row(row),
        col(col), lines(std::move(lines)) {}
};

Snapshot load_snapshot(const std::filesystem::path &path);
