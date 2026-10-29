"""Runs the Chess Programming Wiki perft positions through our engine's `perft` command.
usage: python source/tests/perft_suite.py engine/Corvus.exe [--quick]"""
import subprocess, sys, re, time

SUITE = [
    ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 6, 119060324),
    ("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 5, 193690690),
    ("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 7, 178633661),
    ("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 6, 706045033),
    ("r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", 6, 706045033),
    ("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 5, 89941194),
    ("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 5, 164075551),
]
QUICK = [
    (SUITE[0][0], 5, 4865609), (SUITE[1][0], 4, 4085603), (SUITE[2][0], 6, 11030083),
    (SUITE[3][0], 5, 15833292), (SUITE[4][0], 5, 15833292), (SUITE[5][0], 4, 2103487),
    (SUITE[6][0], 4, 3894594),
]

def run(exe, fen, d):
    t = time.time()
    out = subprocess.run([exe, "perft", str(d), fen], capture_output=True, text=True).stdout
    n = int(re.search(r"Nodes searched: (\d+)", out).group(1))
    return n, time.time() - t

def main():
    exe = sys.argv[1]
    suite = QUICK if "--quick" in sys.argv else SUITE
    ok = True; tot = 0; secs = 0
    print("| FEN | depth | expected | got | ok | time s |\n|---|---|---|---|---|---|")
    for fen, d, want in suite:
        got, s = run(exe, fen, d)
        tot += got; secs += s
        good = got == want; ok &= good
        print(f"| `{fen}` | {d} | {want:,} | {got:,} | {'yes' if good else 'NO'} | {s:.2f} |")
    print(f"\ntotal {tot:,} nodes in {secs:.1f}s = {tot/secs/1e6:.1f} Mnps (legal movegen, bulk count at leaves)")
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)

main()
