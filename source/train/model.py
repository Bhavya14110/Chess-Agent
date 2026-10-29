"""Corvus NNUE model: (768 -> H) x 2 perspectives, SCReLU, (2H -> 1). Written from scratch.

eval_cp = net_out * SCALE (SCALE = 400), from the side to move's point of view.

Quantisation constants and weight clipping (must match export.py and the C++ inference):
  QA = 255, QB = 64
  Feature transformer weights AND bias are clipped to +-FT_CLIP = 3.5
      -> |q| <= round(3.5*255) = 892.  The accumulator is bias + up to 32 rows, so
         |acc| <= 33*892 = 29436 < 32767; even with 2 extra transient rows during an
         incremental update (35*892 = 31220) int16 never overflows.
  Output weights are clipped to +-OUT_CLIP = 127/64 (= 1.984375)
      -> |q| <= 127, so clamp(acc,0,255) * w_q fits in int16 (255*127 = 32385), which
         allows the C++ side to multiply v*w in int16 before widening (v*(v*w)).
  Output bias is not clipped (stored as int32, scale QA*QB).
"""
from __future__ import annotations

import torch
import torch.nn as nn
import torch.nn.functional as F

NUM_FEATURES = 768
SCALE = 400.0
QA = 255
QB = 64
FT_CLIP = 3.5
OUT_CLIP = 127.0 / QB


def screlu(x: torch.Tensor) -> torch.Tensor:
    return torch.clamp(x, 0.0, 1.0).square()


def fake_quant(w: torch.Tensor, scale: float) -> torch.Tensor:
    """Round onto the 1/scale grid in the forward pass, identity gradient (straight-through)."""
    return w + (torch.round(w * scale) / scale - w).detach()


class CorvusNNUE(nn.Module):
    def __init__(self, hidden: int = 256):
        super().__init__()
        self.hidden = hidden
        self.ft = nn.EmbeddingBag(NUM_FEATURES, hidden, mode="sum")
        self.ft_bias = nn.Parameter(torch.zeros(hidden))
        self.out = nn.Linear(2 * hidden, 1)
        # quantisation-aware training: when True the forward pass uses weights rounded onto
        # the export grid (ft: 1/QA, out_w: 1/QB, out_b: 1/(QA*QB)), so the float model
        # equals the exported integer net up to the final truncating divisions.
        self.qat = False
        with torch.no_grad():
            # ~32 active features: keep initial accumulators mostly inside (0, 1)
            self.ft.weight.uniform_(-0.1, 0.1)
            self.ft_bias.uniform_(0.0, 0.2)
            bound = 1.0 / (2 * hidden) ** 0.5
            self.out.weight.uniform_(-bound, bound)
            self.out.bias.zero_()

    def effective_weights(self):
        fw, fb = self.ft.weight, self.ft_bias
        ow, ob = self.out.weight, self.out.bias
        if self.qat:
            fw, fb = fake_quant(fw, QA), fake_quant(fb, QA)
            ow, ob = fake_quant(ow, QB), fake_quant(ob, QA * QB)
        return fw, fb, ow, ob

    def forward(self, stm_idx, nstm_idx, offsets):
        fw, fb, ow, ob = self.effective_weights()
        a = F.embedding_bag(stm_idx, fw, offsets, mode="sum") + fb
        b = F.embedding_bag(nstm_idx, fw, offsets, mode="sum") + fb
        x = torch.cat([screlu(a), screlu(b)], dim=1)
        return F.linear(x, ow, ob).squeeze(1)  # net_out; eval_cp = net_out * SCALE

    @torch.no_grad()
    def clip_weights(self):
        self.ft.weight.clamp_(-FT_CLIP, FT_CLIP)
        self.ft_bias.clamp_(-FT_CLIP, FT_CLIP)
        self.out.weight.clamp_(-OUT_CLIP, OUT_CLIP)

    def float_params(self):
        """numpy copies of the weights the forward pass uses (fake-quantised if self.qat):
        ft_w [768,H], ft_b [H], out_w [2H], out_b scalar."""
        with torch.no_grad():
            fw, fb, ow, ob = (t.detach().cpu().double() for t in self.effective_weights())
        return fw.numpy(), fb.numpy(), ow.numpy().reshape(-1), float(ob.item())


def load_model_from_checkpoint(path: str, map_location="cpu") -> CorvusNNUE:
    ck = torch.load(path, map_location=map_location, weights_only=False)
    model = CorvusNNUE(int(ck["hidden"]))
    model.load_state_dict(ck["model"])
    model.qat = bool(ck.get("qat", False))
    return model
