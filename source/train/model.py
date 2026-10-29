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

NUM_FEATURES = 768
SCALE = 400.0
QA = 255
QB = 64
FT_CLIP = 3.5
OUT_CLIP = 127.0 / QB


def screlu(x: torch.Tensor) -> torch.Tensor:
    return torch.clamp(x, 0.0, 1.0).square()


class CorvusNNUE(nn.Module):
    def __init__(self, hidden: int = 256):
        super().__init__()
        self.hidden = hidden
        self.ft = nn.EmbeddingBag(NUM_FEATURES, hidden, mode="sum")
        self.ft_bias = nn.Parameter(torch.zeros(hidden))
        self.out = nn.Linear(2 * hidden, 1)
        with torch.no_grad():
            # ~32 active features: keep initial accumulators mostly inside (0, 1)
            self.ft.weight.uniform_(-0.1, 0.1)
            self.ft_bias.uniform_(0.0, 0.2)
            bound = 1.0 / (2 * hidden) ** 0.5
            self.out.weight.uniform_(-bound, bound)
            self.out.bias.zero_()

    def forward(self, stm_idx, nstm_idx, offsets):
        a = self.ft(stm_idx, offsets) + self.ft_bias
        b = self.ft(nstm_idx, offsets) + self.ft_bias
        x = torch.cat([screlu(a), screlu(b)], dim=1)
        return self.out(x).squeeze(1)  # net_out; eval_cp = net_out * SCALE

    @torch.no_grad()
    def clip_weights(self):
        self.ft.weight.clamp_(-FT_CLIP, FT_CLIP)
        self.ft_bias.clamp_(-FT_CLIP, FT_CLIP)
        self.out.weight.clamp_(-OUT_CLIP, OUT_CLIP)

    def float_params(self):
        """numpy copies: ft_w [768,H], ft_b [H], out_w [2H], out_b scalar."""
        return (self.ft.weight.detach().cpu().double().numpy(),
                self.ft_bias.detach().cpu().double().numpy(),
                self.out.weight.detach().cpu().double().numpy().reshape(-1),
                float(self.out.bias.detach().cpu().double().item()))


def load_model_from_checkpoint(path: str, map_location="cpu") -> CorvusNNUE:
    ck = torch.load(path, map_location=map_location, weights_only=False)
    model = CorvusNNUE(int(ck["hidden"]))
    model.load_state_dict(ck["model"])
    return model
