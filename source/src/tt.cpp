#include "tt.h"
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

TT tt;

TT::~TT() { std::free(tab); }

void TT::resize(size_t mb) {
    std::free(tab);
    count = mb * 1024 * 1024 / sizeof(TTBucket);
    if (count < 1) count = 1;
    tab = static_cast<TTBucket*>(std::malloc(count * sizeof(TTBucket)));
    clear();
}

void TT::clear() {
    // big tables take a while to zero, so split the work across threads
    size_t nthreads = std::max(1u, std::thread::hardware_concurrency() / 2);
    std::vector<std::thread> ts;
    size_t chunk = count / nthreads + 1;
    for (size_t i = 0; i < nthreads; i++) {
        size_t lo = i * chunk, hi = std::min(count, lo + chunk);
        if (lo >= hi) break;
        ts.emplace_back([this, lo, hi] { std::memset(&tab[lo], 0, (hi - lo) * sizeof(TTBucket)); });
    }
    for (auto& t : ts) t.join();
    gen = 0;
}

bool TT::probe(u64 key, int ply, TTHit& out) const {
    const TTBucket& b = tab[index(key)];
    u16 k = u16(key >> 48);
    for (const auto& e : b.e) {
        if (e.key16 == k && (e.genbound & 3)) {
            out.move = e.move;
            out.score = scorefromtt(e.score, ply);
            out.eval = e.eval;
            out.depth = e.depth;
            out.bound = e.genbound & 3;
            return true;
        }
    }
    return false;
}

void TT::store(u64 key, int ply, Move m, int score, int eval, int depth, int bound) {
    TTBucket& b = tab[index(key)];
    u16 k = u16(key >> 48);
    TTEntry* rep = &b.e[0];
    for (auto& e : b.e) {
        if (e.key16 == k || !(e.genbound & 3)) {
            rep = &e;
            break;
        }
        // replace the shallowest, preferring stale entries from old searches
        auto worth = [this](const TTEntry& x) { return x.depth - ((u8(gen - x.genbound) & 0xFC) >> 1); };
        if (worth(e) < worth(*rep)) rep = &e;
    }
    // keep the old move if we don't have one
    if (m != NOMOVE || rep->key16 != k) rep->move = m;
    // don't overwrite a deeper entry for the same position with a shallow non-exact one
    if (rep->key16 == k && bound != BEXACT && depth + 4 < rep->depth && (rep->genbound & 0xFC) == gen) return;
    rep->key16 = k;
    rep->score = i16(scoretott(score, ply));
    rep->eval = i16(eval);
    rep->depth = u8(std::max(0, depth));
    rep->genbound = u8(gen | bound);
}

int TT::hashfull() const {
    int n = 0;
    for (size_t i = 0; i < 1000 && i < count; i++)
        for (const auto& e : tab[i].e)
            if ((e.genbound & 3) && (e.genbound & 0xFC) == gen) n++;
    return n / 3;
}
