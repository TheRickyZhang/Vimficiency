#pragma once

struct Params {
  int maxResults = 10;
  int maxNodes = 50000;
  double factor = 2.0;
  int threshold = 3;
  double weight1 = 1.0;
  double weight2 = 1.0;
  bool useFeature = true;
  bool debug = false;

  Params& withFeature(bool v) { useFeature = v; return *this; }
  Params& withMaxNodes(int v) { maxNodes = v; return *this; }
};
