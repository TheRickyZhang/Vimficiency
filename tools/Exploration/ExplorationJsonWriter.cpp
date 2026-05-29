#include "Exploration/ExplorationCollector.h"

#include <fstream>
#include <iostream>

using namespace std;

string jsonEscape(string_view s) {
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

vector<string> tokenizeSequence(const string& seq) {
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

void writeContextJson(ofstream& out, const ContextData& ctx) {
  out << "      \"context\": {\n";
  out << "        \"initialLines\": [";
  for (size_t i = 0; i < ctx.initialLines.size(); i++) {
    if (i > 0) out << ", ";
    out << "\"" << jsonEscape(ctx.initialLines[i]) << "\"";
  }
  out << "],\n";
  out << "        \"goalLines\": [";
  for (size_t i = 0; i < ctx.goalLines.size(); i++) {
    if (i > 0) out << ", ";
    out << "\"" << jsonEscape(ctx.goalLines[i]) << "\"";
  }
  out << "]";
  if (ctx.initialCursorLine >= 0) {
    out << ",\n        \"initialCursor\": [" << ctx.initialCursorLine
        << ", " << ctx.initialCursorCol << "]";
  }
  if (ctx.goalCursorLine >= 0) {
    out << ",\n        \"goalCursor\": [" << ctx.goalCursorLine
        << ", " << ctx.goalCursorCol << "]";
  }
  if (ctx.goalRangeBeginLine >= 0) {
    out << ",\n        \"goalRange\": [" << ctx.goalRangeBeginLine
        << ", " << ctx.goalRangeBeginCol << ", " << ctx.goalRangeEndLine
        << ", " << ctx.goalRangeEndCol << "]";
  }
  if (!ctx.prefix.empty()) {
    out << ",\n        \"prefix\": \"" << jsonEscape(ctx.prefix) << "\"";
  }
  if (!ctx.suffix.empty()) {
    out << ",\n        \"suffix\": \"" << jsonEscape(ctx.suffix) << "\"";
  }
  if (ctx.hasLinesAbove) {
    out << ",\n        \"hasLinesAbove\": true";
  }
  if (ctx.hasLinesBelow) {
    out << ",\n        \"hasLinesBelow\": true";
  }
  out << "\n      },\n";
}

void writeExplorationJson(const string& filename,
                                  const vector<ExploreCase>& cases) {
  ofstream out(filename);
  out << "{\n  \"cases\": [\n";
  for (size_t i = 0; i < cases.size(); i++) {
    const auto& ec = cases[i];
    out << "    {\n";
    out << "      \"name\": \"" << jsonEscape(ec.name) << "\",\n";
    out << "      \"nodesExplored\": " << ec.stats.nodesExplored() << ",\n";
    writeContextJson(out, ec.context);

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
    for (size_t j = 0; j < ec.stats.exploredStates().size(); j++) {
      const auto& s = ec.stats.exploredStates()[j];
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
