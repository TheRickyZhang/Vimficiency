// Debug.h

#pragma once
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <string>

#ifdef VIMFICIENCY_DEBUG
constexpr bool DEBUG_ENABLED = true;
#else
constexpr bool DEBUG_ENABLED = false;
#endif

// Per-thread: async analyze/explore workers each trace into and consume their
// own stream, so one worker's payload never drains another's output.
inline std::ostringstream& dout() {
  thread_local std::ostringstream stream;
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

// Always-on diagnostic channel for soft-fail conditions that should
// surface to the user (vs. `debug()`, which is dev-only). Drained from
// Lua via `vf_get_warnings()` and routed through `vim.notify`. Process-wide
// (a worker's warning must still reach the main-thread drain), so guarded.
inline std::ostringstream& wout() {
  static std::ostringstream stream;
  return stream;
}
inline std::mutex& wout_mutex() {
  static std::mutex m;
  return m;
}

template <typename... Args> inline void warning(Args&&... args) {
  std::lock_guard<std::mutex> lock(wout_mutex());
  auto& os = wout();
  const char* sep = "";
  ((os << sep << std::forward<Args>(args), sep = " "), ...);
  os << '\n';
}

inline std::string consume_warning_output() {
  std::lock_guard<std::mutex> lock(wout_mutex());
  std::string result = wout().str();
  wout().str("");
  wout().clear();
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
