# Data provenance

Everything our engine learned from is listed here, with the exact commands to rebuild it.
No engine-produced data is used anywhere: no `[%eval]` comments, no engine-scored positions,
no other engine's nets or parameters, no games played by Lichess BOT accounts.

## 1. Human games (Texel tuning of the hand-crafted eval)

- Source: Lichess open database, `https://database.lichess.org/standard/lichess_db_standard_rated_2026-09.pgn.zst` (CC0).
- Filter (`source/tools/lichess_filter.py`, streamed, nothing else downloaded):
  both players rated >= 2200, base time >= 180 s, Termination "Normal", result 1-0 / 0-1 / 1/2-1/2,
  standard start position, neither player a BOT account. All comments (incl. `[%eval]`, `[%clk]`),
  NAGs and variations are stripped; only SAN moves + result are kept.
  - Pass 1 (stream): first 42.9% of the file (time budget 9000 s): 38,274,963 games scanned, 528,929 kept.
    Stats: `data/lichess/filter_stats_stream.json`.
  - Pass 2 (re-filter the output, BOT rule added): 493,215 kept, 35,714 BOT games dropped.
    Stats: `data/lichess/filter_stats_botpass.json`.
  ```
  python source/tools/lichess_filter.py --url https://database.lichess.org/standard/lichess_db_standard_rated_2026-09.pgn.zst --out data/lichess/stream.pgn --max-seconds 9000
  python source/tools/lichess_filter.py --url data/lichess/stream.pgn --out data/lichess/lichess_2026-09_elo2200_base180.pgn
  ```
  (Re-running pass 1 with a different network speed may scan a different fraction; use
  `--max-games 528929` instead of `--max-seconds` to reproduce the same game set exactly.)
- Positions (`source/tools/extract_positions.py`): drop the first 8 plies, drop in-check positions
  and a final stalemate, keep a random 40 per game (seeded per game), label = game result.
  ```
  python source/tools/extract_positions.py data/lichess/lichess_2026-09_elo2200_base180.pgn data/lichess/positions_raw.txt --per-game 40 --seed 1
  ```
  Result: 18,388,189 positions.
- Quiet filter (our own engine: keep positions where our qsearch == our static eval):
  ```
  engine/Corvus.exe quiet data/lichess/positions_raw.txt data/lichess/positions_quiet.txt 22
  ```
- Texel tuning (our own tuner, labels are game results only):
  ```
  engine/Corvus.exe tune data/lichess/positions_quiet.txt <epochs> 22 <lr> source/src/evaltuned.h
  ```
  Exact epochs/lr used are recorded in PROGRESS.md.

## 2. Self-play data (NNUE training)

- Generated only by our own engine (`Corvus datagen`), scored by our own search at a fixed node
  count; game openings are 8-10 uniformly random legal moves from the start position (no book).
- Commands and the seed/node settings are recorded in PROGRESS.md and `source/train/README.md`.

## 3. Test openings

- `source/tests/openings_v1.epd`: our own random-move openings (6-8 random plies from the start
  position, kept if our own depth-7 eval is within +-100 cp). Made by `source/tools/make_openings.py`.
