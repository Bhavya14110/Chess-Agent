#pragma once
#include "types.h"
#if defined(USE_PEXT)
#include <immintrin.h>
#endif

inline int popcnt(u64 b) { return __builtin_popcountll(b); }
inline int lsb(u64 b) { return __builtin_ctzll(b); }
inline int msb(u64 b) { return 63 - __builtin_clzll(b); }
inline int poplsb(u64& b) { int s = lsb(b); b &= b - 1; return s; }
inline u64 sqbb(int sq) { return 1ULL << sq; }
inline bool several(u64 b) { return b & (b - 1); }

constexpr u64 FILEA = 0x0101010101010101ULL;
constexpr u64 RANK1 = 0xFFULL;
inline u64 filebb(int f) { return FILEA << f; }
inline u64 rankbb(int r) { return RANK1 << (8 * r); }

inline u64 shiftn(u64 b) { return b << 8; }
inline u64 shifts(u64 b) { return b >> 8; }
inline u64 shifte(u64 b) { return (b & ~filebb(7)) << 1; }
inline u64 shiftw(u64 b) { return (b & ~filebb(0)) >> 1; }
inline u64 pushup(int c, u64 b) { return c == WHITE ? shiftn(b) : shifts(b); }
inline u64 pawnattbb(int c, u64 p) {
    return c == WHITE ? shifte(shiftn(p)) | shiftw(shiftn(p)) : shifte(shifts(p)) | shiftw(shifts(p));
}

namespace bb {

struct Magic {
    u64 mask;
    u64 magic;
    u64* att;
    int shift;
    u64 index(u64 occ) const {
#if defined(USE_PEXT)
        return _pext_u64(occ, mask);
#else
        return ((occ & mask) * magic) >> shift;
#endif
    }
};

extern u64 pawnatt[2][64];
extern u64 knightatt[64];
extern u64 kingatt[64];
extern u64 between[64][64];   // squares strictly between a and b on a line (empty if not aligned)
extern u64 line[64][64];      // full line through a and b (empty if not aligned)
extern int dist[64][64];      // chebyshev distance
extern Magic rmag[64], bmag[64];

void init();

inline u64 ratt(int sq, u64 occ) { auto& m = rmag[sq]; return m.att[m.index(occ)]; }
inline u64 batt(int sq, u64 occ) { auto& m = bmag[sq]; return m.att[m.index(occ)]; }
inline u64 qatt(int sq, u64 occ) { return ratt(sq, occ) | batt(sq, occ); }

inline u64 attacks(int t, int sq, u64 occ) {
    switch (t) {
    case KNIGHT: return knightatt[sq];
    case BISHOP: return batt(sq, occ);
    case ROOK: return ratt(sq, occ);
    case QUEEN: return qatt(sq, occ);
    case KING: return kingatt[sq];
    default: return 0;
    }
}

} // namespace bb
