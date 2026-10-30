# PROGRESS

## Final summary
**Submitted version: tag `v-submit-final`.** Launch `engine/Corvus.exe` (UCI); fallback `engine/Corvus-portable.exe`.
Lichess BOT: `Bahubali8421` (lichess-bot 2026.10.27.3, config from `lichess-bot/config.example.yml`).

- **Engine:** our own C++20 bitboard engine (PEXT magics), PVS + aspiration + quiescence/SEE, bucketed TT, staged
  move ordering with history/continuation history, NMP, LMR, RFP/FP/LMP/SEE pruning, IIR, Lazy SMP, time management
  with panic mode. Perft exact on the CPW suite and 2000 cross-checked positions.
- **Evaluation:** NNUE `nnue1` ((768 -> 256) x 2 -> 1, SCReLU, int16), trained on 15.0M positions of Corvus self-play
  scored by Corvus's own search. Fallback: Texel-tuned hand-crafted eval.
- **Strength chain (SPRT, 8+0.08, our own openings):** Texel tuning **+67.7 +/- 19.0** (966 games);
  NNUE vs tuned HCE **+137** (526 games, LLR 2.95). Both H1.
- **Robustness:** uci_test 17/17 on both builds; 500 games at 10+0.1, 24 at 3+2 and 4 at 10+5 with 0 time losses,
  illegal moves, crashes or stalls; first Lichess game (3+2) won with 1:00 left on the clock.
- **Reproducibility:** a fresh `git clone` + `./build.sh` gives the same bench signature (218421 nodes) and the same
  net (sha256 94a27334...60c4) as the shipped binaries; uci_test passes on the clone build. The exe bytes differ
  (build paths), the behaviour does not.
- **Rules:** no opening book, no tablebases, no external engine, no engine-made data, no network access during play.
  Data: Lichess open database games (results only) and our own self-play. See DATA_PROVENANCE.md and README "Declared sources".

**Known gaps (honest list):**
- The Phase 3 per-feature SPRTs were spoiled by CPU starvation (see "Lesson") and the clean re-runs (`p3f-*`) were not
  finished; the features ship untested one by one. The two SPRTs after them (tuning, NNUE) were run under clean conditions.
