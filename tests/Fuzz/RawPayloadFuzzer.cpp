#include <cstddef>
#include <cstdint>
#include <string_view>

#include "LuaExports/Common.h"

using namespace std;

namespace payload = VF::LuaExports::payload;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const string_view bytes(reinterpret_cast<const char*>(data), size);

  (void)payload::decodeLengthPrefixedStrings(bytes);
  (void)payload::decodeLineArray(bytes);
  (void)payload::decodeRecallRecordMeta(bytes);
  (void)payload::decodeKeyTrackingEvents(bytes);

  return 0;
}
