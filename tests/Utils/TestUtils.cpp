#include "TestUtils.h"

using namespace std;


Lines readLines(istream &in) {
  Lines lines;
  string line;
  while(getline(in, line)) {
    lines.push_back(line);
  }
  return lines;
}
