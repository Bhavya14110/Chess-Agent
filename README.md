# Corvus — chess-agent competition entry

Corvus is a UCI chess engine written from scratch in C++20 by our team.

## Launch command

From the `engine/` folder:

```
Corvus.exe
```

(Windows x64, built with `-march=native` for our hosting machine: Intel i7-14650HX with AVX2/BMI2.)
A fallback build that only needs x86-64 + POPCNT ships next to it as `Corvus-portable.exe`.
The engine reads nothing from disk at startup: the neural network (and the hand-crafted fallback) are compiled in.

- **Lichess bot username:** `Bahubali8421`
- **Protocol:** UCI (via lichess-bot; config template in `lichess-bot/config.example.yml`)
- **UCI options:** `Hash` (MB, default 64), `Threads` (default 1), `Move Overhead` (ms, default 150),
  `UseNNUE` (default true; only matters when a net is built in), `EvalFile` (default `<embedded>`)

## Approach

- **Board / move generation:** bitboards, magic bitboards (PEXT on BMI2 CPUs), incremental Zobrist hashing,
  make/unmake with an undo stack, legal move generation (pseudo-legal + pin/check-aware legality test).
  Verified with perft against the Chess Programming Wiki suite and against python-chess on random and Lichess positions.
- **Search:** iterative deepening, principal variation search with aspiration windows, quiescence search with SEE,
  transposition table (bucketed, depth/age replacement), staged move ordering (TT move, good captures, killers,
  counter-move, butterfly + continuation history, bad captures), check extensions, null-move pruning,
  late move reductions, reverse futility / futility / late-move / SEE pruning, internal iterative reduction,
  repetition / fifty-move / insufficient-material draw detection, mate-distance pruning.
- **Evaluation:** an NNUE network trained only on our own data: (768 -> 256) x 2 perspectives -> 1, SCReLU,
  int16 weights, AVX2 inference, accumulators updated incrementally along the search stack. It was trained on
  15.0M positions from Corvus playing itself (scored by Corvus's own search at 5000 nodes, start positions from
  human Lichess games), with our own PyTorch trainer. SPRT vs the hand-crafted eval: **+137 Elo** (526 games).
  The fallback (`UseNNUE false`) is a tapered hand-crafted evaluation (material, piece-square tables, mobility,
  pawn structure, passed pawns, king safety, threats, endgame scaling, mop-up), Texel-tuned on quiet positions
  from human Lichess games (labels = game results only).
- **Time management:** soft/hard limits from the clock and increment, extra time when the best move is unstable,
  panic mode under 3 s, `Move Overhead` for network lag.
- **No** opening book, **no** endgame tablebases, **no** external engine, **no** engine-produced data, **no** network access.
  Every move including move 1 comes from the search.

## Building

Requirements: GCC >= 13 (we use WinLibs GCC 16.2, mingw-w64 UCRT) and CMake >= 3.20, Ninja.

```
./build.sh
```

builds `engine/Corvus.exe` (native) and `engine/Corvus-portable.exe` (portable).

## Reproducing the evaluation weights

See `DATA_PROVENANCE.md` for every dataset (source URL, date, filters, exact commands, counts, seeds).

The network `engine/corvus.nnue` is rebuilt with one script (needs a CUDA GPU for training; CPU works but is slow):

```
THREADS=8 POSITIONS=15000000 SEED=201 source/tools/nnue_pipeline.sh data   # self-play data, our engine only
RUN=nnue1 HIDDEN=256 EPOCHS=30 source/tools/nnue_pipeline.sh train export  # train, export, bit-exact checks
./build.sh                                                                   # embeds engine/corvus.nnue
```

The export step checks the C++ inference against a Python reference (0/1000 mismatches) and
incremental updates against full refreshes before it copies the net into `engine/`.

## Python tooling

Python 3.10 for training/tooling (`pip install -r requirements.txt`); lichess-bot itself needs Python >= 3.11.

## Declared sources

We wrote all code ourselves. Ideas (not code) come from public documentation:

- Chess Programming Wiki (chessprogramming.org): perft positions and node counts, magic bitboards, PVS, null-move pruning,
  LMR, futility pruning, SEE (swap algorithm), Texel tuning method, NNUE overview.
- NNUE ideas from public descriptions (CPW NNUE page and general community write-ups): perspective accumulators,
  incremental updates, SCReLU activation, int16 quantisation. The network format, inference code and PyTorch
  trainer are our own; no other engine's net, trainer code or data was used.
- "How to Build a Chess Agent" (competition handout) — Texel tuning / SPRT workflow.
- Test harness only (not part of the engine, not shipped): fastchess (github.com/Disservin/fastchess) for engine-vs-engine
  matches between our own builds; python-chess for perft cross-checks and PGN parsing.
- lichess-bot (github.com/lichess-bot-devs/lichess-bot) is the official bridge; we only provide its config.
