#pragma once
// basic types shared by everything. squares are a1=0 .. h8=63.
#include <cstdint>
#include <string>

using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8 = uint8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

enum : int { WHITE = 0, BLACK = 1 };
enum : int { PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING, NOTYPE };
constexpr int NOPIECE = 12;
constexpr int NOSQ = 64;

constexpr int MAXPLY = 128;      // max search depth from root
constexpr int MAXMOVES = 256;    // max legal moves in any position (218 is the real max)
constexpr int MAXGAME = 1024;    // max game plies we keep in the undo stack

constexpr int INF = 32000;
constexpr int MATE = 31000;
constexpr int MATEBOUND = MATE - MAXPLY;   // anything above is a mate score
constexpr int NOSCORE = 32001;

inline int mkpiece(int c, int t) { return c * 6 + t; }
inline int ptype(int p) { return p % 6; }
inline int pcolor(int p) { return p / 6; }
inline int filex(int sq) { return sq & 7; }
inline int rankx(int sq) { return sq >> 3; }
inline int mksq(int f, int r) { return r * 8 + f; }
// rank from the point of view of colour c (0 = own back rank)
inline int relrank(int c, int sq) { return c == WHITE ? rankx(sq) : 7 - rankx(sq); }
inline int flipsq(int sq) { return sq ^ 56; }

// castling rights bits
enum : int { WK = 1, WQ = 2, BK = 4, BQ = 8 };

// move = from(6) | to(6) | flag(4)
using Move = u16;
constexpr Move NOMOVE = 0;
enum : int {
    MQUIET = 0, MDOUBLE = 1, MOO = 2, MOOO = 3, MCAP = 4, MEP = 5,
    MPN = 8, MPB = 9, MPR = 10, MPQ = 11,
    MPNC = 12, MPBC = 13, MPRC = 14, MPQC = 15
};
inline Move mkmove(int from, int to, int flag) { return Move(from | (to << 6) | (flag << 12)); }
inline int mfrom(Move m) { return m & 63; }
inline int mto(Move m) { return (m >> 6) & 63; }
inline int mflag(Move m) { return m >> 12; }
inline bool iscap(Move m) { return mflag(m) & 4; }          // MCAP, MEP, promo-captures
inline bool ispromo(Move m) { return mflag(m) & 8; }
inline int promotype(Move m) { return (mflag(m) & 3) + KNIGHT; }
inline bool iscastle(Move m) { return mflag(m) == MOO || mflag(m) == MOOO; }
inline bool isnoisy(Move m) { return iscap(m) || mflag(m) == MPQ; }

std::string sqname(int sq);
std::string movestr(Move m);
