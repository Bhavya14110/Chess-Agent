"""Cross-checks our move generator against python-chess on many positions.
Positions come from a PGN file (Lichess games) if given, else from random playouts.
usage: python source/tests/perft_random.py ENGINE [--pgn file.pgn] [--n 1000] [--depth 3]"""
import argparse, random, subprocess, multiprocessing as mp
import chess, chess.pgn

def pyperft(fen, d):
    b = chess.Board(fen)
    def rec(d):
        if d == 0: return 1
        if d == 1: return b.legal_moves.count()
        n = 0
        for m in b.legal_moves:
            b.push(m); n += rec(d - 1); b.pop()
        return n
    return rec(d)

def from_pgn(path, n, rng):
    fens = []
    with open(path, encoding="utf-8", errors="ignore") as f:
        while len(fens) < n * 3:
            g = chess.pgn.read_game(f)
            if g is None: break
            b = g.board()
            for i, m in enumerate(g.mainline_moves()):
                b.push(m)
                if rng.random() < 0.05: fens.append(b.fen())
    rng.shuffle(fens)
    return fens[:n]

def from_random(n, rng):
    fens = []
    while len(fens) < n:
        b = chess.Board()
        for _ in range(rng.randint(4, 120)):
            ms = list(b.legal_moves)
            if not ms: break
            # bias towards captures/promos so the endgame-ish stuff gets covered
            caps = [m for m in ms if b.is_capture(m) or m.promotion]
            b.push(rng.choice(caps if caps and rng.random() < 0.3 else ms))
            if b.is_game_over(): break
        if not b.is_game_over(): fens.append(b.fen())
    return fens

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("engine"); ap.add_argument("--pgn"); ap.add_argument("--n", type=int, default=1000)
    ap.add_argument("--depth", type=int, default=3); ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    fens = from_pgn(a.pgn, a.n, rng) if a.pgn else from_random(a.n, rng)
    out = subprocess.run([a.engine, "perftlist", str(a.depth)], input="\n".join(fens) + "\n",
                         capture_output=True, text=True).stdout.split("\n")
    with mp.Pool() as pool:
        want = pool.starmap(pyperft, [(f, a.depth) for f in fens])
    bad = 0
    for fen, w, line in zip(fens, want, out):
        got, keyok = map(int, line.split())
        if got != w or not keyok:
            bad += 1; print("MISMATCH", fen, "py", w, "ours", got, "keyok", keyok)
    print(f"{len(fens)} positions, depth {a.depth}, source={'pgn' if a.pgn else 'random playouts'}: "
          f"{len(fens)-bad} ok, {bad} bad")
    raise SystemExit(1 if bad else 0)

if __name__ == "__main__":
    main()
