#pragma once
#include "movegen.h"
#include <cstdlib>

constexpr int HISTMAX = 16384;

// history-style tables keep entries inside +-HISTMAX ("gravity" update)
inline void histupdate(int16_t& h, int bonus) {
    h += int16_t(bonus - h * std::abs(bonus) / HISTMAX);
}

using ContHist = int16_t[12 * 64];   // indexed by piece * 64 + to

struct Histories {
    int16_t butterfly[2][64 * 64];
    Move counter[12][64];
    // continuation history: [previous piece][previous to][piece * 64 + to].
    // piece 12 is a dummy row used after null moves / at the root, never updated
    ContHist cont[13][64];
    void clear();
};

class MovePicker {
public:
    // main search
    MovePicker(const Position& p, const Histories& h, Move tt, Move k1, Move k2, Move cm,
               const int16_t* c1, const int16_t* c2)
        : pos(p), hist(h), ttmove(tt), killer1(k1), killer2(k2), counter(cm), ch1(c1), ch2(c2) {
        stage = pos.pseudolegal(tt) ? TTMOVE : GENNOISY;
    }
    // qsearch: noisy moves only unless in check
    MovePicker(const Position& p, const Histories& h, Move tt)
        : pos(p), hist(h), ttmove(tt), killer1(NOMOVE), killer2(NOMOVE), counter(NOMOVE) {
        qmode = !pos.incheck();
        bool ok = pos.pseudolegal(tt) && (!qmode || isnoisy(tt));
        stage = ok ? TTMOVE : GENNOISY;
    }

    Move next(bool skipquiets = false);
    int stagenow() const { return stage; }

    enum { TTMOVE, GENNOISY, GOODNOISY, KILLER1, KILLER2, COUNTER, GENQUIET, QUIET, BADNOISY, DONE };

private:
    const Position& pos;
    const Histories& hist;
    Move ttmove, killer1, killer2, counter;
    const int16_t* ch1 = nullptr;
    const int16_t* ch2 = nullptr;
    bool qmode = false;
    int stage;
    MoveList noisy, quiet;
    int ni = 0, qi = 0;
    MoveList bad;
    int bi = 0;

    bool special(Move m) const { return m == ttmove || m == killer1 || m == killer2 || m == counter; }
    static Move pick(MoveList& ml, int& i);
    void scorenoisy();
    void scorequiet();
};
