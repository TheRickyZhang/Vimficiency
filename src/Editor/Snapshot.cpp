#include <fstream>

#include "Snapshot.h"

using namespace std;

Snapshot load_snapshot(const std::filesystem::path& path) {
  ifstream in(path);
  if(!in) throw runtime_error("Can't read");

  string header;
  if(!getline(in, header)) {
    throw runtime_error("Snapshot empty");
  }

  string magic;
  int version;
  {
    istringstream ss(header);
    if(! (ss >> magic >> version)) {
      throw runtime_error("bad header");
    }
  }
  if(magic != "vimficiency" || version != 1) {
    throw runtime_error("unsupported version");
  }

  string filename;
  if(!getline(in, filename)) {
    throw runtime_error("No filename");
  }
  
  string bufname;
  if(!getline(in, bufname)) {
    throw runtime_error("No buffer name");
  }

  int row, col;
  string rowcol;
  if(!getline(in, rowcol)) {
    throw runtime_error("No row or col");
  } 
  {
    istringstream ss(rowcol);
    if(!(ss >> row >> col)) {
      throw runtime_error("Huh");
    }
  }

  vector<string> lines;
  string line;
  while(getline(in, line)) {
    lines.push_back(line);
  }

  Snapshot s(std::move(bufname), std::move(filename), row, col, std::move(lines));
  return s;
}
