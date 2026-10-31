"""Check 2: tricky positions through UCI. For every case: fresh engine, uci/isready, setoption like
the live bot (Threads 4, Hash 256, Move Overhead 150), ucinewgame, "position fen ... moves ...",
the go command, then check: exactly one bestmove, legal (python-chess used only as a rules library),
within the timeout, every stdout line valid UCI, engine still answers isready, and quits cleanly.

usage: python check2_tricky.py ENGINE.exe [--threads N] [--json out.json]
Writes the exact cases to check2_positions.txt next to this script.
"""
import argparse
import json
import os
import queue
import random
import re
import subprocess
import sys
import threading
import time

import chess

HERE = os.path.dirname(os.path.abspath(__file__))
STD = "go wtime 3000 btime 3000 winc 2000 binc 2000"
START = chess.STARTING_FEN

UCI_LINE = re.compile(
    r"^(id (name|author) .+"
    r"|option name .+ type (spin|check|string|button|combo).*"
    r"|uciok|readyok"
    r"|info( .*)?"
    r"|bestmove [a-h][1-8][a-h][1-8][qrbn]?( ponder [a-h][1-8][a-h][1-8][qrbn]?)?)$")


def long_game(plies: int, seed: int) -> list[str]:
    """A deterministic legal game of `plies` plies that never ends in mate/stalemate/insufficient material."""
    rng = random.Random(seed)
    while True:
        b = chess.Board()
        out = []
        while len(out) < plies:
            moves = list(b.legal_moves)
            rng.shuffle(moves)
            for m in moves:
                b.push(m)
                ok = not (b.is_checkmate() or b.is_stalemate() or b.is_insufficient_material()) \
                    and any(True for _ in b.legal_moves)
                if ok:
                    out.append(m.uci())
                    break
                b.pop()
            else:
                break
        if len(out) == plies:
            return out
        seed += 1
        rng = random.Random(seed)


def long_quiet_game(plies: int, seed: int) -> list[str]:
    """A legal game of `plies` plies with no repetition and no capture-heavy play: pushes a pawn before the
    50-move rule bites, never repeats a position, never ends. Exercises the engine's 1024-ply undo-stack guard."""
    rng = random.Random(seed)
    b = chess.Board()
    out = []
    while len(out) < plies:
        ms = list(b.legal_moves)
        rng.shuffle(ms)
        pawn = [m for m in ms if b.piece_type_at(m.from_square) == chess.PAWN and not b.is_capture(m)]
        quiet = [m for m in ms if not b.is_capture(m)]
        for m in (pawn if b.halfmove_clock > 80 else []) + quiet + ms:
            b.push(m)
            if not b.is_game_over(claim_draw=True) and not b.is_repetition(2):
                out.append(m.uci())
                break
            b.pop()
        else:
            raise RuntimeError("long_quiet_game got stuck; try another seed")
    return out


