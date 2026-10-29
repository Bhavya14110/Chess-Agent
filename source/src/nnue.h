#pragma once
#include "position.h"
#include <string>

// our own perspective net: (768 -> H) x 2 -> 1, SCReLU, trained by source/train on our self-play data.
// file format and quantisation are documented in source/train/README.md.
namespace nnue {

constexpr int QA = 255;
constexpr int QB = 64;
constexpr int SCALE = 400;
constexpr int MAXH = 1024;

struct alignas(64) Accumulator {
    int16_t v[2][MAXH];   // [perspective][hidden]
};

extern bool loaded;
extern int H;

bool loadfile(const std::string& path);
bool loadembedded();
// feature index of piece p on square sq, seen from perspective `persp`
inline int feature(int persp, int p, int sq) {
    int rel = pcolor(p) != persp;
    return (rel * 6 + ptype(p)) * 64 + (persp == WHITE ? sq : sq ^ 56);
}

void refresh(const Position& pos, Accumulator& acc);
void add(Accumulator& acc, int p, int sq);
void sub(Accumulator& acc, int p, int sq);
// child = parent with one piece moved (and optionally one removed / one changed), done in one pass
void update(const Accumulator& parent, Accumulator& child, int addp1, int addsq1, int subp1, int subsq1,
            int addp2, int addsq2, int subp2, int subsq2);
int evaluate(const Accumulator& acc, int stm);

} // namespace nnue
