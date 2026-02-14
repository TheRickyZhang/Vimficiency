// tests/Exploration/ExplorationCollector.cpp
//
// Standalone binary that collects explored A* states from representative
// benchmark cases and outputs JSON for the dashboard visualization.
//
// Usage:
//   VIMFICIENCY_SEED_MODE=fixed ./build/tests/vimficiency_explore
//
// Outputs: motion_explore.json, edit_explore.json, composition_explore.json

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "Boundary/EditBoundary.h"
#include "Boundary/MotionBoundary.h"
#include "Optimizer/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Optimizer/EditOptimizer/EditOptimizerParams.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Optimizer/MotionOptimizer/MotionOptimizerParams.h"
#include "Optimizer/SearchStats.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"
#include "Utils/SeedManager.h"

using namespace std;

static Config config = Config::uniform();

// =============================================================================
// Case data: stats + found results
// =============================================================================

struct FoundResult {
  string sequence;
  double effort;
};

struct ExploreCase {
  string name;
  SearchStats stats;
  vector<FoundResult> results;
};

// =============================================================================
// Minimal JSON writer
// =============================================================================

static string jsonEscape(string_view s) {
  string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out;
}

static string truncateSeq(const string& seq, size_t maxLen = 20) {
  if (seq.size() <= maxLen) return seq;
  return seq.substr(0, maxLen) + "...";
}

// ---------------------------------------------------------------------------
// Tokenize a Vim command sequence into individual command strings.
// Uses greedy longest-prefix matching against the known command vocabulary
// (derived from XMacroKeyedSequenceDefinitions.h and EditToKeys).
// ---------------------------------------------------------------------------

static vector<string> tokenizeSequence(const string& seq) {
  // Multi-char tokens, longest first for greedy matching.
  static const vector<string> MULTI_CHAR = {
      // Special keys
      "<C-u>", "<C-d>", "<BS>", "<Del>", "<Esc>", "<CR>",
      // g-prefixed
      "ge", "gE", "gg", "gJ",
      // d-operator + motion
      "dd", "dj", "dk", "dw", "de", "db", "dW", "dB", "dE", "d$", "d0",
      // c-operator
      "cc",
      // Compound edits
      "hs", "0C",
  };

  vector<string> tokens;
  size_t i = 0;
  while (i < seq.size()) {
    // Try multi-char tokens first (greedy longest match)
    bool matched = false;
    for (const auto& tok : MULTI_CHAR) {
      if (i + tok.size() <= seq.size() && seq.compare(i, tok.size(), tok) == 0) {
        tokens.push_back(tok);
        i += tok.size();
        matched = true;
        break;
      }
    }
    if (matched) continue;

    // Count prefix: digit 1-9 followed by more digits then a command
    if (seq[i] >= '1' && seq[i] <= '9') {
      size_t j = i;
      while (j < seq.size() && seq[j] >= '0' && seq[j] <= '9') j++;
      if (j < seq.size()) {
        // Try multi-char command after count
        size_t cmdLen = 1;
        for (const auto& tok : MULTI_CHAR) {
          if (tok[0] != '<' && j + tok.size() <= seq.size() &&
              seq.compare(j, tok.size(), tok) == 0) {
            cmdLen = tok.size();
            break;
          }
        }
        // f/F/t/T after count: include target char
        if (cmdLen == 1 && string("fFtT").find(seq[j]) != string::npos &&
            j + 1 < seq.size()) {
          cmdLen = 2;
        }
        // r after count: include replacement char
        if (cmdLen == 1 && seq[j] == 'r' && j + 1 < seq.size() &&
            seq[j + 1] != '\x1b') {
          cmdLen = 2;
        }
        tokens.push_back(seq.substr(i, j - i + cmdLen));
        i = j + cmdLen;
      } else {
        tokens.push_back(seq.substr(i));
        i = seq.size();
      }
      continue;
    }

    // f/F/t/T + target char
    if (string("fFtT").find(seq[i]) != string::npos && i + 1 < seq.size()) {
      tokens.push_back(seq.substr(i, 2));
      i += 2;
      continue;
    }

    // r + replacement char
    if (seq[i] == 'r' && i + 1 < seq.size() && seq[i + 1] != '\x1b') {
      tokens.push_back(seq.substr(i, 2));
      i += 2;
      continue;
    }

    // Raw escape byte
    if (seq[i] == '\x1b') {
      tokens.push_back("<Esc>");
      i++;
      continue;
    }

    // Raw backspace byte
    if (seq[i] == '\x08') {
      tokens.push_back("<BS>");
      i++;
      continue;
    }

    // Single character
    tokens.push_back(string(1, seq[i]));
    i++;
  }
  return tokens;
}

