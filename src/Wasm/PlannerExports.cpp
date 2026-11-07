// C ABI for the docs site's interactive explainer (dev/web/how-it-works-site.md).
// Every export takes UTF-8 C strings and returns a malloc'd JSON string the
// caller frees with `_free`. Costs that are INF serialize as null.

#include <emscripten.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "Keyboard/Config.h"
#include "Keyboard/ToKeys/CharToKeys.h"
#include "Optimizer/DiffPlanner/DiffState.h"
#include "Optimizer/DiffPlanner/PlannerCosts.h"
#include "Optimizer/DiffPlanner/SealMatchedRuns.h"
#include "Optimizer/DiffPlanner/VimDiff.h"
#include "Optimizer/DiffPlanner/VimDiffTrace.h"
#include "Types/Lines.h"

using namespace std;
using namespace VimDiff;

namespace {

constexpr int MAX_GRID_SIDE = 200;      // cells are dumped per block
constexpr int MAX_TEXT = 20000;          // seal / plan inputs
constexpr long long WIDGET_CELLS = 4'000'000;

constexpr const char* STEP_NAMES[] = {"LEADING", "MOVE", "CROSS", "DELETE",
                                      "CHANGE", "ENTER", "TYPE", "EXIT"};
constexpr const char* DELETE_CMDS[] = {"x", "dw", "dW", "dd", "dap", "D", "d0"};
constexpr const char* MOVE_CMDS[] = {"l", "w", "W", "j", "}", "$", "0"};

class JsonOut {
public:
  void beginObject() { value(); out_ += '{'; first_.push_back(true); }
  void endObject() { out_ += '}'; first_.pop_back(); }
  void beginArray() { value(); out_ += '['; first_.push_back(true); }
  void endArray() { out_ += ']'; first_.pop_back(); }
  void key(string_view k) {
    comma();
    writeString(k);
    out_ += ':';
    keyed_ = true;
  }
  void str(string_view s) { value(); writeString(s); }
  void num(double v) {
    value();
    if (v >= INF) { out_ += "null"; return; }
    char buf[32];
    snprintf(buf, sizeof buf, "%.10g", v);
    out_ += buf;
  }
  void integer(long long v) { value(); out_ += to_string(v); }
  void boolean(bool b) { value(); out_ += b ? "true" : "false"; }

  char* release() const {
    char* p = (char*)malloc(out_.size() + 1);
    memcpy(p, out_.c_str(), out_.size() + 1);
    return p;
  }

private:
  void comma() {
    if (first_.empty()) return;
    if (!first_.back()) out_ += ',';
    first_.back() = false;
  }
  void value() {
    if (keyed_) keyed_ = false;
    else comma();
  }
  void writeString(string_view s) {
    out_ += '"';
    for (unsigned char c : s) {
      switch (c) {
        case '"': out_ += "\\\""; break;
        case '\\': out_ += "\\\\"; break;
        case '\n': out_ += "\\n"; break;
        case '\t': out_ += "\\t"; break;
        case '\r': out_ += "\\r"; break;
        default:
          if (c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof buf, "\\u%04x", c);
            out_ += buf;
          } else {
            out_ += (char)c;
          }
      }
    }
    out_ += '"';
  }

  string out_;
  vector<bool> first_;
  bool keyed_ = false;
};

char* error(string_view message) {
  JsonOut j;
  j.beginObject();
  j.key("error");
  j.str(message);
  j.endObject();
  return j.release();
}

// KeyedSequence prices typed text with CHAR_TO_KEYS.at(), which throws on bytes
// without a key; the module is built without exception catching. planner.ts
// normalizes to the same set first so offsets match; this guards other callers.
string sanitize(const char* s) {
  string out(s ? s : "");
  for (char& c : out)
    if (!CHAR_TO_KEYS.count(c)) c = '?';
  return out;
}

Config layoutOf(const char* s) {
  const string_view layout = s ? s : "";
  if (layout == "colemak_dh") return Config::colemakDh();
  if (layout == "uniform") return Config::uniform();
  return Config::qwerty();
}

CostOptions optionsOf(double scale, int mxcnt) {
  CostOptions o;
  if (scale > 0) o.moveDeleteScale = scale;
  if (mxcnt >= 1) o.maxPrefixCount = mxcnt;
  o.maxPlannerCells = WIDGET_CELLS;
  return o;
}

