#pragma once

#include <utility>

#if defined(VIMF_DETAILED_DEBUG_TRACKING)
inline constexpr bool kDebugTrackingEnabled = true;
#else
inline constexpr bool kDebugTrackingEnabled = false;
#endif

template<typename T, bool Enabled = kDebugTrackingEnabled>
struct Maybe;

template<typename T>
struct Maybe<T, true> {
  T& get() { return value_; }
  const T& get() const { return value_; }

  template<typename U>
  void assign(U&& value) {
    value_ = std::forward<U>(value);
  }

private:
  T value_{};
};

template<typename T>
struct Maybe<T, false> {
  T& get() {
    static T empty{};
    return empty;
  }

  const T& get() const {
    static const T empty{};
    return empty;
  }

  template<typename U>
  void assign(U&&) {}
};