static void writeExplorationJson(const string& filename,
                                  const vector<ExploreCase>& cases) {
  ofstream out(filename);
  out << "{\n  \"cases\": [\n";
  for (size_t i = 0; i < cases.size(); i++) {
    const auto& ec = cases[i];
    out << "    {\n";
    out << "      \"name\": \"" << jsonEscape(ec.name) << "\",\n";
    out << "      \"nodesExplored\": " << ec.stats.nodesExplored << ",\n";

    // Found results (optimal sequences)
    out << "      \"results\": [";
    for (size_t r = 0; r < ec.results.size(); r++) {
      if (r > 0) out << ",";
      auto tokens = tokenizeSequence(ec.results[r].sequence);
      out << "\n        {\"tokens\": [";
      for (size_t t = 0; t < tokens.size(); t++) {
        if (t > 0) out << ", ";
        out << "\"" << jsonEscape(tokens[t]) << "\"";
      }
      out << "], \"effort\": " << ec.results[r].effort << "}";
    }
    out << "\n      ],\n";

    // Explored states
    out << "      \"states\": [";
    for (size_t j = 0; j < ec.stats.exploredStates.size(); j++) {
      const auto& s = ec.stats.exploredStates[j];
      if (j > 0) out << ",";
      auto tokens = tokenizeSequence(s.sequence);
      out << "\n        {\"effort\": " << s.effort << ", \"tokens\": [";
      for (size_t t = 0; t < tokens.size(); t++) {
        if (t > 0) out << ", ";
        out << "\"" << jsonEscape(tokens[t]) << "\"";
      }
      out << "]}";
    }
    out << "\n      ]\n";
    out << "    }";
    if (i + 1 < cases.size()) out << ",";
    out << "\n";
  }
  out << "  ]\n}\n";
  cout << "Wrote " << filename << " (" << cases.size() << " cases)\n";
}

// =============================================================================
// Motion cases
// =============================================================================

static vector<ExploreCase> collectMotionCases() {
  vector<ExploreCase> cases;
  auto& seedMgr = SeedManager::instance();

  struct MotionCase {
    string name;
    int numLines;
    int avgLen;
  };

  vector<MotionCase> motionCases = {
    {"BufferSize/1", 1, 30},
    {"BufferSize/5", 5, 30},
    {"BufferSize/10", 10, 30},
    {"BufferSize/20", 20, 30},
    {"LineLength/10", 20, 10},
    {"LineLength/40", 20, 40},
    {"LineLength/80", 20, 80},
  };

  MotionOptimizerParams params;
  params.trackExploredStates = true;

  for (const auto& mc : motionCases) {
    RandomGen::seed(seedMgr.getSeed(0));
    Lines lines = randomCodeBuffer(mc.numLines, mc.avgLen);
    Position firstPos = randomFirstPos(lines);
    Position lastPos = randomLastPos(lines);
    Position boundaryEnd(lastPos.line, lastPos.col + 1);
    MotionBoundary boundary(lines, firstPos, boundaryEnd, true, true);

    MotionOptimizer opt(config);
    auto result = opt.optimize(lines, firstPos, lastPos, params, "", boundary);

    vector<FoundResult> found;
    for (const auto& r : result.results) {
      if (r.isValid()) {
        found.push_back({r.sequence.str(), r.keyCost});
      }
    }

    cases.push_back({mc.name, result.stats, std::move(found)});
  }

  return cases;
}

// =============================================================================
// Edit cases
// =============================================================================

