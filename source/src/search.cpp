#include "search.h"
#include "eval.h"
#include "timeman.h"
#include "tt.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

int search::moveoverhead = 150;
int search::nthreads = 1;
std::atomic<bool> search::stopflag{false};

static std::mutex outmtx;
void uciout(const std::string& s) {
    std::lock_guard<std::mutex> lk(outmtx);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

std::string scorestr(int score) {
    if (score >= MATEBOUND) return "mate " + std::to_string((MATE - score + 1) / 2);
    if (score <= -MATEBOUND) return "mate " + std::to_string(-(MATE + score) / 2);
    return "cp " + std::to_string(score);
}

using search::stopflag;

namespace {

struct Searcher {
    Position pos;
    Limits lim;
    TimeMan tm;
    bool mainthread = true;
    bool print = true;
    u64 nodes = 0;
    int seldepth = 0;
    Move pv[MAXPLY + 2][MAXPLY + 2];
    int pvlen[MAXPLY + 2];

    Move rootbest = NOMOVE;
    int rootscore = 0;
    int completed = 0;

    bool timeup() {
        if (stopflag.load(std::memory_order_relaxed)) return true;
        if (!mainthread) return false;
        if (lim.nodes && nodes >= lim.nodes) return stopflag = true;
        if ((nodes & 1023) == 0 && tm.timed && tm.elapsed() >= tm.hard) return stopflag = true;
        return false;
    }

    static int mvvlva(const Position& p, Move m) {
        int victim = mflag(m) == MEP ? PAWN : (p.board[mto(m)] == NOPIECE ? NOTYPE : ptype(p.board[mto(m)]));
        int v = victim == NOTYPE ? 0 : seevalue[victim];
        if (ispromo(m)) v += seevalue[promotype(m)];
        return v * 8 - ptype(p.board[mfrom(m)]);
    }

    void scoremoves(MoveList& ml, Move ttmove) {
        for (int i = 0; i < ml.n; i++) {
            Move m = ml.mv[i];
            ml.sc[i] = m == ttmove ? (1 << 30) : isnoisy(m) ? 1000000 + mvvlva(pos, m) : 0;
        }
    }

    static Move pickbest(MoveList& ml, int i) {
        int b = i;
        for (int j = i + 1; j < ml.n; j++)
            if (ml.sc[j] > ml.sc[b]) b = j;
        std::swap(ml.mv[i], ml.mv[b]);
        std::swap(ml.sc[i], ml.sc[b]);
        return ml.mv[i];
    }

    int qsearch(int alpha, int beta, int ply) {
        nodes++;
        if (ply > seldepth) seldepth = ply;
        if (timeup()) return 0;
        if (pos.isdraw(ply)) return 0;
        bool inchk = pos.incheck();
        if (ply >= MAXPLY - 1) return inchk ? 0 : evaluate(pos);

        TTHit h;
        bool tthit = tt.probe(pos.key(), ply, h);
        if (tthit && (h.bound == BEXACT || (h.bound == BLOWER && h.score >= beta) || (h.bound == BUPPER && h.score <= alpha)))
            return h.score;
        Move ttmove = tthit ? h.move : NOMOVE;
        int oldalpha = alpha;
        Move bestmv = NOMOVE;

        int best = -INF;
        if (!inchk) {
            best = evaluate(pos);
            if (best >= beta) return best;
            if (best > alpha) alpha = best;
        }
        MoveList ml;
        if (inchk) genall(pos, ml);
        else gennoisy(pos, ml);
        scoremoves(ml, ttmove);
        int legal = 0;
        for (int i = 0; i < ml.n; i++) {
            Move m = pickbest(ml, i);
            if (!inchk && ispromo(m) && promotype(m) != QUEEN) continue;
            // captures that lose material by SEE aren't worth looking at in qsearch
            if (!inchk && !pos.see(m, 0)) continue;
            if (!pos.legal(m)) continue;
            legal++;
            pos.make(m);
            int score = -qsearch(-beta, -alpha, ply + 1);
            pos.unmake();
            if (stopflag.load(std::memory_order_relaxed)) return 0;
            if (score > best) {
                best = score;
                if (score > alpha) {
                    alpha = score;
                    bestmv = m;
                    if (alpha >= beta) break;
                }
            }
        }
        if (inchk && !legal) return -MATE + ply;
        tt.store(pos.key(), ply, bestmv, best, NOSCORE, 0,
                 best >= beta ? BLOWER : best > oldalpha ? BEXACT : BUPPER);
        return best;
    }

    int negamax(int alpha, int beta, int depth, int ply) {
        pvlen[ply] = ply;
        if (depth <= 0) return qsearch(alpha, beta, ply);
        nodes++;
        if (ply > seldepth) seldepth = ply;
        bool root = ply == 0;
        if (!root) {
            if (timeup()) return 0;
            if (pos.isdraw(ply)) return 0;
            if (ply >= MAXPLY - 1) return pos.incheck() ? 0 : evaluate(pos);
            // mate distance pruning
            alpha = std::max(alpha, -MATE + ply);
            beta = std::min(beta, MATE - ply - 1);
            if (alpha >= beta) return alpha;
        }
        bool inchk = pos.incheck();

        TTHit h;
        bool tthit = tt.probe(pos.key(), ply, h);
        Move ttmove = root ? rootbest : tthit ? h.move : NOMOVE;
        if (!root && tthit && h.depth >= depth &&
            (h.bound == BEXACT || (h.bound == BLOWER && h.score >= beta) || (h.bound == BUPPER && h.score <= alpha)))
            return h.score;
        int oldalpha = alpha;
        Move bestmv = NOMOVE;

        MoveList ml;
        genall(pos, ml);
        scoremoves(ml, ttmove);

        int best = -INF, legal = 0;
        for (int i = 0; i < ml.n; i++) {
            Move m = pickbest(ml, i);
            if (!pos.legal(m)) continue;
            legal++;
            pos.make(m);
            int score = -negamax(-beta, -alpha, depth - 1, ply + 1);
            pos.unmake();
            if (stopflag.load(std::memory_order_relaxed)) return 0;
            if (score > best) {
                best = score;
                if (score > alpha) {
                    alpha = score;
                    bestmv = m;
                    pv[ply][ply] = m;
                    for (int j = ply + 1; j < pvlen[ply + 1]; j++) pv[ply][j] = pv[ply + 1][j];
                    pvlen[ply] = pvlen[ply + 1];
                    if (root) {
                        rootbest = m;
                        rootscore = score;
                    }
                    if (alpha >= beta) break;
                }
            }
        }
        if (!legal) return inchk ? -MATE + ply : 0;
        tt.store(pos.key(), ply, bestmv, best, NOSCORE, depth,
                 best >= beta ? BLOWER : best > oldalpha ? BEXACT : BUPPER);
        return best;
    }

    std::string pvstring() {
        std::string s;
        for (int j = 0; j < pvlen[0]; j++) s += movestr(pv[0][j]) + " ";
        if (!s.empty()) s.pop_back();
        return s;
    }

    void iterate() {
        nodes = 0;
        completed = 0;
        rootbest = NOMOVE;
        // always have a legal move ready, even if we get stopped right away
        MoveList ml;
        genlegal(pos, ml);
        if (ml.n == 0) return;
        rootbest = ml.mv[0];
        if (ml.n == 1 && tm.timed) tm.soft = std::min<i64>(tm.soft, 30);

        Move prevbest = NOMOVE;
        int stable = 0;
        for (int d = 1; d <= lim.depth; d++) {
            seldepth = 0;
            int score = negamax(-INF, INF, d, 0);
            // a half-finished iteration is still usable: rootbest only changes
            // when a root move was searched all the way through
            if (stopflag.load()) break;
            completed = d;
            rootscore = score;
            if (print && mainthread) {
                i64 ms = std::max<i64>(1, tm.elapsed());
                uciout("info depth " + std::to_string(d) + " seldepth " + std::to_string(seldepth) +
                       " score " + scorestr(score) + " nodes " + std::to_string(nodes) +
                       " nps " + std::to_string(nodes * 1000 / ms) + " time " + std::to_string(ms) +
                       " pv " + pvstring());
            }
            if (rootbest == prevbest) stable = std::min(stable + 1, 6);
            else stable = 0;
            prevbest = rootbest;
            if (tm.timed) {
                static const double stabfac[7] = {2.0, 1.4, 1.1, 0.9, 0.8, 0.75, 0.7};
                double f = stabfac[stable];
                if (tm.elapsed() >= i64(tm.soft * f)) break;
            }
            if (lim.nodes && nodes >= lim.nodes) break;
        }
    }
};

std::unique_ptr<Searcher> mainsearcher;
std::thread searchthread;
std::atomic<bool> searching{false};

} // namespace

void search::init() {
    tt.resize(64);
    mainsearcher = std::make_unique<Searcher>();
}

void search::newgame() {
    wait();
    tt.clear();
}

void search::go(const Position& pos, const Limits& lim) {
    wait();
    stopflag = false;
    searching = true;
    Searcher& s = *mainsearcher;
    std::memcpy(static_cast<void*>(&s.pos), &pos, sizeof(Position));
    s.lim = lim;
    s.tm.init(lim, pos.stm, moveoverhead);
    s.print = true;
    s.mainthread = true;
    searchthread = std::thread([&s, lim] {
        tt.newsearch();
        s.iterate();
        // with "go infinite" we must not answer until told to stop
        while (lim.infinite && !stopflag.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        uciout("bestmove " + movestr(s.rootbest));
        searching = false;
    });
}

void search::stop() { stopflag = true; }

void search::wait() {
    if (searchthread.joinable()) searchthread.join();
}

bool search::running() { return searching.load(); }

SearchResult search::searchsync(const Position& pos, const Limits& lim, bool print) {
    auto s = std::make_unique<Searcher>();
    std::memcpy(static_cast<void*>(&s->pos), &pos, sizeof(Position));
    s->lim = lim;
    s->tm.init(lim, pos.stm, 0);
    s->print = print;
    stopflag = false;
    s->iterate();
    return {s->rootbest, s->rootscore, s->completed, s->nodes};
}
