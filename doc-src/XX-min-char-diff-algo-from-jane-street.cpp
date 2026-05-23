// Note that top-down is most intuitive for understanding the algorithm

// Remember that in an S-expression textual representation, as given in the blog, all non-parenthesis strings are leaves. The tree structure comes implicitly from the parenthetical structure, so:
/*
(A
  (b c)
  (d)
)
Is actually:
List("A", List("b", "c"), List("d"))
*/
#include <bits/stdc++.h>
using namespace std;

template<typename T> using v = vector<T>;
using vi = v<int>;
using vvi = v<v<int>>;
template<typename T>
bool cmn(T& x, const T& y) {
  if(y < x) {
    x = y;
    return true;
  }
  return false;
}

struct Node {
  vector<Node> ch;
  string val; // only present when ch.empty()
  int cost = 0;

  Node() = default;

  explicit Node(string val) : val(std::move(val)), cost(this->val.size()) {}

  explicit Node(v<Node> ch) : ch(std::move(ch)) {
    for (const Node& child : this->ch) {
      cost += child.cost;
    }
  }

  bool leaf() const {
    return ch.empty();
  }
  string id() const {
    return leaf() ? val : format("{:p}", static_cast<const void*>(this));
  }
  bool operator==(const Node& other) const {
    if(leaf() && other.leaf()) {
      return val == other.val;
    } else if(ch.size() != other.ch.size()) {
      return false;
    } else {
      for(int i = 0; i < ch.size(); i++) {
        if(ch[i] != other.ch[i]) {
          return false;
        }
      }
      return true;
    }
  }
  bool operator!=(const Node& other) const {
    return !(*this == other);
  }
};

struct TreeDiffResult {
  int cost;
  v<Node> nodes;
};

struct TreeDiffSolver {
  static constexpr int INF = INT_MAX;
  static constexpr string_view DIFF_LABEL = ":diff";
  static constexpr string_view OLD_LABEL = "old:";
  static constexpr string_view NEW_LABEL = "new:";
  static constexpr int DIFF_LABEL_COST = 5;
 
  TreeDiffSolver() = default;

  TreeDiffResult solveAnnotated(const v<Node>& a, const v<Node>& b) {
    return solveList(a, b);
  }

private:
  struct ListDp {
    TreeDiffSolver& solver;
    const v<Node>& a;
    const v<Node>& b;
    int n, m;
    v<string> suf_a, suf_b;
    vvi dp_outer, dp_inner;

    enum class OuterChoice { Unset, Diff, Keep, Recurse };
    enum class InnerChoice {
      Unset,
      DropOld,
      DropOldThenStop,
      TakeNew,
      TakeNewThenStop
    };

    v<v<OuterChoice>> outer_choice;
    v<v<InnerChoice>> inner_choice;

    ListDp(TreeDiffSolver& solver, const v<Node>& a, const v<Node>& b)
        : solver(solver),
          a(a), b(b), n(a.size()), m(b.size()),
          suf_a(makeSuf(a)), suf_b(makeSuf(b)),
          dp_outer(n + 1, vi(m + 1, INF)),
          dp_inner(n + 1, vi(m + 1, INF)),
          outer_choice(n + 1, v<OuterChoice>(m + 1, OuterChoice::Unset)),
          inner_choice(n + 1, v<InnerChoice>(m + 1, InnerChoice::Unset)) {}

    TreeDiffResult solve() {
      int cost = outer(0, 0);
      return {cost, buildOuter(0, 0)};
    }

    int outer(int i, int j) {
      if (i == n && j == m) return 0;

      int& res = dp_outer[i][j];
      if (res != INF) return res;

      auto update = [&](int cost, OuterChoice choice) {
        if (cost < res) {
          res = cost;
          outer_choice[i][j] = choice;
        }
      };

      if (i < n || j < m) {
        update(DIFF_LABEL_COST + inner(i, j), OuterChoice::Diff);
      }

      if (i < n && j < m) {
        // if (suf_a[i] == suf_b[j]) return res = 0;
        if (a[i] == b[j]) {
          update(outer(i + 1, j + 1), OuterChoice::Keep);
        } else if (solver.canRecurse(a[i], b[j])) {
          update(solver.solveList(a[i].ch, b[j].ch).cost + outer(i+1, j+1), OuterChoice::Recurse);
        }
      }

      return res;
    }

    int inner(int i, int j) {
      if (i == n && j == m) return 0;

      int& res = dp_inner[i][j];
      if (res != INF) return res;

      auto update = [&](int cost, InnerChoice choice) {
        if (cost < res) {
          res = cost;
          inner_choice[i][j] = choice;
        }
      };

      if (i < n) {
        update(inner(i + 1, j), InnerChoice::DropOld);
        update(outer(i + 1, j), InnerChoice::DropOldThenStop);
      }

      if (j < m) {
        int cost = b[j].cost;
        update(cost + inner(i, j + 1), InnerChoice::TakeNew);
        update(cost + outer(i, j + 1), InnerChoice::TakeNewThenStop);
      }

      return res;
    }