- The NNUE gain was not re-confirmed at 40+0.4 (would take days at the laptop's allowed load); +137 is far outside any
  TC effect.
- Gate game counts at 3+2 / 10+5 are small (24 / 4) because the laptop runs hot and was often unattended.
- Competition config: lichess-bot runs with Threads 4 (heat). Raise it on the day if the laptop stays cool.

## Section 0 values (detected / chosen)

| Item | Value | How |
|---|---|---|
| Team name / repo folder | `chess-agent` (this repo root) | not given; default chosen |
| Engine name | `Corvus` | not given; picked a short original name |
| Lichess bot username | `TBD by you` (suggested: `CorvusChessBot`) | account must be created by you (see MY_TODO.md) |
| Submission / competition date | **2026-11-03** | given by the user on 2026-10-29 |
| Days available | ~4 (from 2026-10-29) | target: `v-submit-final` tagged by 2026-11-01, leaving 2 days for the bot account + live test |
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

## Phase 2 (uci_test PASS; 10+0.1 gate PASS; 3+2 gate running)
- 10+0.1 self-play, 500 games, conc 6 (build p4-hce-untuned2): 500/500 normal terminations, **0 time losses / illegal moves / crashes / stalls** (+161 =184 -155 colour-wise). Log: logs/selfplay/gate2-10s.log
- `source/tools/uci_test.py`: 17 checks (uci, isready mid-search, setoption, ucinewgame, position startpos/fen+moves incl. ep/promo/castling/50-move, 300-ply move list, go wtime/btime/winc/binc, movetime, depth, nodes, movestogo, infinite+stop, quit mid-search, panic at 300 ms, stdout cleanliness) — PASS on native and portable builds.

## Phase 3 (features committed one per commit, SPRT queue running)
p3-02 SEE in qsearch, p3-03 TT, p3-04 staged ordering, p3-05 PVS, p3-06 aspiration, p3-07 check ext, p3-08 NMP, p3-09 LMR, p3-10 RFP/FP/LMP/SEE pruning, p3-11 IIR, p3-12 continuation history. Results go to source/tests/sprt_log.md.

## Phase 6 prep (done early, by the NNUE helper agent + main session)
- PyTorch 2.14.1+cu126 installed; trainer runs on the RTX 4060 (~0.5M pos/s incl. loading). Synthetic smoke test: quantised vs float max 1.02 cp, mean 0.51 cp over 10k positions (QAT on the last epoch; without it QB=64 costs ~16 cp mean).
- C++ inference (`nnuetest`) matches the python integer reference on 1000 vectors: 0 mismatches, both AVX2 and portable scalar builds.

## Lesson: CPU starvation causes time losses
- With engines at Idle priority and another job saturating all cores, 317/528 games in the TT SPRT were lost on time (~100 ms overruns). Rules for testing from now on: no other heavy job while a time-control SPRT runs; engines at BelowNormal (not Idle) with concurrency <= ~14.
- Same lesson for competition day: the hosting laptop must run nothing heavy while the bot plays (added to MY_TODO).

## Phase 4: Texel tuning (done 2026-10-30)
- Data: 493,215 human Lichess games (BOT accounts removed) -> 18,388,189 positions -> 13,519,190 quiet (our qsearch == our eval). See DATA_PROVENANCE.md.
- `corvus tune data/lichess/positions_quiet.txt 800 22 1.0 source/src/evaltuned.h`: K = 0.8657, error 0.192112 -> 0.187941 (4 min). Linear model matches eval within 0.96 cp.
- Bug found: `writeparams` left out the comma at each line end, so the generated header did not compile. Fixed, and a writeparams round trip gives the same values.
- SPRT tuned (p4-tuned) vs untuned (p7-smp-ttfix): **H1, +67.7 +/- 19.0 Elo**, 966 games, 0 time losses.

## Phase 8 (first package): `v-submit-hce` tagged 2026-10-30 (d7846ad)
- Native + portable builds of the tuned HCE, both pass uci_test (17 checks); bench signature 28634 at depth 6 equals the SPRT'd build.
- Prohibited-item grep over all tracked files: only disclaimers / disabled settings match.

## Phase 6: NNUE in search (done 2026-10-30)
- Lazy accumulator stack: one slot per search ply, a slot is valid when its stored key equals the position key at that ply, so unmake needs no work and only nodes that evaluate pay for updates. `State.moved` records the moving piece.
- `nnuecheck`: 27,678 incremental-vs-refresh checks with null moves and take-backs, 0 mismatches. `nnuetest` vs python reference: 0/1000 mismatches. AVX2 lane sum now 64-bit.
- UCI: `UseNNUE`, `EvalFile` (`<embedded>` default). Tools that tune the HCE never load the net.
- Datagen can start games from human-game positions (3 in 4 games), see OPEN_QUESTIONS #4.
- Pipeline: `source/tools/nnue_pipeline.sh` (datagen -> train -> export -> bit-exact checks -> engine/corvus.nnue).

## Phase 6: pipeline check with an interim net (2026-10-30)
- Trained H=256 for 6 epochs on the 7.0M seed-201 positions: 42 s on the RTX 4060 (~1.7M pos/s), val loss 0.00964.
- Export: worst-case |acc| 4756 (int16 safe), max |sum| 32.3M (int32 safe). Float vs quantised: max 1.02 cp, mean 0.51 cp.
- C++ vs python reference: 0/1000 mismatches; incremental vs refresh: 0/7032 mismatches.
- Sanity evals (stm): start +28, queen down -870, K+P vs K +256, K vs K +12.

## Phase 7: first real net `nnue1` (2026-10-30)
- Data: 15,000,236 self-play positions (seeds 101-204, see DATA_PROVENANCE 2), made at user-limited load (3-8 threads, CPU capped at 85% for heat).
- Training: H=256, 30 epochs (last 2 QAT), ~4.5 min on the RTX 4060. Val loss 0.00989 (epoch 1) -> 0.00837 (epoch 27), 0.00853 after QAT.
- Export: worst-case |acc| 13951 (int16 safe), max |sum| 36.6M (int32 safe), float vs quantised max 1.02 cp. C++ vs python 0/1000, incremental vs refresh 0/13938.
- **SPRT vs tuned HCE: H1, about +137 Elo, 526 games, concurrency 1** (sprt_log.md). Net committed as `engine/corvus.nnue`, release binaries rebuilt with it embedded; uci_test PASS on both; bench 218421 nodes.
- Decision: the 40+0.4 confirm is skipped for this change for now. +137 at 8+0.08 is far outside any TC effect, and a 40+0.4 SPRT at concurrency 1 would take days. Revisit if the laptop can run it at higher concurrency.

## Phase 8 gates on the release build with `nnue1` (2026-10-30)
- Self-play with the exact `engine/Corvus.exe` (f3656b0), no adjudication, Hash 64, 1 thread, 3 games at a time.
- **3+2: 24/24 games ended normally, 0 time losses / illegal moves / crashes / stalls.** Log: logs/selfplay/gate8-3p2.log
- **10+5: 4/4 finished games clean** (2 more were stopped mid-game when the user asked to stop all load). Log: logs/selfplay/gate8-10p5.log
- Decision: game counts are small because the laptop runs hot and is unattended; earlier gates (500 games at 10+0.1, Phase 2) cover crash/time robustness at volume.
- Live check: Lichess BOT `Bahubali8421` (lichess-bot 2026.10.27.3, local config, Threads 4) won its first 3+2 casual game, ending with 1:00 on the clock, normal chat.

## Status
- 2026-10-30 08:50 IST: user allowed full power. Datagen seed 201: 25M positions, 22 threads, 5000 nodes, ~6800 pos/s.
- Overnight the laptop slept from ~00:45 to ~08:30 (keep-awake lapsed when the session went idle); only code work happened at night, by the user's choice (laptop at 95 C).
- **PAUSED by the user (2026-10-30 ~09:15 IST)**: datagen seed 201 stopped at ~7.0M positions (224 MB in data/selfplay, kept). Resume with a NEW seed so games don't repeat: `THREADS=22 POSITIONS=18000000 SEED=202 source/tools/nnue_pipeline.sh data`.
- Resumed 2026-10-30 at user-limited load: seed 202 (8 threads, ~0.7M positions), then **seed 203 with 4 threads** (user: 3-4 threads, ~5 GB RAM max), target 18M total.
- Next: train H=256 on GPU -> embed -> SPRT NNUE vs HCE (8+0.08, then 40+0.4) -> Phase 3 re-runs -> gates -> v-submit-final.
- 2026-10-30 16:47 IST: `nnue1` passed its SPRT (+137 Elo) and is embedded in engine/. Next: Phase 3 re-runs (low concurrency), 3+2 and 10+5 gates, clean-clone build check, v-submit-final.
- 2026-10-30 19:20 IST: all load stopped at the user's request (test games and lichess-bot). Next: clean-clone build check, v-submit-final.
