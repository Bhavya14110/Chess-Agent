# PROGRESS

## Final summary
_(filled in at the end of the run)_

## Section 0 values (detected / chosen)

| Item | Value | How |
|---|---|---|
| Team name / repo folder | `chess-agent` (this repo root) | not given; default chosen |
| Engine name | `Corvus` | not given; picked a short original name |
| Lichess bot username | `TBD by you` (suggested: `CorvusChessBot`) | account must be created by you (see MY_TODO.md) |
| Submission date | unknown | not given; logged in OPEN_QUESTIONS.md |
| Days available | assumed 14 | not given |
| Host | Windows 11 Home, Intel i7-14650HX (16 cores: 8P+8E, 24 threads), 15.7 GB RAM, AVX2 yes, BMI2 yes (no AVX-512) | detected via Win32_Processor / nproc |
| Training compute | NVIDIA RTX 4060 Laptop 8 GB, CUDA 13.1 driver; PyTorch 2.14.1 on Python 3.10 | detected via nvidia-smi / pip |
| Free disk | ~549 GB on C: | df -h |
| Toolchain | WinLibs GCC 16.2 (mingw-w64 UCRT, POSIX threads) + CMake 4.x, installed via winget | only compiler present was 32-bit GCC 6.3, too old for C++20 |

## Rules summary (from docs/guidelines.pdf + docs/how-to-build.pdf)

1. All move selection, search and evaluation must be the team's own code; core search/eval must not be copied. Anything adapted from tutorials/books/repos is declared in README.
2. Never use, call, wrap or copy any existing engine (Stockfish, Lc0, Komodo, ...), its code, weights, parameters or analysis.
3. Any NN must be trained by us on data we collected (human games) or generated (our own engine's self-play). No engine-scored data (e.g. Stockfish evals, `[%eval]` PGN comments).
4. Chess-rules libraries (python-chess) and general libs (NumPy, PyTorch) are fine; anything that judges positions or picks moves is not.
5. Prohibited: opening books, endgame tablebases, any external service/API/LLM during play, lichess-bot online move sources, polyglot book, Syzygy/Gaviota.
6. UCI engine: must handle `uci`, `isready`, `ucinewgame`, `position startpos|fen ... moves`, `go wtime btime winc binc`, `quit`, and always answer `go` with a legal `bestmove`.
7. Single launch command from its own folder; stdout = UCI only, debug to stderr/log; files loaded by relative path; compiled agents ship a ready-to-run executable plus source.
8. Knockout at 3+2 (single game, replay on draw), final vs chess club at 10+5 with the *exact same* submitted version. Time loss / illegal move / crash / hang = loss.
9. Bot runs on our own hardware via lichess-bot; standard + casual only, 3+2 and 10+5, challenges only from organisers/competitors/club, matchmaking off, courteous chat, resign/draw on own eval allowed. No human moves.
10. Submit: GitHub repo (engine/, source/, README.md with launch command + bot username + approach + declared sources), bot username, `challenge:write` token. Organisers may ask us to rebuild from source — training must be reproducible.

## Decisions log
- Repo root = this folder (`chess agent`); layout per Section 7 of the brief.
- Installed a modern GCC via winget because the system g++ is MinGW.org 6.3 (32-bit, no C++20). CMake from winget (user scope) and WinLibs both present; build scripts use WinLibs' cmake/ninja.
- Magic numbers are searched at startup with a fixed-seed PRNG (portable build); native build uses PEXT (i7-14650HX has fast BMI2).
- Draw detection (repetition incl. game history, 50-move, insufficient material) went into the Phase 2 baseline because it is correctness, not strength.
- Phase 2 eval values are hand-picked by us (no PeSTO / other-engine tables — those are engine-tuned parameters).
- fastchess (match runner only, not an engine) built from source into `tools-bin/` (gitignored). Test openings are our own (`source/tests/openings_v1.epd`).
- Windows locks running .exe files, so dev builds are snapshotted to `tools-bin/versions/<name>.exe` and SPRTs run on those.
- Parallel helpers (user asked for multiple agents): (1) Lichess data pipeline, (2) lichess-bot config + MY_TODO, (3) PyTorch NNUE trainer. Core engine work stays in the main session.
- PyTorch installed here is CPU-only; the NNUE agent tries a CUDA wheel.
- lichess-bot needs Python >= 3.11 → use `py -3.12` for the bot (engine tooling stays on 3.10).

## Phase 1 gate (PASS)
- CPW perft suite, full depth, all exact (`python source/tests/perft_suite.py ./engine/Corvus.exe`):
  startpos d6 119,060,324; Kiwipete d5 193,690,690; pos3 d7 178,633,661; pos4 d6 706,045,033 (+mirror); pos5 d5 89,941,194; pos6 d5 164,075,551. 2.16G nodes in 11.6 s ≈ **186 Mnps** (legal movegen, bulk count at leaves, native PEXT build).
- 1000 random-playout positions vs python-chess at depth 3: 1000/1000 OK, incremental Zobrist == recomputed at every node.
- 1000 Lichess-game positions (first 5000 kept games of the 2026-09 2200+ filter) vs python-chess at depth 3: **1000/1000 OK**.

## Bug found and fixed (important for reading the SPRT log)
- TT verified entries with `key >> 48` while the bucket index also comes from the high key bits, so the check was nearly useless and other positions' entries were accepted. Symptom: uci_test sometimes missed a mate in 1 after earlier searches; PVS looked like it exploded on Kiwipete. Fixed in 3fc0d33 (use the low 16 bits). All Phase 3 SPRTs from the TT feature onward were re-run on rebuilt binaries (`p3f-*`) that carry the fix.

## Phase 2 (code done; gate games pending)
- `source/tools/uci_test.py`: 17 checks (uci, isready mid-search, setoption, ucinewgame, position startpos/fen+moves incl. ep/promo/castling/50-move, 300-ply move list, go wtime/btime/winc/binc, movetime, depth, nodes, movestogo, infinite+stop, quit mid-search, panic at 300 ms, stdout cleanliness) — PASS on native and portable builds.

## Phase 3 (features committed one per commit, SPRT queue running)
p3-02 SEE in qsearch, p3-03 TT, p3-04 staged ordering, p3-05 PVS, p3-06 aspiration, p3-07 check ext, p3-08 NMP, p3-09 LMR, p3-10 RFP/FP/LMP/SEE pruning, p3-11 IIR, p3-12 continuation history. Results go to source/tests/sprt_log.md.

## Status
- Phase: 3 SPRTs running; Phase 4 HCE + tuner + quiet filter written; Phase 5 datagen written.
- Running jobs: SPRT queue (`source/tools/sprt_queue.sh logs/queue_p3.txt`, log logs/sprt_queue_p3.out, per-test logs in logs/sprt/); Lichess download (agent); NNUE trainer smoke test (agent).
- Next step: quiet-filter Lichess positions → Texel tune → SPRT tuned vs untuned → package v-submit-hce.
