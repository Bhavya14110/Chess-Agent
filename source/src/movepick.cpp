#include "movepick.h"
#include <cstring>

void Histories::clear() {
    std::memset(butterfly, 0, sizeof(butterfly));
    std::memset(counter, 0, sizeof(counter));
    std::memset(cont, 0, sizeof(cont));
}

Move MovePicker::pick(MoveList& ml, int& i) {
    if (i >= ml.n) return NOMOVE;
    int b = i;
    for (int j = i + 1; j < ml.n; j++)
        if (ml.sc[j] > ml.sc[b]) b = j;
    std::swap(ml.mv[i], ml.mv[b]);
    std::swap(ml.sc[i], ml.sc[b]);
    return ml.mv[i++];
}

void MovePicker::scorenoisy() {
    for (int i = 0; i < noisy.n; i++) {
        Move m = noisy.mv[i];
        int victim = mflag(m) == MEP ? PAWN : pos.board[mto(m)] == NOPIECE ? NOTYPE : ptype(pos.board[mto(m)]);
        int v = victim == NOTYPE ? 0 : seevalue[victim];
        if (ispromo(m)) v += promotype(m) == QUEEN ? seevalue[QUEEN] : -2000;
        noisy.sc[i] = v * 8 - ptype(pos.board[mfrom(m)]);
    }
}

void MovePicker::scorequiet() {
    for (int i = 0; i < quiet.n; i++) {
        Move m = quiet.mv[i];
        int pc = pos.board[mfrom(m)] * 64 + mto(m);
        quiet.sc[i] = hist.butterfly[pos.stm][m & 4095] + (ch1 ? ch1[pc] : 0) + (ch2 ? ch2[pc] : 0);
    }
}

Move MovePicker::next(bool skipquiets) {
    while (true) {
        switch (stage) {
        case TTMOVE:
            stage = GENNOISY;
            return ttmove;

        case GENNOISY:
            gennoisy(pos, noisy);
            scorenoisy();
            stage = GOODNOISY;
            [[fallthrough]];

        case GOODNOISY: {
            Move m;
            while ((m = pick(noisy, ni)) != NOMOVE) {
                if (m == ttmove) continue;
                // losing captures wait until after the quiets (in qsearch the caller decides)
                if (!qmode && !pos.see(m, 0)) {
                    bad.mv[bad.n++] = m;
                    continue;
                }
                return m;
            }
            if (qmode) {
                stage = DONE;
                return NOMOVE;
            }
            stage = KILLER1;
            [[fallthrough]];
        }

        case KILLER1:
            stage = KILLER2;
            if (!skipquiets && killer1 != ttmove && !isnoisy(killer1) && pos.pseudolegal(killer1)) return killer1;
            [[fallthrough]];

        case KILLER2:
            stage = COUNTER;
            if (!skipquiets && killer2 != ttmove && killer2 != killer1 && !isnoisy(killer2) && pos.pseudolegal(killer2))
                return killer2;
            [[fallthrough]];

        case COUNTER:
            stage = GENQUIET;
            if (!skipquiets && counter != ttmove && counter != killer1 && counter != killer2 && !isnoisy(counter) &&
                pos.pseudolegal(counter))
                return counter;
            [[fallthrough]];

        case GENQUIET:
            if (!skipquiets) {
                genquiet(pos, quiet);
                scorequiet();
            }
            stage = QUIET;
            [[fallthrough]];

        case QUIET:
            if (!skipquiets) {
                Move m;
                while ((m = pick(quiet, qi)) != NOMOVE)
                    if (!special(m)) return m;
            }
            stage = BADNOISY;
            [[fallthrough]];

        case BADNOISY:
            while (bi < bad.n) {
                Move m = bad.mv[bi++];
                if (m != ttmove) return m;
            }
            stage = DONE;
            [[fallthrough]];

        case DONE:
            return NOMOVE;
        }
    }
}
