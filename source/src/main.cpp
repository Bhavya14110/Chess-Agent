#include "movegen.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    bb::init();
    zob::init();
    if (argc >= 3 && !std::strcmp(argv[1], "perft")) {
        Position pos;
        pos.setfen(argc >= 4 ? argv[3] : STARTFEN);
        divide(pos, std::atoi(argv[2]));
        return 0;
    }
    // perftlist <depth>: one FEN per stdin line -> "<nodes> <key-ok>"
    // key-ok checks that the incremental hash matches a from-scratch one at every node
    if (argc >= 3 && !std::strcmp(argv[1], "perftlist")) {
        int d = std::atoi(argv[2]);
        std::string line;
        Position pos, chk;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            pos.setfen(line);
            bool keyok = true;
            auto rec = [&](auto&& self, int depth) -> u64 {
                chk.setfen(pos.fen());
                if (chk.key() != pos.key() || chk.cur().pawnkey != pos.cur().pawnkey) keyok = false;
                if (depth == 0) return 1;
                MoveList ml;
                genlegal(pos, ml);
                u64 n = 0;
                for (int i = 0; i < ml.n; i++) {
                    pos.make(ml.mv[i]);
                    n += self(self, depth - 1);
                    pos.unmake();
                }
                return n;
            };
            u64 n = rec(rec, d);
            std::printf("%llu %d\n", (unsigned long long)n, keyok ? 1 : 0);
            std::fflush(stdout);
        }
        return 0;
    }
    return 0;
}
