# DP walkthrough of the VimDiff planner for the README examples.
# Data: trace.json (tiny example) + trace2.json (fox example), exported and
# verified by tests/Debug/VimDiffTraceExport.cpp.
# Render (from anim/): .venv/bin/manim render -qh dp-walkthrough/scene.py DPWalkthrough

import json
import pathlib

import numpy as np
from manim import *

HERE = pathlib.Path(__file__).parent
TRACE = json.loads((HERE / "trace.json").read_text())
TRACE2 = json.loads((HERE / "trace2.json").read_text())

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

S = 0.62             # tiny-example lattice spacing
X0, Y0 = -5.2, 2.55  # tiny-example cell (0,0) center
BOX_W = 0.56

S2 = 0.17            # fox-example lattice spacing
X2, Y2 = -6.1, 2.75  # fox-example node (0,0)


def node(i, j):
    return np.array([X0 + j * S, Y0 - i * S, 0])


def node2(i, j):
    return np.array([X2 + j * S2, Y2 - i * S2, 0])


def fmt(x):
    return str(int(x)) if abs(x - round(x)) < 1e-9 else f"{x:g}"


def glyph(ch):
    return "␣" if ch == " " else ch


def glyph_span(s, lo, hi):
    """Text() drops spaces; map a char span to glyph indices."""
    start = sum(1 for c in s[:lo] if not c.isspace())
    count = sum(1 for c in s[lo:hi] if not c.isspace())
    return start, start + count


STEP_COLOR = {"MOVE": KEPT_C, "CROSS": KEPT_C, "DELETE": DEL_C, "CHANGE": DEL_C,
              "ENTER": IN_C, "TYPE": IN_C, "EXIT": IN_C, "LEADING": OUT_C}


