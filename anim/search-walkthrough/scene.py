# Walkthrough of the composition A* search (Navigate + Transform) for the
# README main() example — the companion to dp-walkthrough, which ends where
# this begins: the plan meets the exact search.
# Data: trace.json, exported and verified by tests/Debug/CompositionTraceExport.cpp.
# Render (from anim/): .venv/bin/manim render -qh search-walkthrough/scene.py SearchWalkthrough

import difflib
import json
import pathlib

import numpy as np
from manim import *

HERE = pathlib.Path(__file__).parent
TRACE = json.loads((HERE / "trace.json").read_text())

BG = "#12161c"
DEL_C = "#ff6b6b"
INS_C = "#69db7c"
KEPT_C = "#8a949e"
OUT_C = "#dee2e6"
IN_C = "#ffa94d"
OPT_C = "#ffd43b"
ALT_C = "#4dabf7"
DIM_C = "#495057"
BOX_C = "#343b45"
MONO = "monospace"

config.background_color = BG

KIND_C = {"nav": KEPT_C, "insert": IN_C, "edit": IN_C, "textobj": IN_C,
          "join": IN_C, "reset0": KEPT_C}


def keyglyph(raw):
    return (raw.replace("<Esc>", "⎋").replace("<Space>", "␣")
            .replace("<CR>", "↵").replace("<BS>", "⌫"))


def fmt(x):
    return str(int(x)) if abs(x - round(x)) < 1e-9 else f"{x:g}"


def glyph_span(s, lo, hi):
    """Text() drops spaces; map a char span to glyph indices."""
    start = sum(1 for c in s[:lo] if not c.isspace())
    count = sum(1 for c in s[lo:hi] if not c.isspace())
    return start, start + count


class BufPanel:
    """Monospace line panel with char-grid coordinates for cursor/region marks."""

    def __init__(self, lines, ox, oy, fs=16, lh=0.26):
        self.ox, self.oy, self.fs, self.lh = ox, oy, fs, lh
        self.adv = Text("m" * 10, font=MONO, font_size=fs).width / 10
        self.lines = list(lines)
        self.rows = {}
        self.group = VGroup()
        for li, ln in enumerate(self.lines):
            if not ln.strip():
                continue
            row = self._make_row(ln, li)
            self.rows[li] = row
            self.group.add(row)

    def _make_row(self, ln, li):
        t = Text(ln, font=MONO, font_size=self.fs, color=OUT_C)
        indent = len(ln) - len(ln.lstrip(" "))
        t.move_to(np.array([self.ox + indent * self.adv, self.oy - li * self.lh, 0]),
                  aligned_edge=LEFT)
        return t

    def char_center(self, li, col):
        return np.array([self.ox + (col + 0.5) * self.adv, self.oy - li * self.lh, 0])

    def span_rect(self, li, lo, hi, color, dashed=False):
        r = Rectangle(width=max(hi - lo, 1) * self.adv + 0.03, height=self.lh * 0.92,
                      stroke_color=color, stroke_width=2.2)
        r.move_to(np.array([self.ox + (lo + hi) / 2 * self.adv, self.oy - li * self.lh, 0]))
        if dashed:
            return DashedVMobject(r, num_dashes=24)
        return r

    def caret(self, li, col, color):
        p = np.array([self.ox + col * self.adv, self.oy - li * self.lh, 0])
        return Line(p + 0.4 * self.lh * UP, p + 0.4 * self.lh * DOWN,
                    color=color, stroke_width=3.5)

    def transition_anims(self, new_lines):
        sm = difflib.SequenceMatcher(a=self.lines, b=list(new_lines), autojunk=False)
        anims = []
        new_rows = {}
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == "equal":
                for off in range(i2 - i1):
                    li, lj = i1 + off, j1 + off
                    row = self.rows.pop(li, None)
                    if row is None:
                        continue
                    new_rows[lj] = row
                    if li != lj:
                        anims.append(row.animate.shift((li - lj) * self.lh * UP))
            else:
                for li in range(i1, i2):
                    row = self.rows.pop(li, None)
                    if row is not None:
                        self.group.remove(row)
                        anims.append(FadeOut(row))
                for lj in range(j1, j2):
                    if not new_lines[lj].strip():
                        continue
                    row = self._make_row(new_lines[lj], lj)
                    new_rows[lj] = row
                    self.group.add(row)
                    anims.append(FadeIn(row))
        self.rows = new_rows
        self.lines = list(new_lines)
        return anims


