#include "eval.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "evaltuned.h"

Score evp[NPARAMS];

static const int phaseinc[6] = {0, 1, 1, 2, 4, 0};

static u64 fwdfile[2][64];     // squares in front of sq on its file
static u64 passmask[2][64];    // squares an enemy pawn would need to stop a passer
static u64 adjfiles[8];
static u64 kingzone[2][64];

// hand-picked starting point for every weight; the tuner takes it from here
void setdefaultparams() {
    std::memset(evp, 0, sizeof(evp));
    const int matmg[6] = {80, 320, 330, 460, 980, 0};
    const int mateg[6] = {100, 300, 320, 530, 950, 0};
    for (int t = 0; t < 6; t++) evp[P_MAT + t] = S(matmg[t], mateg[t]);
    for (int sq = 0; sq < 64; sq++) {
        int f = filex(sq), r = rankx(sq);
        int cd = std::max(std::abs(2 * f - 7), std::abs(2 * r - 7)) / 2;
        int fc = std::abs(2 * f - 7) / 2;
        int pmg = (r >= 1 && r <= 6) ? (3 - fc) * 4 * (r >= 3 ? 2 : 1) - (r == 1 && fc == 0 ? 10 : 0) : 0;
        int peg = (r >= 1 && r <= 6) ? (r - 1) * (r - 1) * 3 : 0;
        evp[P_PSQT + PAWN * 64 + sq] = S(pmg, peg);
        evp[P_PSQT + KNIGHT * 64 + sq] = S(15 - 10 * cd, 10 - 8 * cd);
        evp[P_PSQT + BISHOP * 64 + sq] = S(8 - 5 * cd, 6 - 4 * cd);
        evp[P_PSQT + ROOK * 64 + sq] = S(fc == 0 ? 5 : 0, 0);
        evp[P_PSQT + QUEEN * 64 + sq] = S(4 - 3 * cd, 8 - 5 * cd);
        int kmg = r == 0 ? (fc >= 2 ? 15 : (fc == 0 ? -5 : 0)) : -15 * r;
        evp[P_PSQT + KING * 64 + sq] = S(kmg, 20 - 12 * cd);
    }
    for (int i = 0; i < 9; i++) evp[P_MOBN + i] = S(6 * (i - 4), 6 * (i - 4));
    for (int i = 0; i < 14; i++) evp[P_MOBB + i] = S(5 * (i - 6), 5 * (i - 6));
    for (int i = 0; i < 15; i++) evp[P_MOBR + i] = S(3 * (i - 7), 6 * (i - 7));
    for (int i = 0; i < 28; i++) evp[P_MOBQ + i] = S(2 * (i - 13), 3 * (i - 13));
    evp[P_DOUBLED] = S(-10, -20);
    evp[P_ISOLATED] = S(-10, -12);
    evp[P_BACKWARD] = S(-8, -8);
    for (int r = 1; r < 7; r++) {
        evp[P_PHALANX + r] = S(3 * r, 3 * r);
        evp[P_SUPPORTED + r] = S(4 * r, 3 * r);
        evp[P_PASSED + r] = S(5 * r, 10 * r * r / 2 + 10);
        evp[P_PASSFREE + r] = S(0, 4 * r * r / 2);
        evp[P_PASSKOWN + r] = S(0, -2 * r);
        evp[P_PASSKTHEM + r] = S(0, 4 * r);
    }
    evp[P_BPAIR] = S(30, 50);
    evp[P_ROOKOPEN] = S(25, 10);
    evp[P_ROOKSEMI] = S(12, 6);
    evp[P_KNOUTPOST] = S(25, 15);
    evp[P_BOUTPOST] = S(15, 8);
    evp[P_MINORBEHINDPAWN] = S(8, 2);
    evp[P_SHIELD1] = S(12, 0);
    evp[P_SHIELD2] = S(6, 0);
    evp[P_SHIELDMISSING] = S(-15, 0);
    evp[P_KINGOPEN] = S(-20, 0);
    const int katt[6] = {0, 8, 6, 10, 14, 0};
    for (int t = 0; t < 6; t++) evp[P_KATT + t] = S(katt[t], 0);
    for (int n = 0; n < 8; n++) evp[P_KATTN + n] = S(n <= 1 ? 0 : 10 * n * n / 2, 0);
    const int chk[6] = {0, 30, 20, 30, 35, 0};
    for (int t = 0; t < 6; t++) evp[P_SAFECHECK + t] = S(chk[t], chk[t] / 3);
    evp[P_THRPAWN] = S(50, 40);
    for (int t = 0; t < 6; t++) {
        evp[P_THRMINOR + t] = S(t >= ROOK ? 40 : t == PAWN ? 5 : 20, t >= ROOK ? 30 : 15);
        evp[P_THRROOK + t] = S(t == QUEEN ? 40 : t == PAWN ? 5 : 15, 20);
    }
    evp[P_HANGING] = S(30, 20);
    evp[P_TEMPO] = S(15, 5);
}

