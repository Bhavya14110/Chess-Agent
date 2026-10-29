#pragma once
#include "movegen.h"
#include <cstdlib>

constexpr int HISTMAX = 16384;

// history-style tables keep entries inside +-HISTMAX ("gravity" update)
inline void histupdate(int16_t& h, int bonus) {
    h += int16_t(bonus - h * std::abs(bonus) / HISTMAX);
}

struct Histories {
    int16_t butterfly[2][64 * 64];
    Move counter[12][64];
    void clear();
};

class MovePicker {
public:
    // main search
    MovePicker(const Position& p, const Histories& h, Move tt, Move k1, Move k2, Move cm)
        : pos(p), hist(h), ttmove(tt), killer1(k1), killer2(k2), counter(cm) {
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
