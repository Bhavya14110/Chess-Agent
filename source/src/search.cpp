#include "search.h"
#include "eval.h"
#include "movepick.h"
#include "timeman.h"
#include "tt.h"
#include <algorithm>
#include <cmath>
#include <functional>
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

// late move reduction amounts, indexed by [depth][move number]
static int lmrtab[64][64];

namespace {

struct Stack {
    Move mv;          // move played to reach the next ply
    int piece;        // piece that moved
    int staticeval;
    Move killers[2];
    int16_t* ch;      // continuation history row for the move played here
};

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
    Stack stackbuf[MAXPLY + 8];
    Stack* ss = stackbuf + 4;   // a few entries of slack so ss[ply - k] is always valid
    Histories hist;
    TT* ttab = &tt;   // datagen threads bring their own table
    int threadid = 0;
    std::function<u64()> totalnodes;   // set for the uci main thread to report all threads' nodes
    std::atomic<bool> ownstop{false};
    std::atomic<bool>* stopp = &stopflag;   // the uci search shares the global flag

    Move rootbest = NOMOVE;
    int rootscore = 0;
    int completed = 0;

    bool timeup() {
        if (stopp->load(std::memory_order_relaxed)) return true;
        if (!mainthread) return false;
        if (lim.nodes && nodes >= lim.nodes) return *stopp = true;
        if ((nodes & 1023) == 0 && tm.timed && tm.elapsed() >= tm.hard) return *stopp = true;
        return false;
    }

    void makemove(Move m, int ply) {
        ss[ply].mv = m;
        ss[ply].piece = pos.board[mfrom(m)];
        ss[ply].ch = hist.cont[ss[ply].piece][mto(m)];
        pos.make(m);
        ttab->prefetch(pos.key());
    }

    Move countermove(int ply) const {
        if (ply == 0 || ss[ply - 1].mv == NOMOVE) return NOMOVE;
        return hist.counter[ss[ply - 1].piece][mto(ss[ply - 1].mv)];
    }

    // reward the quiet move that caused a cutoff, punish the quiets tried before it
    void updatequiets(Move best, const Move* tried, int ntried, int depth, int ply) {
        int bonus = std::min(16 * depth * depth + 32 * depth, 1600);
        int us = pos.stm;
        auto upd = [&](Move m, int b) {
            histupdate(hist.butterfly[us][m & 4095], b);
            int pc = pos.board[mfrom(m)] * 64 + mto(m);
            if (ply >= 1 && ss[ply - 1].piece != NOPIECE) histupdate(ss[ply - 1].ch[pc], b);
            if (ply >= 2 && ss[ply - 2].piece != NOPIECE) histupdate(ss[ply - 2].ch[pc], b);
        };
        upd(best, bonus);
        for (int i = 0; i < ntried; i++)
            if (tried[i] != best) upd(tried[i], -bonus);
        if (ss[ply].killers[0] != best) {
            ss[ply].killers[1] = ss[ply].killers[0];
            ss[ply].killers[0] = best;
        }
        if (ply > 0 && ss[ply - 1].mv != NOMOVE) hist.counter[ss[ply - 1].piece][mto(ss[ply - 1].mv)] = best;
    }