class SearchWalkthrough(Scene):
    def construct(self):
        initial = TRACE["initial"]
        goal_lines = TRACE["goal"]
        plan = TRACE["plan"]
        fenceposts = TRACE["fenceposts"]
        nodes = TRACE["nodes"]
        nav_calls = TRACE["navCalls"]
        suffix_costs = TRACE["suffixEditCosts"]
        result = TRACE["results"][0]
        result_segments = TRACE["resultSegments"]
        alternatives = TRACE["alternatives"]
        alt_by_node = {a["branchNode"]: a for a in alternatives}
        goal_pos = TRACE["goalPos"]
        E = len(plan)

        caption = VGroup()

        def say(text, size=26, wait=0.0):
            new = Text(text, font_size=size, color=OUT_C, line_spacing=0.9)
            new.to_edge(DOWN, buff=0.3)
            anims = [FadeIn(new, shift=0.15 * UP)]
            if len(caption):
                anims.append(FadeOut(caption[0]))
                caption.remove(caption[0])
            caption.add(new)
            self.play(*anims, run_time=0.6)
            # Hold long enough to read, even when the caller passes no wait.
            self.wait(max(wait, 0.016 * len(text)))

        # ---- Act 1: the plan arrives --------------------------------------
        title = Text("How vimficiency turns a plan into keystrokes",
                     font_size=30, color=OUT_C)
        title.to_edge(UP, buff=0.35)
        buf = BufPanel(initial, ox=-6.7, oy=2.45)
        self.play(FadeIn(title), Write(buf.group), run_time=1.6)

        say("The planner marked five regions in main().", wait=0.2)
        # Regions drawn on the initial buffer (edits 1..4 live one line lower
        # once the new line from edit 0 exists). Tags collect per line, past
        # the line's end, since lines 2 and 8 each host several regions.
        plan_marks = VGroup()
        line_tags = {}
        for p in plan:
            li = p["begin"][0] - (0 if p["idx"] == 0 else 1)
            lo = min(p["begin"][1], len(initial[li]))
            hi = min(p["end"][1], len(initial[li]))
            def qtext(s):
                s0 = s.split("\n")[0]
                more = "\n" in s or len(s0) > 12
                return '"' + s0[:12] + '"' + ("…" if more else "")

            if p["pureInsertion"]:
                m = buf.caret(li, lo, INS_C)
                tag = f"{p['idx'] + 1} +{qtext(p['ins'])}"
            else:
                m = buf.span_rect(li, lo, hi, DEL_C)
                tag = f"{p['idx'] + 1} {qtext(p['del'])}→{qtext(p['ins'])}"
            plan_marks.add(m)
            line_tags.setdefault(li, []).append(tag)
        plan_tags = VGroup()
        for li, tags in line_tags.items():
            t = Text("   ".join(tags), font=MONO, font_size=14, color=INS_C)
            t.move_to(buf.char_center(li, len(initial[li]) + 3), aligned_edge=LEFT)
            plan_tags.add(t)
        self.play(LaggedStart(*[Create(m) for m in plan_marks], lag_ratio=0.2),
                  LaggedStart(*[FadeIn(t) for t in plan_tags], lag_ratio=0.2),
                  run_time=1.6)
        self.wait(0.6)
        say("The search visits the regions in this order.\nIt needs Vim commands to reach and perform each one.", wait=1.2)

        # ---- Act 2: transform menus ---------------------------------------
        say("First, each region is solved on its own\nby a transform search.")
        say("Zoom into region 2: change 10 into n.")
        self.play(Indicate(plan_marks[1], color=DEL_C, scale_factor=1.6), run_time=0.8)

        veil = Rectangle(width=15, height=9, fill_color=BG, fill_opacity=0.93,
                         stroke_width=0)
        card = VGroup()
        zoom = next(z for z in TRACE["transformZooms"] if z["editIdx"] == 1)
        eff = zoom["effectiveLines"][0]
        L = zoom["leftColOffset"]
        R = len(eff) - zoom["rightColOffset"]

        def eff_text(s, y=1.3):
            t = Text(s, font=MONO, font_size=22, color=KEPT_C)
            t.move_to(np.array([0, y, 0]))
            return t

        adv22 = Text("m" * 10, font=MONO, font_size=22).width / 10

        def color_span(t, s, lo, hi, color):
            gl, gh = glyph_span(s, lo, hi)
            for g in t[gl:gh]:
                g.set_color(color)

        line0 = eff_text(eff)
        color_span(line0, eff, L, R, DEL_C)
        region_box = SurroundingRectangle(
            VGroup(*line0[glyph_span(eff, L, R)[0]:glyph_span(eff, L, R)[1]]),
            color=DEL_C, buff=0.07, stroke_width=2.5)
        ztitle = Text("zoom: transform search for region 2", font_size=24, color=OUT_C)
        ztitle.move_to(np.array([0, 2.3, 0]))
        self.play(FadeIn(veil), FadeIn(ztitle), FadeIn(line0), Create(region_box),
                  run_time=0.9)
        card.add(ztitle, line0, region_box)
        say("This search sees one line. Only 10 changes.\nThe rest of the line must stay intact.", wait=0.8)

        cur1 = Rectangle(width=adv22, height=0.42, fill_color=OPT_C, fill_opacity=0.35,
                         stroke_width=0)
        g0 = glyph_span(eff, L, L + 1)[0]
        cur1.move_to(line0[g0])
        card.add(cur1)
        say("There is one start position per character of 10.\nFrom the 1, the search tries deletions: x, de, dw ...")
        self.play(FadeIn(cur1), run_time=0.5)
        de_lbl = Text("de deletes exactly 10", font=MONO, font_size=19,
                      color=DEL_C).move_to(np.array([0, 0.55, 0]))
        self.play(FadeIn(de_lbl), Indicate(region_box, color=DEL_C), run_time=0.9)
        card.add(de_lbl)
        say("de removes the whole region. So instead of the\ndelete, it emits a change: ce, then type n.", wait=0.4)
        goal_eff = eff[:L] + "n" + eff[R:]
        line1 = eff_text(goal_eff)
        color_span(line1, goal_eff, L, L + 1, INS_C)
        chip1 = Text("cen⎋ · 4 keys", font=MONO, font_size=21, color=OPT_C)
        chip1.move_to(np.array([-2.4, -0.35, 0]))
        self.play(FadeTransform(line0, line1), FadeOut(region_box), FadeOut(cur1),
                  FadeOut(de_lbl), FadeIn(chip1), run_time=1.0)
        card.remove(line0, region_box, cur1, de_lbl)
        card.add(line1, chip1)
        self.wait(0.5)

        say("x and X were explored too, 1 key each.\nThey leave the 0, so the region is not finished.")
        explored_rows = VGroup()
        for p in zoom["pops"]:
            if p["seq"]["raw"] not in ("x", "X"):
                continue
            r = Text(f"{p['seq']['raw']} · {fmt(p['g'])} key · leaves the 0",
                     font=MONO, font_size=17, color=KEPT_C)
            if len(explored_rows):
                r.next_to(explored_rows[-1], DOWN, buff=0.14, aligned_edge=LEFT)
            else:
                r.move_to(np.array([-3.3, -1.05, 0]), aligned_edge=LEFT)
            explored_rows.add(r)
        self.play(LaggedStart(*[FadeIn(r, shift=0.1 * LEFT) for r in explored_rows],
                              lag_ratio=0.3), run_time=0.7)
        self.wait(0.5)

        say("Continuing from X gives the best sequence for the\n0 start: X, then ciw, then type n. 6 keys in total.")
        chip2 = Text("Xciwn⎋ · 6 keys", font=MONO, font_size=21, color=ALT_C)
        chip2.move_to(np.array([2.4, -0.35, 0]))
        self.play(FadeIn(chip2), run_time=0.7)
        card.add(chip2)
        self.wait(0.6)

        menu = VGroup(
            Text("region 2 menu", font_size=21, color=OUT_C),
            Text("from (3,21)  cen⎋    4", font=MONO, font_size=19, color=OPT_C),
            Text("from (3,22)  Xciwn⎋  6", font=MONO, font_size=19, color=ALT_C),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.16).move_to(np.array([0, -1.6, 0]))
        say("This is the region's menu. For each start position,\nit lists the cheapest command that performs the edit.", wait=0.2)
        self.play(FadeIn(menu), FadeOut(explored_rows), run_time=0.8)
        card.add(menu)
        self.wait(1.0)
        say("Every region gets a menu like this. Insertion\nregions list entry strategies instead: I, A, i, o.", wait=1.0)
        say("Zoom back out to the full buffer.")
        self.play(FadeOut(veil), FadeOut(card), run_time=0.7)

        # ---- Act 3: the composition A* ------------------------------------
        say("Now the composition search. A state is a cursor\nposition plus a count of regions done. Start: (0,0), 0 of 5.")
        self.play(FadeOut(title), FadeOut(plan_tags), FadeOut(plan_marks),
                  run_time=0.6)

        seq_chips = VGroup()
        seq_x0, seq_y = -6.9, 3.55

        def append_chip(pretty, color):
            chip = Text(keyglyph(pretty), font=MONO, font_size=18, color=color)
            if len(seq_chips):
                chip.next_to(seq_chips[-1], RIGHT, buff=0.14)
                chip.align_to(seq_chips[-1], DOWN)
            else:
                chip.move_to(np.array([seq_x0, seq_y, 0]), aligned_edge=LEFT)
            seq_chips.add(chip)
            return chip

        badge = VGroup(
            Text("regions done 0/5", font=MONO, font_size=19, color=OUT_C),
            Text("g 0 keys · f 37.8", font=MONO, font_size=17, color=KEPT_C),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.1)
        badge.move_to(np.array([3.1, 2.8, 0]), aligned_edge=LEFT)

        h_note = VGroup(
            Text("f = keys so far + estimate:", font_size=18, color=KEPT_C),
            Text("menu medians left + √distance", font_size=18, color=KEPT_C),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.08)
        h_note.next_to(badge, DOWN, buff=0.3, aligned_edge=LEFT)

        cursor = Rectangle(width=buf.adv, height=buf.lh * 0.9,
                           fill_color=OPT_C, fill_opacity=0.45, stroke_width=0)
        cursor.move_to(buf.char_center(0, 0))
        self.play(FadeIn(cursor), FadeIn(badge), FadeIn(h_note), run_time=0.8)
        say("Each state gets a score f: keys typed so far, plus\nthe remaining menu costs and the distance to the\nnext region.", wait=0.6)

        q_hdr = Text("frontier · lowest f pops", font_size=18, color=OUT_C)
        q_hdr.move_to(np.array([3.1, 1.35, 0]), aligned_edge=LEFT)
        q_rows = {}  # queue label -> Text

        def q_slot(i):
            return np.array([3.1, 1.0 - i * 0.3, 0])

        def q_layout_anims():
            entries = sorted(q_rows.items(), key=lambda kv: kv[1].fval)
            return [t.animate.move_to(q_slot(i), aligned_edge=LEFT)
                    for i, (_, t) in enumerate(entries)]

        def q_push(key, label, fval, color=OUT_C):
            t = Text(f"{fval:5.1f}  {label}", font=MONO, font_size=16, color=color)
            t.fval = fval
            t.move_to(q_slot(len(q_rows)), aligned_edge=LEFT)
            q_rows[key] = t
            return t

        self.play(FadeIn(q_hdr), run_time=0.4)

        # candidate rows area (bottom right, above caption)
        cand_hdr_y = -0.6

        region_mark = None

        def show_region_mark(e):
            nonlocal region_mark
            anims = []
            if region_mark is not None:
                anims.append(FadeOut(region_mark))
                region_mark = None
            if e < E:
                p = plan[e]
                li, (lo, hi) = p["begin"][0], (p["begin"][1], p["end"][1])
                if p["pureInsertion"]:
                    region_mark = buf.caret(li, min(lo, len(fenceposts[e][li])), INS_C)
                else:
                    region_mark = buf.span_rect(li, lo, hi, DEL_C, dashed=True)
                anims.append(FadeIn(region_mark))
            return anims

        by_id = {n["id"]: n for n in nodes}
        node_qkeys = {}   # node id -> queue key of its entry
        displayed_e = 0

        # The bar shows the POPPED state's own sequence: popping a state from
        # another branch rewinds the shared prefix and swaps the tail.
        cur_path = [0]
        edge_chips = []

        def node_path(nid):
            path = []
            while nid != -1:
                path.append(nid)
                nid = by_id[nid]["parent"]
            return path[::-1]

        def set_path_anims(nid):
            new_path = node_path(nid)
            c = 0
            while (c < len(cur_path) and c < len(new_path)
                   and cur_path[c] == new_path[c]):
                c += 1
            anims = []
            while len(edge_chips) > c - 1:
                chip = edge_chips.pop()
                seq_chips.remove(chip)
                anims.append(FadeOut(chip, shift=0.2 * UP))
            for idx in range(max(c, 1), len(new_path)):
                n = by_id[new_path[idx]]
                chip = append_chip(n["viaSuffix"]["pretty"],
                                   KIND_C.get(n["viaKind"], OUT_C))
                edge_chips.append(chip)
                anims.append(FadeIn(chip))
            cur_path[:] = new_path
            return anims

        def short_label(c):
            s = keyglyph(c["suffix"]["pretty"])
            return s if len(s) <= 12 else s[:11] + "…"

        beats_before = {
            0: lambda: say("Region 1 is an insertion. Each entry strategy asks\na motion search for a path to its line. Motion plus\ninsertion form one candidate."),
            1: lambda: say("Next is region 2. One motion search finds a landing\nfor both of its menu starts: f1 and f0;"),
            3: lambda: say("f 40.0 is the lowest score, so the f0; state is\nexpanded next. The sequence above switches to\nthat state's own path.", wait=0.2),
            4: lambda: say("Region 2 is done. Region 3 is 6 lines down.\nZoom into the motion search for this move.", wait=0.4),
            5: lambda: say("The motion landed on the +. The menu here lists\nr- for 2 keys and s-⎋ for 3. Both become candidates.", wait=0.2),
            8: lambda: say("The last region inserts ) before the semicolon.\n$ moves to the last column, then i) inserts it.", wait=0.2),
        }
        beats_after = {
            0: lambda: say("Both strategies end at the same position. The\ncostlier one is dropped. Completed, that path\n"
                           f"would total {fmt(alt_by_node[0]['cost'])} keys.", wait=1.0),
            3: lambda: say("Its edit Xciwn⎋ reaches the same state that cen⎋\nalready reached for less. It is dropped. Completed,\n"
                           f"that path would total {fmt(alt_by_node[3]['cost'])} keys.", wait=1.2),
            5: lambda: say(f"The s-⎋ version is dropped the same way.\nCompleted, it would total {fmt(alt_by_node[5]['cost'])} keys.", wait=0.4),
            8: lambda: say("This candidate completes all 5 regions with the\ncursor at the target, so its f is exact: 45 keys.", wait=0.8),
        }

        first_pop = True
        for node in nodes:
            k = node["id"]

            # Pop: apply the recorded transition onto the live buffer view.
            if not first_pop:
                qt = q_rows.pop(node_qkeys[k], None)
                pop_anims = []
                if qt is not None:
                    self.play(Indicate(qt, color=OPT_C, scale_factor=1.15), run_time=0.5)
                    pop_anims.append(FadeOut(qt, shift=0.3 * LEFT))
                new_e = node["e"]
                if new_e != displayed_e:
                    pop_anims += buf.transition_anims(fenceposts[new_e])
                    displayed_e = new_e
                pop_anims += show_region_mark(new_e)
                pop_anims += set_path_anims(k)
                new_badge = VGroup(
                    Text(f"regions done {new_e}/{E}", font=MONO, font_size=19, color=OUT_C),
                    Text(f"g {fmt(node['g'])} keys · f {node['f']:.1f}",
                         font=MONO, font_size=17, color=KEPT_C),
                ).arrange(DOWN, aligned_edge=LEFT, buff=0.1)
                new_badge.move_to(badge, aligned_edge=LEFT)
                pop_anims += [cursor.animate.move_to(
                                  buf.char_center(node["pos"][0], node["pos"][1])),
                              FadeTransform(badge, new_badge),
                              *q_layout_anims()]
                badge = new_badge
                self.play(*pop_anims, run_time=0.9)
            else:
                self.play(*show_region_mark(0), run_time=0.5)
                first_pop = False

            if k in beats_before:
                beats_before[k]()

            # Nav zoom replaces the plain candidate listing for node 4.
            if k == 4:
                self.nav_zoom(nav_calls[3])
                say("Back in the composition search. The found motion\nis one candidate for this state.", wait=0.2)

            # Children: candidate rows + queue updates.
            rows = VGroup()
            row_anims = []
            for c in node["children"]:
                color = KIND_C.get(c["kind"], OUT_C)
                mark = "✓" if c["status"] in ("enqueued", "improved") else "✗"
                label = short_label(c)
                r = Text(f"{label:<13} f {c['f']:5.1f}  {mark}",
                         font=MONO, font_size=16, color=color)
                if len(rows):
                    r.next_to(rows[-1], DOWN, buff=0.12, aligned_edge=LEFT)
                else:
                    r.move_to(np.array([3.1, cand_hdr_y, 0]), aligned_edge=LEFT)
                if mark == "✗":
                    r.set_opacity(0.4)
                rows.add(r)
            if len(rows):
                self.play(LaggedStart(*[FadeIn(r, shift=0.1 * LEFT) for r in rows],
                                      lag_ratio=0.25), run_time=0.7)
            push_anims = []
            for c in node["children"]:
                if c["status"] not in ("enqueued", "improved"):
                    continue
                key = (k, c["suffix"]["raw"])
                color = OPT_C if c["terminal"] else OUT_C
                qt = q_push(key, short_label(c), c["f"], color)
                if "nodeId" in c:
                    node_qkeys[c["nodeId"]] = key
                else:
                    node_qkeys[("terminal", c["suffix"]["raw"])] = key
                push_anims.append(FadeIn(qt, shift=0.2 * RIGHT))
            if push_anims:
                self.play(*push_anims, *q_layout_anims(), run_time=0.6)

            if k in beats_after:
                beats_after[k]()
            if len(rows):
                self.play(FadeOut(rows), run_time=0.35)

        # Goal pop.
        say("45.0 is now the lowest f in the queue.")
        term_key = ("terminal", result["seq"]["raw"][len(nodes[-1]["seq"]["raw"]):])
        qt = q_rows.pop(term_key, None)
        goal_anims = []
        if qt is not None:
            self.play(Indicate(qt, color=OPT_C, scale_factor=1.15), run_time=0.5)
            goal_anims.append(FadeOut(qt, shift=0.3 * LEFT))
        goal_anims += buf.transition_anims(goal_lines)
        goal_anims += show_region_mark(E)
        last_suffix = result["seq"]["pretty"][len(nodes[-1]["seq"]["pretty"]):]
        chip = append_chip(last_suffix, IN_C)
        goal_anims += [FadeIn(chip),
                       cursor.animate.move_to(buf.char_center(*goal_pos))]
        self.play(*goal_anims, run_time=1.0)
        gbadge = Text(f"GOAL · {E}/{E} · cursor ({goal_pos[0]},{goal_pos[1]})",
                      font=MONO, font_size=18, color=OPT_C)
        gbadge.move_to(badge, aligned_edge=LEFT)
        self.play(FadeTransform(badge, gbadge), FadeOut(q_hdr), FadeOut(h_note),
                  Circumscribe(seq_chips, color=OPT_C, buff=0.12), run_time=1.2)
        say("The search stops at the first completed state.\nThe buffer and the cursor now match the target.", wait=1.4)

        # ---- Act 5: finale -------------------------------------------------
        stats = TRACE["stats"]
        keep = caption[0]
        self.play(*[FadeOut(m) for m in self.mobjects if m is not keep],
                  run_time=0.8)

        say("Every dropped branch can be completed with the\nwinner's remaining moves. Ranked, that gives:")

        def cand_row(cost, segments, y, star):
            row = VGroup()
            lbl = Text(("★ " if star else "  ") + fmt(cost), font=MONO,
                       font_size=17, color=OPT_C if star else KEPT_C)
            lbl.move_to(np.array([-6.2, y, 0]), aligned_edge=LEFT)
            row.add(lbl)
            for si, s in enumerate(segments):
                if star:
                    col = OPT_C
                else:
                    same = (si < len(result_segments)
                            and s["raw"] == result_segments[si]["raw"])
                    col = DIM_C if same else ALT_C
                t = Text(keyglyph(s["pretty"]), font=MONO, font_size=14, color=col)
                t.next_to(row[-1], RIGHT, buff=0.13)
                t.align_to(lbl, DOWN)
                row.add(t)
            return row

        rows = VGroup(cand_row(result["cost"], result_segments, 1.9, star=True))
        for i, a in enumerate(alternatives[:3]):
            rows.add(cand_row(a["cost"], a["segments"], 1.9 - 0.6 * (i + 1),
                              star=False))
        self.play(FadeIn(rows[0]), run_time=0.8)
        self.wait(0.6)
        self.play(LaggedStart(*[FadeIn(r, shift=0.15 * UP) for r in rows[1:]],
                              lag_ratio=0.4), run_time=1.4)
        alt_costs = ", ".join(fmt(a["cost"]) for a in alternatives[:3])
        say(f"The blue segments are where each one differs.\nThey complete to {alt_costs} keys. The search kept {fmt(result['cost'])}.",
            wait=1.2)
        tally = Text(f"{stats['pops']} outer pops · {stats['navNodes']} motion nodes · "
                     f"{E} menus", font=MONO, font_size=19, color=KEPT_C)
        tally.move_to(np.array([0, -1.1, 0]))
        self.play(FadeIn(tally), run_time=0.8)
        self.wait(1.0)
        say("The planner picked the regions, transform searches\nsolved them, and the composition search joined them.", size=28)
        self.wait(2.4)

    # ---- inner motion search zoom -----------------------------------------
    def nav_zoom(self, nav):
        veil = Rectangle(width=15, height=9, fill_color=BG, fill_opacity=0.93,
                         stroke_width=0)
        panel = BufPanel(nav["lines"], ox=-5.9, oy=2.15, fs=16, lh=0.42)
        ztitle = Text("zoom: motion search from region 2 to region 3",
                      font_size=24, color=OUT_C)
        ztitle.to_edge(UP, buff=0.5)
        start = Dot(panel.char_center(*nav["start"]), radius=0.07, color=OPT_C)
        goal_ring = panel.span_rect(nav["goalFirst"][0], nav["goalFirst"][1],
                                    nav["goalLast"][1] + 1, INS_C)
        self.play(FadeIn(veil), FadeIn(ztitle), Write(panel.group),
                  FadeIn(start), Create(goal_ring), run_time=1.2)

        zsay = VGroup()

        def say2(text, wait=0.0):
            new = Text(text, font_size=25, color=OUT_C, line_spacing=0.9)
            new.to_edge(DOWN, buff=0.3)
            anims = [FadeIn(new, shift=0.15 * UP)]
            if len(zsay):
                anims.append(FadeOut(zsay[0]))
                zsay.remove(zsay[0])
            zsay.add(new)
            self.play(*anims, run_time=0.6)
            self.wait(max(wait, 0.016 * len(text)))

        say2("This inner search runs over cursor positions only.\nThe yellow dot is the start. The green box is the\ngoal: the + that region 3 changes.")
        say2("Each expansion tries the basic motions:\n0 ^ $ w b e W B j k f and more.\nEach dot is a reached position.")
        dots = VGroup()
        pops = [p for p in nav["pops"] if p["seq"]["raw"]]
        waves = {}
        for p in pops:
            waves.setdefault(int(p["g"]), []).append(p)
        wave_lbl = None
        for g in sorted(waves):
            wave = waves[g]
            anims = []
            for p in wave:
                d = Dot(panel.char_center(p["pos"][0], p["pos"][1]), radius=0.055,
                        color=ALT_C, fill_opacity=0.85)
                dots.add(d)
                anims.append(FadeIn(d, scale=2.2))
            new_lbl = Text(f"depth {g} · {sum(len(waves[x]) for x in waves if x <= g)} states",
                           font=MONO, font_size=20, color=KEPT_C)
            new_lbl.to_corner(UR, buff=0.6).shift(0.4 * DOWN)
            lbl_anim = (FadeTransform(wave_lbl, new_lbl) if wave_lbl is not None
                        else FadeIn(new_lbl))
            self.play(LaggedStart(*anims, lag_ratio=0.12), lbl_anim, run_time=0.85)
            wave_lbl = new_lbl

        say2("Each reached position keeps its cheapest motion\nsequence and its key count. A sample:")
        sample = (waves.get(1, [])[:2] + waves.get(2, [])[:4]
                  + waves.get(3, [])[:3])
        sample_rows = VGroup(Text("reached states", font_size=18, color=OUT_C))
        sample_rows[0].move_to(np.array([3.3, 1.7, 0]), aligned_edge=LEFT)
        for p in sample:
            r = Text(f"{keyglyph(p['seq']['pretty']):<4} {int(p['g'])} key"
                     + ("s" if p["g"] > 1 else ""),
                     font=MONO, font_size=16, color=KEPT_C)
            r.next_to(sample_rows[-1], DOWN, buff=0.13, aligned_edge=LEFT)
            sample_rows.add(r)
        self.play(LaggedStart(*[FadeIn(r, shift=0.1 * LEFT) for r in sample_rows],
                              lag_ratio=0.15), run_time=1.0)
        self.wait(0.8)
        say2("After 5 keys of depth, no state has landed\ninside the goal box yet.", wait=0.6)

        say2("Counted motions are also tried. From column 6,\n5j is added because it lands exactly on the goal.")
        res = nav["results"][0]
        tokens = ["0", "j", "E", "5j"]
        prefix = ""
        path_pts = [panel.char_center(*nav["start"])]
        for tok in tokens[:-1]:
            prefix += tok
            p = next(p for p in nav["pops"] if p["seq"]["raw"] == prefix)
            path_pts.append(panel.char_center(p["pos"][0], p["pos"][1]))
        path_pts.append(panel.char_center(res["landing"][0], res["landing"][1]))
        segs = VGroup()
        lbls = VGroup()
        for i, tok in enumerate(tokens):
            seg = Line(path_pts[i], path_pts[i + 1], color=OPT_C, stroke_width=5,
                       buff=0.05)
            lbl = Text(tok, font=MONO, font_size=19, color=OPT_C)
            off = UP if abs(path_pts[i + 1][1] - path_pts[i][1]) < 0.01 else LEFT
            lbl.next_to(seg.get_center(), off, buff=0.12)
            segs.add(seg)
            lbls.add(lbl)
            self.play(Create(seg), FadeIn(lbl), run_time=0.5)
        chip = Text(f"best path: {keyglyph(res['seq']['pretty'])} · 5 keys",
                    font=MONO, font_size=21, color=OPT_C)
        chip.to_corner(UR, buff=0.6).shift(1.0 * DOWN)
        self.play(FadeIn(chip), Flash(path_pts[-1], color=OPT_C, flash_radius=0.3),
                  run_time=0.8)
        self.wait(1.2)
        say2(f"The composition search receives 0jE5j as one\n"
             f"candidate. It never sees these {len(nav['pops'])} expansions.\n"
             f"Zoom back out.", wait=1.0)
        self.play(FadeOut(veil), FadeOut(ztitle), FadeOut(panel.group), FadeOut(start),
                  FadeOut(goal_ring), FadeOut(dots), FadeOut(segs), FadeOut(lbls),
                  FadeOut(chip), FadeOut(wave_lbl), FadeOut(sample_rows),
                  FadeOut(zsay[0]), run_time=0.8)