# name, fen, moves, go command(s), timeout s, check(board, move) -> problem or None
def cases() -> list[dict]:
    c = []

    def add(name, fen, moves, go=STD, timeout=5.0, expect=None, note=""):
        c.append(dict(name=name, fen=fen, moves=moves, go=go, timeout=timeout, expect=expect, note=note))

    mate_n = "7b/5Ppk/6pp/3B4/8/8/8/K7 w - - 0 1"
    add("underpromo_knight_mate_white", mate_n, [], expect="f7f8n",
        note="f8=N# is the only mate; f8=Q is not even check")
    add("underpromo_knight_mate_black", chess.Board(mate_n).mirror().fen(), [], expect="f2f1n",
        note="colour-mirrored copy: black must promote ...f1=N#")
    add("underpromo_rook_saavedra", "8/2P5/8/8/3r4/8/2K5/k7 w - - 0 1", [],
        note="Saavedra: c8=R wins, c8=Q? Rc4+ stalemates. Only legality is required")
    add("ep_legal_only_move", "1r5k/1p6/n2b4/P7/K7/7r/8/8 b - - 0 1", ["b7b5"], expect="a5b6",
        note="after ...b5+ the en-passant capture axb6 is the ONLY legal move")
    add("ep_legal_normal", START, ["e2e4", "g8f6", "e4e5", "d7d5"],
        note="exd6 e.p. is legal (FEN ep square comes from the moves)")
    add("ep_illegal_pinned", "7k/2p5/8/KP5r/8/8/8/8 b - - 0 1", ["c7c5"],
        note="bxc6 e.p. would expose Ka5 to Rh5 along the 5th rank: illegal")
    add("castle_after_rook_returned", START,
        ["e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6", "h1g1", "f8c5", "g1h1", "d7d6"],
        note="white rook h1-g1-h1: O-O (e1g1) is illegal although king and rook are home")
    add("castle_after_rook_returned_fen", "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w Qkq - 4 5", [],
        note="same idea from a FEN without the K right")
    add("one_legal_move", "7k/8/5QK1/8/8/8/8/8 b - - 0 1", [], expect="h8g8")
    add("halfmove_99", "8/8/4k3/8/8/3K4/3R4/8 w - - 99 120", [],
        note="any quiet move completes the 50-move rule")
    add("halfmove_99_moves", "8/8/4k3/8/8/3K4/3R4/8 w - - 98 120", ["d2d1", "e6e5"],
        note="clock reaches 100 from the moves list; engine must still answer")
    add("long_game_320_plies", START, long_game(320, 7), note="320-ply move list")
    add("long_game_1100_plies", START, long_quiet_game(1100, 1),
        note="1100 plies, no repetition: passes the engine's 1024-ply undo stack (guard at main.cpp:82)")
    add("threefold_repetition", START,
        ["g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8"],
        note="start position for the 3rd time")
    mid = ["e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6"]
    add("panic_wtime_50", START, mid, go="go wtime 50 btime 50 winc 0 binc 0")
    add("panic_wtime_1_inc", START, mid, go="go wtime 1 btime 180000 winc 2000 binc 2000",
        note="what lichess-bot sends once clock - move_overhead(2000) <= 0 (floored at 1 ms)")
    add("go_no_params_then_stop", START, mid, go=["go", "@1.0", "stop"])
    add("go_infinite_then_stop", START, mid, go=["go infinite", "@1.0", "stop"])
    add("first_move_movetime_10000", START, [], go="go movetime 10000", timeout=12.0,
        note="lichess-bot's first move of every game (engine_wrapper.first_move_time)")
    return c