void evalinit() {
    if (TUNED_OK) {
        for (int i = 0; i < NPARAMS; i++) evp[i] = S(tunedmg[i], tunedeg[i]);
    } else {
        setdefaultparams();
    }
    for (int f = 0; f < 8; f++) adjfiles[f] = (f > 0 ? filebb(f - 1) : 0) | (f < 7 ? filebb(f + 1) : 0);
    for (int sq = 0; sq < 64; sq++) {
        for (int c = 0; c < 2; c++) {
            u64 fwd = 0;
            for (int r = rankx(sq) + (c == WHITE ? 1 : -1); r >= 0 && r < 8; r += (c == WHITE ? 1 : -1))
                fwd |= sqbb(mksq(filex(sq), r));
            fwdfile[c][sq] = fwd;
            u64 span = fwd;
            if (filex(sq) > 0) span |= c == WHITE ? fwd >> 1 : fwd >> 1;
            if (filex(sq) < 7) span |= c == WHITE ? fwd << 1 : fwd << 1;
            passmask[c][sq] = span;
            // the squares around the king plus one more rank toward the enemy
            u64 z = bb::kingatt[sq] | sqbb(sq);
            z |= c == WHITE ? shiftn(z) : shifts(z);
            kingzone[c][sq] = z;
        }
    }
}

namespace {

template <bool TRACE>
struct Evaluator {
    const Position& pos;
    EvalTrace* tr;
    Score sc = 0;   // white's point of view
    u64 att[2][7] = {};   // [colour][type], index 6 = all
    u64 att2[2] = {};     // squares attacked at least twice
    u64 mobarea[2];
    int kattcount[2] = {};
    int kattn[2] = {};

    Evaluator(const Position& p, EvalTrace* t) : pos(p), tr(t) {}

    void add(int c, int idx, int n = 1) {
        sc += (c == WHITE ? evp[idx] : -evp[idx]) * n;
        if constexpr (TRACE) tr->coef[idx][c] += int16_t(n);
    }

    void addatt(int c, int t, u64 a) {
        att2[c] |= att[c][6] & a;
        att[c][t] |= a;
        att[c][6] |= a;
    }

    void pawns(int c) {
        int them = c ^ 1;
        u64 mine = pos.bbof(c, PAWN), theirs = pos.bbof(them, PAWN);
        u64 b = mine;
        while (b) {
            int sq = poplsb(b);
            int r = relrank(c, sq), f = filex(sq);
            int stop = c == WHITE ? sq + 8 : sq - 8;
            bool supported = bb::pawnatt[them][sq] & mine;
            bool phalanx = mine & adjfiles[f] & rankbb(rankx(sq));
            if (fwdfile[c][sq] & mine) add(c, P_DOUBLED);
            if (!(adjfiles[f] & mine)) add(c, P_ISOLATED);
            else if (!supported && !phalanx && !(passmask[them][stop] & mine & ~fwdfile[them][stop] & adjfiles[f]) &&
                     (bb::pawnatt[c][stop] & theirs))
                add(c, P_BACKWARD);
            if (phalanx) add(c, P_PHALANX + r);
            if (supported) add(c, P_SUPPORTED + r);
            if (!(passmask[c][sq] & theirs) && !(fwdfile[c][sq] & mine)) {
                add(c, P_PASSED + r);
                if (!(pos.all & sqbb(stop)) && !(att[them][6] & sqbb(stop))) add(c, P_PASSFREE + r);
                add(c, P_PASSKOWN + r, bb::dist[pos.kingsq(c)][stop]);
                add(c, P_PASSKTHEM + r, bb::dist[pos.kingsq(them)][stop]);
            }
        }
    }

