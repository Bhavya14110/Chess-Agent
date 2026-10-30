#!/usr/bin/env bash
# the whole nnue pipeline, reproducible from the repo:
#   1) self-play data with our own engine (tuned hce, fixed nodes, human-game start positions)
#   2) train on the gpu (our own pytorch trainer)
#   3) export, check the c++ inference matches the python reference bit for bit
#   4) copy the net to engine/corvus.nnue (cmake embeds it into the exe)
# usage: source/tools/nnue_pipeline.sh [step...]   steps: data train export   (default: all)
# knobs (env): THREADS=22 NODES=5000 POSITIONS=25000000 SEED=201 HIDDEN=256 EPOCHS=30 RUN=run1
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
THREADS="${THREADS:-22}"; NODES="${NODES:-5000}"; POSITIONS="${POSITIONS:-25000000}"; SEED="${SEED:-201}"
HIDDEN="${HIDDEN:-256}"; EPOCHS="${EPOCHS:-30}"; RUN="${RUN:-run1}"
gen="${GEN:-tools-bin/versions/datagen.exe}"   # any build without an embedded net = tuned hce
steps="${*:-data train export}"
mkdir -p logs data/selfplay

if [[ " $steps " == *" data "* ]]; then
  echo "== datagen: $POSITIONS positions, $THREADS threads, $NODES nodes, seed $SEED"
  "$gen" datagen data/selfplay "$THREADS" "$NODES" "$POSITIONS" "$SEED" data/lichess/positions_raw.txt \
    2>&1 | tee "logs/datagen_$SEED.log" | grep -E "finished|start positions" || true
fi

if [[ " $steps " == *" train "* ]]; then
  echo "== train: H=$HIDDEN, $EPOCHS epochs -> source/train/checkpoints/$RUN"
  python source/train/train.py --data "data/selfplay/*.bin" --hidden "$HIDDEN" --epochs "$EPOCHS" \
    --lambda 0.75 --lr 1e-3 --batch 16384 --seed 12345 --qat-epochs 2 --threads "${TORCH_THREADS:-2}" \
    --out "source/train/checkpoints/$RUN" --device cuda 2>&1 | tee "logs/train_$RUN.log" | grep -E "epoch|val" || true
fi

if [[ " $steps " == *" export "* ]]; then
  echo "== export + bit-exact check"
  python source/train/export.py "source/train/checkpoints/$RUN/last.pt" "source/train/checkpoints/$RUN/corvus.nnue"
  python source/train/quant_ref.py --net "source/train/checkpoints/$RUN/corvus.nnue" --data "data/selfplay/*.bin" \
    --count 10000 --checkpoint "source/train/checkpoints/$RUN/last.pt" \
    --vectors "source/train/checkpoints/$RUN/testvectors.txt" --vector-lines 1000
  "$gen" nnuetest "source/train/checkpoints/$RUN/corvus.nnue" "source/train/checkpoints/$RUN/testvectors.txt"
  "$gen" nnuecheck "source/train/checkpoints/$RUN/corvus.nnue" 100
  cp "source/train/checkpoints/$RUN/corvus.nnue" engine/corvus.nnue
  echo "net copied to engine/corvus.nnue; rebuild to embed it"
fi
