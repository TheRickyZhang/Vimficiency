#include "TestUtils.h"


// #include "Utils/Debug.h"
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

bool contains_all(const vector<Result>& v, initializer_list<string> need){
  unordered_set<string> s;
  for(Result r : v) s.insert(r.sequence);
  for(const auto& x : need) if(s.find(x)==s.end()) return false;
  return true;
}

// Verify that this is the right semantic responsilbiity to put here
void debugResult(vector<string>& results) {
  cout << "Results (" << results.size() << ") : " <<  endl;
  for (const auto& r : results) {
    cout << r << " ";
  }
  cout << endl;
}