    void pieces(int c) {
        int them = c ^ 1;
        u64 mine = pos.bbof(c, PAWN), theirs = pos.bbof(them, PAWN);
        u64 zone = kingzone[them][pos.kingsq(them)];
        for (int t = KNIGHT; t <= QUEEN; t++) {
            u64 b = pos.bbof(c, t);
            while (b) {
                int sq = poplsb(b);
                u64 a = bb::attacks(t, sq, pos.all);
                addatt(c, t, a);
                int mob = popcnt(a & mobarea[c]);
                add(c, (t == KNIGHT ? P_MOBN : t == BISHOP ? P_MOBB : t == ROOK ? P_MOBR : P_MOBQ) + mob);
                if (a & zone) {
                    kattn[c]++;
                    add(c, P_KATT + t, popcnt(a & zone));
                }
                if (t == KNIGHT || t == BISHOP) {
                    int r = relrank(c, sq);
                    bool safe = !(passmask[c][sq] & ~fwdfile[c][sq] & theirs);
                    if (r >= 3 && r <= 5 && safe && (bb::pawnatt[them][sq] & mine))
                        add(c, t == KNIGHT ? P_KNOUTPOST : P_BOUTPOST);
                    int behind = c == WHITE ? sq + 8 : sq - 8;
                    if (sqbb(behind) & (mine | theirs)) add(c, P_MINORBEHINDPAWN);
                }
                if (t == ROOK) {
                    u64 fb = filebb(filex(sq));
                    if (!(fb & (mine | theirs))) add(c, P_ROOKOPEN);
                    else if (!(fb & mine)) add(c, P_ROOKSEMI);
                }
            }
        }
        if (popcnt(pos.bbof(c, BISHOP)) >= 2) add(c, P_BPAIR);
    }

    void king(int c) {
        int them = c ^ 1;
        int ksq = pos.kingsq(c);
        u64 mine = pos.bbof(c, PAWN), theirs = pos.bbof(them, PAWN);
        int kf = std::clamp(filex(ksq), 1, 6);
        for (int f = kf - 1; f <= kf + 1; f++) {
            u64 fb = filebb(f);
            u64 front = fb & (c == WHITE ? ~0ULL << (8 * rankx(ksq)) : ~0ULL >> (8 * (7 - rankx(ksq))));
            u64 own = front & mine;
            if (!own) {
                add(c, P_SHIELDMISSING);
                if (!(fb & theirs)) add(c, P_KINGOPEN);
                continue;
            }
            int sq = c == WHITE ? lsb(own) : msb(own);
            int d = std::abs(rankx(sq) - rankx(ksq));
            if (d == 1) add(c, P_SHIELD1);
            else if (d == 2) add(c, P_SHIELD2);
        }
        // the enemy's attack on our king
        add(them, P_KATTN + std::min(kattn[them], 7));
        // safe checks: squares they can check from that we don't cover
        u64 safe = ~att[c][6] & ~pos.occ[them];
        u64 o = pos.all;
        u64 nchk = bb::knightatt[ksq] & att[them][KNIGHT] & safe;
        u64 bchk = bb::batt(ksq, o) & att[them][BISHOP] & safe;
        u64 rchk = bb::ratt(ksq, o) & att[them][ROOK] & safe;
        u64 qchk = bb::qatt(ksq, o) & att[them][QUEEN] & safe & ~att2[c];
        if (nchk) add(them, P_SAFECHECK + KNIGHT, popcnt(nchk));
        if (bchk) add(them, P_SAFECHECK + BISHOP, popcnt(bchk));
        if (rchk) add(them, P_SAFECHECK + ROOK, popcnt(rchk));
        if (qchk) add(them, P_SAFECHECK + QUEEN, popcnt(qchk));
    }

    void threats(int c) {
        int them = c ^ 1;
        u64 targets = pos.occ[them] & ~pos.bbof(them, KING);
        u64 nonpawn = targets & ~pos.bbof(them, PAWN);
        if (u64 b = att[c][PAWN] & nonpawn) add(c, P_THRPAWN, popcnt(b));
        u64 bm = (att[c][KNIGHT] | att[c][BISHOP]) & targets;
        while (bm) add(c, P_THRMINOR + ptype(pos.board[poplsb(bm)]));
        u64 br = att[c][ROOK] & targets;
        while (br) add(c, P_THRROOK + ptype(pos.board[poplsb(br)]));
        if (u64 h = att[c][6] & targets & ~att[them][6]) add(c, P_HANGING, popcnt(h));
    }

    // 0..128, how much of the endgame score to believe
    int scale(int egscore) {
        int strong = egscore > 0 ? WHITE : BLACK;
        int weak = strong ^ 1;
        u64 sp = pos.bbof(strong, PAWN);
        int nonpawn[2];
        for (int c = 0; c < 2; c++)
            nonpawn[c] = popcnt(pos.bbof(c, KNIGHT) | pos.bbof(c, BISHOP)) * 3 + popcnt(pos.bbof(c, ROOK)) * 5 +
                         popcnt(pos.bbof(c, QUEEN)) * 9;
        // no pawns and at most a minor piece up: very hard to win
        if (!sp && nonpawn[strong] - nonpawn[weak] <= 3) return nonpawn[strong] <= 3 ? 0 : 16;
        // opposite coloured bishops, nothing else
        constexpr u64 dark = 0xAA55AA55AA55AA55ULL;
        u64 wb = pos.bbof(WHITE, BISHOP), bbb = pos.bbof(BLACK, BISHOP);
        if (nonpawn[WHITE] == 3 && nonpawn[BLACK] == 3 && popcnt(wb) == 1 && popcnt(bbb) == 1 &&
            bool(wb & dark) != bool(bbb & dark))
            return 40 + 8 * std::max(0, popcnt(sp) - popcnt(pos.bbof(weak, PAWN)));
        return 128;
    }

