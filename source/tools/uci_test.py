"""Drives the engine through every UCI command the competition rules list and checks the answers.
usage: python source/tools/uci_test.py engine/Corvus.exe"""
import queue, random, subprocess, sys, threading, time
import chess

VALID_PREFIXES = ("id ", "option ", "uciok", "readyok", "info ", "bestmove ")


class Engine:
    def __init__(self, path):
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True, bufsize=1)
        self.q = queue.Queue()
        self.alllines = []
        threading.Thread(target=self._reader, daemon=True).start()
        threading.Thread(target=self._drain_err, daemon=True).start()

    def _reader(self):
        for line in self.p.stdout:
            line = line.rstrip("\r\n")
            self.alllines.append(line)
            self.q.put(line)
        self.q.put(None)

    def _drain_err(self):
        for _ in self.p.stderr:
            pass

    def send(self, s):
        self.p.stdin.write(s + "\n")
        self.p.stdin.flush()

    def expect(self, prefix, timeout):
        """read lines until one starts with prefix; returns (line, seconds waited)"""
        t0 = time.time()
        while True:
            left = timeout - (time.time() - t0)
            if left <= 0:
                raise AssertionError(f"timeout waiting for '{prefix}'")
            try:
                line = self.q.get(timeout=left)
            except queue.Empty:
                raise AssertionError(f"timeout waiting for '{prefix}'")
            if line is None:
                raise AssertionError(f"engine exited while waiting for '{prefix}'")
            if line.startswith(prefix):
                return line, time.time() - t0


failures = []


def check(name, fn):
    try:
        fn()
        print(f"  ok   {name}")
    except Exception as e:  # noqa
        failures.append(name)
        print(f"  FAIL {name}: {e}")


def legal_best(board, line):
    mv = line.split()[1]
    assert mv != "0000", "engine returned null move in a position with legal moves"
    assert chess.Move.from_uci(mv) in board.legal_moves, f"illegal bestmove {mv} in {board.fen()}"
    return mv


