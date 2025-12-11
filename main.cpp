#include <bits/stdc++.h>
#include "EffortModel.h"
#include "EffortState.h"
#include "TemplateKeyboard.cpp"

int main() {
  EffortModel model;
  fill_qwerty(model);

  EffortState st;
  // Suppose BFS is considering the sequence of physical key indices: 3,10,3
  // (e.g., some Vim command using left-index, right-index, left-index).
  st.append(Key::Key_C, model);
  st.append(Key::Key_I, model);
  st.append(Key::Key_W, model);

  double cost = st.score(model);
  std::cout<<"strokes="<<st.strokes<<" cost="<<cost<<"\n";
  return 0;
}
