#pragma once
#include "types.h"
#include <cstddef>

enum : int { BNONE = 0, BUPPER = 1, BLOWER = 2, BEXACT = 3 };

struct TTEntry {
    u16 key16;
    u16 move;
    i16 score;
    i16 eval;
    u8 depth;
    u8 genbound;   // generation in the top 6 bits, bound in the low 2
};

struct TTBucket {
    TTEntry e[3];
    u16 pad;
};
static_assert(sizeof(TTBucket) == 32);

struct TTHit {
    Move move = NOMOVE;
    int score = NOSCORE;
    int eval = NOSCORE;
    int depth = 0;
    int bound = BNONE;
};

class TT {
public:
    ~TT();
    void resize(size_t mb);
    void clear();
    void newsearch() { gen = u8((gen + 4) & 0xFC); }
    bool probe(u64 key, int ply, TTHit& out) const;
    void store(u64 key, int ply, Move m, int score, int eval, int depth, int bound);
    void prefetch(u64 key) const { __builtin_prefetch(&tab[index(key)]); }
    int hashfull() const;

private:
    size_t index(u64 key) const { return size_t((unsigned __int128)key * count >> 64); }
    TTBucket* tab = nullptr;
    size_t count = 0;
    u8 gen = 0;
};

extern TT tt;

// mate scores are stored relative to the node, not the root
inline int scoretott(int s, int ply) { return s >= MATEBOUND ? s + ply : s <= -MATEBOUND ? s - ply : s; }
inline int scorefromtt(int s, int ply) { return s >= MATEBOUND ? s - ply : s <= -MATEBOUND ? s + ply : s; }
