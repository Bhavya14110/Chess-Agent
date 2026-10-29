"""Corvus NNUE training data: record format, vectorised decoder, shuffled batch loader.

Written from scratch for the Corvus engine.

Record layout (32 bytes, little-endian), produced by Corvus' own self-play datagen:
  0..7    u64  occupancy bitboard (bit i = square i, a1=0 ... h8=63)
  8..23   16 B 32 nibbles of piece codes, one per set bit of the occupancy in increasing
               square order; nibble k = low nibble of byte 8+k//2 if k even, high if k odd.
               Piece code: 0..5 = white P,N,B,R,Q,K ; 6..11 = black P,N,B,R,Q,K
  24..25  i16  score, centipawns, WHITE's point of view (Corvus search score)
  26      u8   result: 0 black won, 1 draw, 2 white won
  27      u8   side to move: 0 white, 1 black
  28..29  u16  fullmove number
  30      u8   halfmove clock
  31      u8   reserved (0)

Feature index (768 per perspective):
  white perspective: piece_code * 64 + sq                       (relcolor = colour)
  black perspective: ((1 - colour) * 6 + ptype) * 64 + (sq ^ 56)  (colour flipped, board flipped)
"""
from __future__ import annotations

import glob
import os
import struct
import threading
import queue

import numpy as np

RECORD_SIZE = 32
NUM_FEATURES = 768
MAX_PIECES = 32

RECORD_DTYPE = np.dtype([
    ("occ", "<u8"),
    ("pieces", "u1", (16,)),
    ("score", "<i2"),
    ("result", "u1"),
    ("stm", "u1"),
    ("fullmove", "<u2"),
    ("halfmove", "u1"),
    ("reserved", "u1"),
])
assert RECORD_DTYPE.itemsize == RECORD_SIZE

PIECE_CHARS = "PNBRQKpnbrqk"


# --------------------------------------------------------------------------------------
# file access
# --------------------------------------------------------------------------------------
def expand_paths(pattern_or_list) -> list[str]:
    """Accepts a glob, a comma separated list of globs, or a list of those."""
    if isinstance(pattern_or_list, str):
        pattern_or_list = [p for p in pattern_or_list.split(",") if p]
    out: list[str] = []
    for pat in pattern_or_list:
        hits = sorted(glob.glob(pat))
        if not hits and os.path.isfile(pat):
            hits = [pat]
        out.extend(hits)
    if not out:
        raise FileNotFoundError(f"no data files match {pattern_or_list!r}")
    return out


def open_records(path: str) -> np.ndarray:
    """Memory-map one data file as a structured record array (read only)."""
    size = os.path.getsize(path)
    if size % RECORD_SIZE:
        raise ValueError(f"{path}: size {size} is not a multiple of {RECORD_SIZE}")
    if size == 0:
        return np.zeros(0, dtype=RECORD_DTYPE)
    return np.memmap(path, dtype=RECORD_DTYPE, mode="r")


class RecordStore:
    """Several memory-mapped files seen as one contiguous index space."""

    def __init__(self, paths):
        self.paths = expand_paths(paths)
        self.arrays = [open_records(p) for p in self.paths]
        sizes = np.array([len(a) for a in self.arrays], dtype=np.int64)
        self.starts = np.concatenate([[0], np.cumsum(sizes)])
        self.n = int(self.starts[-1])

    def __len__(self):
        return self.n

    def gather(self, idx) -> np.ndarray:
        """Copy the records at sorted global indices `idx` into memory."""
        idx = np.asarray(idx, dtype=np.int64)
        f = np.searchsorted(self.starts, idx, side="right") - 1
        out = np.empty(len(idx), dtype=RECORD_DTYPE)
        for k in np.unique(f):
            m = f == k
            out[m] = self.arrays[k][idx[m] - self.starts[k]]
        return out

    def read_range(self, lo: int, hi: int) -> np.ndarray:
        """Copy records [lo, hi) of the global index space into memory."""
        parts = []
        f = int(np.searchsorted(self.starts, lo, side="right") - 1)
        while lo < hi:
            fs, fe = int(self.starts[f]), int(self.starts[f + 1])
            take_hi = min(hi, fe)
            if take_hi > lo:
                parts.append(np.array(self.arrays[f][lo - fs:take_hi - fs]))
            lo = take_hi
            f += 1
        if not parts:
            return np.zeros(0, dtype=RECORD_DTYPE)
        return parts[0] if len(parts) == 1 else np.concatenate(parts)


