"""Train the Corvus NNUE on Corvus self-play data. Written from scratch.

Example:
  python source/train/train.py --data "data/selfplay/*.bin" --hidden 256 --epochs 20 \
      --out source/train/checkpoints/run1 --device cuda

Loss: MSE( sigmoid(net_out), lam*sigmoid(score_stm/400) + (1-lam)*result_stm ).
"""
from __future__ import annotations

import argparse
import json
import math
import os
import random
import sys
import time

import numpy as np
import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dataset import BatchLoader, RecordStore  # noqa: E402
from model import CorvusNNUE, QA, QB, SCALE, FT_CLIP, OUT_CLIP  # noqa: E402


def parse_args(argv=None):
    p = argparse.ArgumentParser(description="Corvus NNUE trainer")
    p.add_argument("--data", required=True, help="glob (or comma separated globs) of .bin files")
    p.add_argument("--val-data", default=None, help="optional glob of validation files")
    p.add_argument("--val-frac", type=float, default=0.01,
                   help="if --val-data is not given, hold out this tail fraction of --data")
    p.add_argument("--val-max", type=int, default=500_000, help="cap on validation positions")
    p.add_argument("--hidden", type=int, default=256)
    p.add_argument("--epochs", type=int, default=10)
    p.add_argument("--epoch-size", type=int, default=0,
                   help="positions per epoch (superbatch); 0 = one full pass over the data")
    p.add_argument("--lambda", dest="lam", type=float, default=0.75,
                   help="weight of the search score in the target (1-lambda on the game result)")
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--final-lr-frac", type=float, default=0.05,
                   help="cosine: final lr = lr * this")
    p.add_argument("--schedule", choices=["cosine", "step"], default="cosine")
    p.add_argument("--step-every", type=int, default=4, help="step schedule: epochs per decay")
    p.add_argument("--step-gamma", type=float, default=0.3, help="step schedule: lr multiplier")
    p.add_argument("--wd", type=float, default=0.01, help="AdamW weight decay")
    p.add_argument("--batch", type=int, default=16384)
    p.add_argument("--chunk", type=int, default=1 << 20, help="records per shuffle chunk")
    p.add_argument("--buffer-chunks", type=int, default=4, help="chunks mixed per shuffle buffer")
    p.add_argument("--out", default="source/train/checkpoints/run")
    p.add_argument("--seed", type=int, default=12345)
    p.add_argument("--resume", default=None, help="checkpoint path, or 'auto' = <out>/last.pt")
    p.add_argument("--device", default="auto", help="auto | cpu | cuda | cuda:N")
    p.add_argument("--threads", type=int, default=0, help="torch CPU threads (0 = default)")
    p.add_argument("--log-every", type=int, default=100, help="batches between progress lines")
    return p.parse_args(argv)