    int qsearch(int alpha, int beta, int ply) {
        nodes++;
        if (ply > seldepth) seldepth = ply;
        if (timeup()) return 0;
        if (pos.isdraw(ply)) return 0;
        bool inchk = pos.incheck();
        if (ply >= MAXPLY - 1) return inchk ? 0 : evaluate(pos);

        bool pvnode = beta - alpha > 1;
        TTHit h;
        bool tthit = ttab->probe(pos.key(), ply, h);
        if (!pvnode && tthit && (h.bound == BEXACT || (h.bound == BLOWER && h.score >= beta) || (h.bound == BUPPER && h.score <= alpha)))
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
        MovePicker mp(pos, hist, ttmove);
        int legal = 0;
        Move m;
        while ((m = mp.next()) != NOMOVE) {
            if (!inchk && ispromo(m) && promotype(m) != QUEEN) continue;
            // captures that lose material by SEE aren't worth looking at in qsearch
            if (!inchk && !pos.see(m, 0)) continue;
            if (!pos.legal(m)) continue;
            legal++;
            makemove(m, ply);
            int score = -qsearch(-beta, -alpha, ply + 1);
            pos.unmake();
            if (stopp->load(std::memory_order_relaxed)) return 0;
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
        ttab->store(pos.key(), ply, bestmv, best, NOSCORE, 0,
                 best >= beta ? BLOWER : best > oldalpha ? BEXACT : BUPPER);
        return best;
    }

    int negamax(int alpha, int beta, int depth, int ply) {
        pvlen[ply] = ply;
        if (depth <= 0) return qsearch(alpha, beta, ply);
        nodes++;
        if (ply > seldepth) seldepth = ply;
        bool root = ply == 0;
        bool pvnode = beta - alpha > 1;
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
        ss[ply + 1].killers[0] = ss[ply + 1].killers[1] = NOMOVE;

        TTHit h;
        bool tthit = ttab->probe(pos.key(), ply, h);
        Move ttmove = root ? rootbest : tthit ? h.move : NOMOVE;
        if (!pvnode && tthit && h.depth >= depth &&
            (h.bound == BEXACT || (h.bound == BLOWER && h.score >= beta) || (h.bound == BUPPER && h.score <= alpha)))
            return h.score;
        int oldalpha = alpha;
        Move bestmv = NOMOVE;

        // no tt move: ordering here will be poor, so spend less on this node for now
        if (!root && depth >= 4 && ttmove == NOMOVE) depth--;

        int staticeval = NOSCORE;
        if (!inchk) staticeval = (tthit && h.eval != NOSCORE) ? h.eval : evaluate(pos);
        ss[ply].staticeval = staticeval;

        bool improving = !inchk && ply >= 2 && ss[ply - 2].staticeval != NOSCORE && staticeval > ss[ply - 2].staticeval;

        // reverse futility: way above beta at low depth, assume it holds
        if (!pvnode && !inchk && depth <= 8 && std::abs(beta) < MATEBOUND &&
            staticeval - 80 * (depth - improving) >= beta)
            return (staticeval + beta) / 2;

        // null move: if we're still above beta after passing, this node is almost surely a cutoff.
        // never with only pawns left (zugzwang) and never twice in a row
        if (!pvnode && !inchk && depth >= 3 && staticeval >= beta && pos.hasnonpawn(pos.stm) &&
            ply > 0 && ss[ply - 1].mv != NOMOVE && std::abs(beta) < MATEBOUND) {
            int r = 3 + depth / 4 + std::min((staticeval - beta) / 200, 3);
            ss[ply].mv = NOMOVE;
            ss[ply].piece = NOPIECE;
            ss[ply].ch = hist.cont[12][0];
            pos.makenull();
            int score = -negamax(-beta, -beta + 1, depth - r, ply + 1);
            pos.unmakenull();
            if (stopp->load(std::memory_order_relaxed)) return 0;
            if (score >= beta) return score >= MATEBOUND ? beta : score;
        }

        Move cm = countermove(ply);
        const int16_t* ch1 = ply >= 1 ? ss[ply - 1].ch : nullptr;
        const int16_t* ch2 = ply >= 2 ? ss[ply - 2].ch : nullptr;
        MovePicker mp(pos, hist, ttmove, ss[ply].killers[0], ss[ply].killers[1], cm, ch1, ch2);
        Move quiets[MAXMOVES];
        int nquiets = 0;
        int best = -INF, legal = 0;
        Move m;
        bool skipquiets = false;
        while ((m = mp.next(skipquiets)) != NOMOVE) {
            bool quiet = !isnoisy(m);
            // pruning of moves that very likely don't matter, once we have something to fall back on
            if (!root && best > -MATEBOUND) {
                if (quiet) {
                    // late move pruning: enough quiets tried at low depth
                    if (depth <= 8 && nquiets >= (3 + depth * depth) / (improving ? 1 : 2)) {
                        skipquiets = true;
                        continue;
                    }
                    // futility: even a big positional gain won't reach alpha
                    if (!inchk && depth <= 8 && staticeval + 100 + 100 * depth <= alpha) {
                        skipquiets = true;
                        continue;
                    }
                    if (depth <= 8 && !pos.see(m, -60 * depth)) continue;
                } else if (depth <= 6 && !pos.see(m, -100 * depth)) {
                    continue;
                }
            }
            if (!pos.legal(m)) continue;
            legal++;
            int pcidx = pos.board[mfrom(m)] * 64 + mto(m);
            int hscore = quiet ? hist.butterfly[pos.stm][m & 4095] + (ch1 ? ch1[pcidx] : 0) + (ch2 ? ch2[pcidx] : 0) : 0;
            bool refutation = m == ss[ply].killers[0] || m == ss[ply].killers[1] || m == cm;
            makemove(m, ply);
            bool givescheck = pos.incheck();
            // checks get searched one ply deeper so we don't stop in the middle of a forcing line
            int newdepth = depth - 1 + (givescheck ? 1 : 0);
            int score;
            if (legal == 1) {
                score = -negamax(-beta, -alpha, newdepth, ply + 1);
            } else {
                // late quiet moves are probably bad: look at them with less depth first
                int r = 0;
                if (depth >= 3 && quiet && legal > 1 + pvnode) {
                    r = lmrtab[std::min(depth, 63)][std::min(legal, 63)];
                    r -= pvnode;
                    r -= refutation;
                    r -= givescheck;
                    r -= hscore / 8000;
                    r = std::clamp(r, 0, newdepth - 1);
                }
                score = -negamax(-alpha - 1, -alpha, newdepth - r, ply + 1);
                if (score > alpha && r > 0) score = -negamax(-alpha - 1, -alpha, newdepth, ply + 1);
                // PVS: the null window said it's better, so get the real score
                if (score > alpha && score < beta) score = -negamax(-beta, -alpha, newdepth, ply + 1);
            }
            pos.unmake();
            if (stopp->load(std::memory_order_relaxed)) return 0;
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
                    if (alpha >= beta) {
                        if (!isnoisy(m)) updatequiets(m, quiets, nquiets, depth, ply);
                        break;
                    }
                }
            }
            if (!isnoisy(m) && nquiets < MAXMOVES) quiets[nquiets++] = m;
        }
        if (!legal) return inchk ? -MATE + ply : 0;
        ttab->store(pos.key(), ply, bestmv, best, staticeval, depth,
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
        for (auto& st : stackbuf) {
            st = Stack{};
            st.piece = NOPIECE;
            st.ch = hist.cont[12][0];
        }
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
            // helper threads skip a depth now and then so they explore different trees
            if (threadid > 0 && d > 2 && (d + threadid) % 3 == 0) continue;
            seldepth = 0;
            int score;
            if (d >= 4) {
                // aspiration window around last iteration's score, widened on failure
                int delta = 20;
                int alpha = std::max(rootscore - delta, -INF), beta = std::min(rootscore + delta, INF);
                while (true) {
                    score = negamax(alpha, beta, d, 0);
                    if (stopp->load()) break;
                    if (score <= alpha) {
                        beta = (alpha + beta) / 2;
                        alpha = std::max(score - delta, -INF);
                    } else if (score >= beta) {
                        beta = std::min(score + delta, INF);
                    } else {
                        break;
                    }
                    delta += delta / 2;
                }
            } else {
                score = negamax(-INF, INF, d, 0);
            }
            // a half-finished iteration is still usable: rootbest only changes
            // when a root move was searched all the way through
            if (stopp->load()) break;
            completed = d;
            rootscore = score;
            if (print && mainthread) {
                i64 ms = std::max<i64>(1, tm.elapsed());
                u64 n = totalnodes ? totalnodes() : nodes;
                uciout("info depth " + std::to_string(d) + " seldepth " + std::to_string(seldepth) +
                       " score " + scorestr(score) + " nodes " + std::to_string(n) +
                       " nps " + std::to_string(n * 1000 / ms) + " time " + std::to_string(ms) +
                       " hashfull " + std::to_string(ttab->hashfull()) + " pv " + pvstring());
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

// lazy smp: every thread searches the same root with the shared tt; thread 0 owns the clock and the answer
std::vector<std::unique_ptr<Searcher>> pool;
std::thread searchthread;
std::atomic<bool> searching{false};

} // namespace

void search::init() {
    for (int d = 1; d < 64; d++)
        for (int m = 1; m < 64; m++) lmrtab[d][m] = int(0.75 + std::log(d) * std::log(m) / 2.25);
    tt.resize(64);
    pool.clear();
    pool.push_back(std::make_unique<Searcher>());
}

void search::newgame() {
    wait();
    tt.clear();
    for (auto& p : pool) p->hist.clear();
}

void search::go(const Position& pos, const Limits& lim) {
    wait();
    stopflag = false;
    searching = true;
    while ((int)pool.size() < nthreads) pool.push_back(std::make_unique<Searcher>());
    while ((int)pool.size() > nthreads) pool.pop_back();
    for (int i = 0; i < nthreads; i++) {
        Searcher& s = *pool[i];
        std::memcpy(static_cast<void*>(&s.pos), &pos, sizeof(Position));
        s.lim = lim;
        s.tm.init(lim, pos.stm, moveoverhead);
        if (i > 0) s.tm.timed = false;   // helpers just run until thread 0 says stop
        s.print = i == 0;
        s.mainthread = i == 0;
        s.threadid = i;
        s.nodes = 0;
        s.totalnodes = nullptr;
    }
    pool[0]->totalnodes = [] {
        u64 n = 0;
        for (auto& p : pool) n += p->nodes;
        return n;
    };
    searchthread = std::thread([lim] {
        tt.newsearch();
        std::vector<std::thread> helpers;
        for (size_t i = 1; i < pool.size(); i++) helpers.emplace_back([i] { pool[i]->iterate(); });
        Searcher& s = *pool[0];
        s.iterate();
        // with "go infinite" we must not answer until told to stop
        while (lim.infinite && !stopflag.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        stopflag = true;
        for (auto& h : helpers) h.join();
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

int search::qsearchscore(const Position& pos) {
    // one searcher per thread, reused: they're big
    thread_local std::unique_ptr<Searcher> s;
    if (!s) s = std::make_unique<Searcher>();
    std::memcpy(static_cast<void*>(&s->pos), &pos, sizeof(Position));
    s->mainthread = false;
    s->nodes = 0;
    return s->qsearch(-INF, INF, 0);
}

// self-contained searcher with its own tt, for datagen (no uci output, no globals touched)
struct SearchHandle::Impl {
    Searcher s;
    TT table;
};

SearchHandle::SearchHandle(size_t ttmb) : p(std::make_unique<Impl>()) {
    p->table.resize(ttmb);
    p->s.ttab = &p->table;
}
SearchHandle::~SearchHandle() = default;

void SearchHandle::newgame() {
    p->table.clear();
    p->s.hist.clear();
}

SearchResult SearchHandle::search(const Position& pos, const Limits& lim) {
    Searcher& s = p->s;
    std::memcpy(static_cast<void*>(&s.pos), &pos, sizeof(Position));
    s.lim = lim;
    s.tm.init(lim, pos.stm, 0);
    s.print = false;
    s.mainthread = true;
    s.stopp = &s.ownstop;
    s.ownstop = false;
    p->table.newsearch();
    s.iterate();
    return {s.rootbest, s.rootscore, s.completed, s.nodes};
}
