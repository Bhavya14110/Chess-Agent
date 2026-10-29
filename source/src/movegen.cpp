#include "movegen.h"
#include <chrono>
#include <cstdio>

static void addpromos(MoveList& ml, int from, int to, bool cap) {
    int base = cap ? MPNC : MPN;
    ml.add(mkmove(from, to, base + 3));   // queen first, it's what we want 99% of the time
    ml.add(mkmove(from, to, base + 0));
    ml.add(mkmove(from, to, base + 1));
    ml.add(mkmove(from, to, base + 2));
}

template <bool NOISY>
static void genpawns(const Position& pos, MoveList& ml) {
    int us = pos.stm, them = us ^ 1;
    u64 pawns = pos.bbof(us, PAWN);
    u64 empty = ~pos.all;
    u64 enemy = pos.occ[them];
    u64 last = rankbb(us == WHITE ? 7 : 0);
    u64 third = rankbb(us == WHITE ? 2 : 5);
    int up = us == WHITE ? 8 : -8;

    u64 single = pushup(us, pawns) & empty;
    if (NOISY) {
        u64 promo = single & last;
        while (promo) {
            int to = poplsb(promo);
            addpromos(ml, to - up, to, false);
        }
        // captures to the east / west (from white's point of view)
        u64 ce = (us == WHITE ? shiftn(shifte(pawns)) : shifts(shifte(pawns))) & enemy;
        u64 cw = (us == WHITE ? shiftn(shiftw(pawns)) : shifts(shiftw(pawns))) & enemy;
        int de = up + 1, dw = up - 1;
        while (ce) {
            int to = poplsb(ce);
            if (sqbb(to) & last) addpromos(ml, to - de, to, true);
            else ml.add(mkmove(to - de, to, MCAP));
        }
        while (cw) {
            int to = poplsb(cw);
            if (sqbb(to) & last) addpromos(ml, to - dw, to, true);
            else ml.add(mkmove(to - dw, to, MCAP));
        }
        int ep = pos.cur().ep;
        if (ep != NOSQ) {
            u64 att = bb::pawnatt[them][ep] & pawns;
            while (att) ml.add(mkmove(poplsb(att), ep, MEP));
        }
    } else {
        u64 dbl = pushup(us, single & third) & empty;
        u64 s = single & ~last;
        while (s) {
            int to = poplsb(s);
            ml.add(mkmove(to - up, to, MQUIET));
        }
        while (dbl) {
            int to = poplsb(dbl);
            ml.add(mkmove(to - 2 * up, to, MDOUBLE));
        }
    }
}

template <bool NOISY>
static void genpieces(const Position& pos, MoveList& ml) {
    int us = pos.stm;
    u64 target = NOISY ? pos.occ[us ^ 1] : ~pos.all;
    int flag = NOISY ? MCAP : MQUIET;
    for (int t = KNIGHT; t <= KING; t++) {
        u64 b = pos.bbof(us, t);
        while (b) {
            int from = poplsb(b);
            u64 a = bb::attacks(t, from, pos.all) & target;
            while (a) ml.add(mkmove(from, poplsb(a), flag));
        }
    }
    if (!NOISY) {
        if (us == WHITE) {
            if (pos.cancastle(WK)) ml.add(mkmove(4, 6, MOO));
            if (pos.cancastle(WQ)) ml.add(mkmove(4, 2, MOOO));
        } else {
            if (pos.cancastle(BK)) ml.add(mkmove(60, 62, MOO));
            if (pos.cancastle(BQ)) ml.add(mkmove(60, 58, MOOO));
        }
    }
}

void gennoisy(const Position& pos, MoveList& ml) {
    genpawns<true>(pos, ml);
    genpieces<true>(pos, ml);
}

void genquiet(const Position& pos, MoveList& ml) {
    genpawns<false>(pos, ml);
    genpieces<false>(pos, ml);
}

void genall(const Position& pos, MoveList& ml) {
    gennoisy(pos, ml);
    genquiet(pos, ml);
}

void genlegal(const Position& pos, MoveList& ml) {
    MoveList tmp;
    genall(pos, tmp);
    ml.n = 0;
    for (int i = 0; i < tmp.n; i++)
        if (pos.legal(tmp.mv[i])) ml.add(tmp.mv[i]);
}

u64 perft(Position& pos, int depth) {
    MoveList ml;
    genlegal(pos, ml);
    if (depth == 1) return ml.n;
    u64 n = 0;
    for (int i = 0; i < ml.n; i++) {
        pos.make(ml.mv[i]);
        n += perft(pos, depth - 1);
        pos.unmake();
    }
    return n;
}

void divide(Position& pos, int depth) {
    auto t0 = std::chrono::steady_clock::now();
    MoveList ml;
    genlegal(pos, ml);
    u64 total = 0;
    for (int i = 0; i < ml.n; i++) {
        pos.make(ml.mv[i]);
        u64 n = depth > 1 ? perft(pos, depth - 1) : 1;
        pos.unmake();
        total += n;
        std::printf("%s: %llu\n", movestr(ml.mv[i]).c_str(), (unsigned long long)n);
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\nNodes searched: %llu\ntime %.3fs  nps %.0f\n", (unsigned long long)total, secs,
                secs > 0 ? total / secs : 0.0);
    std::fflush(stdout);
}
