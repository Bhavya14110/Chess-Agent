#pragma once
#include "position.h"

struct MoveList {
    Move mv[MAXMOVES];
    int sc[MAXMOVES];
    int n = 0;
    void add(Move m) { mv[n++] = m; }
};

// captures, en passant and all promotions
void gennoisy(const Position& pos, MoveList& ml);
// everything else (no promotions)
void genquiet(const Position& pos, MoveList& ml);
// all pseudo-legal moves
void genall(const Position& pos, MoveList& ml);
// strictly legal moves
void genlegal(const Position& pos, MoveList& ml);

u64 perft(Position& pos, int depth);
void divide(Position& pos, int depth);
