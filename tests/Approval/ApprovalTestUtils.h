#pragma once

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ApprovalTests.hpp"  // IWYU pragma: keep

class ExactTextWriter final : public ApprovalTests::ApprovalWriter {
public:
  explicit ExactTextWriter(std::string text) : text_(std::move(text)) {}

  std::string getFileExtensionWithDot() const override { return ".txt"; }

  void write(std::string path) const override {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
      throw std::runtime_error("Unable to write file: " + path);
    }
    out << text_;
  }

  void cleanUpReceived(std::string receivedPath) const override {
    std::remove(receivedPath.c_str());
  }

private:
  std::string text_;
};

inline void verifyText(const std::string& report) {
  const ExactTextWriter writer(report);
  ApprovalTests::Approvals::verify(
      writer, ApprovalTests::Options(ApprovalTests::QuietReporter()));
}

inline void verifyText(const std::vector<std::string>& lines) {
  std::ostringstream out;
  for (const std::string& line : lines) out << line << "\n";
  verifyText(out.str());
}

