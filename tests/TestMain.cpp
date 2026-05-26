#include <gtest/gtest.h>

#include "TestMainSupport.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  installTimingListener();
  return RUN_ALL_TESTS();
}
