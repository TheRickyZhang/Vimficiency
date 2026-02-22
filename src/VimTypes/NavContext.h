#pragma once

struct NavContext {
  // Currently unused. Knowing what the user can see would only be useful for modeling cognitive ease,
  // ie executing commands that they can't see the result of, but that is very expensive.
  // Come back after a while, maybe have an optional setting that incorporates very simply somehow
  // int topRow;
  // int bottomRow;

  int windowHeight;
  int scrollAmount;

  NavContext(int window_height, int scroll_amount) :
    windowHeight(window_height), scrollAmount(scroll_amount) {}

  // Observed from window, scroll on my neovim instance (normally 40 for full screen, minus gaps for hyprland)
  NavContext() : windowHeight(39), scrollAmount(19) {}
};
