// texel tuning of the hand-crafted eval, plus the quiet-position filter that feeds it.
// all labels are game results from human games; nothing here ever sees another engine.
#include "tune.h"
#include "eval.h"
#include "search.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Entry {
    u32 first;      // index into the coefficient arrays
    u16 count;
    u8 phase;
    u8 scale;
    i16 egextra;
    float result;   // 1 white win, 0.5 draw, 0 black win
};

struct Data {
    std::vector<Entry> pos;
    std::vector<u16> idx;
    std::vector<int8_t> coef;
};

bool parseline(const std::string& line, std::string& fen, float& res) {
    auto p = line.find(';');
    if (p == std::string::npos) return false;
    fen = line.substr(0, p);
    res = std::stof(line.substr(p + 1));
    return true;
}

double sigm(double k, double e) { return 1.0 / (1.0 + std::pow(10.0, -k * e / 400.0)); }

struct Tuner {
    Data d;
    std::vector<double> mg, eg;
    int nthreads;

    double evalof(const Entry& e) const {
        double m = 0, g = e.egextra;
        for (u32 j = e.first; j < e.first + e.count; j++) {
            m += coefat(j) * mg[d.idx[j]];
            g += coefat(j) * eg[d.idx[j]];
        }
        return (m * e.phase + g * (24 - e.phase) * e.scale / 128.0) / 24.0;
    }
    double coefat(u32 j) const { return d.coef[j]; }

    double error(double k) {
        std::vector<double> part(nthreads, 0);
        std::vector<std::thread> ts;
        size_t n = d.pos.size();
        for (int t = 0; t < nthreads; t++)
            ts.emplace_back([&, t] {
                double s = 0;
                for (size_t i = t; i < n; i += nthreads) {
                    double p = sigm(k, evalof(d.pos[i]));
                    s += (d.pos[i].result - p) * (d.pos[i].result - p);
                }
                part[t] = s;
            });
        for (auto& t : ts) t.join();
        double s = 0;
        for (double x : part) s += x;
        return s / double(n);
    }

    // golden-section search for the K that best fits the starting weights
    double fitk() {
        double lo = 0.1, hi = 3.0;
        const double g = (std::sqrt(5.0) - 1) / 2;
        double a = hi - g * (hi - lo), b = lo + g * (hi - lo);
        double fa = error(a), fb = error(b);
        for (int it = 0; it < 30; it++) {
            if (fa < fb) {
                hi = b; b = a; fb = fa; a = hi - g * (hi - lo); fa = error(a);
            } else {
                lo = a; a = b; fa = fb; b = lo + g * (hi - lo); fb = error(b);
            }
        }
        return (lo + hi) / 2;
    }

    void gradient(double k, std::vector<double>& gm, std::vector<double>& ge) {
        std::vector<std::vector<double>> pm(nthreads, std::vector<double>(NPARAMS, 0)),
            pe(nthreads, std::vector<double>(NPARAMS, 0));
        std::vector<std::thread> ts;
        size_t n = d.pos.size();
        const double ln10 = std::log(10.0);
        for (int t = 0; t < nthreads; t++)
            ts.emplace_back([&, t] {
                auto& lm = pm[t];
                auto& le = pe[t];
                for (size_t i = t; i < n; i += nthreads) {
                    const Entry& e = d.pos[i];
                    double p = sigm(k, evalof(e));
                    // d(err)/d(eval)
                    double g = -2.0 * (e.result - p) * p * (1 - p) * ln10 * k / 400.0;
                    double wm = g * e.phase / 24.0;
                    double we = g * (24 - e.phase) * e.scale / (128.0 * 24.0);
                    for (u32 j = e.first; j < e.first + e.count; j++) {
                        lm[d.idx[j]] += wm * d.coef[j];
                        le[d.idx[j]] += we * d.coef[j];
                    }
                }
            });
        for (auto& t : ts) t.join();
        gm.assign(NPARAMS, 0);
        ge.assign(NPARAMS, 0);
        for (int t = 0; t < nthreads; t++)
            for (int i = 0; i < NPARAMS; i++) {
                gm[i] += pm[t][i] / double(n);
                ge[i] += pe[t][i] / double(n);
            }
    }
};

} // namespace

