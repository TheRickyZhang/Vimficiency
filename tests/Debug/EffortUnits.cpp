// One-off probe for the planner-unit question: what does one keystroke cost in
// RunningEffort units? VimDiff's del/move oracles price flat keystrokes (times
// moveDeleteScale) while its ins term is real RunningEffort; the measured
// effort-per-keystroke ratio is moveDeleteScale's calibration target.
//
// Run: ./build/tests/vimfy_debug --gtest_filter="EffortUnits.*"

#include <gtest/gtest.h>

#include <iomanip>
#include <iostream>
#include <string_view>

#include "Effort/RunningEffort.h"
#include "Keyboard/Config.h"
#include "Keyboard/KeyedSequence.h"

using namespace std;

namespace {

void probe(const char* cfgName, const Config& config) {
  cout << "--- config: " << cfgName << " ---\n" << fixed << setprecision(3);
  auto show = [&](const char* name, string_view text) {
    KeyedSequence ks;
    ks.append(text);
    const double effort = RunningEffort(ks.keys, config).getEffort(config);
    const double keys = (double)ks.keys.size();
    cout << "  " << setw(8) << left << name << " keys=" << setw(3) << (int)keys
         << " effort=" << setw(8) << effort << " effort/key=" << effort / keys
         << "\n";
  };
  show("prose", "the quick brown fox jumps over the lazy dog");
  show("code", "for (int i = 0; i < n; i++) sweep(a, scale);");
  show("caps", "VimDiff::calculate(initialLines, goalLines);");
  show("x", "x");
  show("dw", "dw");
  show("de", "de");
  show("dd", "dd");
  show("w", "w");
  show("3dw", "3dw");
  show("5j", "5j");
  show("12dd", "12dd");
}

}  // namespace

TEST(EffortUnits, PerKeystroke) {
  probe("qwerty", Config::qwerty());
  probe("uniform", Config::uniform());
}
