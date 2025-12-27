#include "TestUtils.h"

using namespace std;
vector<string> readLines(istream &in) {
  vector<string> lines;
  string line;
  while(getline(in, line)) {
    lines.push_back(line);
  }
  return lines;
}
