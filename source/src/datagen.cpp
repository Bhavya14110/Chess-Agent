// self-play data generation: our engine plays itself at a fixed node count and we keep
// (position, our search score, final result). openings are a handful of uniformly random
// legal moves -- nothing is stored or picked by quality, so it's not a book.
#include "datagen.h"
#include "eval.h"
#include "search.h"
#include "timeman.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

// 32-byte record, little endian. layout documented in source/train/README.md
#pragma pack(push, 1)
struct Record {
    u64 occ;
    u8 pcs[16];
    i16 score;    // white pov, cp
    u8 result;    // 0 black won, 1 draw, 2 white won
    u8 stm;
    u16 fullmove;
    u8 halfmove;
    u8 reserved;
};
#pragma pack(pop)
static_assert(sizeof(Record) == 32);

Record pack(const Position& pos, int whitescore) {
    Record r{};
    r.occ = pos.all;
    u64 b = pos.all;
    int k = 0;
    while (b) {
        int sq = poplsb(b);
        u8 code = u8(pos.board[sq]);
        r.pcs[k / 2] |= (k & 1) ? u8(code << 4) : code;
        k++;
    }
    r.score = i16(whitescore);
    r.stm = u8(pos.stm);
    r.fullmove = u16(pos.fullmove0 + (pos.gameply + pos.startstm) / 2);
    r.halfmove = u8(std::min(pos.cur().rule50, 255));
    return r;
}

struct Stats {
    std::atomic<u64> games{0}, positions{0}, wwins{0}, bwins{0}, draws{0}, plies{0};
};

void worker(int id, const DatagenConfig& cfg, Stats& st, std::atomic<bool>& done) {
    std::mt19937_64 rng(cfg.seed * 1000003ULL + u64(id));
    SearchHandle sh(cfg.ttmb);
    std::string path = cfg.outdir + "/data_" + std::to_string(cfg.seed) + "_" + std::to_string(id) + ".bin";
    FILE* f = std::fopen(path.c_str(), "ab");
    if (!f) {
        std::fprintf(stderr, "can't open %s\n", path.c_str());
        return;
    }
    Position pos;
    std::vector<Record> game;
    while (!done.load()) {
        pos.setfen(STARTFEN);
        sh.newgame();
        game.clear();

        // random opening walk
        int nrand = 8 + int(rng() % 3);
        bool ok = true;
        for (int i = 0; i < nrand && ok; i++) {
            MoveList ml;
            genlegal(pos, ml);
            if (ml.n == 0) ok = false;
            else pos.make(ml.mv[rng() % ml.n]);
        }
        if (!ok) continue;
        {
            MoveList ml;
            genlegal(pos, ml);
            if (ml.n == 0) continue;
            Limits lim;
            lim.nodes = cfg.nodes;
            SearchResult r = sh.search(pos, lim);
            if (std::abs(r.score) > 300) continue;   // lopsided start, throw it away
        }

        int result = -1;   // 0/1/2 as in the record
        int winstreak = 0, drawstreak = 0, plies = 0;
        while (result < 0) {
            MoveList ml;
            genlegal(pos, ml);
            if (ml.n == 0) {
                result = pos.incheck() ? (pos.stm == WHITE ? 0 : 2) : 1;
                break;
            }
            if (pos.isdraw(0) || plies >= 600) {
                result = 1;
                break;
            }
            Limits lim;
            lim.nodes = cfg.nodes;
            SearchResult r = sh.search(pos, lim);
            int wscore = pos.stm == WHITE ? r.score : -r.score;

            // adjudication
            if (std::abs(r.score) > 2500) {
                if (++winstreak >= 4) {
                    result = wscore > 0 ? 2 : 0;
                    break;
                }
            } else {
                winstreak = 0;
            }
            if (plies >= 80 && std::abs(r.score) < 10) {
                if (++drawstreak >= 10) {
                    result = 1;
                    break;
                }
            } else {
                drawstreak = 0;
            }

            // keep quiet-ish positions with a real (non-mate) score
            if (!pos.incheck() && !isnoisy(r.best) && std::abs(r.score) < MATEBOUND)
                game.push_back(pack(pos, wscore));
            if (r.best == NOMOVE) {
                result = 1;
                break;
            }
            pos.make(r.best);
            plies++;
            // keep the undo stack bounded on silly long games
            if (pos.sp > MAXGAME - 8) {
                result = 1;
                break;
            }
        }
        for (auto& rec : game) rec.result = u8(result);
        std::fwrite(game.data(), sizeof(Record), game.size(), f);
        std::fflush(f);
        st.games++;
        st.positions += game.size();
        st.plies += plies;
        (result == 2 ? st.wwins : result == 0 ? st.bwins : st.draws)++;
        if (cfg.maxpositions && st.positions >= cfg.maxpositions) done = true;
    }
    std::fclose(f);
}

} // namespace

void datagen(const DatagenConfig& cfg) {
    Stats st;
    std::atomic<bool> done{false};
    std::vector<std::thread> ts;
    for (int i = 0; i < cfg.threads; i++) ts.emplace_back(worker, i, std::cref(cfg), std::ref(st), std::ref(done));
    i64 t0 = nowms();
    while (!done.load()) {
        for (int i = 0; i < 60 && !done.load(); i++) std::this_thread::sleep_for(std::chrono::seconds(1));
        double secs = (nowms() - t0) / 1000.0;
        u64 g = st.games.load();
        std::fprintf(stderr,
                     "[datagen] %.0fs games %llu positions %llu (%.0f/s) W/D/B %llu/%llu/%llu avg plies %.1f\n",
                     secs, (unsigned long long)g, (unsigned long long)st.positions.load(),
                     st.positions.load() / std::max(1.0, secs), (unsigned long long)st.wwins.load(),
                     (unsigned long long)st.draws.load(), (unsigned long long)st.bwins.load(),
                     g ? double(st.plies.load()) / g : 0.0);
    }
    for (auto& t : ts) t.join();
    std::fprintf(stderr, "[datagen] finished: %llu games, %llu positions\n", (unsigned long long)st.games.load(),
                 (unsigned long long)st.positions.load());
}
