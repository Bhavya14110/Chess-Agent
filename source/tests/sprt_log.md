# SPRT log

All tests: fastchess, our own builds only, our own opening suite (`source/tests/openings_v1.epd`),
8+0.08 screening TC unless stated, 1 thread, 16 MB hash, alpha = beta = 0.05, normalized Elo bounds
[elo0, elo1] (gainers [0, 5], simplifications [-5, 0]). Elo below is logistic Elo ± 95% error from fastchess.
Draw adjudication (move 40+, |score| < 8 for 8 moves) and resignation (|score| > 800 for 4 moves) are used in testing only.

| commit | change | vs | TC | result | games | Elo ± err | LLR |
|---|---|---|---|---|---|---|---|
| 489daab | SEE: skip losing captures in qsearch | p2-base | 8+0.08 | **H1 accepted** | 2482 | +26.8 ± 11.6 | 2.97 |
| 26c9dcd+fix | transposition table (fixed key check) | p3-02-see | 8+0.08 | **INVALID** (re-run) | 528 | (-151) | � |

Note on the invalid TT run: 317/528 games were lost on time (~100 ms overruns, both builds) because the engines ran at Windows Idle priority while the data-extraction job used every core at a higher priority. Time-control SPRTs must not share the CPU with other heavy jobs; re-run planned under clean conditions.
