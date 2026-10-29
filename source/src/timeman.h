#pragma once
#include "search.h"
#include <chrono>

inline i64 nowms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct TimeMan {
    i64 start = 0;
    i64 soft = 0;     // don't start a new iteration after this
    i64 hard = 0;     // abort the search after this
    bool timed = false;

    void init(const Limits& lim, int stm, int overhead) {
        start = nowms();
        timed = false;
        if (lim.infinite) return;
        if (lim.movetime > 0) {
            timed = true;
            soft = hard = std::max<i64>(1, lim.movetime - std::min<i64>(overhead, lim.movetime / 2));
            return;
        }
        i64 left = stm == WHITE ? lim.wtime : lim.btime;
        i64 inc = stm == WHITE ? lim.winc : lim.binc;
        if (left < 0) return;
        timed = true;
        i64 avail = std::max<i64>(1, left - overhead);
        if (lim.movestogo > 0) {
            int mtg = std::min(lim.movestogo, 50);
            soft = avail / (mtg + 1) + inc * 3 / 4;
            hard = mtg == 1 ? avail * 7 / 10 : std::min<i64>(avail * 4 / 10, soft * 3);
        } else {
            soft = avail / 25 + inc * 3 / 4;
            hard = std::min<i64>(avail * 3 / 10, soft * 3);
        }
        // panic mode: almost flagged, just move
        if (left < 3000) {
            soft = std::min<i64>(soft, avail / 30 + inc / 2);
            hard = std::min<i64>(hard, avail / 8 + inc / 2);
        }
        hard = std::min(hard, avail * 8 / 10);
        soft = std::max<i64>(1, std::min(soft, hard));
        hard = std::max<i64>(1, hard);
    }

    i64 elapsed() const { return nowms() - start; }
};
