"""eval(position) must equal eval(colour-flipped position), both from the side to move.
usage: python source/tests/eval_symmetry.py ENGINE [n]"""
import random, subprocess, sys
import chess
eng, n = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 500
rng = random.Random(3)
fens = []
while len(fens) < n:
    b = chess.Board()
    for _ in range(rng.randint(5, 80)):
        ms = list(b.legal_moves)
        if not ms: break
        b.push(rng.choice(ms))
    if not b.is_game_over():
        fens += [b.fen(), b.mirror().fen()]
cmds = "".join(f"position fen {f}\neval\n" for f in fens) + "quit\n"
err = subprocess.run([eng], input=cmds, capture_output=True, text=True).stderr
vals = [int(l.split()[-1]) for l in err.splitlines() if l.startswith("eval")]
bad = [(fens[i], vals[i], vals[i + 1]) for i in range(0, len(vals), 2) if vals[i] != vals[i + 1]]
print(f"{len(vals)//2} pairs, {len(bad)} asymmetric")
for b in bad[:5]: print(" ", b)
sys.exit(1 if bad else 0)