void tune(const std::string& file, int epochs, int threads, double lr, const std::string& out) {
    Tuner tn;
    tn.nthreads = threads;
    std::ifstream in(file);
    std::string line, fen;
    Position pos;
    EvalTrace tr;
    size_t bad = 0;
    std::fprintf(stderr, "loading %s ...\n", file.c_str());
    while (std::getline(in, line)) {
        float res;
        if (!parseline(line, fen, res) || !pos.setfen(fen)) {
            bad++;
            continue;
        }
        evaluatetrace(pos, tr);
        Entry e;
        e.first = u32(tn.d.idx.size());
        e.count = 0;
        for (int i = 0; i < NPARAMS; i++) {
            int c = tr.coef[i][WHITE] - tr.coef[i][BLACK];
            if (!c) continue;
            tn.d.idx.push_back(u16(i));
            tn.d.coef.push_back(int8_t(std::clamp(c, -127, 127)));
            e.count++;
        }
        e.phase = u8(tr.phase);
        e.scale = u8(tr.scale);
        e.egextra = i16(tr.egextra);
        e.result = res;
        tn.d.pos.push_back(e);
        if (tn.d.pos.size() % 1000000 == 0) std::fprintf(stderr, "  %zu positions\n", tn.d.pos.size());
    }
    std::fprintf(stderr, "%zu positions loaded (%zu bad lines), %zu coefficients\n", tn.d.pos.size(), bad,
                 tn.d.idx.size());
    tn.mg.resize(NPARAMS);
    tn.eg.resize(NPARAMS);
    for (int i = 0; i < NPARAMS; i++) {
        tn.mg[i] = mgof(evp[i]);
        tn.eg[i] = egof(evp[i]);
    }

    // sanity: the linear model has to agree with the real eval (up to rounding)
    {
        double maxdiff = 0;
        std::ifstream in2(file);
        int n = 0;
        while (n < 2000 && std::getline(in2, line)) {
            float res;
            if (!parseline(line, fen, res) || !pos.setfen(fen)) continue;
            int real = evaluate(pos);
            if (pos.stm == BLACK) real = -real;
            double lin = tn.evalof(tn.d.pos[n]);
            maxdiff = std::max(maxdiff, std::abs(lin - real));
            n++;
        }
        std::fprintf(stderr, "linear model vs eval: max diff %.2f cp over %d positions\n", maxdiff, n);
    }

    double k = tn.fitk();
    std::fprintf(stderr, "fitted K = %.4f, start error %.6f\n", k, tn.error(k));

    // adam on all weights at once
    std::vector<double> m1(2 * NPARAMS, 0), v1(2 * NPARAMS, 0), gm, ge;
    const double b1 = 0.9, b2 = 0.999, eps = 1e-8;
    for (int ep = 1; ep <= epochs; ep++) {
        tn.gradient(k, gm, ge);
        for (int i = 0; i < 2 * NPARAMS; i++) {
            int p = i % NPARAMS;
            if (p == P_MAT + KING) continue;   // the king has no material value
            double g = i < NPARAMS ? gm[p] : ge[p];
            m1[i] = b1 * m1[i] + (1 - b1) * g;
            v1[i] = b2 * v1[i] + (1 - b2) * g * g;
            double mh = m1[i] / (1 - std::pow(b1, ep)), vh = v1[i] / (1 - std::pow(b2, ep));
            double step = lr * mh / (std::sqrt(vh) + eps);
            (i < NPARAMS ? tn.mg[p] : tn.eg[p]) -= step;
        }
        if (ep % 25 == 0 || ep == epochs) {
            std::fprintf(stderr, "epoch %d error %.6f\n", ep, tn.error(k));
            for (int i = 0; i < NPARAMS; i++) evp[i] = S(int(std::lround(tn.mg[i])), int(std::lround(tn.eg[i])));
            writeparams(out.c_str());
        }
    }
    std::fprintf(stderr, "done, final error %.6f, written to %s\n", tn.error(k), out.c_str());
}

// keep positions where our own qsearch agrees with the static eval and nobody is in check
void quietfilter(const std::string& infile, const std::string& outfile, int threads) {
    std::ifstream in(infile);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
        if (!line.empty()) lines.push_back(line);
    std::atomic<size_t> next{0}, kept{0};
    std::mutex mtx;
    std::ofstream out(outfile);
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; t++)
        ts.emplace_back([&] {
            Position pos;
            std::string fen, buf;
            while (true) {
                size_t i = next.fetch_add(1);
                if (i >= lines.size()) break;
                float res;
                if (!parseline(lines[i], fen, res) || !pos.setfen(fen) || pos.incheck()) continue;
                if (search::qsearchscore(pos) != evaluate(pos)) continue;
                buf += lines[i];
                buf += '\n';
                kept++;
                if (buf.size() > (1 << 20)) {
                    std::lock_guard<std::mutex> lk(mtx);
                    out << buf;
                    buf.clear();
                }
            }
            std::lock_guard<std::mutex> lk(mtx);
            out << buf;
        });
    for (auto& t : ts) t.join();
    std::fprintf(stderr, "quiet filter: %zu of %zu positions kept\n", kept.load(), lines.size());
}
