// tests/Debug/DiffDebug.cpp
//
// Standalone tool to visualize how diff planners split buffer changes into regions.
//
// Usage:
//   ./build/tests/vimfy_diff_debug <initial> <goal>
//
// Buffers use \n literal for line breaks:
//   ./build/tests/vimfy_diff_debug 'aaa\nbbb\nccc' 'ccc\nddd'
//
// Or pipe via stdin (two lines: initial then goal):
//   echo -e 'aaa\\nbbb\\nccc\nccc\\nddd' | ./build/tests/vimfy_diff_debug

#include <iostream>
#include <string>

#include "Optimizer/DiffPlanner/DiffState.h"
#include "Optimizer/DiffPlanner/VimDiff.h"
#include "Optimizer/DiffPlanner/MyersDiff.h"
#include "Keyboard/Config.h"
#include "Types/Lines.h"
#include "Utils/PrettyText.h"

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

static void printDiffs(
    const char* name,
    const vector<DiffState>& diffs,
    const Lines& initial,
    const Lines& goal) {
  cout << name << ": " << diffs.size() << " diff region(s):" << endl;
  for (size_t i = 0; i < diffs.size(); i++) {
    const auto& d = diffs[i];
    const char* kind = d.isPureInsertion() ? "INSERT"
                     : d.isPureDeletion()  ? "DELETE"
                                           : "REPLACE";
    cout << "  [" << i << "] " << kind << endl;
    cout << "      range: (" << d.beginPos.line << "," << d.beginPos.col << ")"
         << " -> (" << d.endPos.line << "," << d.endPos.col << ")" << endl;
    cout << "      del: \"" << VF::prettify(d.deletedText) << "\"" << endl;
    cout << "      ins: \"" << VF::prettify(d.insertedText) << "\"" << endl;
    cout << "      boundary: pre=\"" << d.boundary.prefix()
         << "\" suf=\"" << d.boundary.suffix() << "\""
         << " above=" << d.boundary.hasLinesAbove()
         << " below=" << d.boundary.hasLinesBelow() << endl;
  }

  Lines reconstructed = MyersDiff::applyAllDiffState(diffs, initial);
  if (reconstructed.flatten() != goal.flatten()) {
    cout << endl << "WARNING: round-trip mismatch!" << endl;
    cout << "  expected: " << VF::prettify(goal.flatten()) << endl;
    cout << "  got:      " << VF::prettify(reconstructed.flatten()) << endl;
  }
}

static void printDiffs(const Lines& initial, const Lines& goal) {
  cout << "initial: " << initial << "  [" << VF::prettify(initial.flatten()) << "]" << endl;
  cout << "goal:    " << goal << "  [" << VF::prettify(goal.flatten()) << "]" << endl;
  cout << endl;

  printDiffs("Myers", MyersDiff::calculate(initial, goal), initial, goal);
  cout << endl;
  auto vimPlans = VimDiff::calculate(initial, goal, Config::uniform());
  printDiffs("Vim", vimPlans.empty() ? std::vector<DiffState>{} : vimPlans.front().diffs,
             initial, goal);
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
