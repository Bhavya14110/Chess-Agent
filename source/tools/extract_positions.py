"""Turn a filtered (human-games-only) PGN into Texel-tuning lines "FEN;RESULT".

For every game: skip the first --skip-plies plies (default 8), skip positions where the side
to move is in check, skip a final position with no legal moves (mate/stalemate), and label
every remaining position with the GAME RESULT from White's point of view
(1-0 -> 1.0, 1/2-1/2 -> 0.5, 0-1 -> 0.0). Nothing else (no comments, no evals) is read.

--per-game N keeps a random subset of at most N positions per game. Randomness is seeded per
game from (--seed, game index), so the output is identical regardless of worker count.
--max-positions caps the total number of lines (taken in game order).

usage:
  python source/tools/extract_positions.py data/lichess/filtered.pgn data/lichess/positions_raw.txt \
      [--per-game 20] [--seed 1] [--max-positions 0] [--workers N]
"""
import argparse, multiprocessing as mp, os, random, re, sys, time

import chess

RESULT_MAP = {"1-0": "1.0", "0-1": "0.0", "1/2-1/2": "0.5"}
RESULT_TAG = re.compile(r'^\[Result "([^"]*)"\]', re.M)
FEN_TAG = re.compile(r'^\[(FEN|SetUp) ', re.M)
MOVENUM = re.compile(r"^\d+\.+$")
CLEAN = re.compile(r"\{[^}]*\}|;[^\n]*|\$\d+")


def read_games(path, batch):
    """yield lists of (game_index, game_text); games split on lines starting with [Event """
    buf, cur, idx = [], [], 0
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("[Event ") and cur:
                buf.append((idx, "".join(cur)))
                idx += 1
                cur = []
                if len(buf) >= batch:
                    yield buf
                    buf = []
            cur.append(line)
    if cur:
        buf.append((idx, "".join(cur)))
    if buf:
        yield buf


def process(args):
    games, skip, per_game, seed = args
    out, nbad = [], 0
    for idx, text in games:
        m = RESULT_TAG.search(text)
        if not m or m.group(1) not in RESULT_MAP or FEN_TAG.search(text):
            nbad += 1
            continue
        res = RESULT_MAP[m.group(1)]
        sep = text.find("\n\n")
        movetext = CLEAN.sub(" ", text[sep + 2:]) if sep >= 0 else ""
        b = chess.Board()
        fens = []
        ply = 0
        ok = True
        for tok in movetext.split():
            if MOVENUM.match(tok) or tok in ("1-0", "0-1", "1/2-1/2", "*"):
                continue
            try:
                b.push_san(tok.rstrip("!?"))
            except ValueError:
                ok = False
                break
            ply += 1
            if ply >= skip and not b.is_check():
                fens.append(b.fen())
        if not ok:
            nbad += 1
            continue
        if fens and fens[-1] == b.fen() and not any(b.generate_legal_moves()):
            fens.pop()  # stalemate as the last position
        if per_game and len(fens) > per_game:
            rng = random.Random(f"{seed}:{idx}")
            keep = sorted(rng.sample(range(len(fens)), per_game))
            fens = [fens[i] for i in keep]
        out.extend(f"{f};{res}\n" for f in fens)
    return "".join(out), len(games), nbad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("pgn")
    ap.add_argument("out")
    ap.add_argument("--skip-plies", type=int, default=8, help="drop the first N plies of each game")
    ap.add_argument("--per-game", type=int, default=0, help="random subsample of at most N positions per game (0 = all)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--max-positions", type=int, default=0, help="stop after N lines (0 = no limit)")
    ap.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--batch", type=int, default=500, help="games per work unit")
    a = ap.parse_args()

    # a position is "after ply k"; dropping the first 8 plies means the first kept one is after ply 9
    skip = a.skip_plies + 1
    t0 = time.time()
    games = bad = lines = 0
    os.makedirs(os.path.dirname(os.path.abspath(a.out)) or ".", exist_ok=True)
    with open(a.out, "w", encoding="utf-8", newline="\n") as out, mp.Pool(a.workers) as pool:
        jobs = ((batch, skip, a.per_game, a.seed) for batch in read_games(a.pgn, a.batch))
        for text, n, nb in pool.imap(process, jobs, chunksize=1):
            games += n
            bad += nb
            if a.max_positions and lines + text.count("\n") > a.max_positions:
                rest = a.max_positions - lines
                text = "".join(text.splitlines(keepends=True)[:rest])
            out.write(text)
            lines += text.count("\n")
            if games % 100_000 < a.batch:
                print(f"{games:,} games, {lines:,} positions, {time.time() - t0:.0f}s", file=sys.stderr, flush=True)
            if a.max_positions and lines >= a.max_positions:
                pool.terminate()
                break
    print(f"DONE: {games:,} games read, {bad:,} skipped (bad result/movetext), {lines:,} positions written "
          f"to {a.out} in {time.time() - t0:.0f}s (skip_plies={a.skip_plies}, per_game={a.per_game}, seed={a.seed})",
          file=sys.stderr, flush=True)


if __name__ == "__main__":
    main()
