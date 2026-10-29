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
};

void datagen(const DatagenConfig& cfg);
