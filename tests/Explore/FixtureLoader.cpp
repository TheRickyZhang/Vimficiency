#include "FixtureLoader.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace ExploreFixtures {
namespace {

Lines toLines(const nlohmann::json& arr) {
  Lines out;
  out.reserve(arr.size());
  for (const auto& v : arr) {
    out.push_back(Line(v.get<std::string>()));
  }
  return out;
}

}  // namespace

Fixture loadFixture(std::string_view name) {
  auto path = std::filesystem::path(__FILE__).parent_path() / "fixtures" /
              (std::string(name) + ".json");
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open fixture file: " + path.string());
  }
  nlohmann::json root = nlohmann::json::parse(in);

  Fixture f;
  f.lines = toLines(root.at("lines"));
  if (root.contains("goal_lines") && !root["goal_lines"].is_null()) {
    f.goalLines = toLines(root.at("goal_lines"));
  }
  f.startPos = CursorPos(root.at("start_row").get<int>(),
                         root.at("start_col").get<int>());
  f.endPos = CursorPos(root.at("end_row").get<int>(),
                       root.at("end_col").get<int>());
  f.hasLinesAbove = root.value("has_lines_above", false);
  f.hasLinesBelow = root.value("has_lines_below", false);
  f.windowHeight = root.value("window_height", 40);
  f.scrollAmount = root.value("scroll_amount", 20);
  f.userSeq = root.value("user_seq", std::string{});
  if (root.contains("optimal_results")) {
    for (const auto& entry : root.at("optimal_results")) {
      OptimalEntry e;
      e.seq = entry.at("seq").get<std::string>();
      e.cost = entry.at("cost").get<double>();
      f.optimalResults.push_back(std::move(e));
    }
  }
  return f;
}

}  // namespace ExploreFixtures
