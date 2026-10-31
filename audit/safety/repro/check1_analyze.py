"""Check 1 analysis: reads the lag-run PGNs (fastchess comments "{eval/depth 1.234s, tl=56.789s, latency=0.250s}"),
counts failures, finds the lowest clock reached and writes a chart of time used per move and clock left vs move number.

usage: python check1_analyze.py OUT.png LABEL=games.pgn [LABEL=games.pgn ...]
"""
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

MOVE = re.compile(r"\{[^}]*?([\d.]+)s, tl=(-?[\d.]+)s(?:, latency=(-?[\d.]+)s)?[^}]*\}")
BAD = ("time", "illegal", "disconnect", "stall", "crash", "abandon")
COLORS = ["#2a78d6", "#eb6834"]           # reference palette, categorical slots 1-2 (light mode)
INK, MUTED, GRID, SURFACE = "#1f1f1e", "#6b6a64", "#e4e3df", "#fcfcfb"


def games(path):
    text = open(path, encoding="utf-8", errors="ignore").read()
    for g in re.split(r"(?=\[Event )", text):
        if not g.strip():
            continue
        tags = dict(re.findall(r'\[(\w+) "([^"]*)"\]', g))
        body = g[g.rfind("]") + 1:]
        moves = [(float(a), float(b), float(c) if c else None) for a, b, c in MOVE.findall(body)]
        end = re.findall(r"\{([^{}]*)\}\s*(?:1-0|0-1|1/2-1/2|\*)\s*$", body.strip())
        yield tags, moves, (end[-1] if end else "")


def main():
    out = sys.argv[1]
    runs = [a.split("=", 1) for a in sys.argv[2:]]
    fig, axes = plt.subplots(2, len(runs), figsize=(6.2 * len(runs), 7.2), squeeze=False, facecolor=SURFACE)
    overall_ok = True
    for col, (label, path) in enumerate(runs):
        color = COLORS[col % len(COLORS)]
        n = fails = 0
        lowest = (1e9, None)
        maxused = (0.0, None)
        lats = []
        xs, used, clocks = [], [], []
        for tags, moves, reason in games(path):
            n += 1
            term = tags.get("Termination", "")
            if any(w in term.lower() for w in BAD) or any(w in reason.lower() for w in ("on time", "illegal", "disconnect", "stall", "crash")):
                fails += 1
                print(f"  FAILURE in {label} round {tags.get('Round')}: {term} {reason}")
            side = {0: ([], []), 1: ([], [])}   # one clock line per side
            for i, (u, tl, lat) in enumerate(moves):
                mv = i // 2 + 1                       # full-move number from the game start
                xs.append(mv)
                used.append(u)
                side[i % 2][0].append(mv)
                side[i % 2][1].append(tl)
                if tl < lowest[0]:
                    lowest = (tl, f"round {tags.get('Round')}, move {mv}, {'white' if i % 2 == 0 else 'black'}")
                if u > maxused[0]:
                    maxused = (u, f"round {tags.get('Round')}, move {mv}")
                if lat is not None:
                    lats.append(lat)
            clocks.extend(side.values())
        ok = fails == 0 and n > 0
        overall_ok &= ok
        print(f"{label}: games {n}, failures (time/illegal/crash/stall) {fails}, moves {len(used)}")
        print(f"  lowest clock left after a move: {lowest[0]:.3f}s ({lowest[1]})")
        lats.sort()
        q = (lambda p: lats[min(len(lats) - 1, int(p * len(lats)))]) if lats else (lambda p: float("nan"))
        print(f"  longest single move: {maxused[0]:.3f}s ({maxused[1]})")
        # fastchess latency = measured time - the engine's last reported 'info time', so it is the lag
        # plus whatever the engine searched after its last info line (outliers are not real delays)
        print(f"  fastchess latency: median {q(0.5):.3f}s, p99 {q(0.99):.3f}s, max {lats[-1] if lats else 0:.3f}s")
        print(f"  {'PASS' if ok else 'FAIL'}")

        ax = axes[0][col]
        ax.scatter(xs, used, s=9, color=color, alpha=0.45, linewidths=0)
        ax.set_title(f"{label}: time used per move ({n} games)", color=INK, fontsize=11, loc="left")
        ax.set_ylabel("seconds", color=MUTED)
        ax2 = axes[1][col]
        for gx, gc in clocks:
            ax2.plot(gx, gc, color=color, linewidth=1.2, alpha=0.6)
        ax2.axhline(lowest[0], color=MUTED, linewidth=1, linestyle="--")
        ax2.annotate(f"lowest {lowest[0]:.1f}s", xy=(0.99, lowest[0]), xycoords=("axes fraction", "data"),
                     ha="right", va="bottom", color=INK, fontsize=9)
        ax2.set_title(f"{label}: clock left after each move (one line per side)", color=INK, fontsize=11, loc="left")
        ax2.set_ylabel("seconds", color=MUTED)
        ax2.set_xlabel("move number", color=MUTED)
        for a in (ax, ax2):
            a.set_facecolor(SURFACE)
            a.grid(color=GRID, linewidth=0.8)
            a.set_axisbelow(True)
            a.set_ylim(bottom=0)
            a.tick_params(colors=MUTED, labelsize=9)
            for s in ("top", "right"):
                a.spines[s].set_visible(False)
            for s in ("left", "bottom"):
                a.spines[s].set_color(GRID)
    fig.suptitle("Corvus self-play, 100-300 ms lag per engine line",
                 color=INK, fontsize=12, x=0.01, ha="left")
    fig.tight_layout()
    fig.savefig(out, dpi=130, facecolor=SURFACE)
    print(f"chart: {out}")
    print("check1:", "PASS" if overall_ok else "FAIL")
    sys.exit(0 if overall_ok else 1)


if __name__ == "__main__":
    main()
