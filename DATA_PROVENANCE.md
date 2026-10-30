# Data provenance

Everything our engine learned from is listed here, with the exact commands to rebuild it.
No engine-produced data is used anywhere: no `[%eval]` comments, no engine-scored positions,
no other engine's nets or parameters, no games played by Lichess BOT accounts.

## 1. Lichess human games (Texel tuning)

| | |
|---|---|
| Source | Lichess open database, standard rated games, September 2026 |
| URL | https://database.lichess.org/standard/lichess_db_standard_rated_2026-09.pgn.zst (29,224,520,887 bytes, Last-Modified 2026-10-03) |
| Licence | CC0 1.0 (public domain dedication), see https://database.lichess.org/ |
| Downloaded | 2026-10-29, streamed over HTTPS (compressed file never stored on disk) |
| Portion read | first 12,529,434,624 compressed bytes (42.9 % of the file) = games played 2026-09-01 00:00 to 2026-09-13 ~22:17 UTC. The download was time-capped, so the rest of the month was not read |

### Filters (all must hold), implemented in `source/tools/lichess_filter.py`
1. `WhiteElo >= 2200` **and** `BlackElo >= 2200`.
2. Base time from `TimeControl` >= 180 s (`"180+2"` -> 180). `"-"` (correspondence) is rejected.
3. `Termination == "Normal"`. This rejects "Time forfeit", "Abandoned", "Rules infraction" and "Unterminated".
4. `Result` is one of `1-0`, `0-1`, `1/2-1/2`.
5. Standard variant (no `Variant` other than Standard, no `SetUp`/`FEN` tag).
6. **Neither player is a Lichess BOT account** (`WhiteTitle`/`BlackTitle` == `"BOT"`). BOT accounts are engines, so their games are not human games.
7. Movetext is rebuilt from SAN moves only. Every `{...}` comment (including `[%eval ...]` and `[%clk ...]`), every NAG/annotation glyph (`!`, `?`, `!?`, `?!`, `$n`; on Lichess these come from computer analysis) and any `(...)` variation is **stripped**. Tag pairs are kept as-is (Lichess tags contain no evals). A check of the final PGN found 0 occurrences of `%eval`/`%clk`, `{`, `(`, `$` or `!`/`?` in the movetext, and 0 `BOT` titles.

### Position extraction, implemented in `source/tools/extract_positions.py`
- For each game, the positions after plies 1–8 (and the start position) are dropped, so the first candidate is the position after ply 9.
- Positions where the side to move is in check are skipped. A final position with no legal moves (mate/stalemate) is also skipped.
- Each line is `FEN;RESULT`. RESULT is the game result from White's point of view: `1.0` = 1-0, `0.5` = draw, `0.0` = 0-1.
- At most 40 positions per game, chosen at random. The RNG is seeded per game with `random.Random(f"{seed}:{game_index}")`, so the output does not depend on the worker count. **Seed = 1.**
- No quiet-position filter is applied here; that step is done later by our own engine.

### Exact commands (run from the repo root)
```
# 1) stream + filter (this run: 4 h 01 min wall time, see "Notes")
python source/tools/lichess_filter.py \
    --url https://database.lichess.org/standard/lichess_db_standard_rated_2026-09.pgn.zst \
    --out data/lichess/lichess_2026-09_elo2200_base180.pgn --max-games 1000000 --max-seconds 9000 \
    --conns 6 --stats data/lichess/filter_stats.json --stop-file data/lichess/STOP
# 1b) the streamed run above used the script before filter 6 (BOT) was added; the BOT
#     filter was then applied by re-running the (now complete) script over that output:
python source/tools/lichess_filter.py --url data/lichess/stream_output_unfiltered_bots.pgn \
    --out data/lichess/lichess_2026-09_elo2200_base180.pgn --stats data/lichess/filter_stats_botpass.json
#     (it was verified that the result is exactly the stream output minus every game with a
#      BOT player; the intermediate file containing BOT games was then deleted.)
#     Running step 1 with the current script on the same byte range gives the final file directly.

# 2) positions
python source/tools/extract_positions.py data/lichess/lichess_2026-09_elo2200_base180.pgn \
    data/lichess/positions_raw.txt --per-game 40 --seed 1

# move-generator cross-check on these games (not training data)
python source/tests/perft_random.py ./tools-bin/corvus-p1.exe \
    --pgn data/lichess/perft_sample_first5000.pgn --n 1000 --depth 3   # -> logs/perft_lichess.txt
```
Note: the game set depends on how far the stream got in 9000 s, so it depends on network speed. To reproduce the final PGN exactly in one pass with the current script (which already has the BOT rule), replace `--max-seconds 9000` with `--max-games 493215`. The stream order and the filtering are deterministic. Logs: `logs/lichess_filter.log`, `logs/extract_positions.log`. Stats JSON: `data/lichess/filter_stats_stream.json` (step 1; written as `filter_stats.json` and then renamed, and the step-1 output PGN was renamed to `stream_output_unfiltered_bots.pgn` before step 1b) and `data/lichess/filter_stats_botpass.json` (step 1b).