# --------------------------------------------------------------------------------------
# decoding
# --------------------------------------------------------------------------------------
def decode_pieces(recs: np.ndarray):
    """Vectorised board decode.

    Returns (row, sq, code): flat arrays, one entry per piece, rows in increasing order and
    squares increasing within each row, plus counts[N] (pieces per record).
    """
    n = len(recs)
    occ = np.ascontiguousarray(recs["occ"]).astype("<u8", copy=False)
    bits = np.unpackbits(occ.view(np.uint8).reshape(n, 8), axis=1, bitorder="little")  # N,64
    row, sq = np.nonzero(bits)                      # row-major => squares ascending per row
    rank = np.cumsum(bits, axis=1, dtype=np.int16) - 1  # k-th set bit index per square
    k = rank[row, sq]
    pcs = recs["pieces"]
    nib = np.empty((n, 32), dtype=np.uint8)
    nib[:, 0::2] = pcs & 0x0F
    nib[:, 1::2] = pcs >> 4
    code = nib[row, k]
    counts = bits.sum(axis=1, dtype=np.int64)
    if counts.size and counts.max() > MAX_PIECES:
        raise ValueError("record with more than 32 pieces")
    if code.size and code.max() > 11:
        raise ValueError("invalid piece code (>11) in data")
    return row.astype(np.int64), sq.astype(np.int64), code.astype(np.int64), counts


def feature_indices(sq: np.ndarray, code: np.ndarray):
    """(white-perspective index, black-perspective index) for each piece."""
    colour = code // 6
    ptype = code % 6
    white = code * 64 + sq
    black = ((1 - colour) * 6 + ptype) * 64 + (sq ^ 56)
    return white, black


def decode_batch(recs: np.ndarray, lam: float):
    """Turns records into training tensors (as numpy arrays).

    Returns dict with
      stm_idx, nstm_idx : int64 flat feature indices (EmbeddingBag input)
      offsets           : int64 [N] bag offsets (same for both perspectives)
      target            : float32 [N] blended WDL target from side-to-move view
      score_stm, result_stm, stm
    """
    row, sq, code, counts = decode_pieces(recs)
    w, b = feature_indices(sq, code)
    stm = recs["stm"].astype(np.int64)
    black_to_move = stm[row] == 1
    stm_idx = np.where(black_to_move, b, w)
    nstm_idx = np.where(black_to_move, w, b)
    offsets = np.zeros(len(recs), dtype=np.int64)
    if len(recs) > 1:
        np.cumsum(counts[:-1], out=offsets[1:])

    score = recs["score"].astype(np.float32)
    score_stm = np.where(stm == 1, -score, score)
    res_white = recs["result"].astype(np.float32) * 0.5          # 0, 0.5, 1 from white
    result_stm = np.where(stm == 1, 1.0 - res_white, res_white)
    target = lam / (1.0 + np.exp(-score_stm / 400.0)) + (1.0 - lam) * result_stm
    return {
        "stm_idx": stm_idx, "nstm_idx": nstm_idx, "offsets": offsets,
        "target": target.astype(np.float32), "score_stm": score_stm.astype(np.float32),
        "result_stm": result_stm.astype(np.float32), "stm": stm,
    }