def main():
    path = sys.argv[1]
    e = Engine(path)

    def t_uci():
        e.send("uci")
        e.expect("id name", 5)
        e.expect("uciok", 5)
    check("uci -> id + uciok", t_uci)

    def t_ready():
        e.send("isready")
        e.expect("readyok", 5)
    check("isready -> readyok", t_ready)

    def t_options():
        e.send("setoption name Hash value 32")
        e.send("setoption name Move Overhead value 100")
        e.send("setoption name Threads value 1")
        e.send("isready")
        e.expect("readyok", 10)
    check("setoption Hash/Threads/Move Overhead", t_options)

    def t_newgame():
        e.send("ucinewgame")
        e.send("isready")
        e.expect("readyok", 10)
    check("ucinewgame", t_newgame)

    def t_startpos_clock():
        e.send("position startpos")
        e.send("go wtime 3000 btime 3000 winc 0 binc 0")
        line, secs = e.expect("bestmove", 5)
        legal_best(chess.Board(), line)
        assert secs < 1.5, f"took {secs:.2f}s with 3s on the clock"
    check("position startpos + go wtime/btime/winc/binc", t_startpos_clock)

    def t_moves():
        moves = "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1"
        b = chess.Board()
        for m in moves.split():
            b.push_uci(m)
        e.send(f"position startpos moves {moves}")
        e.send("go wtime 180000 btime 180000 winc 2000 binc 2000")
        line, secs = e.expect("bestmove", 30)
        legal_best(b, line)
    check("position startpos moves ... (castling) + 3+2 clock", t_moves)

    def t_fen_moves():
        # en passant + promotion + castling rights edge cases
        cases = [
            ("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3", "e5f6 g7f6"),
            ("8/P7/8/8/8/8/6k1/4K3 w - - 0 1", "a7a8q g2g3"),
            ("8/P7/8/8/8/8/6k1/4K3 w - - 0 1", "a7a8n"),
            ("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1c1 e8g8"),
            ("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "a8a1"),   # takes the rook: white loses Q-side right
            ("4k3/8/8/8/8/8/8/4K2R w K - 99 80", ""),           # one ply off the fifty-move rule
        ]
        for fen, moves in cases:
            b = chess.Board(fen)
            for m in moves.split():
                b.push_uci(m)
            e.send(f"position fen {fen}" + (f" moves {moves}" if moves else ""))
            e.send("go movetime 200")
            line, _ = e.expect("bestmove", 5)
            legal_best(b, line)
    check("position fen ... moves ... (ep, promo, castling, 50-move)", t_fen_moves)

    def t_long_game():
        rng = random.Random(7)
        b = chess.Board()
        moves = []
        while len(moves) < 300 and not b.is_game_over():
            m = rng.choice(list(b.legal_moves))
            b.push(m)
            moves.append(m.uci())
        if b.is_game_over():
            b.pop(); moves.pop()
        e.send("position startpos moves " + " ".join(moves))
        e.send("go wtime 1000 btime 1000 winc 100 binc 100")
        line, _ = e.expect("bestmove", 5)
        legal_best(b, line)
    check("long move list (~300 plies)", t_long_game)

    def t_threefold():
        # shuffling knights: after the moves below the start position has occurred twice
        moves = "g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1 f6g8"
        b = chess.Board()
        for m in moves.split():
            b.push_uci(m)
        e.send("position startpos moves " + moves)
        e.send("go depth 6")
        line, _ = e.expect("bestmove", 10)
        legal_best(b, line)
    check("threefold-ish history", t_threefold)

    def t_mate1():
        for fen, want in [("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", "d1d8"),
                          ("k7/8/1K6/8/8/8/8/6Q1 w - - 0 1", None)]:
            b = chess.Board(fen)
            e.send(f"position fen {fen}")
            e.send("go depth 4")
            line, _ = e.expect("bestmove", 10)
            mv = legal_best(b, line)
            b.push_uci(mv)
            assert b.is_checkmate(), f"missed mate in 1 in {fen}, played {mv}"
    check("finds mate in 1", t_mate1)

    def t_infinite_stop():
        e.send("position startpos")
        e.send("go infinite")
        time.sleep(0.5)
        e.send("isready")
        e.expect("readyok", 2)   # must answer during search
        time.sleep(0.3)
        e.send("stop")
        line, secs = e.expect("bestmove", 2)
        legal_best(chess.Board(), line)
    check("go infinite + isready mid-search + stop", t_infinite_stop)

    def t_stop_fast():
        e.send("position startpos moves d2d4")
        e.send("go wtime 600000 btime 600000")
        time.sleep(0.05)
        e.send("stop")
        line, secs = e.expect("bestmove", 2)
        b = chess.Board(); b.push_uci("d2d4")
        legal_best(b, line)
    check("stop right after go still gives a legal move", t_stop_fast)

    def t_movetime():
        e.send("position startpos")
        t0 = time.time()
        e.send("go movetime 500")
        line, _ = e.expect("bestmove", 3)
        dt = time.time() - t0
        legal_best(chess.Board(), line)
        assert 0.2 < dt < 0.9, f"movetime 500 took {dt:.2f}s"
    check("go movetime", t_movetime)

    def t_depth_nodes():
        e.send("position startpos")
        e.send("go depth 3")
        legal_best(chess.Board(), e.expect("bestmove", 5)[0])
        e.send("go nodes 20000")
        legal_best(chess.Board(), e.expect("bestmove", 5)[0])
        e.send("go movestogo 10 wtime 5000 btime 5000")
        legal_best(chess.Board(), e.expect("bestmove", 5)[0])
    check("go depth / nodes / movestogo", t_depth_nodes)

    def t_lowtime():
        e.send("position startpos moves e2e4")
        t0 = time.time()
        e.send("go wtime 2000 btime 300 winc 0 binc 0")
        line, _ = e.expect("bestmove", 2)
        b = chess.Board(); b.push_uci("e2e4")
        legal_best(b, line)
        assert time.time() - t0 < 0.3, "used too much time with 300ms left"
    check("panic mode with 300ms left", t_lowtime)

    def t_stdout_clean():
        bad = [l for l in e.alllines if not l.startswith(VALID_PREFIXES)]
        assert not bad, f"non-UCI stdout lines: {bad[:5]}"
    check("stdout only has UCI lines", t_stdout_clean)

    def t_quit():
        e.send("position startpos")
        e.send("go infinite")
        time.sleep(0.2)
        e.send("quit")
        e.p.wait(timeout=3)
    check("quit during search exits", t_quit)

    print(f"\n{'PASS' if not failures else 'FAIL'}: {len(failures)} failures")
    sys.exit(1 if failures else 0)


main()