### Counts
| | |
|---|---|
| Games scanned | 38,274,963 |
| Rejected: Elo < 2200 (either side) | 35,855,956 |
| Rejected: base time < 180 s / correspondence | 1,738,665 |
| Rejected: Termination != Normal | 151,406 |
| Rejected: unparsable movetext | 7 |
| Kept by stream filter | 528,929 |
| Rejected: BOT player | 35,714 |
| **Games kept (final)** | **493,215** (1-0: 238,399 · 0-1: 209,987 · 1/2-1/2: 44,829) |
| **Positions written** | **18,388,189** (1.0: 8,791,671 · 0.5: 1,756,499 · 0.0: 7,840,019; 16,227,296 unique by board+stm+castling+ep) |
| Random seed | 1 |

SHA-256:
- `data/lichess/lichess_2026-09_elo2200_base180.pgn`: `a0015b8043884f87aec14ba46698847af33f653c5636d651ea6251ae6f6248d2`
- `data/lichess/positions_raw.txt`: `bffb899ada893bf5f7f8808bc37a8b136311ac87dcc8348bf9fd8d6940aa4aa5`

### Downstream steps (our own engine only)
- Quiet filter (our own engine: keep positions where our qsearch == our static eval):
  ```
  engine/Corvus.exe quiet data/lichess/positions_raw.txt data/lichess/positions_quiet.txt 22
  ```
- Texel tuning (our own tuner, labels are game results only):
  ```
  engine/Corvus.exe tune data/lichess/positions_quiet.txt <epochs> 22 <lr> source/src/evaltuned.h
  ```
  Exact epochs/lr used are recorded in PROGRESS.md.

### Notes
- Only Python packages were used: `python-chess` (PGN/SAN parsing and FEN; `chess.engine` is never used), `zstandard` and `requests` (which was already installed). No chess engine was involved.
- Network: about 2 MB/s total over 6 parallel HTTP Range connections. There were 59 retried segments (dropped connections, read timeouts, and a burst of SSL errors around 18:10 local time). One request trickled for about 2 h 13 min without triggering the socket timeout, which is why the 9000 s cap fired late. The script now also aborts and retries any 8 MB segment that takes longer than 300 s. That patch was checked to give byte-identical output.

## 2. Self-play data (NNUE training)

- Generated only by our own engine (`corvus datagen`, tuned HCE build without a net), scored by our
  own search at 5000 nodes per move. Record format: 32 bytes (see `source/train/README.md`):
  position, our search score (white POV, cp), game result.
- Start positions: 1 in 4 games start with 8-10 uniformly random legal moves from the initial
  position; 3 in 4 start from a position of the human Lichess set above (`positions_raw.txt`,
  every 9th line -> 2,043,133 candidates) plus 1-2 uniformly random legal moves. Openings with
  |our score| > 300 are discarded. Nothing from these starts is stored in the engine.
- Kept positions: not in check, our best move not noisy, score not a mate score.
- Adjudication: |score| > 2500 for 4 plies = win; after ply 80, |score| < 10 for 10 plies = draw.
- Runs (files `data/selfplay/data_<seed>_<thread>.bin`, appended):
  - seed 101: ~1 min, 2 threads (test of the start-position option)
  - seed 201: 22 threads, stopped by the user at ~7.0M positions
  - seed 202: 8 threads, stopped at 366,023 positions (load cut at the user's request)
  - seed 203: 4 threads, stopped at 3,489,752 positions (paused for heat)
  - seed 204: 3 threads, ran to its 3,823,059 target (3,823,295 written)
  - **total used for net `nnue1`: 15,000,236 positions** (101: 49,332; 201: 7,271,834;
    202: 366,023; 203: 3,489,752; 204: 3,823,295)
  ```
  THREADS=22 POSITIONS=25000000 SEED=201 source/tools/nnue_pipeline.sh data
  THREADS=8  POSITIONS=11000000 SEED=202 source/tools/nnue_pipeline.sh data
  THREADS=4  POSITIONS=10312811 SEED=203 source/tools/nnue_pipeline.sh data
  THREADS=3  POSITIONS=3823059  SEED=204 source/tools/nnue_pipeline.sh data
  ```
  The stopped runs mean the exact data set is "these seeds until these counts"; any ~15M positions
  made this way give a net of about the same strength.
  Thread scheduling makes the exact game set vary between machines; the seeds and settings are fixed.
- Training / export / checks: `source/tools/nnue_pipeline.sh train export` (hyperparameters in that script
  and in `source/train/checkpoints/<run>/hparams.json`).
- Net `engine/corvus.nnue` (run `nnue1`): `RUN=nnue1 HIDDEN=256 EPOCHS=30 source/tools/nnue_pipeline.sh train export`
  (lambda 0.75, lr 1e-3, batch 16384, seed 12345, last 2 epochs QAT), RTX 4060, ~4.5 min.

## 3. Test openings

- `source/tests/openings_v1.epd`: our own random-move openings (6-8 random plies from the start
  position, kept if our own depth-7 eval is within +-100 cp). Made by `source/tools/make_openings.py`.
