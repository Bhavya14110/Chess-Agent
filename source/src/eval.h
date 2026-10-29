#pragma once
#include "position.h"

// packed middlegame/endgame score, eg in the high 16 bits
using Score = int32_t;
constexpr Score S(int mg, int eg) { return Score(u32(eg) << 16) + mg; }
inline int mgof(Score s) { return int16_t(u16(u32(s))); }
inline int egof(Score s) { return int16_t(u16(u32(s + 0x8000) >> 16)); }

// every tunable weight lives in one flat table so the texel tuner can see all of them
enum : int {
    P_MAT = 0,                       // [6] by piece type
    P_PSQT = P_MAT + 6,              // [6][64], white's point of view
    P_MOBN = P_PSQT + 6 * 64,        // [9]
    P_MOBB = P_MOBN + 9,             // [14]
    P_MOBR = P_MOBB + 14,            // [15]
    P_MOBQ = P_MOBR + 15,            // [28]
    P_DOUBLED = P_MOBQ + 28,
    P_ISOLATED,
    P_BACKWARD,
    P_PHALANX,                       // [8] by relative rank
    P_SUPPORTED = P_PHALANX + 8,     // [8]
    P_PASSED = P_SUPPORTED + 8,      // [8]
    P_PASSFREE = P_PASSED + 8,       // [8] stop square empty and safe
    P_PASSKOWN = P_PASSFREE + 8,     // [8] times own king distance to the stop square
    P_PASSKTHEM = P_PASSKOWN + 8,    // [8] times enemy king distance
    P_BPAIR = P_PASSKTHEM + 8,
    P_ROOKOPEN,
    P_ROOKSEMI,
    P_KNOUTPOST,
    P_BOUTPOST,
    P_MINORBEHINDPAWN,
    P_SHIELD1,                       // own pawn right in front of the king area
    P_SHIELD2,                       // one rank further
    P_SHIELDMISSING,                 // king-area file with no own pawn in front
    P_KINGOPEN,                      // king-area file with no pawns at all
    P_KATT,                          // [6] by attacker type, times attacked king-zone squares
    P_KATTN = P_KATT + 6,            // [8] one-hot on the number of attacking pieces
    P_SAFECHECK = P_KATTN + 8,       // [6] by checking piece type
    P_THRPAWN = P_SAFECHECK + 6,     // pawn attacks a piece
    P_THRMINOR,                      // [6] minor attacks piece of type
    P_THRROOK = P_THRMINOR + 6,      // [6] rook attacks piece of type
    P_HANGING = P_THRROOK + 6,       // attacked and undefended
    P_TEMPO,
    NPARAMS
};

extern Score evp[NPARAMS];

// trace of how many times each term fired for each colour (for tuning)
struct EvalTrace {
    int16_t coef[NPARAMS][2];
    int phase;
    int scale;      // endgame scale factor out of 128
    int egextra;    // untuned eg term from white's point of view (mop-up)
};

void evalinit();
void setdefaultparams();
// static eval from the side to move's point of view, in centipawns
int evaluate(const Position& pos);
// white-pov eval with a full trace
int evaluatetrace(const Position& pos, EvalTrace& tr);
// write the current table out as the generated header
void writeparams(const char* path);
