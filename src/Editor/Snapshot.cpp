#include <cassert>
#include <fstream>

#include "Snapshot.h"

using namespace std;

Snapshot load_snapshot(const std::filesystem::path& path) {
  ifstream in(path);
  if(!in) assert(false && "Can't read");

  string header;
  if(!getline(in, header)) {
    assert(false && "Snapshot empty");
  }

  string magic;
  int version;
  {
    istringstream ss(header);
    if(! (ss >> magic >> version)) {
      assert(false && "bad header");
    }
  }
  if(magic != "vimficiency" || version != 1) {
    assert(false && "unsupported version");
  }

  string filename;
  if(!getline(in, filename)) {
    assert(false && "No filename");
  }
  
  string bufname;
  if(!getline(in, bufname)) {
    assert(false && "No buffer name");
  }

  int row, col;
  string rowcol;
  if(!getline(in, rowcol)) {
    assert(false && "No row or col");
  }
  {
    istringstream ss(rowcol);
    if(!(ss >> row >> col)) {
      assert(false && "Huh");
    }
  }

  int topRow, bottomRow, windowHeight, scrollAmount;
  string navContext;
  if(!getline(in, navContext)) {
    assert(false && "No navContext");
  }
  {
    istringstream ss(navContext);
    if(!(ss >> topRow >> bottomRow >> windowHeight >> scrollAmount)) {
      assert(false && "Bad navContext");
    }
  }

  Lines lines;
  string line;
  while(getline(in, line)) {
    lines.push_back(line);
  }

  Snapshot s(std::move(bufname), std::move(filename), row, col,
             topRow, bottomRow, windowHeight, scrollAmount, std::move(lines));
  return s;
}