static vector<ExploreCase> collectEditCases() {
  vector<ExploreCase> cases;
  auto& seedMgr = SeedManager::instance();

  EditOptimizerParams params;
  params.trackExploredStates = true;

  struct EditCase {
    string name;
    int numLines;
    int avgLen;
  };

  vector<EditCase> pureDeletionCases = {
    {"BufferSize/1", 1, 30},
    {"BufferSize/3", 3, 30},
    {"BufferSize/5", 5, 30},
    {"BufferSize/10", 10, 30},
    {"LineLength/10", 5, 10},
    {"LineLength/40", 5, 40},
    {"LineLength/60", 5, 60},
  };

  auto collectEditResults = [](const EditResult& result) {
    vector<FoundResult> found;
    // Deduplicate: EditResult has one result per starting position,
    // collect unique valid sequences sorted by effort
    map<string, double> bestBySeq;
    for (const auto& r : result.getResults()) {
      if (r.isValid()) {
        auto it = bestBySeq.find(r.sequence.str());
        if (it == bestBySeq.end() || r.keyCost < it->second) {
          bestBySeq[r.sequence.str()] = r.keyCost;
        }
      }
    }
    for (const auto& [seq, effort] : bestBySeq) {
      found.push_back({seq, effort});
    }
    sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
      return a.effort < b.effort;
    });
    // Keep top 10
    if (found.size() > 10) found.resize(10);
    return found;
  };

  for (const auto& ec : pureDeletionCases) {
    RandomGen::seed(seedMgr.getSeed(0));
    Lines lines = randomCodeBuffer(ec.numLines, ec.avgLen);
    EditBoundary boundary(lines, Position(0, 0), lines.endPos());
    auto p = params;
    p.maxResults = max(10, lines.totalPositions() / 4);

    EditOptimizer opt(config);
    auto result = opt.optimizePureDeletion(lines, boundary, p);
    cases.push_back({ec.name, result.stats, collectEditResults(result)});
  }

  // Multi-line edit case
  {
    RandomGen::seed(seedMgr.getSeed(0));
    Lines buffer = {"I saw a pig in barn in Switzerland", "Inconspicuous, even"};
    Lines goal = {"Florida"};
    EditBoundary boundary(buffer, Position(0, 23), Position(1, 19));
    Lines editRegion = buffer.getSpan(Position(0, 23), Position(1, 19));
    auto p = params;
    p.maxResults = max(10, editRegion.totalPositions() / 4);

    EditOptimizer opt(config);
    auto result = opt.optimizeEdit(editRegion, goal, boundary, p);
    cases.push_back({"MultiLineEdit/2L->1w", result.stats, collectEditResults(result)});
  }

  return cases;
}

// =============================================================================
// Composition cases
// =============================================================================

static Lines generateBuffer(int numLines, int avgLineLen) {
  return randomCodeBuffer(numLines, avgLineLen);
}

static vector<ExploreCase> collectCompositionCases() {
  vector<ExploreCase> cases;
  auto& seedMgr = SeedManager::instance();

  CompositionOptimizerParams params;
  params.trackExploredStates = true;

  auto makeDefaultSetup = [](int numLines, int avgLen, int editCount) {
    Lines initial = generateBuffer(numLines, avgLen);
    Lines goal = initial;
    for (int e = 0; e < editCount; e++) {
      int line = editCount <= 1 ? numLines / 2
                                : e * (numLines - 1) / max(1, editCount - 1);
      int len = max(1, static_cast<int>(initial[line].size()));
      goal[line] = randomWord(len);
      if (goal[line] == initial[line]) goal[line] = "changed";
    }
    return make_pair(initial, goal);
  };

  struct CompCase {
    string name;
    int numLines;
    int avgLen;
    int editCount;
  };

  vector<CompCase> compCases = {
    {"EditCount/1", 15, 20, 1},
    {"EditCount/2", 15, 20, 2},
    {"EditCount/5", 15, 20, 5},
    {"EditCount/8", 15, 20, 8},
    {"BufferSize/5", 5, 20, 5},
    {"BufferSize/10", 10, 20, 5},
    {"BufferSize/20", 20, 20, 5},
  };

  for (const auto& cc : compCases) {
    RandomGen::seed(seedMgr.getSeed(0));
    auto [initial, goal] = makeDefaultSetup(cc.numLines, cc.avgLen, cc.editCount);

    CompositionOptimizer opt(config);
    auto result = opt.optimize(initial, {0, 0}, goal, {0, 0}, params);

    vector<FoundResult> found;
    for (const auto& r : result.results) {
      if (r.isValid()) {
        found.push_back({r.sequence.str(), r.keyCost});
      }
    }

    cases.push_back({cc.name, result.stats, std::move(found)});
  }

  return cases;
}

// =============================================================================
// Main
// =============================================================================

int main() {
  auto& seedMgr = SeedManager::instance();
  cout << "Exploration collector\n";
  cout << "Seed mode: " << (seedMgr.isRandom() ? "random" : "fixed") << "\n\n";

  auto motionCases = collectMotionCases();
  writeExplorationJson("motion_explore.json", motionCases);

  auto editCases = collectEditCases();
  writeExplorationJson("edit_explore.json", editCases);

  auto compositionCases = collectCompositionCases();
  writeExplorationJson("composition_explore.json", compositionCases);

  cout << "\nDone.\n";
  return 0;
}