class DPWalkthrough(Scene):
    def construct(self):
        A = TRACE["example"]["initial"]
        B = TRACE["example"]["goal"]
        n, m = len(A), len(B)
        out_cells = TRACE["cells"]["out"][0]
        in_cells = TRACE["cells"]["in"][0]
        delC = TRACE["delCost"]
        movC = TRACE["moveCost"]
        plans = TRACE["plans"]

        caption = VGroup()

        def say(text, size=26, wait=0.0):
            new = Text(text, font_size=size, color=OUT_C, line_spacing=0.9)
            new.to_edge(DOWN, buff=0.35)
            anims = [FadeIn(new, shift=0.15 * UP)]
            if len(caption):
                anims.append(FadeOut(caption[0]))
                caption.remove(caption[0])
            caption.add(new)
            self.play(*anims, run_time=0.6)
            if wait:
                self.wait(wait)

        # ---- Act 1: the problem -------------------------------------------
        title = Text("How vimficiency partitions a change", font_size=30, color=OUT_C)
        title.to_edge(UP, buff=0.4)
        src = Text(A, font=MONO, font_size=44, color=OUT_C)
        dst = Text(B, font=MONO, font_size=44, color=OUT_C)
        arrow = Arrow(LEFT * 0.6, RIGHT * 0.6, color=KEPT_C, stroke_width=4)
        VGroup(src, arrow, dst).arrange(RIGHT, buff=0.5).shift(UP * 0.8)
        self.play(FadeIn(title), Write(src), run_time=1.2)
        self.play(GrowArrow(arrow), Write(dst), run_time=1.0)

        def span_rect(text_mobj, lo, hi, color):
            return SurroundingRectangle(
                VGroup(*text_mobj[lo:hi]), color=color, buff=0.06, stroke_width=3
            )

        say("Plan 1: two edits — change aa, hop over b, change cc", wait=0.2)
        p1 = VGroup(
            span_rect(src, 0, 2, DEL_C), span_rect(src, 3, 5, DEL_C),
            span_rect(dst, 0, 2, INS_C), span_rect(dst, 3, 5, INS_C),
        )
        self.play(*map(Create, p1), run_time=0.9)
        self.wait(0.8)
        say("Plan 2: one edit — delete everything, retype everything", wait=0.2)
        p2 = VGroup(span_rect(src, 0, 5, DEL_C), span_rect(dst, 0, 5, INS_C))
        self.play(FadeOut(p1), *map(Create, p2), run_time=0.9)
        self.wait(0.8)
        say("Which costs fewer keystrokes?  Search every split at once.", wait=1.0)
        self.play(FadeOut(p2), FadeOut(arrow), FadeOut(title))

        # ---- Act 2: the grid ----------------------------------------------
        boxes = VGroup()
        dividers = VGroup()
        in_tints = VGroup()
        for i in range(n + 1):
            for j in range(m + 1):
                b = RoundedRectangle(corner_radius=0.05, width=BOX_W, height=BOX_W,
                                     stroke_color=BOX_C, stroke_width=1.4)
                b.move_to(node(i, j))
                boxes.add(b)
                dividers.add(Line(node(i, j) + LEFT * (BOX_W / 2 - 0.05),
                                  node(i, j) + RIGHT * (BOX_W / 2 - 0.05),
                                  stroke_color=BOX_C, stroke_width=1.0))
                tint = Rectangle(width=BOX_W - 0.06, height=BOX_W / 2 - 0.05,
                                 stroke_width=0, fill_color=IN_C, fill_opacity=0.06)
                tint.move_to(node(i, j) + DOWN * (BOX_W / 4 - 0.005))
                in_tints.add(tint)
        col_labels = VGroup(*[
            Text(glyph(B[j]), font=MONO, font_size=24, color=INS_C)
            .move_to(node(0, j) + (S / 2) * RIGHT + 0.55 * UP)
            for j in range(m)
        ])
        row_labels = VGroup(*[
            Text(glyph(A[i]), font=MONO, font_size=24, color=DEL_C)
            .move_to(node(i, 0) + (S / 2) * DOWN + 0.55 * LEFT)
            for i in range(n)
        ])
        goal_tag = Text("goal (typed) →", font_size=20, color=INS_C)
        goal_tag.next_to(col_labels, UP, buff=0.15).align_to(col_labels, LEFT)
        init_tag = Text("initial ↓", font_size=20, color=DEL_C)
        init_tag.next_to(row_labels, UP, buff=0.3).align_to(row_labels, RIGHT)

        say("Lay both strings on a grid of cells.")
        self.play(
            src.animate.scale(24 / 44).move_to(row_labels.get_center()).set_opacity(0),
            dst.animate.scale(24 / 44).move_to(col_labels.get_center()).set_opacity(0),
            LaggedStart(*[Create(b) for b in boxes], lag_ratio=0.008),
            FadeIn(dividers), FadeIn(in_tints),
            FadeIn(col_labels), FadeIn(row_labels), FadeIn(goal_tag), FadeIn(init_tag),
            run_time=1.8,
        )
        self.remove(src, dst)

        say("Cell (i, j): consumed i chars of the initial,\ntyped j chars of the goal — cheapest keystrokes to get there.")
        spot = SurroundingRectangle(boxes[3 * (m + 1) + 2], color=OUT_C, buff=0.03)
        self.play(Create(spot), run_time=0.6)
        self.wait(1.2)
        legend = VGroup(
            Text("top half: OUT · normal mode", font_size=20, color=OUT_C),
            Text("bottom half: IN · insert mode", font_size=20, color=IN_C),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.25).to_corner(UR, buff=0.6)
        say("Each cell holds two costs: normal mode on top,\ninsert mode below.", wait=0.4)
        self.play(FadeIn(legend), run_time=0.8)
        self.play(FadeOut(spot))

        # ---- Act 3: the moves ---------------------------------------------
        def arr(a, b, color):
            return Arrow(a, b, color=color, stroke_width=4, buff=0.28,
                         max_tip_length_to_length_ratio=0.18)

        def demo(mobjs, text, wait=1.6):
            say(text)
            self.play(*[Create(mo) if isinstance(mo, (Arrow, CurvedArrow, Line)) else FadeIn(mo) for mo in mobjs], run_time=0.8)
            self.wait(wait)
            self.play(*map(FadeOut, mobjs), run_time=0.4)

        d_arrow = arr(node(0, 0), node(2, 0), DEL_C)
        d_lbl = Text(f"de · {fmt(delC[0][2])} keys", font=MONO, font_size=20, color=DEL_C).next_to(d_arrow, RIGHT, buff=0.1)
        demo([d_arrow, d_lbl], "DELETE moves down: counted Vim deletes (x, dw, dd, D …)\npriced by the tiling oracle.")

        t_arrow = arr(node(2, 0), node(2, 2), IN_C)
        t_lbl = Text(f"ixx · {fmt(TRACE['enterExtra'][0])}+1+1 keys", font=MONO, font_size=20, color=IN_C).next_to(t_arrow, UP, buff=0.1)
        demo([t_arrow, t_lbl], "ENTER/TYPE moves right on the insert layer:\ni + the characters — the <Esc> is prepaid. EXIT back is free.")

        c_arrow = arr(node(0, 0), node(2, 1) + (BOX_W / 4) * DOWN, STEP_COLOR["CHANGE"])
        c_lbl = Text("cex · entry key free", font=MONO, font_size=20, color=STEP_COLOR["CHANGE"]).next_to(c_arrow, RIGHT, buff=0.1)
        demo([c_arrow, c_lbl], "CHANGE fuses them: the c-form deletes and lands in\ninsert mode directly — the entry key is never paid.")

        mv_arrow = arr(node(2, 2), node(4, 4), KEPT_C)
        mv_lbl = Text(f"w · {fmt(movC[2][4])} key", font=MONO, font_size=20, color=OUT_C).next_to(mv_arrow, RIGHT, buff=0.05).shift(0.1 * UP)
        demo([mv_arrow, mv_lbl], "MOVE slides along the diagonal — but only where the\ncharacters already match: motions never edit.")

        # ---- Act 4: the fill, in code order -------------------------------
        say("Fill in code order: down one column, then that column's\ndelete sweep — and every cell stores its backpointer.")
        vals = {}
        ticks = {}
        accepted = [e for e in TRACE["events"] if e["accepted"]]

        def half_center(table, i, j):
            return node(i, j) + (BOX_W / 4 - 0.005) * (UP if table == "out" else DOWN)

        def tick_for(e):
            if e["step"] == "LEADING":
                return None
            if e["step"] in ("MOVE", "CROSS", "CHANGE"):
                d = normalize(node(e["pi"], e["pj"]) - node(e["i"], e["j"]))
            elif e["step"] == "DELETE":
                d = UP
            elif e["step"] == "EXIT":
                d = DOWN
            else:
                d = LEFT
            p = node(e["i"], e["j"]) + np.array([
                -BOX_W / 2 + 0.1, 0.16 if e["table"] == "out" else -0.1, 0])
            return Line(p, p + d * 0.1, stroke_width=2.5, color=STEP_COLOR[e["step"]])

        col_hl = Rectangle(width=BOX_W + 0.08, height=(n + 1) * S + 0.15,
                           stroke_width=0, fill_color=OUT_C, fill_opacity=0.06)
        col_hl.move_to(node(n / 2, 0))
        self.add(col_hl)
        for j in range(m + 1):
            col_anims = []
            for e in [ev for ev in accepted if ev["j"] == j]:
                key = (e["table"], e["i"], e["j"])
                is_out = e["table"] == "out"
                t = Text(fmt(e["total"]), font=MONO, font_size=16 if is_out else 13,
                         color=OUT_C if is_out else IN_C)
                t.move_to(half_center(e["table"], e["i"], e["j"]))
                tk = tick_for(e)
                if key in vals:
                    sub = [Transform(vals[key], t),
                           Flash(t.get_center(), line_length=0.1, num_lines=8,
                                 flash_radius=0.18, color=STEP_COLOR[e["step"]])]
                    if tk is not None and key in ticks:
                        sub.append(Transform(ticks[key], tk))
                    col_anims.append(AnimationGroup(*sub))
                else:
                    vals[key] = t
                    sub = [FadeIn(t, scale=0.5)]
                    if tk is not None:
                        ticks[key] = tk
                        sub.append(FadeIn(tk))
                    col_anims.append(AnimationGroup(*sub))
            self.play(col_hl.animate.move_to(node(n / 2, j)),
                      LaggedStart(*col_anims, lag_ratio=0.3), run_time=1.15)
        self.play(FadeOut(col_hl), run_time=0.3)
        say("The colored ticks are the backpointers:\nwhich step won each cell, and from where.", wait=1.2)

        # ---- Act 4b: every candidate into one cell ------------------------
        ti, tj = 5, 5
        cand_events = [e for e in TRACE["events"]
                       if e["table"] == "out" and e["i"] == ti and e["j"] == tj]
        say("Watch one cell fill: every way in is priced,\nthe minimum wins.")
        self.play(FadeOut(legend), run_time=0.4)
        ring = SurroundingRectangle(boxes[ti * (m + 1) + tj], color=OPT_C, buff=0.03, stroke_width=3)
        self.play(Create(ring), run_time=0.5)

        rows = VGroup()
        arrows = VGroup()
        header = Text(f"candidates for cell ({ti},{tj})", font_size=20, color=OUT_C)
        header.to_corner(UR, buff=0.5).shift(1.2 * LEFT)
        self.play(FadeIn(header), run_time=0.4)
        del_rank = 0
        for e in cand_events:
            col = STEP_COLOR[e["step"]]
            base = e["total"] - e["add"]
            mark = "✓" if e["accepted"] else "·"
            row = Text(
                f"{e['step']:<7}({e['pi']},{e['pj']}) {fmt(base):>4}+{fmt(e['add'])} = {fmt(e['total']):>4} {mark}",
                font=MONO, font_size=16, color=col,
            )
            if len(rows):
                row.next_to(rows[-1], DOWN, buff=0.12, aligned_edge=LEFT)
            else:
                row.next_to(header, DOWN, buff=0.25).align_to(header, LEFT)
            rows.add(row)
            if e["step"] == "EXIT":
                a = CurvedArrow(node(ti, tj) + 0.5 * DOWN + 0.15 * LEFT,
                                node(ti, tj) + 0.5 * UP + 0.15 * LEFT,
                                color=col, stroke_width=3, angle=TAU / 4, tip_length=0.15)
            elif e["step"] == "DELETE":
                bend = 0.32 + 0.16 * del_rank
                del_rank += 1
                a = CurvedArrow(node(e["pi"], e["pj"]) + 0.3 * DOWN,
                                node(ti, tj) + 0.34 * UP,
                                color=col, stroke_width=3, angle=bend, tip_length=0.15)
            else:
                a = arr(node(e["pi"], e["pj"]), node(ti, tj), col)
            arrows.add(a)
            self.play(Create(a), FadeIn(row), run_time=0.45)
            if not e["accepted"]:
                self.play(a.animate.set_stroke(opacity=0.25).set_fill(opacity=0.25),
                          row.animate.set_opacity(0.4), run_time=0.25)
        winner = next(i for i, e in enumerate(cand_events) if e["accepted"])
        say("Three moves tie for the minimum — the first one in\nstays. Nothing else comes close.", wait=0.4)
        self.play(Indicate(rows[winner], color=OPT_C), Indicate(vals[("out", ti, tj)], color=OPT_C), run_time=1.0)
        self.wait(0.8)
        self.play(FadeOut(arrows), FadeOut(rows), FadeOut(header), FadeOut(ring), run_time=0.6)

        # ---- Act 5: the payoff --------------------------------------------
        def plan_path(regions, color):
            pts = [node(0, 0)]
            prevA = 0
            for r in regions:
                if r["aBegin"] > prevA:
                    pts.append(node(r["aBegin"], r["bBegin"]))
                pts.append(node(r["aEnd"], r["bBegin"]))
                pts.append(node(r["aEnd"], r["bEnd"]))
                prevA = r["aEnd"]
            if prevA < n:
                pts.append(node(n, m))
            path = VMobject(color=color, stroke_width=6)
            path.set_points_as_corners(pts[::-1])
            return path

        opt = plans[0]
        alt = plans[1]
        alt_path = plan_path(alt["regions"], ALT_C)

        say("The answer waits at (7,7) — walk the stored\nbackpointers home from the end.")
        opt_pts = [node(p["i"], p["j"]) + (BOX_W / 4 - 0.005) * (UP if p["table"] == "out" else DOWN)
                   for p in TRACE["optPath"]]
        goal_ring = SurroundingRectangle(boxes[n * (m + 1) + m], color=OPT_C, buff=0.03)
        self.play(Create(goal_ring), run_time=0.5)
        segs = VGroup()
        hop_lbls = VGroup()
        shown_type = False
        for idx in range(len(opt_pts) - 1, 0, -1):
            cur = TRACE["optPath"][idx]
            seg = Line(opt_pts[idx], opt_pts[idx - 1], color=OPT_C, stroke_width=6)
            segs.add(seg)
            anims = [Create(seg)]
            if cur["step"] != "TYPE" or not shown_type:
                shown_type = shown_type or cur["step"] == "TYPE"
                lbl = Text(cur["step"], font=MONO, font_size=15, color=STEP_COLOR[cur["step"]])
                lbl.next_to(seg.get_center(), UP if cur["step"] in ("TYPE", "ENTER") else RIGHT, buff=0.12)
                hop_lbls.add(lbl)
                anims.append(FadeIn(lbl))
            self.play(*anims, run_time=0.3)
        self.play(FadeOut(goal_ring), FadeOut(hop_lbls), run_time=0.4)
        tally = VGroup(
            Text("plan 2 · one edit", font_size=22, color=OPT_C),
            Text("Cxx b zz⎋", font=MONO, font_size=20, color=OUT_C),
            Text(f"del {fmt(opt['regions'][0]['del'])} + ins {fmt(opt['regions'][0]['ins'])}", font=MONO, font_size=20, color=OUT_C),
            Text(f"total  {fmt(opt['cost'])} ★", font=MONO, font_size=22, color=OPT_C),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.18).to_corner(UR, buff=0.5).shift(0.9 * DOWN)
        self.play(FadeIn(tally), run_time=0.8)
        self.wait(1.2)

        say("A K-best cell keeps runners-up too. The 'obvious'\ntwo-edit plan? It never caught up.")
        self.play(Create(alt_path), run_time=2.0)
        r0, r1 = alt["regions"]
        tally2 = VGroup(
            Text("plan 1 · two edits", font_size=22, color=ALT_C),
            Text(f"edits  {fmt(r0['del']+r0['ins'])} + {fmt(r1['del']+r1['ins'])}", font=MONO, font_size=20, color=OUT_C),
            Text(f"move       {fmt(r1['move'])}", font=MONO, font_size=20, color=OUT_C),
            Text(f"total     {fmt(alt['cost'])}", font=MONO, font_size=22, color=ALT_C),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.18).next_to(tally, DOWN, buff=0.5, aligned_edge=LEFT)
        self.play(FadeIn(tally2), run_time=0.8)
        self.wait(1.6)

        say("Two mode switches and a hop cost more than retyping ' b '.", wait=1.8)

        # ---- Act 6: real size — eliminate, then focus ---------------------
        say("Same job at real size — still just main().", wait=0.2)
        self.play(*[FadeOut(mo) for mo in self.mobjects if mo is not caption[0]],
                  run_time=0.8)

        A2 = TRACE2["example"]["initial"]
        B2 = TRACE2["example"]["goal"]
        blocks2 = TRACE2["blocks"]
        N2, M2 = len(A2), len(B2)
        plans2 = TRACE2["plans"]
        pA, pB = plans2[0], plans2[1]
        SEAL_C = "#57c47a"

        adv = Text("mmmmmmmmmm", font=MONO, font_size=13).width / 10
        LH = 0.205

        def make_panel(sname):
            rows = {}
            grp = VGroup()
            for li, ln in enumerate(sname.split("\n")):
                if not ln.strip():
                    continue
                t = Text(ln, font=MONO, font_size=13, color=OUT_C)
                indent = len(ln) - len(ln.lstrip(" "))
                t.move_to(np.array([indent * adv, -li * LH, 0]), aligned_edge=UL)
                rows[li] = t
                grp.add(t)
            return grp, rows

        def line_slices(sname, lo, hi):
            starts = [0] + [k + 1 for k, c in enumerate(sname) if c == "\n"]
            out = []
            for li, ls in enumerate(starts):
                le = (starts[li + 1] - 1) if li + 1 < len(starts) else len(sname)
                a, b = max(lo, ls), min(hi, le)
                if a >= b:
                    continue
                gl = sum(1 for c in sname[ls:a] if not c.isspace())
                gh = gl + sum(1 for c in sname[a:b] if not c.isspace())
                if gh > gl:
                    out.append((li, gl, gh))
            return out

        src2, src_rows = make_panel(A2)
        dst2, dst_rows = make_panel(B2)
        panels = VGroup(src2, dst2).arrange(DOWN, buff=0.3, aligned_edge=LEFT)
        panels.to_corner(UR, buff=0.35)

        def span_boxes(sname, rows, lo, hi, color):
            return [SurroundingRectangle(VGroup(*rows[li][gl:gh]), color=color,
                                         buff=0.045, stroke_width=2)
                    for li, gl, gh in line_slices(sname, lo, hi)]

        marks2 = VGroup()
        for r in pA["regions"]:
            for bx in span_boxes(A2, src_rows, r["aBegin"], r["aEnd"], DEL_C):
                marks2.add(bx)
            for bx in span_boxes(B2, dst_rows, r["bBegin"], r["bEnd"], INS_C):
                marks2.add(bx)
        say("Real edits: a new line, 10 → n, and += → -= foo(…).")
        self.play(Write(src2), Write(dst2), run_time=1.6)
        self.play(*map(Create, marks2), run_time=0.9)
        self.wait(0.6)

        sealA0, sealA1 = blocks2[0]["aEnd"], blocks2[1]["aBegin"]
        sealB0, sealB1 = blocks2[0]["bEnd"], blocks2[1]["bBegin"]
        say("First sweep: every matched run is tested against the\nseal gate. This one is long enough — sealed.")
        seal_glyphs = VGroup(
            *[g for li, gl, gh in line_slices(A2, sealA0, sealA1) for g in src_rows[li][gl:gh]],
            *[g for li, gl, gh in line_slices(B2, sealB0, sealB1) for g in dst_rows[li][gl:gh]])
        self.play(seal_glyphs.animate.set_color(SEAL_C), run_time=0.9)
        self.wait(0.8)
        say("Shorter matched runs stay in play — retyping a few\nchars can beat a worst-case tiling cut.", wait=1.2)

        S3 = min(4.6 / (M2 + 1), 4.2 / (N2 + 1))
        GX, GY = -6.85, 3.15

        def node3(i, j):
            return np.array([GX + j * S3, GY - i * S3, 0])

        full_rect = DashedVMobject(
            Rectangle(width=(M2 + 1) * S3, height=(N2 + 1) * S3,
                      stroke_color=DIM_C, stroke_width=2),
            num_dashes=90)
        full_rect.move_to(node3(N2 / 2, M2 / 2))
        block_rects = VGroup()
        free_lines = VGroup()
        for b in blocks2:
            w = (b["bEnd"] - b["bBegin"] + 1) * S3
            h = (b["aEnd"] - b["aBegin"] + 1) * S3
            br = Rectangle(width=w, height=h, stroke_color=OUT_C, stroke_width=2,
                           fill_color=OUT_C, fill_opacity=0.04)
            br.move_to(node3((b["aBegin"] + b["aEnd"]) / 2, (b["bBegin"] + b["bEnd"]) / 2))
            block_rects.add(br)
            if b["lead"]:
                free_lines.add(DashedLine(node3(b["aBegin"], b["bBegin"]),
                                          node3(b["aBegin"] + b["lead"], b["bBegin"] + b["lead"]),
                                          color=INS_C, stroke_width=2, dash_length=0.05))
            if b["trail"]:
                free_lines.add(DashedLine(node3(b["aEnd"] - b["trail"], b["bEnd"] - b["trail"]),
                                          node3(b["aEnd"], b["bEnd"]),
                                          color=INS_C, stroke_width=2, dash_length=0.05))
        seal_line = Line(node3(sealA0, sealB0), node3(sealA1, sealB1),
                         color=SEAL_C, stroke_width=4)
        seal_lbl = Text("sealed", font_size=16, color=SEAL_C)
        seal_lbl.next_to(seal_line.get_center(), UR, buff=0.06)

        full_cells = (N2 + 1) * (M2 + 1)
        block_cells = sum((b["aEnd"] - b["aBegin"] + 1) * (b["bEnd"] - b["bBegin"] + 1)
                          for b in blocks2)
        say("Now the grid — the seal splits it,\nand only the blocks are ever built.")
        self.play(Create(full_rect), run_time=1.0)
        self.play(*map(Create, block_rects), Create(seal_line), FadeIn(seal_lbl),
                  *map(Create, free_lines), run_time=1.4)
        say(f"{full_cells:,} cells shrink to {block_cells:,} —\n"
            f"{100 - round(100 * block_cells / full_cells)}% eliminated before the DP runs.", wait=1.4)

        win = TRACE2["insetWin"]
        i0, i1, j0, j1 = win["i0"], win["i1"], win["j0"], win["j1"]
        S4 = 0.3
        IX, IY = 0.5, 2.2

        def node4(i, j):
            return np.array([IX + (j - j0) * S4, IY - (i - i0) * S4, 0])

        marker = Rectangle(width=(j1 - j0 + 1) * S3, height=(i1 - i0 + 1) * S3,
                           stroke_color=OPT_C, stroke_width=2)
        marker.move_to(node3((i0 + i1) / 2, (j0 + j1) / 2))
        inset_frame = Rectangle(width=(j1 - j0 + 2) * S4, height=(i1 - i0 + 2) * S4,
                                stroke_color=OPT_C, stroke_width=1.5)
        inset_frame.move_to(node4((i0 + i1) / 2, (j0 + j1) / 2))
        callout = VGroup(
            Line(marker.get_corner(UR), inset_frame.get_corner(UL), color=OPT_C, stroke_width=1.2),
            Line(marker.get_corner(DR), inset_frame.get_corner(DL), color=OPT_C, stroke_width=1.2))
        say("Focus on what remains: magnify block 1, right where\nthe leading diagonal meets the first edit.")
        self.play(FadeOut(panels), FadeOut(marks2), run_time=0.5)
        self.play(Create(marker), Create(inset_frame), Create(callout), run_time=1.0)

        inset_stuff = VGroup()
        ibox = VGroup(*[
            RoundedRectangle(corner_radius=0.03, width=S4 - 0.04, height=S4 - 0.04,
                             stroke_color=BOX_C, stroke_width=1).move_to(node4(i, j))
            for i in range(i0, i1 + 1) for j in range(j0, j1 + 1)])
        inset_stuff.add(ibox)
        self.play(FadeIn(ibox), run_time=0.6)

        def fmt4(x):
            return str(int(round(x))) if abs(x - round(x)) < 1e-6 else f"{x:.1f}"

        def tick4(c, i, j, is_out):
            if c["step"] == "LEADING":
                return None
            if c["step"] in ("MOVE", "CROSS", "CHANGE"):
                d = normalize(node4(c["pi"], c["pj"]) - node4(i, j))
            elif c["step"] == "DELETE":
                d = UP
            elif c["step"] == "EXIT":
                d = DOWN
            else:
                d = LEFT
            p = node4(i, j) + np.array([-S4 / 2 + 0.05, 0.06 if is_out else -0.05, 0])
            return Line(p, p + d * 0.06, stroke_width=1.6, color=STEP_COLOR[c["step"]])

        say("Fill it exactly like the small grid: column by column,\nbackpointers and all.")
        for j in range(j0, j1 + 1):
            col_anims = []
            for i in range(i0, i1 + 1):
                for tbl in ("out", "in"):
                    c = TRACE2["insetOut" if tbl == "out" else "insetIn"][i - i0][j - j0]
                    if c is None:
                        continue
                    is_out = tbl == "out"
                    t = Text(fmt4(c["cost"]), font=MONO, font_size=9 if is_out else 8,
                             color=OUT_C if is_out else IN_C)
                    t.move_to(node4(i, j) + (S4 / 4 - 0.01) * (UP if is_out else DOWN))
                    inset_stuff.add(t)
                    sub = [FadeIn(t, scale=0.6)]
                    tk = tick4(c, i, j, is_out)
                    if tk is not None:
                        inset_stuff.add(tk)
                        sub.append(FadeIn(tk))
                    col_anims.append(AnimationGroup(*sub))
            if col_anims:
                self.play(LaggedStart(*col_anims, lag_ratio=0.2), run_time=0.5)
        self.wait(0.8)

        say("The rest fills the same way; one CROSS move rides the seal.")
        for k, b in enumerate(blocks2):
            if k:
                self.play(Indicate(seal_line, color=SEAL_C, scale_factor=1.05), run_time=0.7)
            bh = (b["aEnd"] - b["aBegin"]) * S3
            w = Rectangle(width=0.01, height=bh, stroke_width=0,
                          fill_color=OUT_C, fill_opacity=0.12)
            w.next_to(node3((b["aBegin"] + b["aEnd"]) / 2, b["bBegin"]), RIGHT, buff=0)
            self.add(w)
            self.play(w.animate.stretch_to_fit_width((b["bEnd"] - b["bBegin"]) * S3,
                                                     about_edge=LEFT), run_time=0.8)
            self.play(FadeOut(w), run_time=0.2)

        self.play(FadeOut(inset_stuff), FadeOut(inset_frame), FadeOut(callout),
                  FadeOut(marker), run_time=0.6)

        def plan_path3(regions, color, off=0.0):
            o = np.array([off, off, 0])
            pts = [node3(0, 0) + o]
            prevA = 0
            for r in regions:
                if r["aBegin"] > prevA:
                    pts.append(node3(r["aBegin"], r["bBegin"]) + o)
                pts.append(node3(r["aEnd"], r["bBegin"]) + o)
                pts.append(node3(r["aEnd"], r["bEnd"]) + o)
                prevA = r["aEnd"]
            if prevA < N2:
                pts.append(node3(N2, M2) + o)
            path = VMobject(color=color, stroke_width=3.5)
            path.set_points_as_corners(pts[::-1])
            return path

        say(f"Walk the backpointers home: {len(pA['regions'])} edits, one seal ride.")
        self.play(Create(plan_path3(pA["regions"], OPT_C)), run_time=2.4)
        n_tied = sum(1 for p in plans2 if abs(p["cost"] - pA["cost"]) < 1e-9)
        tallyA = Text(f"plan A ★ {fmt(pA['cost'])} · {len(pA['regions'])} edits",
                      font=MONO, font_size=19, color=OPT_C)
        tallyA.move_to(np.array([3.4, 1.6, 0]))
        self.play(FadeIn(tallyA), run_time=0.5)
        self.wait(0.8)
        say(f"maxPlans = 2 keeps the runner-up — {n_tied} partitions\ntie at {fmt(pA['cost'])}; the exact search decides.")
        self.play(Create(plan_path3(pB["regions"], ALT_C, off=0.03)), run_time=1.8)
        tallyB = Text(f"plan B   {fmt(pB['cost'])} · {len(pB['regions'])} edits",
                      font=MONO, font_size=19, color=ALT_C)
        tallyB.next_to(tallyA, DOWN, buff=0.2, aligned_edge=LEFT)
        self.play(FadeIn(tallyB), run_time=0.5)
        self.wait(1.4)

        say("Every plan then meets an exact shortest-path search\nthat realizes each edit with real Vim commands.", size=28)
        self.wait(2.4)
