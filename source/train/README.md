# Corvus NNUE training

Everything in this folder was written from scratch for Corvus. Real nets are trained
**only on Corvus self-play data**. `make_synthetic.py` exists only to test the pipeline;
its data is never used to train a real net.

## Requirements

- Python 3.10, `numpy`, `torch` (training/export), `python-chess` (only for
  `make_synthetic.py` and the `dataset.py` self test).
- GPU (optional): CUDA build of PyTorch. Installed here as `torch 2.14.1+cu126` from
  `https://download.pytorch.org/whl/cu126` (wheel `torch-2.14.1+cu126-cp310-cp310-win_amd64.whl`,
  2.6 GB, sha256 `f3ec1126a975ac573f691e8cf1001051a96a14f8d27a75365f56173d801424d7`):
  `python -m pip install --force-reinstall --no-deps torch==2.14.1+cu126 --index-url https://download.pytorch.org/whl/cu126`
  (if the download keeps breaking, fetch the wheel with a resumable downloader, check the
  sha256, then `python -m pip install --force-reinstall --no-deps <wheel file>`).
- `quant_ref.py` (and `export.read_nnue`) need numpy only.

## Files

| file | purpose |
|---|---|
| `dataset.py` | 32-byte record format, memory-mapped reader, vectorised numpy decoder and an identical torch decoder (used by the trainer, runs on the GPU), shuffled background batch loader, record<->FEN helpers, `encode_record()` (python-chess board -> record). `python dataset.py` runs a round-trip self test. |
| `model.py` | `CorvusNNUE(hidden)`: shared 768->H feature transformer (`EmbeddingBag(sum)` + bias), SCReLU, (2H)->1. Weight clipping constants. |
| `train.py` | trainer (AdamW, cosine/step LR, checkpoints, resume, hparams JSON). |
| `export.py` | checkpoint -> `.nnue` (quantised). |
| `quant_ref.py` | numpy integer reference inference (the spec the C++ must match), test-vector writer, float-vs-quantised comparison. |
| `make_synthetic.py` | SYNTHETIC pipeline-test data (random playouts, material labels). |

## Train

```
python source/train/train.py --data "data/selfplay/*.bin" --hidden 256 --epochs 20 \
    --lambda 0.75 --lr 1e-3 --batch 16384 --seed 12345 \
    --out source/train/checkpoints/run1 --device cuda
```

Useful flags: `--qat-epochs N` (default 1: the last N epochs train quantisation-aware,
i.e. the forward pass uses weights rounded onto the export grid with straight-through
gradients; this makes the exported integer net match the float net to ~1 cp instead of
~16 cp mean / 65 cp max, because with QB = 64 the output weights only have a few bits of
resolution; `0` disables it), `--epoch-size N` (positions per epoch/"superbatch"; default one full pass),
`--schedule cosine|step` (`--final-lr-frac`, `--step-every`, `--step-gamma`), `--wd`
(AdamW weight decay, 0.01), `--val-data GLOB` or `--val-frac 0.01` (tail of the data held
out), `--device auto|cpu|cuda`, `--threads N`.

Output in `--out`: `epochNNN.pt` every epoch, `last.pt`, `hparams.json` (all settings,
data files, quantisation constants), `history.json` (loss / speed per epoch).
Resume: same command plus `--resume auto` (or `--resume path/to/epochNNN.pt`); the
optimiser state, step counter (so the LR schedule) and RNG state are restored, and the
data order for each epoch depends only on `(seed, epoch)`.

Target, from the side to move's point of view:
`target = lambda * sigmoid(score_stm / 400) + (1 - lambda) * result_stm` with
`score_stm = score` if white to move else `-score`, `result_stm = result/2` (white to move)
or `1 - result/2` (black to move). Loss = `mean((sigmoid(net_out) - target)^2)`;
`eval_cp = net_out * 400`.

## Export and check

```
python source/train/export.py source/train/checkpoints/run1/last.pt corvus.nnue
python source/train/quant_ref.py --net corvus.nnue --fen "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"
python source/train/quant_ref.py --net corvus.nnue --data "data/selfplay/*.bin" --count 10000 \
    --checkpoint source/train/checkpoints/run1/last.pt \
    --vectors source/train/testvectors.txt --vector-lines 1000
```

`testvectors.txt` lines are `FEN;eval` (eval = integer cp, side to move POV). FENs come
from records, so castling and en passant are written as `-` (they do not affect the net).

## Pipeline smoke test (synthetic data)

```
python source/train/dataset.py
python source/train/make_synthetic.py --out source/train/synthetic/synth.bin --positions 2000000
python source/train/train.py --data source/train/synthetic/synth.bin --hidden 256 --epochs 4 \
    --out source/train/checkpoints/synthetic --device cuda
python source/train/export.py source/train/checkpoints/synthetic/last.pt source/train/synthetic/synthetic.nnue
python source/train/quant_ref.py --net source/train/synthetic/synthetic.nnue \
    --data source/train/synthetic/synth.bin --count 10000 \
    --checkpoint source/train/checkpoints/synthetic/last.pt \
    --vectors source/train/testvectors_synthetic.txt --vector-lines 1000
```

Smoke-test result (2M synthetic positions, H=256, 4 epochs, last epoch QAT, RTX 4060
laptop): ~0.5M pos/s on GPU (CPU: ~26k pos/s, measured while the CPU was saturated by
other jobs); float vs quantised over 10,000 positions: max 1.02 cp, mean 0.51 cp.

## Network and file format (what the C++ must implement)

Features (768 per perspective), piece code `c` = 0..5 white P,N,B,R,Q,K / 6..11 black:
- white perspective: `c * 64 + sq`
- black perspective: `((1 - c/6) * 6 + c%6) * 64 + (sq ^ 56)`
- `stm` accumulator = perspective of the side to move, `nstm` = the other one.

Quantisation: QA = 255, QB = 64, SCALE = 400.

`.nnue` (little-endian): `"CVNN"`, u32 version = 1, u32 H, `ft_w_q` int16[768*H]
(index `f*H + j`), `ft_b_q` int16[H], `out_w_q` int16[2H] (first H = stm half),
`out_b_q` int32. Nothing else.

Integer inference:
```
acc[p][j] = ft_b_q[j] + sum_f ft_w_q[f*H + j]                    (int16, cannot overflow)
sum  = sum_j clamp(acc[stm][j],0,255)^2 * out_w_q[j]
     + sum_j clamp(acc[nstm][j],0,255)^2 * out_w_q[H + j]         (accumulate in int64,
                                                                    or int32, see below)
t    = sum / 255        (C truncation toward zero)
t   += out_b_q
eval = t * 400 / (255 * 64)   (C truncation toward zero)  -> cp, side to move POV
```

Clipping applied after every optimiser step (see `model.py`):
- feature transformer weights and biases in [-3.5, 3.5] -> |q| <= 892, so
  bias + 32 rows <= 29436 and even bias + 34 rows (transient states during an incremental
  add-before-sub update) <= 31220 < 32767.
- output weights in [-127/64, 127/64] -> |out_w_q| <= 127, so `v * out_w_q` with
  v <= 255 fits int16 (32385), allowing a `v * (v * w)` multiply-add scheme.
- int32 accumulation of `sum` is not guaranteed by the clipping alone (theoretical
  worst case 2H * 255^2 * 127 > 2^31 for H = 256); `quant_ref.py` prints the max |sum|
  seen so you can check the margin, or simply accumulate in int64.