def set_seeds(seed: int):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def lr_at(args, step: int, total_steps: int, steps_per_epoch: int) -> float:
    if args.schedule == "cosine":
        frac = min(1.0, step / max(1, total_steps))
        lo = args.lr * args.final_lr_frac
        return lo + 0.5 * (args.lr - lo) * (1.0 + math.cos(math.pi * frac))
    epoch = step // max(1, steps_per_epoch)
    return args.lr * (args.step_gamma ** (epoch // args.step_every))


def to_device(b, device):
    nb = device.type == "cuda"
    return (torch.from_numpy(b["stm_idx"]).to(device, non_blocking=nb),
            torch.from_numpy(b["nstm_idx"]).to(device, non_blocking=nb),
            torch.from_numpy(b["offsets"]).to(device, non_blocking=nb),
            torch.from_numpy(b["target"]).to(device, non_blocking=nb))


@torch.no_grad()
def evaluate(model, loader, device) -> float:
    model.eval()
    tot, n = 0.0, 0
    for b in loader.epoch(0):
        si, ni, off, tgt = to_device(b, device)
        pred = torch.sigmoid(model(si, ni, off))
        tot += float(((pred - tgt) ** 2).sum())
        n += len(tgt)
    model.train()
    return tot / max(1, n)


def main(argv=None):
    args = parse_args(argv)
    if args.threads:
        torch.set_num_threads(args.threads)
    if args.device == "auto":
        device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    else:
        device = torch.device(args.device)
    os.makedirs(args.out, exist_ok=True)
    set_seeds(args.seed)

    store = RecordStore(args.data)
    if args.val_data:
        val_store = RecordStore(args.val_data)
        tr_lo, tr_hi = 0, len(store)
        va_lo, va_hi = 0, min(len(val_store), args.val_max)
    else:
        val_store = store
        n_val = min(int(len(store) * args.val_frac), args.val_max)
        tr_lo, tr_hi = 0, len(store) - n_val
        va_lo, va_hi = tr_hi, len(store)
    train_loader = BatchLoader(store, tr_lo, tr_hi, args.batch, args.lam, args.seed,
                               chunk=args.chunk, buffer_chunks=args.buffer_chunks)
    val_loader = BatchLoader(val_store, va_lo, va_hi, args.batch, args.lam, args.seed,
                             chunk=args.chunk, shuffle=False, drop_last=False) if va_hi > va_lo else None
    epoch_limit = args.epoch_size or None
    steps_per_epoch = train_loader.num_batches(epoch_limit)
    if steps_per_epoch == 0:
        raise SystemExit("not enough training positions for one batch")
    total_steps = steps_per_epoch * args.epochs

    model = CorvusNNUE(args.hidden).to(device)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=args.wd)
    start_epoch, step = 0, 0
    history = []

    resume = os.path.join(args.out, "last.pt") if args.resume == "auto" else args.resume
    if resume and os.path.exists(resume):
        ck = torch.load(resume, map_location=device, weights_only=False)
        if int(ck["hidden"]) != args.hidden:
            raise SystemExit(f"checkpoint hidden={ck['hidden']} != --hidden {args.hidden}")
        model.load_state_dict(ck["model"])
        opt.load_state_dict(ck["optimizer"])
        start_epoch, step = int(ck["epoch"]), int(ck["step"])
        history = ck.get("history", [])
        torch.set_rng_state(ck["torch_rng"])
        print(f"resumed from {resume}: epoch {start_epoch}, step {step}")
    elif args.resume and args.resume != "auto":
        raise SystemExit(f"--resume {args.resume} not found")

    hparams = {
        "argv": sys.argv, **{k: v for k, v in vars(args).items()},
        "device_resolved": str(device), "torch": torch.__version__,
        "train_positions": tr_hi - tr_lo, "val_positions": va_hi - va_lo,
        "steps_per_epoch": steps_per_epoch, "total_steps": total_steps,
        "data_files": store.paths, "QA": QA, "QB": QB, "SCALE": SCALE,
        "FT_CLIP": FT_CLIP, "OUT_CLIP": OUT_CLIP, "features": 768,
        "activation": "screlu", "loss": "mse(sigmoid(out), lam*sigmoid(score/400)+(1-lam)*wdl)",
    }
    with open(os.path.join(args.out, "hparams.json"), "w") as f:
        json.dump(hparams, f, indent=2)
    print(f"device={device} train={tr_hi - tr_lo} val={va_hi - va_lo} "
          f"steps/epoch={steps_per_epoch} H={args.hidden}", flush=True)

    model.train()
    for epoch in range(start_epoch, args.epochs):
        t0 = time.perf_counter()
        run_loss, run_n, seen = 0.0, 0, 0
        loss_sum = torch.zeros((), device=device)
        for i, b in enumerate(train_loader.epoch(epoch, epoch_limit)):
            for g in opt.param_groups:
                g["lr"] = lr_at(args, step, total_steps, steps_per_epoch)
            si, ni, off, tgt = to_device(b, device)
            pred = torch.sigmoid(model(si, ni, off))
            loss = torch.mean((pred - tgt) ** 2)
            opt.zero_grad(set_to_none=True)
            loss.backward()
            opt.step()
            model.clip_weights()
            loss_sum += loss.detach()
            run_n += 1
            seen += len(tgt)
            step += 1
            if args.log_every and (i + 1) % args.log_every == 0:
                el = time.perf_counter() - t0
                print(f"  epoch {epoch + 1} batch {i + 1}/{steps_per_epoch} "
                      f"loss {float(loss_sum) / run_n:.6f} lr {opt.param_groups[0]['lr']:.2e} "
                      f"{seen / el:,.0f} pos/s", flush=True)
        if device.type == "cuda":
            torch.cuda.synchronize()
        el = time.perf_counter() - t0
        train_loss = float(loss_sum) / max(1, run_n)
        val_loss = evaluate(model, val_loader, device) if val_loader else None
        rec = {"epoch": epoch + 1, "train_loss": train_loss, "val_loss": val_loss,
               "positions": seen, "seconds": el, "pos_per_s": seen / el,
               "lr_end": opt.param_groups[0]["lr"]}
        history.append(rec)
        print(f"epoch {epoch + 1}/{args.epochs} train {train_loss:.6f} "
              f"val {val_loss if val_loss is None else f'{val_loss:.6f}'} "
              f"{el:.1f}s {seen / el:,.0f} pos/s", flush=True)
        ck = {"model": model.state_dict(), "optimizer": opt.state_dict(), "epoch": epoch + 1,
              "step": step, "hidden": args.hidden, "args": vars(args), "history": history,
              "torch_rng": torch.get_rng_state()}
        path = os.path.join(args.out, f"epoch{epoch + 1:03d}.pt")
        torch.save(ck, path)
        torch.save(ck, os.path.join(args.out, "last.pt"))
        with open(os.path.join(args.out, "history.json"), "w") as f:
            json.dump(history, f, indent=2)
    print("done")


if __name__ == "__main__":
    main()