def decode_batch_torch(raw, lam: float):
    """Same decode as decode_batch(), but in torch on raw.device (GPU decoding keeps the
    CPU free). raw: uint8 tensor [N, 32] of records.
    Returns (stm_idx, nstm_idx, offsets, target) tensors on raw.device."""
    import torch

    dev = raw.device
    n = raw.shape[0]
    raw = raw.to(torch.int64)
    shifts = torch.arange(8, device=dev)
    bits = ((raw[:, :8, None] >> shifts) & 1).reshape(n, 64)        # bit k of byte b = square 8b+k
    row, sq = torch.nonzero(bits, as_tuple=True)                       # row-major, squares ascending
    rank = torch.cumsum(bits, dim=1) - 1
    k = rank[row, sq]
    pcs = raw[:, 8:24]
    nib = torch.stack([pcs & 0x0F, pcs >> 4], dim=2).reshape(n, 32)    # nibble 2i = low, 2i+1 = high
    code = nib[row, k]
    colour = torch.div(code, 6, rounding_mode="floor")
    w = code * 64 + sq
    b = ((1 - colour) * 6 + (code - colour * 6)) * 64 + (sq ^ 56)
    stm = raw[:, 27]
    btm = stm[row] == 1
    stm_idx = torch.where(btm, b, w)
    nstm_idx = torch.where(btm, w, b)
    counts = bits.sum(dim=1)
    offsets = torch.cumsum(counts, dim=0) - counts
    score = raw[:, 24] | (raw[:, 25] << 8)
    score = torch.where(score >= 32768, score - 65536, score).to(torch.float32)
    score_stm = torch.where(stm == 1, -score, score)
    res_white = raw[:, 26].to(torch.float32) * 0.5
    result_stm = torch.where(stm == 1, 1.0 - res_white, res_white)
    target = lam * torch.sigmoid(score_stm / 400.0) + (1.0 - lam) * result_stm
    return stm_idx, nstm_idx, offsets, target


# --------------------------------------------------------------------------------------
# FEN helpers (no python-chess needed)
# --------------------------------------------------------------------------------------
def record_to_fen(rec) -> str:
    """FEN for one record. Castling / en-passant are not stored, written as '-'."""
    occ = int(rec["occ"])
    pcs = bytes(rec["pieces"])
    board = [None] * 64
    k = 0
    for s in range(64):
        if occ >> s & 1:
            byte = pcs[k >> 1]
            board[s] = (byte >> 4) if (k & 1) else (byte & 0x0F)
            k += 1
    ranks = []
    for r in range(7, -1, -1):
        txt, empty = "", 0
        for f in range(8):
            c = board[r * 8 + f]
            if c is None:
                empty += 1
            else:
                if empty:
                    txt += str(empty)
                    empty = 0
                txt += PIECE_CHARS[c]
        if empty:
            txt += str(empty)
        ranks.append(txt)
    stm = "b" if int(rec["stm"]) else "w"
    return f"{'/'.join(ranks)} {stm} - - {int(rec['halfmove'])} {max(1, int(rec['fullmove']))}"


def fen_to_pieces(fen: str):
    """Returns (list of (square, code), stm 0/1) parsed from a FEN."""
    fields = fen.split()
    placement = fields[0]
    stm = 1 if len(fields) > 1 and fields[1] == "b" else 0
    out = []
    r, f = 7, 0
    for ch in placement:
        if ch == "/":
            r -= 1
            f = 0
        elif ch.isdigit():
            f += int(ch)
        else:
            out.append((r * 8 + f, PIECE_CHARS.index(ch)))
            f += 1
    return sorted(out), stm


def fen_features(fen: str):
    """(stm feature list, nstm feature list) for a FEN."""
    pieces, stm = fen_to_pieces(fen)
    sq = np.array([p[0] for p in pieces], dtype=np.int64)
    code = np.array([p[1] for p in pieces], dtype=np.int64)
    w, b = feature_indices(sq, code)
    return (b, w) if stm else (w, b)


