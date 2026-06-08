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
  explicit ExactTextWriter(std::string text, std::string extensionWithDot = ".txt")
      : text_(std::move(text)), extension_(std::move(extensionWithDot)) {}

  std::string getFileExtensionWithDot() const override { return extension_; }

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
  std::string extension_;
};

inline void verifyWithExtension(const std::string& report, const std::string& extensionWithDot) {
  const ExactTextWriter writer(report, extensionWithDot);
  ApprovalTests::Approvals::verify(
      writer, ApprovalTests::Options(ApprovalTests::QuietReporter()));
}

inline void verifyText(const std::string& report) {
  verifyWithExtension(report, ".txt");
}

// Markdown fixture (`.approved.md`). Use when the report is markdown-structured
// (headers, fenced code blocks) so it stays readable rendered as well as raw —
// buffer/diff content MUST be fenced or rendered markdown collapses whitespace.
inline void verifyMarkdown(const std::string& report) {
  verifyWithExtension(report, ".md");
}

inline void verifyText(const std::vector<std::string>& lines) {
  std::ostringstream out;
  for (const std::string& line : lines) out << line << "\n";
  verifyText(out.str());
}