void writePos(JsonOut& j, const CursorPos& p) {
  j.beginObject();
  j.key("line");
  j.integer(p.line);
  j.key("col");
  j.integer(p.col);
  j.endObject();
}

// Raw spans from the kept-run identity: between regions both sides advance
// together, so bBegin = prevBEnd + (aBegin - prevAEnd).
void writeRegions(JsonOut& j, const CostBreakdown& bd, const Lines& initialLines) {
  j.key("total");
  j.num(bd.total);
  j.key("regions");
  j.beginArray();
  int prevAEnd = 0, prevBEnd = 0;
  for (const RegionBreakdown& r : bd.regions) {
    const int aBegin = DiffText::positionToFlatIndex(r.diff.beginPos, initialLines);
    const int aEnd = aBegin + (int)r.diff.deletedText.size();
    const int bBegin = prevBEnd + (aBegin - prevAEnd);
    const int bEnd = bBegin + (int)r.diff.insertedText.size();
    prevAEnd = aEnd;
    prevBEnd = bEnd;
    j.beginObject();
    j.key("aBegin"); j.integer(aBegin);
    j.key("aEnd"); j.integer(aEnd);
    j.key("bBegin"); j.integer(bBegin);
    j.key("bEnd"); j.integer(bEnd);
    j.key("begin"); writePos(j, r.diff.beginPos);
    j.key("end"); writePos(j, r.diff.endPos);
    j.key("deleted"); j.str(r.diff.deletedText);
    j.key("inserted"); j.str(r.diff.insertedText);
    j.key("del"); j.num(r.del);
    j.key("ins"); j.num(r.ins);
    j.key("move"); j.num(r.move);
    j.endObject();
  }
  j.endArray();
}

void writeBlock(JsonOut& j, const Block& b) {
  j.key("aBegin"); j.integer(b.aBegin);
  j.key("aEnd"); j.integer(b.aEnd);
  j.key("bBegin"); j.integer(b.bBegin);
  j.key("bEnd"); j.integer(b.bEnd);
}

void writeCell(JsonOut& j, const TraceCell& c) {
  if (c.cost >= INF) { j.num(INF); return; }
  j.beginObject();
  j.key("cost"); j.num(c.cost);
  j.key("step"); j.str(STEP_NAMES[c.step]);
  j.key("pi"); j.integer(c.pi);
  j.key("pj"); j.integer(c.pj);
  j.endObject();
}

