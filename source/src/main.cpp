#include "eval.h"
#include "movegen.h"
#include "search.h"
#include "timeman.h"
#include "tt.h"
#include "tune.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#define ENGINE_NAME "Corvus"
#define ENGINE_VERSION "0.2"

// positions for `bench`: a mix of openings, middlegames and endgames (written by hand / from perft suites)
static const char* benchfens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
    "r2q1rk1/ppp2ppp/2np1n2/2b1p1B1/2B1P1b1/2NP1N2/PPP2PPP/R2Q1RK1 w - - 6 8",
    "2r3k1/pp3ppp/4p3/3pP3/3P4/P4N2/1P3PPP/2R3K1 w - - 0 25",
    "8/5pk1/6p1/7p/7P/6P1/5PK1/8 w - - 0 40",
    "8/8/4k3/8/2p5/8/B2K4/8 w - - 0 1",
    "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
    "r1b1k2r/ppppnppp/2n2q2/2b5/3NP3/2P1B3/PP3PPP/RN1QKB1R w KQkq - 0 1",
    "3r1rk1/p4ppp/1qp5/2p1n3/4P3/1PN2Q2/P1P2PPP/3R1RK1 b - - 0 18",
    "8/3k4/3p4/p2P1p2/P2P1P2/6K1/8/8 w - - 0 1",
    "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1",
};

static void bench(int depth) {
    u64 total = 0;
    i64 ms = 0;
    Position pos;
    for (const char* f : benchfens) {
        pos.setfen(f);
        search::newgame();
        Limits lim;
        lim.depth = depth;
        i64 t0 = nowms();
        total += search::searchsync(pos, lim, false).nodes;
        ms += nowms() - t0;
    }
    ms = std::max<i64>(1, ms);
    std::printf("%llu nodes %llu nps\n", (unsigned long long)total, (unsigned long long)(total * 1000 / ms));
    std::fflush(stdout);
}

static void setposition(Position& pos, std::istringstream& in) {
    std::string tok, fen;
    in >> tok;
    if (tok == "startpos") {
        fen = STARTFEN;
        in >> tok;
    } else if (tok == "fen") {
        while (in >> tok && tok != "moves") fen += tok + " ";
    } else {
        return;
    }
    if (!pos.setfen(fen)) {
        std::fprintf(stderr, "bad fen: %s\n", fen.c_str());
        pos.setfen(STARTFEN);
    }
    if (tok == "moves") {
        while (in >> tok) {
            Move m = pos.parsemove(tok);
            if (m == NOMOVE) {
                std::fprintf(stderr, "illegal move in position command: %s\n", tok.c_str());
                break;
            }
            // keep the undo stack from overflowing in absurdly long games:
            // anything before the last irreversible move can't matter for repetition
            if (pos.sp >= MAXGAME - 2) {
                std::string f = pos.fen();
                pos.setfen(f);
            }
            pos.make(m);
        }
    }
}

static void go(const Position& pos, std::istringstream& in) {
    Limits lim;
    std::string tok;
    while (in >> tok) {
        if (tok == "wtime") in >> lim.wtime;
        else if (tok == "btime") in >> lim.btime;
        else if (tok == "winc") in >> lim.winc;
        else if (tok == "binc") in >> lim.binc;
        else if (tok == "movestogo") in >> lim.movestogo;
        else if (tok == "movetime") in >> lim.movetime;
        else if (tok == "depth") in >> lim.depth;
        else if (tok == "nodes") in >> lim.nodes;
        else if (tok == "infinite") lim.infinite = true;
    }
    lim.depth = std::clamp(lim.depth, 1, MAXPLY - 4);
    search::go(pos, lim);
}

static void setoption(std::istringstream& in) {
    std::string tok, name, value;
    in >> tok;   // "name"
    while (in >> tok && tok != "value") name += (name.empty() ? "" : " ") + tok;
    while (in >> tok) value += (value.empty() ? "" : " ") + tok;
    for (auto& c : name) c = char(std::tolower(c));
    try {
        if (name == "hash") tt.resize(std::clamp(std::stoi(value), 1, 65536));
        else if (name == "threads") search::nthreads = std::clamp(std::stoi(value), 1, 256);
        else if (name == "move overhead") search::moveoverhead = std::clamp(std::stoi(value), 0, 5000);
    } catch (...) {
        std::fprintf(stderr, "bad option value: %s\n", value.c_str());
    }
}

static void uciloop() {
    Position pos;
    pos.setfen(STARTFEN);
    std::string line, cmd;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream in(line);
        cmd.clear();
        in >> cmd;
        if (cmd == "uci") {
            uciout("id name " ENGINE_NAME " " ENGINE_VERSION);
            uciout("id author chess-agent team");
            uciout("option name Hash type spin default 64 min 1 max 65536");
            uciout("option name Threads type spin default 1 min 1 max 256");
            uciout("option name Move Overhead type spin default 150 min 0 max 5000");
            uciout("uciok");
        } else if (cmd == "isready") {
            uciout("readyok");
        } else if (cmd == "ucinewgame") {
            search::stop();
            search::newgame();
        } else if (cmd == "position") {
            search::stop();
            search::wait();
            setposition(pos, in);
        } else if (cmd == "go") {
            search::stop();
            search::wait();
            go(pos, in);
        } else if (cmd == "stop") {
            search::stop();
        } else if (cmd == "setoption") {
            search::stop();
            search::wait();
            setoption(in);
        } else if (cmd == "quit") {
            break;
        } else if (cmd == "d") {
            pos.print();
        } else if (cmd == "eval") {
            std::fprintf(stderr, "eval (stm): %d\n", evaluate(pos));
        } else if (cmd == "perft") {
            int d = 1;
            in >> d;
            divide(pos, d);
        } else if (cmd == "bench") {
            int d = 10;
            in >> d;
            bench(d);
        } else if (!cmd.empty()) {
            std::fprintf(stderr, "unknown command: %s\n", line.c_str());
        }
    }
    search::stop();
    search::wait();
}

int main(int argc, char** argv) {
    bb::init();
    zob::init();
    evalinit();
    search::init();

    // tune <quiet positions file> <epochs> <threads> <lr> <out header>
    if (argc >= 7 && !std::strcmp(argv[1], "tune")) {
        tune(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), std::atof(argv[5]), argv[6]);
        return 0;
    }
    // quiet <in fen;result file> <out file> <threads>
    if (argc >= 5 && !std::strcmp(argv[1], "quiet")) {
        quietfilter(argv[2], argv[3], std::atoi(argv[4]));
        return 0;
    }
    // writeparams <out>: dump the current eval table (e.g. the hand-picked defaults)
    if (argc >= 3 && !std::strcmp(argv[1], "writeparams")) {
        writeparams(argv[2]);
        return 0;
    }
    if (argc >= 2 && !std::strcmp(argv[1], "bench")) {
        bench(argc >= 3 ? std::atoi(argv[2]) : 10);
        return 0;
    }
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
    uciloop();
    return 0;
}
