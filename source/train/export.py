"""Checkpoint (.pt) -> quantised Corvus .nnue file. Written from scratch.

Layout (little-endian):
  b"CVNN", u32 version=1, u32 H,
  ft_w_q  int16[768*H]  (feature-major: index f*H + j)   = round(ft_w * QA)
  ft_b_q  int16[H]                                        = round(ft_b * QA)
  out_w_q int16[2H]  (first H: side-to-move half)         = round(out_w * QB)
  out_b_q int32[1]                                        = round(out_b * QA * QB)

Usage:
  python source/train/export.py source/train/checkpoints/run/last.pt corvus.nnue
"""
from __future__ import annotations

import argparse
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

QA = 255  # must equal model.QA
QB = 64   # must equal model.QB
MAGIC = b"CVNN"
VERSION = 1
MAX_ACTIVE = 32


def quantise(ft_w, ft_b, out_w, out_b):
    """float params -> int arrays (np.round = round half to even; values never sit on .5
    in practice, the C++ side only consumes the integers so the rule does not matter)."""
    ft_w_q = np.round(ft_w * QA)
    ft_b_q = np.round(ft_b * QA)
    out_w_q = np.round(out_w * QB)
    out_b_q = np.round(out_b * QA * QB)
    for name, a, lo, hi in (("ft_w", ft_w_q, -32768, 32767), ("ft_b", ft_b_q, -32768, 32767),
                            ("out_w", out_w_q, -32768, 32767),
                            ("out_b", np.array([out_b_q]), -2**31, 2**31 - 1)):
        if a.min() < lo or a.max() > hi:
            raise OverflowError(f"{name} does not fit its integer type")
    ft_w_q = ft_w_q.astype(np.int16)
    ft_b_q = ft_b_q.astype(np.int16)
    out_w_q = out_w_q.astype(np.int16)
    out_b_q = np.int32(out_b_q)
    # worst-case accumulator: bias + MAX_ACTIVE largest-magnitude rows per column
    worst = np.abs(ft_b_q.astype(np.int64)) + MAX_ACTIVE * np.abs(ft_w_q.astype(np.int64)).max(axis=0)
    if worst.max() > 32767:
        raise OverflowError(f"int16 accumulator could overflow (worst case {worst.max()})")
    return ft_w_q, ft_b_q, out_w_q, out_b_q, int(worst.max())


def write_nnue(path, ft_w_q, ft_b_q, out_w_q, out_b_q):
    H = ft_b_q.shape[0]
    assert ft_w_q.shape == (768, H) and out_w_q.shape == (2 * H,)
    with open(path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<II", VERSION, H))
        f.write(np.ascontiguousarray(ft_w_q, dtype="<i2").tobytes())
        f.write(np.ascontiguousarray(ft_b_q, dtype="<i2").tobytes())
        f.write(np.ascontiguousarray(out_w_q, dtype="<i2").tobytes())
        f.write(struct.pack("<i", int(out_b_q)))


def read_nnue(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != MAGIC:
        raise ValueError("bad magic")
    version, H = struct.unpack_from("<II", data, 4)
    if version != VERSION:
        raise ValueError(f"unsupported version {version}")
    o = 12
    ft_w = np.frombuffer(data, "<i2", 768 * H, o).reshape(768, H); o += 768 * H * 2
    ft_b = np.frombuffer(data, "<i2", H, o); o += H * 2
    out_w = np.frombuffer(data, "<i2", 2 * H, o); o += 2 * H * 2
    out_b = struct.unpack_from("<i", data, o)[0]; o += 4
    if o != len(data):
        raise ValueError(f"trailing bytes in {path}: expected {o}, got {len(data)}")
    return H, ft_w, ft_b, out_w, out_b


def main(argv=None):
    p = argparse.ArgumentParser(description="export a Corvus checkpoint to .nnue")
    p.add_argument("checkpoint")
    p.add_argument("output")
    a = p.parse_args(argv)
    import model as M  # torch only needed for exporting, not for read_nnue
    assert (M.QA, M.QB) == (QA, QB)
    model = M.load_model_from_checkpoint(a.checkpoint)
    ft_w_q, ft_b_q, out_w_q, out_b_q, worst = quantise(*model.float_params())
    write_nnue(a.output, ft_w_q, ft_b_q, out_w_q, out_b_q)
    print(f"wrote {a.output}: H={model.hidden}, {os.path.getsize(a.output)} bytes, "
          f"worst-case |acc| {worst} (int16 limit 32767), "
          f"max|ft_w_q| {np.abs(ft_w_q).max()}, max|out_w_q| {np.abs(out_w_q).max()}, out_b_q {int(out_b_q)}")


if __name__ == "__main__":
    main()
