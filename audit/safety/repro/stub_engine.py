"""Scripted UCI stub for check 4 (not a chess engine: it only replays the black moves of GAME).

STUB_MODE=crash: on go number STUB_AT of the FIRST process (marker file absent) it writes the
marker and exits with code 3, like a crashed engine. Later processes play normally.
STUB_MODE=hang: on go number STUB_AT it stops answering (sleeps up to STUB_HANG_S, then exits).
Every event is appended to STUB_LOG with a wall-clock timestamp.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check4_game import GAME  # noqa: E402

mode = os.environ.get("STUB_MODE", "crash")
at = int(os.environ.get("STUB_AT", "10"))
marker = os.environ.get("STUB_MARKER", "")
logf = os.environ.get("STUB_LOG", "")
hang_s = float(os.environ.get("STUB_HANG_S", "120"))


def log(msg: str) -> None:
    if logf:
        with open(logf, "a") as f:
            f.write(f"{time.time():.3f} pid={os.getpid()} {msg}\n")


def send(s: str) -> None:
    print(s, flush=True)


log("started")
gos = 0
moves: list[str] = []
for raw in sys.stdin:
    parts = raw.split()
    if not parts:
        continue
    cmd = parts[0]
    if cmd == "uci":
        send("id name SafetyStub")
        send("id author audit")
        send("uciok")
    elif cmd == "isready":
        send("readyok")
    elif cmd == "position":
        moves = parts[parts.index("moves") + 1:] if "moves" in parts else []
    elif cmd == "go":
        gos += 1
        first_process = not (marker and os.path.exists(marker))
        if gos == at and mode == "crash" and first_process:
            if marker:
                open(marker, "w").write("crashed\n")
            log(f"CRASH on go {gos} (ply {len(moves)})")
            os._exit(3)
        if gos == at and mode == "hang":
            log(f"HANG on go {gos} (ply {len(moves)})")
            time.sleep(hang_s)
            log("hang over, exiting")
            os._exit(4)
        mv = GAME[len(moves)]
        log(f"go {gos} ply {len(moves)} -> {mv}")
        send(f"bestmove {mv}")
    elif cmd == "quit":
        log("quit")
        break
