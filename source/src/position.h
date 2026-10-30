#pragma once
#include "bitboard.h"
#include <string>

namespace zob {
extern u64 psq[12][64];
extern u64 castle[16];
extern u64 ep[8];
extern u64 side;
void init();
}

// everything we need to undo a move, plus cached per-node info
struct State {
    u64 key;
    u64 pawnkey;
    u64 checkers;     // enemy pieces giving check to side to move
    u64 pinned;       // our pieces pinned to our king
    int castle;
    int ep;
    int rule50;
    int nullply;      // plies since last null move (repetition scan stops there)
    int captured;     // piece captured by the move that led here
    int moved;        // piece that made that move (before promotion), for nnue updates
    Move mv;          // move that led here
};

class Position {
public:
    u64 pcs[12];
    u64 occ[2];
    u64 all;
    u8 board[64];
    int stm;
    int gameply;       // plies since the FEN was set (for fullmove output)
    int fullmove0;
    int startstm;
    int sp;            // index of current state
    State st[MAXGAME + MAXPLY + 8];

    // call setfen() before use (tables must be initialised first)
    bool setfen(const std::string& fen);
    std::string fen() const;
    void print() const;

    State& cur() { return st[sp]; }
    const State& cur() const { return st[sp]; }
    u64 key() const { return st[sp].key; }
    u64 checkers() const { return st[sp].checkers; }
    bool incheck() const { return st[sp].checkers != 0; }

    u64 bbof(int c, int t) const { return pcs[mkpiece(c, t)]; }
    u64 typebb(int t) const { return pcs[t] | pcs[t + 6]; }
    int kingsq(int c) const { return lsb(pcs[mkpiece(c, KING)]); }
    int pieceat(int sq) const { return board[sq]; }

    u64 attackersto(int sq, u64 occupied) const;
    bool attacked(int sq, int bycolor) const { return attackersto(sq, all) & occ[bycolor]; }

    void make(Move m);
    void unmake();
    void makenull();
    void unmakenull();

    bool pseudolegal(Move m) const;
    bool legal(Move m) const;
    bool givescheck(Move m) const;
    bool see(Move m, int threshold) const;

    bool isrepetition(int searchply) const;
    bool isdraw(int searchply) const;
    bool insufficient() const;
    bool hasnonpawn(int c) const {
        return occ[c] & ~pcs[mkpiece(c, PAWN)] & ~pcs[mkpiece(c, KING)];
    }

    Move parsemove(const std::string& s) const;
    bool cancastle(int right) const;

private:
    void putpiece(int p, int sq);
    void delpiece(int sq);
    void movepiece(int from, int to);
    void setcheckinfo();
};

extern const int seevalue[7];
constexpr const char* STARTFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
