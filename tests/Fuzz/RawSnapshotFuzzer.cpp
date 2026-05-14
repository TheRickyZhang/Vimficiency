#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Session/Snapshot.h"

using namespace std;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const string_view bytes(reinterpret_cast<const char*>(data), size);

  const auto parsed = parseSnapshot(bytes);
  if (!parsed) {
    (void)formatSnapshotParseError(parsed.error());
    return 0;
  }

  (void)parsed->bufname;
  (void)parsed->filetype;
  (void)parsed->row;
  (void)parsed->col;
  (void)parsed->topRow;
  (void)parsed->bottomRow;
  (void)parsed->windowHeight;
  (void)parsed->scrollAmount;
  (void)parsed->lines;

  return 0;
}
