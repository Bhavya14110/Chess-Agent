"""Builds our own test-opening suite: random 6-8 ply walks from the start position,
kept only if our own engine scores them within +-maxcp. No external books or engine-scored suites.
usage: python source/tools/make_openings.py ENGINE OUT.epd [--n 10000] [--seed 1] [--depth 8]"""
import argparse, random, subprocess
from concurrent.futures import ThreadPoolExecutor
import chess


def score_fens(engine, fens, depth):
    p = subprocess.Popen([engine], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True, bufsize=1)
    p.stdin.write("uci\nsetoption name Hash value 16\nisready\n")
    p.stdin.flush()
    while p.stdout.readline().strip() != "readyok":
        pass
    out = []
    for fen in fens:
        p.stdin.write(f"ucinewgame\nposition fen {fen}\ngo depth {depth}\n")
        p.stdin.flush()
        score = None
        while True:
            line = p.stdout.readline().strip()
            if line.startswith("info") and " score " in line:
                t = line.split()
                i = t.index("score")
                score = int(t[i + 2]) if t[i + 1] == "cp" else 100000
            if line.startswith("bestmove"):
                break
        out.append((fen, score))
    p.stdin.write("quit\n")
    p.stdin.flush()
    p.wait()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("engine"); ap.add_argument("out")
    ap.add_argument("--n", type=int, default=10000); ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--depth", type=int, default=8); ap.add_argument("--maxcp", type=int, default=100)
    ap.add_argument("--workers", type=int, default=16)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    seen, cands = set(), []
    while len(cands) < a.n * 3:
        b = chess.Board()
        for _ in range(rng.choice([6, 7, 8])):
            ms = list(b.legal_moves)
            if not ms:
                break
            b.push(rng.choice(ms))
        if b.is_game_over():
            continue
        key = b.epd()
        if key not in seen:
            seen.add(key)
            cands.append(b.fen())
    chunks = [cands[i::a.workers] for i in range(a.workers)]
    with ThreadPoolExecutor(a.workers) as ex:
        res = [r for part in ex.map(lambda c: score_fens(a.engine, c, a.depth), chunks) for r in part]
    keep = [f for f, s in res if s is not None and abs(s) <= a.maxcp]
    rng.shuffle(keep)
    keep = keep[: a.n]
    with open(a.out, "w") as f:
        for fen in keep:
            f.write(" ".join(fen.split()[:4]) + "\n")
    print(f"{len(cands)} candidates, {len(keep)} kept (|eval| <= {a.maxcp} at depth {a.depth}) -> {a.out}")


main()
