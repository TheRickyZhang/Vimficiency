// Debug.h

#pragma once
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#ifdef VIMFICIENCY_DEBUG
constexpr bool DEBUG_ENABLED = true;
#else
constexpr bool DEBUG_ENABLED = false;
#endif

inline std::ostringstream& dout() {
  static std::ostringstream stream;
  return stream;
}

// Space between elements, and new line
template <typename... Args> inline void debug([[maybe_unused]] Args&&... args) {
  if constexpr (DEBUG_ENABLED) {
    auto& os = dout();
    const char* sep = "";
    ((os << sep << std::forward<Args>(args), sep = " "), ...);
    os << '\n';
  }
}

inline std::string consume_debug_output() {
  std::string result =
      "-----------------DEBUG------------------\n" + dout().str();
  dout().str("");
  dout().clear();
  return result;
}

// inline void clear_debug_output() {
//     dout().str("");
//     dout().clear();
// }

// Format like this so headers are always recognized as needed.
namespace VF::detail {
[[noreturn]] inline void check_fail(const char* file, int line,
                                    const char* cond, const char* msg) {
  std::fprintf(stderr, "CHECK failed at %s:%d: (%s) %s\n", file, line, cond,
               msg);
  std::abort();
}
} // namespace VF::detail

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond))                                                               \
      ::VF::detail::check_fail(__FILE__, __LINE__, #cond, msg);                \
  } while (0)
