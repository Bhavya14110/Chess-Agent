"""Check 4 runner: plays one game through lichess-bot's REAL game loop (lib/lichess_bot.py play_game,
lib/engine_wrapper.py) against lichess-bot's own offline mock of lichess.org (test_bot/lichess.py).
No network, no token. The engine is stub_engine.py (scripted), which crashes or hangs on its 10th go.

Run with lichess-bot's venv python, from the lichess-bot folder:
  venv/Scripts/python.exe <repro>/check4_runner.py crash|hang <logdir>
Uses our template config (lichess-bot/config.example.yml), so move_overhead etc. match the real bot.
"""
import datetime
import json
import logging
import os
import sys
import threading
import time
from multiprocessing import Manager
from queue import Queue

import chess
import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.getcwd())          # the lichess-bot folder
sys.path.insert(0, HERE)

import test_bot.lichess  # noqa: E402  (lichess-bot's offline mock)
from lib import config, lichess_bot  # noqa: E402
from lib.timer import Timer, seconds  # noqa: E402
from check4_game import GAME  # noqa: E402

mode = sys.argv[1]
logdir = os.path.abspath(sys.argv[2])
os.makedirs(logdir, exist_ok=True)
events_path = os.path.join(logdir, f"check4-{mode}-events.txt")


def ev(msg: str) -> None:
    with open(events_path, "a") as f:
        f.write(f"{time.time():.3f} sim {msg}\n")


def simulator(move_queue, board_queue, clock_queue, results) -> None:
    """Mock lichess.org opponent (white) with a 3+2 clock; replays GAME's white moves."""
    wtime = btime = seconds(180)
    inc = seconds(2)
    board = chess.Board()
    while not board.is_game_over():
        t = Timer()
        if board.turn == chess.WHITE:
            board.push_uci(GAME[len(board.move_stack)])
            board_queue.put(board)
            clock_queue.put((wtime, btime, inc))
        else:
            ev(f"waiting for bot move at ply {len(board.move_stack)}")
            while (mv := move_queue.get()) is None:      # None = lichess-bot reopened the game stream
                ev("game stream reopened by lichess-bot (play_game retried)")
                board_queue.put(board)
                clock_queue.put((wtime, btime, inc))
                move_queue.task_done()
            used = t.time_since_reset()
            ev(f"bot move {mv.uci()} at ply {len(board.move_stack)} after {used.total_seconds():.2f}s")
            board.push(mv)
            move_queue.task_done()
            if len(board.move_stack) > 2:
                btime = btime - used + inc
    board_queue.put(board)
    clock_queue.put((wtime, btime, inc))
    out = board.outcome()
    ev(f"game over: {out}")
    results.put(out is not None and out.winner == chess.BLACK)


def main() -> None:
    with open(os.path.join(REPO, "lichess-bot", "config.example.yml")) as f:
        raw = yaml.safe_load(f)
    raw["token"] = ""
    raw["engine"]["dir"] = HERE
    raw["engine"]["working_dir"] = HERE
    raw["engine"]["name"] = "stub_engine.py"
    raw["engine"]["interpreter"] = sys.executable
    raw["engine"]["uci_options"] = {}   # the stub declares no options
    raw["pgn_directory"] = None
    os.environ["STUB_MODE"] = mode
    os.environ["STUB_AT"] = "10"
    os.environ["STUB_MARKER"] = os.path.join(logdir, f"check4-{mode}.marker")
    os.environ["STUB_LOG"] = os.path.join(logdir, f"check4-{mode}-stub.txt")
    os.environ["STUB_HANG_S"] = os.environ.get("STUB_HANG_S", "120")
    for p in (events_path, os.environ["STUB_MARKER"], os.environ["STUB_LOG"]):
        if os.path.exists(p):
            os.remove(p)

    lichess_bot.logging_configurer(logging.DEBUG, os.path.join(logdir, f"check4-{mode}-lichess-bot.log"), True)
    config.insert_default_values(raw)
    cfg = config.Configuration(raw)
    manager = Manager()
    board_queue = manager.Queue()
    clock_queue = manager.Queue()
    move_queue = manager.Queue()
    results = manager.Queue()
    li = test_bot.lichess.Lichess(move_queue, board_queue, clock_queue)
    profile = li.get_profile()
    lichess_bot.disable_restart()
    thr = threading.Thread(target=simulator, args=[move_queue, board_queue, clock_queue, results], daemon=True)
    thr.start()
    ev("start")
    if mode == "hang":
        # lichess-bot never gives up on a silent engine in a real-time game, so watch from outside:
        # after `observe` seconds without the 10th move, record it and kill this whole process tree.
        observe = float(os.environ.get("HANG_OBSERVE_S", "45"))

        def watchdog() -> None:
            time.sleep(observe)
            ev(f"WATCHDOG: no bot move and no engine restart within {observe:.0f}s of the hang")
            print(json.dumps({"mode": mode, "bot_won": None, "stuck_s": observe}), flush=True)
            time.sleep(2)   # let lichess-bot's log listener flush
            os.system(f"taskkill /T /F /PID {os.getpid()} >NUL 2>&1")
        threading.Thread(target=watchdog, daemon=True).start()
    lichess_bot.start(li, profile, cfg, logging.DEBUG, None, True, one_game=True)
    won = results.get()
    ev(f"RESULT bot_won={won}")
    print(json.dumps({"mode": mode, "bot_won": won}), flush=True)
    time.sleep(2)   # let lichess-bot's log listener flush
    manager.shutdown()
    os._exit(0)


if __name__ == "__main__":
    main()
