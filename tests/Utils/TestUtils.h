#pragma once

#include "Optimizer/Result.h"
#include "Keyboard/KeyboardModel.h"
#include "Utils/StringUtils.h"
#include "Utils/Lines.h"

#include <bits/stdc++.h>
using namespace std;

struct KeyAdjustment {
  Key k;
  double cost;
  KeyAdjustment(Key k, double cost) : k(k), cost(cost) {}
};



namespace TestFiles {

inline Lines load(const std::string& filename) {
    auto path = std::filesystem::path(__FILE__).parent_path() / ".." / ".." / "data" / "TestFiles" / filename;
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Cannot open: " + path.string());
    }
    Lines lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    return lines;
}

}

Lines readLines(istream &in);

bool contains_all(const vector<Result>& v, initializer_list<string> need);

void printResults(vector<Result>& results);

