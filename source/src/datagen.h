#pragma once
#include "types.h"
#include <string>

struct DatagenConfig {
    std::string outdir = "data/selfplay";
    int threads = 16;
    u64 nodes = 5000;
    u64 maxpositions = 0;   // 0 = run until killed
    u64 seed = 1;
    size_t ttmb = 16;
    // optional "FEN[;anything]" file of human-game positions to start games from (more varied
    // openings/middlegames/endgames than random walks). only used as starting points: nothing
    // from it ends up in the engine.
    std::string startfile;
};

void datagen(const DatagenConfig& cfg);