# --------------------------------------------------------------------------------------
# encoding (tests / synthetic data)
# --------------------------------------------------------------------------------------
def encode_record(board, score_white_cp: int, result: int) -> bytes:
    """Encode a python-chess Board + white-POV score + result (0/1/2) into 32 bytes."""
    import chess  # local import: only tests and the synthetic generator need python-chess

    occ = int(board.occupied)
    nibbles = []
    for s in range(64):
        if occ >> s & 1:
            p = board.piece_at(s)
            nibbles.append((p.piece_type - 1) + (0 if p.color == chess.WHITE else 6))
    if len(nibbles) > MAX_PIECES:
        raise ValueError("more than 32 pieces")
    nibbles += [0] * (32 - len(nibbles))
    packed = bytes(nibbles[2 * i] | (nibbles[2 * i + 1] << 4) for i in range(16))
    score = max(-32768, min(32767, int(score_white_cp)))
    return (struct.pack("<Q", occ) + packed + struct.pack(
        "<hBBHBB", score, int(result), 0 if board.turn == chess.WHITE else 1,
        min(65535, board.fullmove_number), min(255, board.halfmove_clock), 0))


# --------------------------------------------------------------------------------------
# batch loader
# --------------------------------------------------------------------------------------
class BatchLoader:
    """Shuffled batches from a RecordStore.

    Each epoch the index space [lo, hi) is cut into contiguous chunks; chunk order is
    shuffled, `buffer_chunks` chunks are read together, permuted, and served as batches.
    Reading/shuffling (and numpy decoding unless raw=True) runs in a background thread so
    it overlaps with the optimiser step.
    """

    def __init__(self, store: RecordStore, lo: int, hi: int, batch: int, lam: float,
                 seed: int, chunk: int = 1 << 20, buffer_chunks: int = 4,
                 shuffle: bool = True, prefetch: int = 4, drop_last: bool = True,
                 raw: bool = False):
        self.store, self.lo, self.hi = store, lo, hi
        self.batch, self.lam, self.seed = batch, lam, seed
        self.chunk, self.buffer_chunks = chunk, buffer_chunks
        self.shuffle, self.prefetch, self.drop_last = shuffle, prefetch, drop_last
        self.raw = raw  # True: yield uint8 [n,32] record bytes (decode later, e.g. on GPU)

    def num_batches(self, limit: int | None = None) -> int:
        n = self.hi - self.lo if limit is None else min(limit, self.hi - self.lo)
        return n // self.batch if self.drop_last else -(-n // self.batch)

    def _produce(self, epoch: int, limit: int | None, q: queue.Queue, stop: threading.Event):
        try:
            rng = np.random.default_rng([self.seed, epoch])
            starts = np.arange(self.lo, self.hi, self.chunk)
            if self.shuffle:
                rng.shuffle(starts)
            remaining = self.num_batches(limit) * self.batch if self.drop_last else (
                (self.hi - self.lo) if limit is None else min(limit, self.hi - self.lo))
            carry = np.zeros(0, dtype=RECORD_DTYPE)
            for g in range(0, len(starts), self.buffer_chunks):
                if remaining <= 0 or stop.is_set():
                    break
                parts = [carry] + [self.store.read_range(int(s), int(min(s + self.chunk, self.hi)))
                                   for s in starts[g:g + self.buffer_chunks]]
                buf = np.concatenate(parts)
                if self.shuffle:
                    buf = buf[rng.permutation(len(buf))]
                last_group = g + self.buffer_chunks >= len(starts)
                pos = 0
                while remaining > 0 and len(buf) - pos >= (1 if last_group and not self.drop_last else self.batch):
                    take = min(self.batch, remaining, len(buf) - pos)
                    part = buf[pos:pos + take]
                    q.put(np.ascontiguousarray(part).view(np.uint8).reshape(-1, RECORD_SIZE)
                          if self.raw else decode_batch(part, self.lam))
                    pos += take
                    remaining -= take
                    if stop.is_set():
                        return
                carry = buf[pos:]
        except BaseException as e:  # surface errors in the consumer
            q.put(e)
        finally:
            q.put(None)

    def epoch(self, epoch: int, limit: int | None = None):
        q: queue.Queue = queue.Queue(maxsize=self.prefetch)
        stop = threading.Event()
        t = threading.Thread(target=self._produce, args=(epoch, limit, q, stop), daemon=True)
        t.start()
        try:
            while True:
                item = q.get()
                if item is None:
                    break
                if isinstance(item, BaseException):
                    raise item
                yield item
        finally:
            stop.set()
            while t.is_alive():
                try:
                    q.get_nowait()
                except queue.Empty:
                    t.join(0.05)


