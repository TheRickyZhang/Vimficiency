#include <bits/stdc++.h>
using namespace std;

struct position {
  int line;
  int currChar;
  int maxChar; // For accurately tracking line movement 

  position(int l, int cc, int mc) {
    line = l;
    cc = cc;
    maxChar = mc;
  }

  position getNewPosition(const string& motion, const vector<string>& lines) {
    int l, c, m;
    if(motion == "j") {
      l = min(l+1, (int)lines.size()-1); 
      c = min((int)lines[l].size()-1, maxChar);
      m = maxChar;
    } else if(motion == "k") {
      l = max(l-1, 0); 
      c = min((int)lines[l].size()-1, maxChar);
      m = maxChar;
    } else if(motion == "")
      }
      case "k": default
    }
    return position(l, c, m);
  }
};