    v<Node> buildOuter(int i, int j) {
      if (i == n && j == m) return {};

      outer(i, j);
      assert(outer_choice[i][j] != OuterChoice::Unset);

      switch (outer_choice[i][j]) {
        case OuterChoice::Unset:
          assert(false);
          return {};
        case OuterChoice::Diff: {
          InnerBuild build = buildInner(i, j);
          v<Node> res = solver.makeDiffNodes(std::move(build.oldNodes),
                                              std::move(build.newNodes));
          append(res, buildOuter(build.nextI, build.nextJ));
          return res;
        }
        case OuterChoice::Keep: {
          v<Node> res = {b[j]};
          append(res, buildOuter(i + 1, j + 1));
          return res;
        }
        case OuterChoice::Recurse: {
          TreeDiffResult child = solver.solveList(a[i].ch, b[j].ch);
          v<Node> res = {Node(std::move(child.nodes))};
          append(res, buildOuter(i + 1, j + 1));
          return res;
        }
      }
      assert(false);
      return {};
    }

    struct InnerBuild {
      int nextI, nextJ;
      v<Node> oldNodes, newNodes;
    };

    InnerBuild buildInner(int i, int j) {
      InnerBuild build{i, j, {}, {}};

      while (build.nextI < n || build.nextJ < m) {
        inner(build.nextI, build.nextJ);
        InnerChoice choice = inner_choice[build.nextI][build.nextJ];
        assert(choice != InnerChoice::Unset);

        switch (choice) {
          case InnerChoice::Unset:
            assert(false);
            return build;
          case InnerChoice::DropOld:
            build.oldNodes.push_back(a[build.nextI++]);
            break;
          case InnerChoice::DropOldThenStop:
            build.oldNodes.push_back(a[build.nextI++]);
            return build;
          case InnerChoice::TakeNew:
            build.newNodes.push_back(b[build.nextJ++]);
            break;
          case InnerChoice::TakeNewThenStop:
            build.newNodes.push_back(b[build.nextJ++]);
            return build;
        }
      }

      return build;
    }
    static void append(v<Node>& target, v<Node> source) {
      target.insert(target.end(), make_move_iterator(source.begin()),
                    make_move_iterator(source.end()));
    }
  };

  static v<string> makeSuf(const v<Node>& a) {
    int n = a.size();
    string s;
    v<string> res(n + 1);
    for (int i = n - 1; i >= 0; --i) {
      s += a[i].id();
      res[i] = s;
    }
    return res;
  }

  TreeDiffResult solveList(const v<Node>& a, const v<Node>& b) {
    if (a.empty() && b.empty()) return {0, {}};
    ListDp dp(*this, a, b);
    return dp.solve();
  }

  bool canRecurse(const Node& a, const Node& b) const {
    return !a.leaf() && !b.leaf();
  }

  static Node labeledList(string_view label, v<Node> nodes) {
    nodes.insert(nodes.begin(), Node(string(label)));
    return Node(std::move(nodes));
  }

  static v<Node> makeDiffNodes(v<Node> oldNodes, v<Node> newNodes) {
    return {
        Node(string(DIFF_LABEL)),
        labeledList(OLD_LABEL, std::move(oldNodes)),
        labeledList(NEW_LABEL, std::move(newNodes)),
    };
  }
};

string toSexp(const Node& node) {
  if (node.leaf()) return node.val;

  string res = "(";
  for (int i = 0; i < node.ch.size(); i++) {
    if (i > 0) res += " ";
    res += toSexp(node.ch[i]);
  }
  res += ")";
  return res;
}

string toSexp(const v<Node>& nodes) {
  string res;
  for (int i = 0; i < nodes.size(); i++) {
    if (i > 0) res += "\n";
    res += toSexp(nodes[i]);
  }
  return res;
}

int main() {
  v<Node> before = {
      Node("form"),
      Node(v<Node>{Node("name"), Node("old")}),
      Node(v<Node>{Node("items"), Node("a"), Node("b")}),
  };

  v<Node> after = {
      Node("form"),
      Node(v<Node>{Node("name"), Node("new")}),
      Node(v<Node>{Node("items"), Node("a"), Node("c"), Node("d")}),
  };

  TreeDiffSolver solver;
  TreeDiffResult result = solver.solveAnnotated(before, after);

  cout << "Before:\n" << toSexp(before) << "\n\n";
  cout << "After:\n" << toSexp(after) << "\n\n";
  cout << "Annotated result, cost " << result.cost << ":\n"
       << toSexp(result.nodes) << "\n";
}
