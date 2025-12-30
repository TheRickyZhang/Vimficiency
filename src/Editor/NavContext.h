#pragma once

struct NavContext {
  // Currently unused. Knowing what the user can see would only be useful for modeling cognitive ease, ie executing commands that they can't see the result of, but that is very expensive.
  // Come back after a while, maybe have an optional setting that incorporates very simply somehow
  int topRow;
  int bottomRow;

  int windowHeight;
  int scrollAmount;

  NavContext(int top_row, int bottom_row, int window_height, int scroll_amount) :
    topRow(top_row), bottomRow(bottom_row), windowHeight(window_height), scrollAmount(scroll_amount) {}
};
