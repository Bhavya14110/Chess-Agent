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
- Installed a modern GCC via winget because the system g++ is MinGW.org 6.3 (32-bit, no C++20).

## Status
- Phase: 1 (scaffold + board + movegen)
- Running jobs: none
- Next step: write board/movegen, perft gate
