#include "Exploration/ExplorationCollector.h"

#include <fstream>
#include <iostream>
#include <unordered_map>

#include "Exploration/SequenceChunker.h"

using namespace std;

static void writeChunksJson(ofstream& out, const vector<SequenceChunk>& chunks) {
  out << "[";
  for (size_t c = 0; c < chunks.size(); c++) {
    if (c > 0) out << ", ";
    const auto& ch = chunks[c];
    out << "{\"type\": \"" << (ch.type == SequenceChunk::Movement ? "movement" : "edit")
        << "\", \"text\": \"" << jsonEscape(ch.text) << "\", \"tokens\": [";
    for (size_t t = 0; t < ch.tokens.size(); t++) {
      if (t > 0) out << ", ";
      out << "\"" << jsonEscape(ch.tokens[t]) << "\"";
    }
    out << "]";
    if (ch.contentId >= 0) {
      out << ", \"contentId\": " << ch.contentId;
    }
    out << "}";
  }
  out << "]";
}

void writeCompositionExplorationJson(
    const string& filename,
    const vector<CompositionExploreCase>& cases) {
  ofstream out(filename);
  out << "{\n  \"cases\": [\n";
  for (size_t i = 0; i < cases.size(); i++) {
    const auto& ec = cases[i];
    out << "    {\n";
    out << "      \"name\": \"" << jsonEscape(ec.name) << "\",\n";
    out << "      \"nodesExplored\": " << ec.nodesExplored << ",\n";
    writeContextJson(out, ec.context);

    // Diffs (character-level change regions) with per-diff edit exploration data
    out << "      \"diffs\": [";
    for (size_t d = 0; d < ec.diffs.size(); d++) {
      if (d > 0) out << ",";
      const auto& diff = ec.diffs[d];
      out << "\n        {\"beginLine\": " << diff.beginPos.line
          << ", \"beginCol\": " << diff.beginPos.col
          << ", \"endLine\": " << diff.endPos.line
          << ", \"endCol\": " << diff.endPos.col
          << ", \"deletedText\": \"" << jsonEscape(diff.deletedText) << "\""
          << ", \"insertedText\": \"" << jsonEscape(diff.insertedText) << "\"";

      // Per-diff edit exploration data
      if (d < ec.editDetails.size()) {
        const auto& detail = ec.editDetails[d];

        out << ", \"editStates\": [";
        for (size_t s = 0; s < detail.states.size(); s++) {
          if (s > 0) out << ",";
          const auto& st = detail.states[s];
          auto tokens = tokenizeSequence(st.sequence);
          out << "\n          {\"effort\": " << st.effort << ", \"tokens\": [";
          for (size_t t = 0; t < tokens.size(); t++) {
            if (t > 0) out << ", ";
            out << "\"" << jsonEscape(tokens[t]) << "\"";
          }
          out << "]}";
        }
        out << "]";

        out << ", \"transformResults\": [";
        for (size_t r = 0; r < detail.results.size(); r++) {
          if (r > 0) out << ",";
          auto tokens = tokenizeSequence(detail.results[r].sequence);
          out << "\n          {\"effort\": " << detail.results[r].effort << ", \"tokens\": [";
          for (size_t t = 0; t < tokens.size(); t++) {
            if (t > 0) out << ", ";
            out << "\"" << jsonEscape(tokens[t]) << "\"";
          }
          out << "]}";
        }
        out << "]";
      }

      out << "}";
    }
    out << "\n      ],\n";

    // Chunk all results and collect global contents for this case
    vector<ChunkedSequence> chunkedResults;
    vector<string> globalContents;
    unordered_map<string, int> contentRemap;

    for (const auto& r : ec.results) {
      auto chunked = chunkCompositionSequence(r.sequence);
      // Remap content IDs to global
      for (auto& ch : chunked.chunks) {
        if (ch.contentId >= 0) {
          const string& content = chunked.contents[ch.contentId];
          auto it = contentRemap.find(content);
          if (it != contentRemap.end()) {
            ch.contentId = it->second;
          } else {
            int newId = static_cast<int>(globalContents.size());
            globalContents.push_back(content);
            contentRemap[content] = newId;
            ch.contentId = newId;
          }
        }
      }
      chunkedResults.push_back(std::move(chunked));
    }

    // Chunk all states too (to collect any additional contents)
    vector<ChunkedSequence> chunkedStates;
    for (const auto& s : ec.states) {
      auto chunked = chunkCompositionSequence(s.sequence);
      for (auto& ch : chunked.chunks) {
        if (ch.contentId >= 0) {
          const string& content = chunked.contents[ch.contentId];
          auto it = contentRemap.find(content);
          if (it != contentRemap.end()) {
            ch.contentId = it->second;
          } else {
            int newId = static_cast<int>(globalContents.size());
            globalContents.push_back(content);
            contentRemap[content] = newId;
            ch.contentId = newId;
          }
        }
      }
      chunkedStates.push_back(std::move(chunked));
    }

    // Contents
    out << "      \"contents\": [";
    for (size_t c = 0; c < globalContents.size(); c++) {
      if (c > 0) out << ", ";
      out << "\"" << jsonEscape(globalContents[c]) << "\"";
    }
    out << "],\n";

    // Results
    out << "      \"results\": [";
    for (size_t r = 0; r < ec.results.size(); r++) {
      if (r > 0) out << ",";
      out << "\n        {\"effort\": " << ec.results[r].effort << ", \"chunks\": ";
      writeChunksJson(out, chunkedResults[r].chunks);
      out << "}";
    }
    out << "\n      ],\n";

    // States
    out << "      \"states\": [";
    for (size_t j = 0; j < ec.states.size(); j++) {
      if (j > 0) out << ",";
      out << "\n        {\"effort\": " << ec.states[j].effort
          << ", \"editsCompleted\": " << ec.states[j].editsCompleted
          << ", \"chunks\": ";
      writeChunksJson(out, chunkedStates[j].chunks);
      out << "}";
    }
    out << "\n      ]\n";
    out << "    }";
    if (i + 1 < cases.size()) out << ",";
    out << "\n";
  }
  out << "  ]\n}\n";
  cout << "Wrote " << filename << " (" << cases.size() << " cases)\n";
}
