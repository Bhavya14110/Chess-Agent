"""Pure-numpy integer reference inference for Corvus .nnue files. Written from scratch.

This is the bit-exact specification the C++ engine must reproduce:

  acc[persp][j] = ft_b_q[j] + sum_{f active in persp} ft_w_q[f][j]            (fits int16)
  sum = sum_j clamp(acc[stm][j],0,QA)^2 * out_w_q[j]
      + sum_j clamp(acc[nstm][j],0,QA)^2 * out_w_q[H+j]                         (int64 here)
  t    = trunc_div(sum, QA)          # C-style truncation toward zero
  t    = t + out_b_q
  eval = trunc_div(t * 400, QA*QB)   # centipawns, side to move's point of view

Modes:
  python quant_ref.py --net corvus.nnue --fen "<FEN>"
  python quant_ref.py --net corvus.nnue --data "data/*.bin" --count 10000 \
        --vectors testvectors.txt --vector-lines 1000 [--checkpoint run/last.pt]
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dataset import RecordStore, decode_pieces, feature_indices, fen_features, record_to_fen  # noqa: E402
from export import read_nnue  # noqa: E402

QA, QB, SCALE = 255, 64, 400


def trunc_div(a, b: int):
    """Integer division truncating toward zero (C semantics), b > 0."""
    a = np.asarray(a, dtype=np.int64)
    q = np.abs(a) // b
    return np.where(a < 0, -q, q)


def _accumulate(weights, bias, idx, offsets):
    """bias + sum of rows per bag. weights [768,H], idx flat, offsets [N]."""
    rows = weights[idx]
    sums = np.add.reduceat(rows, offsets, axis=0) if len(idx) else np.zeros((len(offsets), weights.shape[1]), weights.dtype)
    # reduceat misbehaves for empty bags; positions always have 2 kings so bags are non-empty
    return sums + bias


def int_eval(net, stm_idx, nstm_idx, offsets, stats=None):
    H, ft_w, ft_b, out_w, out_b = net
    w = ft_w.astype(np.int64)
    b = ft_b.astype(np.int64)
    acc_s = _accumulate(w, b, stm_idx, offsets)
    acc_n = _accumulate(w, b, nstm_idx, offsets)
    lim = max(np.abs(acc_s).max(), np.abs(acc_n).max())
    if lim > 32767:
        raise OverflowError(f"accumulator {lim} overflows int16")
    vs = np.clip(acc_s, 0, QA)
    vn = np.clip(acc_n, 0, QA)
    ow = out_w.astype(np.int64)
    s = (vs * vs * ow[:H]).sum(axis=1) + (vn * vn * ow[H:]).sum(axis=1)
    if stats is not None:
        stats["max_abs_acc"] = max(stats.get("max_abs_acc", 0), int(lim))
        stats["max_abs_sum"] = max(stats.get("max_abs_sum", 0), int(np.abs(s).max()))
    t = trunc_div(s, QA) + int(out_b)
    return trunc_div(t * SCALE, QA * QB)


def float_eval(params, stm_idx, nstm_idx, offsets):
    ft_w, ft_b, out_w, out_b = params
    H = ft_b.shape[0]
    a = np.clip(_accumulate(ft_w, ft_b, stm_idx, offsets), 0, 1) ** 2
    c = np.clip(_accumulate(ft_w, ft_b, nstm_idx, offsets), 0, 1) ** 2
    return (a @ out_w[:H] + c @ out_w[H:] + out_b) * SCALE


def eval_fen(net, fen: str) -> int:
    s, n = fen_features(fen)
    return int(int_eval(net, s, n, np.array([0], dtype=np.int64))[0])


def records_features(recs):
    row, sq, code, counts = decode_pieces(recs)
    w, b = feature_indices(sq, code)
    btm = recs["stm"].astype(np.int64)[row] == 1
    offsets = np.zeros(len(recs), dtype=np.int64)
    np.cumsum(counts[:-1], out=offsets[1:])
    return np.where(btm, b, w), np.where(btm, w, b), offsets


def main(argv=None):
    p = argparse.ArgumentParser(description="Corvus integer NNUE reference")
    p.add_argument("--net", required=True)
    p.add_argument("--fen", default=None)
    p.add_argument("--data", default=None, help="glob of .bin files to draw positions from")
    p.add_argument("--count", type=int, default=10000, help="positions to evaluate")
    p.add_argument("--seed", type=int, default=7)
    p.add_argument("--vectors", default=None, help="write 'FEN;eval' lines here")
    p.add_argument("--vector-lines", type=int, default=1000)
    p.add_argument("--checkpoint", default=None, help="float checkpoint to compare against")
    a = p.parse_args(argv)

    net = read_nnue(a.net)
    if a.fen:
        print(eval_fen(net, a.fen))
        return
    if not a.data:
        raise SystemExit("need --fen or --data")

    store = RecordStore(a.data)
    rng = np.random.default_rng(a.seed)
    pick = np.sort(rng.choice(len(store), size=min(a.count, len(store)), replace=False))
    recs = store.gather(pick)

    params = None
    if a.checkpoint:
        from model import load_model_from_checkpoint
        params = load_model_from_checkpoint(a.checkpoint).float_params()

    stats: dict = {}
    q_all, f_all = [], []
    for lo in range(0, len(recs), 1000):
        chunk = recs[lo:lo + 1000]
        s, n, off = records_features(chunk)
        q_all.append(int_eval(net, s, n, off, stats))
        if params is not None:
            f_all.append(float_eval(params, s, n, off))
    q = np.concatenate(q_all)
    print(f"evaluated {len(q)} positions: quantised eval mean {q.mean():.1f} cp, "
          f"min {q.min()}, max {q.max()}; max|acc| {stats['max_abs_acc']}, "
          f"max|sum| {stats['max_abs_sum']} (int32 limit {2**31 - 1})")

    # cross-check: the FEN path must give the same integers as the record path
    fens = [record_to_fen(r) for r in recs[:min(len(recs), max(a.vector_lines, 200))]]
    for i, fen in enumerate(fens[:200]):
        assert eval_fen(net, fen) == q[i], (fen, eval_fen(net, fen), q[i])

    if params is not None:
        fl = np.concatenate(f_all)
        d = np.abs(fl - q)
        print(f"float vs quantised over {len(q)} positions: max |diff| {d.max():.2f} cp, "
              f"mean |diff| {d.mean():.3f} cp, float eval mean |{np.abs(fl).mean():.1f}| cp")
    if a.vectors:
        with open(a.vectors, "w", newline="\n") as f:
            for i in range(min(a.vector_lines, len(fens))):
                f.write(f"{fens[i]};{int(q[i])}\n")
        print(f"wrote {min(a.vector_lines, len(fens))} test vectors to {a.vectors}")


if __name__ == "__main__":
    main()