    // drive a lone king to the edge (and the right corner for KBN) in won endings. not tuned.
    int mopup() {
        for (int strong = 0; strong < 2; strong++) {
            int weak = strong ^ 1;
            if (pos.occ[weak] != pos.bbof(weak, KING)) continue;
            if (pos.occ[strong] == pos.bbof(strong, KING)) return 0;
            int wk = pos.kingsq(weak), sk = pos.kingsq(strong);
            int cd = std::max(std::abs(2 * filex(wk) - 7), std::abs(2 * rankx(wk) - 7)) / 2;
            int v = 40 * cd + 10 * (7 - bb::dist[wk][sk]);
            u64 bish = pos.bbof(strong, BISHOP);
            if (popcnt(pos.occ[strong]) == 3 && pos.bbof(strong, KNIGHT) && bish) {
                // KBN: the corner the bishop can cover
                constexpr u64 dark = 0xAA55AA55AA55AA55ULL;
                bool darkb = bish & dark;
                int c1 = darkb ? 0 : 7, c2 = darkb ? 63 : 56;   // a1/h8 are dark
                v += 60 * (7 - std::min(bb::dist[wk][c1], bb::dist[wk][c2]));
            }
            return strong == WHITE ? v : -v;
        }
        return 0;
    }

    int run() {
        for (int c = 0; c < 2; c++) {
            u64 p = pos.bbof(c, PAWN);
            u64 pa = pawnattbb(c, p);
            addatt(c, PAWN, pa);
            addatt(c, KING, bb::kingatt[pos.kingsq(c)]);
        }
        for (int c = 0; c < 2; c++) {
            u64 lowrank = rankbb(c == WHITE ? 1 : 6) | rankbb(c == WHITE ? 2 : 5);
            u64 blocked = pos.bbof(c, PAWN) & (lowrank | pushup(c ^ 1, pos.all));
            mobarea[c] = ~(blocked | pos.bbof(c, KING) | att[c ^ 1][PAWN]);
        }
        int phase = 0;
        for (int c = 0; c < 2; c++)
            for (int t = 0; t < 6; t++) {
                u64 b = pos.bbof(c, t);
                phase += phaseinc[t] * popcnt(b);
                if (t != KING) add(c, P_MAT + t, popcnt(b));
                while (b) {
                    int sq = poplsb(b);
                    add(c, P_PSQT + t * 64 + (c == WHITE ? sq : flipsq(sq)));
                }
            }
        pieces(WHITE);
        pieces(BLACK);
        pawns(WHITE);
        pawns(BLACK);
        king(WHITE);
        king(BLACK);
        threats(WHITE);
        threats(BLACK);
        add(pos.stm, P_TEMPO);

        phase = std::min(phase, 24);
        int mg = mgof(sc), eg = egof(sc);
        int extra = mopup();
        eg += extra;
        int sf = scale(eg);
        if constexpr (TRACE) {
            tr->phase = phase;
            tr->scale = sf;
            tr->egextra = extra;
        }
        return (mg * phase + eg * (24 - phase) * sf / 128) / 24;
    }
};

} // namespace

int evaluate(const Position& pos) {
    Evaluator<false> e(pos, nullptr);
    int v = e.run();
    return pos.stm == WHITE ? v : -v;
}

int evaluatetrace(const Position& pos, EvalTrace& tr) {
    std::memset(&tr, 0, sizeof(tr));
    Evaluator<true> e(pos, &tr);
    return e.run();
}

void writeparams(const char* path) {
    FILE* f = std::fopen(path, "w");
    if (!f) return;
    std::fprintf(f, "#pragma once\n// generated by `corvus tune` (texel tuning on human games). do not edit by hand.\n");
    std::fprintf(f, "constexpr bool TUNED_OK = true;\nstatic const int tunedmg[] = {");
    // commas at line ends too, or the rows glue together
    auto sep = [](int i) { return i == 0 ? "\n    " : i % 16 ? ", " : ",\n    "; };
    for (int i = 0; i < NPARAMS; i++) std::fprintf(f, "%s%d", sep(i), mgof(evp[i]));
    std::fprintf(f, "\n};\nstatic const int tunedeg[] = {");
    for (int i = 0; i < NPARAMS; i++) std::fprintf(f, "%s%d", sep(i), egof(evp[i]));
    std::fprintf(f, "\n};\nstatic_assert(sizeof(tunedmg) / sizeof(int) == %d, \"param count changed, re-tune\");\n", NPARAMS);
    std::fclose(f);
}
