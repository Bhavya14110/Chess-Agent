"""quick W/D/L + Elo estimate for engine NAME from a fastchess pgn. usage: pgn_score.py file.pgn [name]"""
import math, re, sys
text = open(sys.argv[1], encoding="utf-8", errors="ignore").read()
name = sys.argv[2] if len(sys.argv) > 2 else "new"
w = d = l = 0
for g in text.split("[Event ")[1:]:
    white = re.search(r'\[White "([^"]*)"\]', g).group(1)
    res = re.search(r'\[Result "([^"]*)"\]', g).group(1)
    if res == "1/2-1/2": d += 1
    elif (res == "1-0") == (white == name): w += 1
    elif res in ("1-0", "0-1"): l += 1
n = w + d + l
if n:
    s = (w + d / 2) / n
    elo = -400 * math.log10(1 / s - 1) if 0 < s < 1 else float("inf") * (1 if s else -1)
    print(f"{name}: +{w} ={d} -{l} ({n} games) score {s:.3f} elo {elo:+.1f}")
