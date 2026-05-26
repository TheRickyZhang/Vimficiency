#pragma once

#include <gtest/gtest.h>

#include <chrono>

inline constexpr double MAX_TEST_SECONDS = 5.0;

class TimingListener : public testing::EmptyTestEventListener {
  std::chrono::steady_clock::time_point start_;

  void OnTestStart(const testing::TestInfo&) override {
    start_ = std::chrono::steady_clock::now();
  }

  void OnTestEnd(const testing::TestInfo& info) override {
    const auto elapsed = std::chrono::steady_clock::now() - start_;
    const double seconds = std::chrono::duration<double>(elapsed).count();
    if (seconds > MAX_TEST_SECONDS) {
      ADD_FAILURE() << info.test_suite_name() << "." << info.name()
                    << " took " << seconds << "s (limit: "
                    << MAX_TEST_SECONDS << "s)";
    }
  }
};

inline void installTimingListener() {
  testing::UnitTest::GetInstance()->listeners().Append(new TimingListener);
}
