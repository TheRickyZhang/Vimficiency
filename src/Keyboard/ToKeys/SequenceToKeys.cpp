#include "SequenceToKeys.h"
#include "CharToKeys.h"
#include "Utils/Debug.h"

#include <algorithm>
#include <cstdio>

namespace {
// Render `[start, end)` of `s` as `byte/hex` pairs so non-printable bytes
// (e.g. K_SPECIAL 0x80) survive the trip through `vim.notify`.
std::string hexWindow(std::string_view s, size_t start, size_t end) {
  char buf[8];
  std::string out;
  out.reserve((end - start) * 5);
  for (size_t k = start; k < end; ++k) {
    const auto byte = static_cast<unsigned>(static_cast<unsigned char>(s[k]));
    std::snprintf(buf, sizeof(buf), "%02x ", byte);
    out += buf;
  }
  if (!out.empty()) out.pop_back();
  return out;
}
}  // namespace

using namespace std;

SequenceToKeys::SequenceToKeys(const CommandToKeys &actions,
                                     const CommandToKeys &motions) {
  tokens_.reserve(actions.size() + motions.size());

  for (const auto &p : actions) {
    tokens_.push_back(TokenDef{p.first, &p.second});
  }
  for (const auto &p : motions) {
    tokens_.push_back(TokenDef{p.first, &p.second});
  }

  // Longest tokens first, so we greedily match "gg" before "g".
  sort(tokens_.begin(), tokens_.end(),
            [](const TokenDef &a, const TokenDef &b) {
              if (a.token.size() != b.token.size())
                return a.token.size() > b.token.size();
              return a.token < b.token;
            });
}


PhysicalKeys SequenceToKeys::tokenize(string_view s) const {
  PhysicalKeys out;
  size_t i=0;

  while(i < s.size()){
    bool matched=false;

    for(const auto& td: tokens_){
      const string& tok = td.token;
      const size_t len = tok.size();
      if(len<=s.size()-i && s.compare(i,len,tok)==0){
        const auto& keys = *td.keys;
        out.append(keys);
        i += len;
        matched = true;
        break;
      }
    }
    if(!matched){
      // Fallback: single character via CHAR_TO_KEYS (handles typed content)
      char ch = s[i];
      auto it = CHAR_TO_KEYS.find(ch);
      if (it != CHAR_TO_KEYS.end()) {
        out.append(it->second);
        i++;
      } else {
        // Soft-fail: typed text can contain bytes with no token or
        // CHAR_TO_KEYS mapping (e.g. UTF-8 continuation bytes, or
        // K_SPECIAL-encoded keys that bypassed Lua-side keytrans).
        // Emit a hex window so the surrounding bytes are inspectable
        // even when they're non-printable, and skip so cost computation
        // continues — under-counting one byte beats crashing.
        const size_t winStart = i >= 4 ? i - 4 : 0;
        const size_t winEnd = std::min(i + 4, s.size());
        char hex[8];
        std::snprintf(hex, sizeof(hex), "0x%02x",
                      static_cast<unsigned>(static_cast<unsigned char>(s[i])));
        warning("SequenceToKeys::tokenize: unknown byte", hex,
                "at index", i, "of", s.size(),
                "; window[", winStart, ":", winEnd, "]=",
                hexWindow(s, winStart, winEnd));
        i++;
      }
    }
  }
  return out;
}
