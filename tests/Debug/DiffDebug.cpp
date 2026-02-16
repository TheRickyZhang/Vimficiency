// tests/Debug/DiffDebug.cpp
//
// Standalone tool to visualize how Myers diff splits buffer changes into regions.
//
// Usage:
//   ./build/tests/vimficiency_diff_debug <initial> <goal>
//
// Buffers use \n literal for line breaks:
//   ./build/tests/vimficiency_diff_debug 'aaa\nbbb\nccc' 'ccc\nddd'
//
// Or pipe via stdin (two lines: initial then goal):
//   echo -e 'aaa\\nbbb\\nccc\nccc\\nddd' | ./build/tests/vimficiency_diff_debug

#include <iostream>
#include <string>

#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Utils/Lines.h"

using namespace std;

// Replace literal "\n" sequences with actual newlines
static string unescapeNewlines(const string& s) {
  string result;
  result.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'n') {
      result += '\n';
      i++;
    } else {
      result += s[i];
    }
  }
  return result;
}

// Make newlines visible in output
static string printable(const string& s) {
  string result;
  for (char c : s) {
    if (c == '\n')
      result += "\\n";
    else
      result += c;
  }
  return result;
}

static void printDiffs(const Lines& initial, const Lines& goal) {
  cout << "initial: " << initial << "  [" << printable(initial.flatten()) << "]" << endl;
  cout << "goal:    " << goal << "  [" << printable(goal.flatten()) << "]" << endl;
  cout << endl;

  auto diffs = Myers::calculate(initial, goal);

  cout << diffs.size() << " diff region(s):" << endl;
  for (size_t i = 0; i < diffs.size(); i++) {
    const auto& d = diffs[i];
    const char* kind = d.isPureInsertion() ? "INSERT"
                     : d.isPureDeletion()  ? "DELETE"
                                           : "REPLACE";
    cout << "  [" << i << "] " << kind << endl;
    cout << "      range: (" << d.beginPos.line << "," << d.beginPos.col << ")"
         << " -> (" << d.endPos.line << "," << d.endPos.col << ")" << endl;
    cout << "      del: \"" << printable(d.deletedText) << "\"" << endl;
    cout << "      ins: \"" << printable(d.insertedText) << "\"" << endl;
    cout << "      boundary: pre=\"" << d.boundary.prefix()
         << "\" suf=\"" << d.boundary.suffix() << "\""
         << " above=" << d.boundary.hasLinesAbove()
         << " below=" << d.boundary.hasLinesBelow() << endl;
  }

  // Verify round-trip
  Lines reconstructed = Myers::applyAllDiffState(diffs, initial);
  if (reconstructed.flatten() != goal.flatten()) {
    cout << endl << "WARNING: round-trip mismatch!" << endl;
    cout << "  expected: " << printable(goal.flatten()) << endl;
    cout << "  got:      " << printable(reconstructed.flatten()) << endl;
  }
}

int main(int argc, char* argv[]) {
  if (argc == 3) {
    // Command-line args
    string initialFlat = unescapeNewlines(argv[1]);
    string goalFlat = unescapeNewlines(argv[2]);
    Lines initial = Lines::unflatten(initialFlat);
    Lines goal = Lines::unflatten(goalFlat);
    printDiffs(initial, goal);
  } else if (argc == 1) {
    // Read from stdin: two lines
    string line1, line2;
    if (!getline(cin, line1) || !getline(cin, line2)) {
      cerr << "Usage: " << argv[0] << " <initial> <goal>" << endl;
      cerr << "  Buffers use \\n for line breaks." << endl;
      cerr << "  Example: " << argv[0] << " 'aaa\\nbbb\\nccc' 'ccc\\nddd'" << endl;
      cerr << "  Or pipe two lines via stdin." << endl;
      return 1;
    }
    Lines initial = Lines::unflatten(unescapeNewlines(line1));
    Lines goal = Lines::unflatten(unescapeNewlines(line2));
    printDiffs(initial, goal);
  } else {
    cerr << "Usage: " << argv[0] << " <initial> <goal>" << endl;
    cerr << "  Buffers use \\n for line breaks." << endl;
    cerr << "  Example: " << argv[0] << " 'aaa\\nbbb\\nccc' 'ccc\\nddd'" << endl;
    return 1;
  }

  return 0;
}
