#include "eval.h"
#include <cstdlib>

// phase-2 baseline: material + piece-square tables, values picked by hand.
// the full hand-crafted eval and its tuned weights replace this in phase 4.

static const int matmg[6] = {82, 330, 350, 470, 1000, 0};
static const int mateg[6] = {100, 300, 320, 530, 950, 0};
static const int phaseinc[6] = {0, 1, 1, 2, 4, 0};

static Score psq[12][64];

void evalinit() {
    for (int sq = 0; sq < 64; sq++) {
        int f = filex(sq), r = rankx(sq);
        // 0 in the middle four squares, 3 on the rim
        int cd = std::max(std::abs(2 * f - 7), std::abs(2 * r - 7)) / 2;
        int fc = std::abs(2 * f - 7) / 2;   // file distance from centre
        Score t[6];
        // pawns: push the centre ones, reward advancing more in the endgame
        int pmg = (r >= 1 && r <= 6) ? (3 - fc) * 4 * (r >= 3 ? 2 : 1) - (r == 1 && fc == 0 ? 10 : 0) : 0;
        int peg = (r >= 1 && r <= 6) ? (r - 1) * (r - 1) * 3 : 0;
        t[PAWN] = S(pmg, peg);
        t[KNIGHT] = S(15 - 10 * cd, 10 - 8 * cd);
        t[BISHOP] = S(8 - 5 * cd, 6 - 4 * cd);
        t[ROOK] = S((r == 6 ? 15 : 0) + (fc == 0 ? 5 : 0), r == 6 ? 10 : 0);
        t[QUEEN] = S(4 - 3 * cd, 8 - 5 * cd);
        // king hides on the back rank early, walks to the centre late
        int kmg = r == 0 ? (fc >= 2 ? 15 : (fc == 0 ? -5 : 0)) : -15 * r;
        t[KING] = S(kmg, 20 - 12 * cd);
        for (int p = 0; p < 6; p++) {
            Score mat = S(matmg[p], mateg[p]);
            psq[mkpiece(WHITE, p)][sq] = mat + t[p];
            psq[mkpiece(BLACK, p)][flipsq(sq)] = -(mat + t[p]);
        }
    }
}

int evaluate(const Position& pos) {
    Score s = 0;
    int phase = 0;
    for (int p = 0; p < 12; p++) {
        u64 b = pos.pcs[p];
        phase += phaseinc[ptype(p)] * popcnt(b);
        while (b) s += psq[p][poplsb(b)];
    }
    if (phase > 24) phase = 24;
    int v = (mgof(s) * phase + egof(s) * (24 - phase)) / 24;
    return (pos.stm == WHITE ? v : -v) + 10;   // small tempo bonus
}
