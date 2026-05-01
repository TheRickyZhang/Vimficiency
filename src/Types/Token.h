#pragma once

#include <string>
#include <string_view>
#include <utility>

struct Token final : std::string {
  using std::string::string;

  Token(const std::string& value) : std::string(value) {}
  Token(std::string&& value) : std::string(std::move(value)) {}
  Token(std::string_view value) : std::string(value) {}

  operator std::string_view() const {
    return std::string_view(data(), size());
  }

  bool operator==(const Token&) const = default;
};
