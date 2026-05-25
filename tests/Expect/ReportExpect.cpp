#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "Boundary/NavBoundary.h"
#include "Effort/RunningEffort.h"
#include "Explore/Explore.h"
#include "Keyboard/Config.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

using namespace std;

namespace {

struct ExpectFixture {
  string name;
  Lines lines;
  Lines goalLines;
  CursorPos start{0, 0};
  CursorPos goal{0, 0};
  string userSeq;
  int maxRecommendations = 5;
};

filesystem::path fixtureDir() {
  return filesystem::path(__FILE__).parent_path() / "fixtures";
}

Lines jsonToLines(const nlohmann::json& arr) {
  Lines lines;
  for (const auto& value : arr) {
    lines.push_back(Line(value.get<string>()));
  }
  return lines;
}

CursorPos jsonToCursor(const nlohmann::json& arr) {
  return CursorPos(arr.at(0).get<int>(), arr.at(1).get<int>());
}

string readText(const filesystem::path& path) {
  ifstream in(path);
  if (!in) throw runtime_error("cannot open " + path.string());
  ostringstream out;
  out << in.rdbuf();
  return out.str();
}

ExpectFixture loadFixture(string_view name) {
  const auto path = fixtureDir() / (string(name) + ".json");
  ifstream in(path);
  if (!in) throw runtime_error("cannot open " + path.string());
  const nlohmann::json root = nlohmann::json::parse(in);

  ExpectFixture fixture;
  fixture.name = root.at("name").get<string>();
  fixture.lines = jsonToLines(root.at("lines"));
  fixture.goalLines = jsonToLines(root.at("goal_lines"));
  fixture.start = jsonToCursor(root.at("start"));
  fixture.goal = jsonToCursor(root.at("goal"));
  fixture.userSeq = root.value("user_seq", string{});
  fixture.maxRecommendations = root.value("max_recommendations", 5);
  return fixture;
}

string expectPhaseName(const Explore::Phase& phase) {
  if (holds_alternative<Explore::Navigate>(phase)) return "Navigate";
  if (holds_alternative<Explore::Transform>(phase)) return "Transform";
  if (holds_alternative<Explore::Insert>(phase)) return "Insert";
  return "Completed";
}

string renderLines(const Lines& lines) {
  ostringstream out;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    out << "  " << i << ": " << static_cast<const string&>(lines[i]) << "\n";
  }
  return out.str();
}

string renderReport(const ExpectFixture& fixture) {
  Config config = Config::uniform();
  NavContext navContext{24, 12};
  NavBoundary boundary(
      fixture.lines,
      CursorPos(0, 0),
      CursorPos(static_cast<int>(fixture.lines.size()) - 1,
                static_cast<int>(fixture.lines.back().size()) + 1),
      false,
      false);
  Explore::View view(
      fixture.lines, fixture.start, fixture.goalLines, fixture.goal,
      std::move(boundary), navContext, config);

  ostringstream out;
  out << fixed << setprecision(2);
  out << "Scenario: " << fixture.name << "\n";
  out << "Start cursor: (" << fixture.start.line << "," << fixture.start.col << ")\n";
  out << "Goal cursor: (" << fixture.goal.line << "," << fixture.goal.col << ")\n";
  out << "Start lines:\n" << renderLines(fixture.lines);
  out << "Goal lines:\n" << renderLines(fixture.goalLines);
  out << "User sequence: " << fixture.userSeq
      << " (effort " << getEffort(fixture.userSeq, config) << ")\n";
  out << "Phase: " << expectPhaseName(view.phase()) << "("
      << Explore::phaseIndex(view.phase()) << "/" << view.totalEdits() << ")\n";
  out << "Recommendations:\n";

  const auto recommendations = view.recommendations(fixture.maxRecommendations);
  for (size_t i = 0; i < recommendations.size(); ++i) {
    const auto& rec = recommendations[i];
    out << "  " << (i + 1) << ". " << rec.token
        << " -> (" << rec.landingPos.line << "," << rec.landingPos.col << ")"
        << " effort +" << rec.costDiff
        << " distance " << rec.distance << "\n";
  }
  return out.str();
}

void expectFixture(string_view name) {
  const ExpectFixture fixture = loadFixture(name);
  const string expected = readText(fixtureDir() / (string(name) + ".expect.txt"));
  EXPECT_EQ(renderReport(fixture), expected);
}

TEST(ExpectReportTest, MotionNextWord) {
  expectFixture("motion_next_word");
}

TEST(ExpectReportTest, DeleteSingleChar) {
  expectFixture("delete_single_char");
}

}  // namespace
