#pragma once

#include <utility>

#include "BuildConfig.h"

inline constexpr bool DEBUG_TRACKING_ENABLED = VIMF_TRACK_STATES;

template<typename T, bool Enabled = DEBUG_TRACKING_ENABLED>
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