# --------------------------------------------------------------------------------------
# self test:  python dataset.py
# --------------------------------------------------------------------------------------
def _selftest(n_games: int = 200, seed: int = 1):
    import random
    import chess

    rnd = random.Random(seed)
    boards, blobs = [], []
    for _ in range(n_games):
        b = chess.Board()
        for _ in range(rnd.randint(0, 120)):
            moves = list(b.legal_moves)
            if not moves:
                break
            b.push(rnd.choice(moves))
            score, res = rnd.randint(-3000, 3000), rnd.randint(0, 2)
            boards.append((b.copy(stack=False), score, res))
            blobs.append(encode_record(b, score, res))
    recs = np.frombuffer(b"".join(blobs), dtype=RECORD_DTYPE)
    d = decode_batch(recs, 0.75)
    off = np.append(d["offsets"], len(d["stm_idx"]))
    for i, (b, score, res) in enumerate(boards):
        # slow reference features straight from python-chess
        w_ref, b_ref = [], []
        for s, p in sorted(b.piece_map().items()):
            col = 0 if p.color == chess.WHITE else 1
            pt = p.piece_type - 1
            w_ref.append((col * 6 + pt) * 64 + s)
            b_ref.append(((1 - col) * 6 + pt) * 64 + (s ^ 56))
        stm_ref, nstm_ref = (w_ref, b_ref) if b.turn == chess.WHITE else (b_ref, w_ref)
        got_s = list(d["stm_idx"][off[i]:off[i + 1]])
        got_n = list(d["nstm_idx"][off[i]:off[i + 1]])
        assert got_s == stm_ref and got_n == nstm_ref, i
        fen = record_to_fen(recs[i])
        assert fen.split()[:2] == b.fen().split()[:2], (fen, b.fen())
        s_stm = score if b.turn == chess.WHITE else -score
        r_w = res / 2
        r_stm = r_w if b.turn == chess.WHITE else 1 - r_w
        t = 0.75 / (1 + np.exp(-s_stm / 400)) + 0.25 * r_stm
        assert abs(d["target"][i] - t) < 1e-5
        fs, fn = fen_features(fen)
        assert list(fs) == stm_ref and list(fn) == nstm_ref
    try:
        import torch
    except ImportError:
        torch = None
    if torch is not None:
        devs = ["cpu"] + (["cuda"] if torch.cuda.is_available() else [])
        for dev in devs:
            raw_t = torch.from_numpy(recs.view(np.uint8).reshape(-1, RECORD_SIZE).copy()).to(dev)
            si, ni, off, tg = (x.cpu().numpy() for x in decode_batch_torch(raw_t, 0.75))
            assert (si == d["stm_idx"]).all() and (ni == d["nstm_idx"]).all()
            assert (off == d["offsets"]).all() and np.allclose(tg, d["target"], atol=1e-6)
        print(f"torch decoder matches numpy decoder on {devs}")
    print(f"dataset selftest OK: {len(boards)} positions round-tripped")


if __name__ == "__main__":
    _selftest()
