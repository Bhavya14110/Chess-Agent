#pragma once
#include "position.h"

// packed middlegame/endgame score, eg in the high 16 bits
using Score = int32_t;
constexpr Score S(int mg, int eg) { return Score(u32(eg) << 16) + mg; }
inline int mgof(Score s) { return int16_t(u16(u32(s))); }
inline int egof(Score s) { return int16_t(u16(u32(s + 0x8000) >> 16)); }

void evalinit();
// static eval from the side to move's point of view, in centipawns
int evaluate(const Position& pos);
