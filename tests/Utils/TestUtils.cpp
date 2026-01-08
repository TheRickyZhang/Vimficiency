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

bool contains_all(const vector<Result>& v, initializer_list<string> need){
  unordered_set<string> s;
  for(Result r : v) s.insert(r.getSequenceString());
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