class Engine:
    def __init__(self, path: str):
        self.p = subprocess.Popen([path], cwd=os.path.dirname(path), stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
        self.q: queue.Queue = queue.Queue()
        self.lines: list[tuple[float, str]] = []
        threading.Thread(target=self._read, daemon=True).start()
        threading.Thread(target=lambda: [None for _ in self.p.stderr], daemon=True).start()

    def _read(self):
        for line in self.p.stdout:
            item = (time.time(), line.rstrip("\r\n"))
            self.lines.append(item)
            self.q.put(item)
        self.q.put((time.time(), None))

    def send(self, s: str):
        self.p.stdin.write(s + "\n")
        self.p.stdin.flush()

    def wait_for(self, prefix: str, timeout: float):
        end = time.time() + timeout
        while True:
            left = end - time.time()
            if left <= 0:
                return None
            try:
                t, line = self.q.get(timeout=left)
            except queue.Empty:
                return None
            if line is None:
                return None
            if line.startswith(prefix):
                return t, line


def run_case(path: str, case: dict, threads: int) -> dict:
    res = dict(name=case["name"], ok=False, problems=[])
    b = chess.Board(case["fen"])
    for m in case["moves"]:
        b.push_uci(m)
    e = Engine(path)
    t_start = time.time()
    e.send("uci")
    if not e.wait_for("uciok", 5):
        res["problems"].append("no uciok")
    for name, val in (("Threads", threads), ("Hash", 256), ("Move Overhead", 150)):
        e.send(f"setoption name {name} value {val}")
    e.send("isready")
    if not e.wait_for("readyok", 5):
        res["problems"].append("no readyok after setoption")
    res["startup_s"] = round(time.time() - t_start, 3)
    e.send("ucinewgame")
    pos = f"position fen {case['fen']}" + (" moves " + " ".join(case["moves"]) if case["moves"] else "")
    e.send(pos)
    gos = case["go"] if isinstance(case["go"], list) else [case["go"]]
    t_go = None
    for g in gos:
        if g.startswith("@"):
            time.sleep(float(g[1:]))
        else:
            e.send(g)
            t_go = time.time()   # timeout counts from the last command (go, or stop)
    got = e.wait_for("bestmove", case["timeout"])
    if not got:
        res["problems"].append(f"HANG: no bestmove within {case['timeout']} s")
    else:
        t_bm, line = got
        res["elapsed_s"] = round(t_bm - t_go, 3)
        mv = line.split()[1]
        res["bestmove"] = mv
        try:
            move = chess.Move.from_uci(mv)
            if move not in b.legal_moves:
                res["problems"].append(f"ILLEGAL bestmove {mv}")
        except ValueError:
            res["problems"].append(f"unparsable bestmove {mv}")
        if case["expect"] and mv != case["expect"]:
            res["note_expect"] = f"expected {case['expect']} (strength only, not a failure)"
    time.sleep(0.3)
    e.send("isready")
    if not e.wait_for("readyok", 5):
        res["problems"].append("no readyok after the search")
    e.send("quit")
    try:
        e.p.wait(5)
    except subprocess.TimeoutExpired:
        res["problems"].append("did not exit within 5 s of quit")
        e.p.kill()
    time.sleep(0.1)
    lines = [l for _, l in e.lines]
    nbest = sum(1 for l in lines if l.startswith("bestmove"))
    if nbest != 1:
        res["problems"].append(f"{nbest} bestmove lines (want exactly 1)")
    bad = [l for l in lines if not UCI_LINE.match(l)]
    if bad:
        res["problems"].append(f"non-UCI stdout lines: {bad[:3]}")
    res["stdout_lines"] = len(lines)
    res["exit_code"] = e.p.returncode
    res["ok"] = not res["problems"]
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("engine")
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--json")
    ap.add_argument("--positions-only", action="store_true", help="only self-test the positions and write the file")
    a = ap.parse_args()
    path = os.path.abspath(a.engine)
    cs = cases()
    # self-test of the positions themselves (so a typo can't make a case meaningless)
    for cse in cs:
        b = chess.Board(cse["fen"])
        assert b.is_valid(), cse["name"]
        for m in cse["moves"]:
            assert chess.Move.from_uci(m) in b.legal_moves, (cse["name"], m)
            b.push_uci(m)
        assert not b.is_game_over(), cse["name"]
        legal = {m.uci() for m in b.legal_moves}
        n = cse["name"]
        if n.startswith("underpromo_knight"):
            mates = []
            for m in b.legal_moves:
                b.push(m)
                if b.is_checkmate():
                    mates.append(m.uci())
                b.pop()
            assert mates == [cse["expect"]], (n, mates)
        if n == "ep_legal_only_move":
            assert legal == {"a5b6"} and b.is_en_passant(chess.Move.from_uci("a5b6")), legal
        if n == "ep_legal_normal":
            assert "e5d6" in legal and b.is_en_passant(chess.Move.from_uci("e5d6"))
        if n == "ep_illegal_pinned":
            assert b.ep_square == chess.C6 and "b5c6" not in legal and b.is_pseudo_legal(chess.Move.from_uci("b5c6"))
        if n.startswith("castle_after"):
            assert "e1g1" not in legal and not b.has_kingside_castling_rights(chess.WHITE)
            assert b.piece_at(chess.E1) == chess.Piece(chess.KING, chess.WHITE)
            assert b.piece_at(chess.H1) == chess.Piece(chess.ROOK, chess.WHITE)
            assert not b.is_attacked_by(chess.BLACK, chess.F1) and not b.is_attacked_by(chess.BLACK, chess.G1)
            assert b.piece_at(chess.F1) is None and b.piece_at(chess.G1) is None
        if n == "one_legal_move":
            assert legal == {"h8g8"}, legal
        if n.startswith("halfmove_99"):
            assert b.halfmove_clock >= 99
        if n == "threefold_repetition":
            assert b.can_claim_threefold_repetition()
        if n == "long_game_320_plies":
            assert len(b.move_stack) == 320
        if n == "long_game_1100_plies":
            assert len(b.move_stack) == 1100
    with open(os.path.join(HERE, "check2_positions.txt"), "w") as f:
        for cse in cs:
            go = " | ".join(cse["go"]) if isinstance(cse["go"], list) else cse["go"]
            pos = f"position fen {cse['fen']}" + (" moves " + " ".join(cse["moves"]) if cse["moves"] else "")
            f.write(f"# {cse['name']}: {cse['note']}\n{pos}\n{go}\n\n")
    if a.positions_only:
        print(f"{len(cs)} positions self-tested OK")
        return
    results = []
    for cse in cs:
        r = run_case(path, cse, a.threads)
        results.append(r)
        flag = "ok  " if r["ok"] else "FAIL"
        print(f"{flag} {r['name']:<32} bestmove={r.get('bestmove')} t={r.get('elapsed_s')}s "
              f"startup={r['startup_s']}s {'; '.join(r['problems'])} {r.get('note_expect', '')}", flush=True)
    if a.json:
        with open(a.json, "w") as f:
            json.dump(results, f, indent=1)
    allok = all(r["ok"] for r in results)
    print(f"check2: {sum(r['ok'] for r in results)}/{len(results)} cases clean")
    print("PASS" if allok else "FAIL")
    sys.exit(0 if allok else 1)


if __name__ == "__main__":
    main()
