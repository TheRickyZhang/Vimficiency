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
  for(Result r : v) s.insert(r.getSequenceString());
  for(const auto& x : need) if(s.find(x)==s.end()) return false;
  return true;
}

// Replace invisible characters with visible symbols for display
static string makePrintable(const string& seq) {
  string result;
  result.reserve(seq.size() * 3);  // UTF-8 symbols may be multi-byte
  for (char c : seq) {
    if (c == ' ') {
      result += "␣";  // U+2423 OPEN BOX
    } else if (c == '\t') {
      result += "⇥";  // U+21E5 RIGHTWARDS ARROW TO BAR
    } else {
      result += c;
    }
  }
  return result;
}

void printResults(vector<Result>& results) {
  cout << "Results (" << results.size() << ") : " <<  endl;
  for (const auto& r : results) {
    cout << makePrintable(r.getSequenceString()) << " ";
  }
  cout << endl;
}
