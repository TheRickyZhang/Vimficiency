#include <gtest/gtest.h>

#include <chrono>

static constexpr double MAX_TEST_SECONDS = 5.0;

class TimingListener : public testing::EmptyTestEventListener {
  std::chrono::steady_clock::time_point start_;

  void OnTestStart(const testing::TestInfo&) override {
    start_ = std::chrono::steady_clock::now();
  }

  void OnTestEnd(const testing::TestInfo& info) override {
    auto elapsed = std::chrono::steady_clock::now() - start_;
    double seconds =
        std::chrono::duration<double>(elapsed).count();
    if (seconds > MAX_TEST_SECONDS) {
      ADD_FAILURE() << info.test_suite_name() << "." << info.name()
                    << " took " << seconds << "s (limit: "
                    << MAX_TEST_SECONDS << "s)";
    }
  }
};

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(new TimingListener);
  return RUN_ALL_TESTS();
}