void writeGrid(JsonOut& j, const TraceBlock& tb, bool in) {
  j.beginArray();
  for (int i = 0; i <= tb.block.n(); i++) {
    j.beginArray();
    for (int c = 0; c <= tb.block.m(); c++) writeCell(j, in ? tb.inAt(i, c) : tb.outAt(i, c));
    j.endArray();
  }
  j.endArray();
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE char* vf_plan(const char* initial, const char* goal, const char* layout,
                                    double scale, int mxcnt) {
  const string a = sanitize(initial), b = sanitize(goal);
  if ((int)a.size() > MAX_TEXT || (int)b.size() > MAX_TEXT) return error("input too long");
  const Lines initialLines = Lines::unflatten(a), goalLines = Lines::unflatten(b);
  const CostBreakdown bd =
      calculateBreakdown(initialLines, goalLines, layoutOf(layout), optionsOf(scale, mxcnt));
  JsonOut j;
  j.beginObject();
  writeRegions(j, bd, initialLines);
  j.endObject();
  return j.release();
}

EMSCRIPTEN_KEEPALIVE char* vf_grid(const char* initial, const char* goal, const char* layout,
                                    double scale, int mxcnt) {
  const string a = sanitize(initial), b = sanitize(goal);
  if ((int)a.size() > MAX_GRID_SIDE || (int)b.size() > MAX_GRID_SIDE)
    return error("grid inputs are capped at 200 characters a side");
  const Lines initialLines = Lines::unflatten(a), goalLines = Lines::unflatten(b);
  const Trace t = trace(initialLines, goalLines, layoutOf(layout), optionsOf(scale, mxcnt));
  JsonOut j;
  j.beginObject();
  j.key("sealedFallback"); j.boolean(t.sealedFallback);
  j.key("dpCells"); j.integer(t.dpCells);
  j.key("rawCells"); j.integer(t.rawCells);
  j.key("blocks");
  j.beginArray();
  for (const TraceBlock& tb : t.blocks) {
    j.beginObject();
    writeBlock(j, tb.block);
    j.key("lead"); j.integer(tb.lead);
    j.key("trail"); j.integer(tb.trail);
    if (!t.sealedFallback) {
      j.key("out"); writeGrid(j, tb, false);
      j.key("in"); writeGrid(j, tb, true);
    }
    j.endObject();
  }
  j.endArray();
  j.key("path");
  j.beginArray();
  for (const TracePathCell& pc : t.path) {
    j.beginObject();
    j.key("block"); j.integer(pc.block);
    j.key("i"); j.integer(pc.i);
    j.key("j"); j.integer(pc.j);
    j.key("table"); j.str(pc.inTable ? "in" : "out");
    j.endObject();
  }
  j.endArray();
  writeRegions(j, t.breakdown, initialLines);
  j.endObject();
  return j.release();
}

EMSCRIPTEN_KEEPALIVE char* vf_tiling(const char* initial, int begin, int end, const char* kind,
                                      double scale, int mxcnt) {
  const string a = sanitize(initial);
  const int N = (int)a.size();
  if (N > MAX_TEXT) return error("input too long");
  if (begin < 0 || end > N || begin >= end) return error("span must satisfy 0 <= begin < end <= N");
  const bool isMove = kind && string_view(kind) == "move";
  const CostOptions o = optionsOf(scale, mxcnt);
  const FlatText text(Lines::unflatten(a));
  TilingCost cost(text, o.moveDeleteScale, o.maxPrefixCount,
                  isMove ? TilingCost::Kind::Move : TilingCost::Kind::Delete);
  const vector<TilingChunk> chunks = cost.cover(begin, end);
  JsonOut j;
  j.beginObject();
  j.key("cost"); j.num(cost.query(begin, end));
  j.key("charOnly"); j.num(cost.chainCost(CHAR, end - begin));  // {k}x / {k}l chain for contrast
  j.key("chunks");
  j.beginArray();
  for (const TilingChunk& c : chunks) {
    const char* cmd = (isMove ? MOVE_CMDS : DELETE_CMDS)[c.kind];
    j.beginObject();
    j.key("begin"); j.integer(c.begin);
    j.key("end"); j.integer(c.end);
    j.key("kind"); j.integer(c.kind);
    j.key("count"); j.integer(c.count);
    j.key("cost"); j.num(c.cost);
    j.key("label"); j.str((c.count > 1 ? to_string(c.count) : string()) + cmd);
    j.endObject();
  }
  j.endArray();
  j.endObject();
  return j.release();
}

EMSCRIPTEN_KEEPALIVE char* vf_seal(const char* initial, const char* goal, const char* layout,
                                    double scale, int mxcnt) {
  const string a = sanitize(initial), b = sanitize(goal);
  if ((int)a.size() > MAX_TEXT || (int)b.size() > MAX_TEXT) return error("input too long");
  const Lines initialLines = Lines::unflatten(a), goalLines = Lines::unflatten(b);
  const CostOptions o = optionsOf(scale, mxcnt);
  const Config config = layoutOf(layout);
  const FlatText initialText(initialLines), goalText(goalLines);
  const Typing typing(goalText, config);
  TilingCost del(initialText, o.moveDeleteScale, o.maxPrefixCount, TilingCost::Kind::Delete);
  TilingCost move(initialText, o.moveDeleteScale, o.maxPrefixCount, TilingCost::Kind::Move);
  const vector<Block> blocks = initialText.text == goalText.text
      ? vector<Block>{}
      : sealMatchedRuns(initialText, goalText, typing, del, move, initialLines, goalLines);
  long long dpCells = 0;
  for (const Block& blk : blocks) dpCells += (long long)(blk.n() + 1) * (blk.m() + 1);
  JsonOut j;
  j.beginObject();
  j.key("N"); j.integer((long long)a.size());
  j.key("M"); j.integer((long long)b.size());
  j.key("dpCells"); j.integer(dpCells);
  j.key("rawCells"); j.integer((long long)(a.size() + 1) * (long long)(b.size() + 1));
  j.key("blocks");
  j.beginArray();
  for (const Block& blk : blocks) {
    j.beginObject();
    writeBlock(j, blk);
    j.endObject();
  }
  j.endArray();
  j.endObject();
  return j.release();
}

}  // extern "C"
