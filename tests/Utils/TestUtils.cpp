#include "TestUtils.h"

#include <unordered_set>

using namespace std;


Lines readLines(istream &in) {
  Lines lines;
  string line;
  while(getline(in, line)) {
    lines.push_back(line);
  }
  return lines;
}

bool contains_all(const vector<Result>& v, initializer_list<string> need){
  unordered_set<string> s;
  for(const auto& r : v) s.insert(r.sequence.str());
  for(const auto& x : need) if(s.find(x)==s.end()) return false;
  return true;
}

void printResults(vector<Result>& results) {
  cout << "Results (" << results.size() << ") : " <<  endl;
  for (const auto& r : results) {
    cout << r << " ";  // operator<< already escapes special chars
  }
  cout << endl;
}
