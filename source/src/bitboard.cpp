#include "bitboard.h"
#include <cstdlib>

namespace bb {

u64 pawnatt[2][64];
u64 knightatt[64];
u64 kingatt[64];
u64 between[64][64];
u64 line[64][64];
int dist[64][64];
Magic rmag[64], bmag[64];

static u64 rtable[0x19000];   // 102400 rook entries
static u64 btable[0x1480];    // 5248 bishop entries

// slow ray walker, only used to build tables
static u64 slide(int sq, u64 occ, const int dirs[4][2]) {
    u64 r = 0;
    for (int d = 0; d < 4; d++) {
        int f = filex(sq), k = rankx(sq);
        while (true) {
            f += dirs[d][0];
            k += dirs[d][1];
            if (f < 0 || f > 7 || k < 0 || k > 7) break;
            int s = mksq(f, k);
            r |= sqbb(s);
            if (occ & sqbb(s)) break;
        }
    }
    return r;
}

static const int rdirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
static const int bdirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

// tiny xorshift so magic search is deterministic between runs
static u64 rngstate = 0x9E3779B97F4A7C15ULL;
static u64 rng() {
    rngstate ^= rngstate >> 12;
    rngstate ^= rngstate << 25;
    rngstate ^= rngstate >> 27;
    return rngstate * 2685821657736338717ULL;
}
static u64 sparse() { return rng() & rng() & rng(); }

static void initmagics(Magic* mags, u64* table, const int dirs[4][2]) {
    u64* next = table;
    u64 occs[4096], refs[4096];
    int epoch[4096] = {0};
    int cnt = 0;
    for (int sq = 0; sq < 64; sq++) {
        auto& m = mags[sq];
        // edges don't matter for blocking unless the piece itself sits on them
        u64 edges = ((rankbb(0) | rankbb(7)) & ~rankbb(rankx(sq))) |
                    ((filebb(0) | filebb(7)) & ~filebb(filex(sq)));
        m.mask = slide(sq, 0, dirs) & ~edges;
        int bits = popcnt(m.mask);
        m.shift = 64 - bits;
        m.att = next;
        int size = 1 << bits;
        next += size;

        // enumerate every subset of the mask (carry-rippler trick)
        u64 b = 0;
        int n = 0;
        do {
            occs[n] = b;
            refs[n] = slide(sq, b, dirs);
            n++;
            b = (b - m.mask) & m.mask;
        } while (b);

#if defined(USE_PEXT)
        for (int i = 0; i < n; i++) m.att[m.index(occs[i])] = refs[i];
#else
        // try random sparse numbers until one maps every subset without a bad collision
        while (true) {
            m.magic = sparse();
            if (popcnt((m.mask * m.magic) >> 56) < 6) continue;
            cnt++;
            bool ok = true;
            for (int i = 0; i < n && ok; i++) {
                u64 idx = m.index(occs[i]);
                if (epoch[idx] < cnt) {
                    epoch[idx] = cnt;
                    m.att[idx] = refs[i];
                } else if (m.att[idx] != refs[i]) {
                    ok = false;
                }
            }
            if (ok) break;
        }
#endif
    }
}

void init() {
    for (int sq = 0; sq < 64; sq++) {
        u64 b = sqbb(sq);
        pawnatt[WHITE][sq] = pawnattbb(WHITE, b);
        pawnatt[BLACK][sq] = pawnattbb(BLACK, b);
        u64 k = shifte(b) | shiftw(b);
        k |= shiftn(k | b) | shifts(k | b);
        kingatt[sq] = k;
        u64 l = shifte(b) | shiftw(b), l2 = shifte(shifte(b)) | shiftw(shiftw(b));
        knightatt[sq] = shiftn(shiftn(l)) | shifts(shifts(l)) | shiftn(l2) | shifts(l2);
    }
    for (int a = 0; a < 64; a++)
        for (int b = 0; b < 64; b++) {
            int df = std::abs(filex(a) - filex(b)), dr = std::abs(rankx(a) - rankx(b));
            dist[a][b] = df > dr ? df : dr;
        }

    initmagics(rmag, rtable, rdirs);
    initmagics(bmag, btable, bdirs);

    for (int a = 0; a < 64; a++)
        for (int b = 0; b < 64; b++) {
            between[a][b] = line[a][b] = 0;
            if (a == b) continue;
            if (ratt(a, 0) & sqbb(b)) {
                line[a][b] = (ratt(a, 0) & ratt(b, 0)) | sqbb(a) | sqbb(b);
                between[a][b] = ratt(a, sqbb(b)) & ratt(b, sqbb(a));
            } else if (batt(a, 0) & sqbb(b)) {
                line[a][b] = (batt(a, 0) & batt(b, 0)) | sqbb(a) | sqbb(b);
                between[a][b] = batt(a, sqbb(b)) & batt(b, sqbb(a));
            }
        }
}

} // namespace bb
