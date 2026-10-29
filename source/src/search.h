#pragma once
#include "movegen.h"
#include <atomic>
#include <string>

struct Limits {
    i64 wtime = -1, btime = -1, winc = 0, binc = 0, movetime = -1;
    int movestogo = 0;
    int depth = MAXPLY - 4;
    u64 nodes = 0;
    bool infinite = false;
};

struct SearchResult {
    Move best = NOMOVE;
    int score = 0;
    int depth = 0;
    u64 nodes = 0;
};

namespace search {

extern int moveoverhead;   // ms, covers lichess / network lag
extern int nthreads;
extern std::atomic<bool> stopflag;

void init();
void newgame();
// starts a search in the background; it prints "bestmove" when it's done
void go(const Position& pos, const Limits& lim);
void stop();
void wait();
bool running();
// blocking search without uci output (bench, datagen, tests)
SearchResult searchsync(const Position& pos, const Limits& lim, bool print);

} // namespace search

void uciout(const std::string& s);
std::string scorestr(int score);
