#include "TestUtils.h"

using namespace std;
// namespace fs = std::filesystem;

vector<string> readLines(istream &in) {
  vector<string> lines;
  string line;
  while(getline(in, line)) {
    lines.push_back(line);
  }
  return lines;
}

bool contains_all(const vector<string>& v, initializer_list<string> need){
  unordered_set<string> s(v.begin(), v.end());
  for(const auto& x : need) if(s.find(x)==s.end()) return false;
  return true;
}
