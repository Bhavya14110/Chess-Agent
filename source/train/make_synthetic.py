"""SYNTHETIC PIPELINE-TEST DATA ONLY.

!!! This data exists only to test the training / export / inference pipeline before
!!! Corvus' own self-play datagen is ready. It is NEVER to be used to train a real net.
!!! Real Corvus nets are trained exclusively on Corvus self-play data.

Positions come from uniformly random legal playouts (python-chess). Labels are a toy
function of the position, not any engine's evaluation:
  score  = 100 * (white material - black material), P=1 N=3 B=3 R=5 Q=9, white POV
  result = 2 if material > 0, 0 if < 0, 1 if equal

Written from scratch for Corvus.

Usage:
  python source/train/make_synthetic.py --out data/synthetic/synth.bin --positions 2000000
"""
from __future__ import annotations

import argparse
import multiprocessing as mp
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

VALUES = {1: 1, 2: 3, 3: 3, 4: 5, 5: 9, 6: 0}


def _material(board) -> int:
    import chess
    m = 0
    for pt, v in VALUES.items():
        if v:
            m += v * (len(board.pieces(pt, chess.WHITE)) - len(board.pieces(pt, chess.BLACK)))
    return m


def _worker(job):
    seed, n_positions = job
    import chess
    from dataset import encode_record

    rnd = random.Random(seed)
    out = []
    count = 0
    while count < n_positions:
        board = chess.Board()
        plies = rnd.randint(20, 300)
        for ply in range(plies):
            moves = list(board.legal_moves)
            if not moves:
                break
            board.push(rnd.choice(moves))
            if ply >= 4 and rnd.random() < 0.5:   # skip some positions to decorrelate
                m = _material(board)
                res = 2 if m > 0 else (0 if m < 0 else 1)
                out.append(encode_record(board, 100 * m, res))
                count += 1
                if count >= n_positions:
                    break
    return b"".join(out)


def main(argv=None):
    p = argparse.ArgumentParser(description="SYNTHETIC pipeline-test data (never for real nets)")
    p.add_argument("--out", required=True)
    p.add_argument("--positions", type=int, default=2_000_000)
    p.add_argument("--seed", type=int, default=2024)
    p.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    a = p.parse_args(argv)

    per_job = 25_000
    jobs = []
    left = a.positions
    j = 0
    while left > 0:
        jobs.append((a.seed * 100_003 + j, min(per_job, left)))
        left -= per_job
        j += 1
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    t0 = time.time()
    with mp.Pool(a.workers) as pool, open(a.out, "wb") as f:
        for blob in pool.imap(_worker, jobs):   # imap keeps job order => deterministic file
            f.write(blob)
    print(f"wrote {a.positions} SYNTHETIC positions to {a.out} in {time.time() - t0:.1f}s")


if __name__ == "__main__":
    main()
