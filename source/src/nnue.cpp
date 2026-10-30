#include "nnue.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#if defined(__AVX2__)
#include <immintrin.h>
#endif

#if defined(EMBED_NET)
// the net gets baked into the binary at build time, so there's no file to lose
#define STR2(x) #x
#define STR(x) STR2(x)
__asm__(".section .rdata,\"dr\"\n"
        ".p2align 6\n"
        ".globl corvus_net_data\n"
        "corvus_net_data:\n"
        ".incbin \"" STR(EMBED_NET) "\"\n"
        ".globl corvus_net_end\n"
        "corvus_net_end:\n"
        ".byte 0\n"
        ".text\n");
extern "C" const unsigned char corvus_net_data[];
extern "C" const unsigned char corvus_net_end[];
#endif

namespace nnue {

bool loaded = false;
bool use = true;
int H = 0;

static std::vector<int16_t> ftw;   // [768][H]
static std::vector<int16_t> ftb;   // [H]
static std::vector<int16_t> outw;  // [2H]
static int32_t outb = 0;

static bool parse(const unsigned char* d, size_t n) {
    if (n < 12 || std::memcmp(d, "CVNN", 4)) return false;
    u32 ver, h;
    std::memcpy(&ver, d + 4, 4);
    std::memcpy(&h, d + 8, 4);
    if (ver != 1 || h == 0 || h > MAXH || h % 16) return false;
    size_t need = 12 + (768 * h + h + 2 * h) * 2 + 4;
    if (n < need) return false;
    H = int(h);
    ftw.resize(768 * h);
    ftb.resize(h);
    outw.resize(2 * h);
    const unsigned char* p = d + 12;
    std::memcpy(ftw.data(), p, ftw.size() * 2);
    p += ftw.size() * 2;
    std::memcpy(ftb.data(), p, ftb.size() * 2);
    p += ftb.size() * 2;
    std::memcpy(outw.data(), p, outw.size() * 2);
    p += outw.size() * 2;
    std::memcpy(&outb, p, 4);
    loaded = true;
    return true;
}

bool loadfile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parse(buf.data(), buf.size());
}

bool loadembedded() {
#if defined(EMBED_NET)
    return parse(corvus_net_data, size_t(corvus_net_end - corvus_net_data));
#else
    return false;
#endif
}

void refresh(const Position& pos, Accumulator& acc) {
    for (int p = 0; p < 2; p++) std::memcpy(acc.v[p], ftb.data(), H * 2);
    u64 b = pos.all;
    while (b) {
        int sq = poplsb(b);
        add(acc, pos.board[sq], sq);
    }
}

void add(Accumulator& acc, int p, int sq) {
    for (int persp = 0; persp < 2; persp++) {
        const int16_t* w = &ftw[size_t(feature(persp, p, sq)) * H];
        int16_t* a = acc.v[persp];
        for (int i = 0; i < H; i++) a[i] += w[i];
    }
}

void sub(Accumulator& acc, int p, int sq) {
    for (int persp = 0; persp < 2; persp++) {
        const int16_t* w = &ftw[size_t(feature(persp, p, sq)) * H];
        int16_t* a = acc.v[persp];
        for (int i = 0; i < H; i++) a[i] -= w[i];
    }
}

void update(const Accumulator& parent, Accumulator& child, int addp1, int addsq1, int subp1, int subsq1,
            int addp2, int addsq2, int subp2, int subsq2) {
    for (int persp = 0; persp < 2; persp++) {
        const int16_t* a1 = &ftw[size_t(feature(persp, addp1, addsq1)) * H];
        const int16_t* s1 = &ftw[size_t(feature(persp, subp1, subsq1)) * H];
        const int16_t* src = parent.v[persp];
        int16_t* dst = child.v[persp];
        if (addp2 == NOPIECE && subp2 == NOPIECE) {
            for (int i = 0; i < H; i++) dst[i] = src[i] + a1[i] - s1[i];
        } else if (addp2 == NOPIECE) {
            const int16_t* s2 = &ftw[size_t(feature(persp, subp2, subsq2)) * H];
            for (int i = 0; i < H; i++) dst[i] = src[i] + a1[i] - s1[i] - s2[i];
        } else {
            // castling: two adds, two subs
            const int16_t* a2 = &ftw[size_t(feature(persp, addp2, addsq2)) * H];
            const int16_t* s2 = &ftw[size_t(feature(persp, subp2, subsq2)) * H];
            for (int i = 0; i < H; i++) dst[i] = src[i] + a1[i] - s1[i] + a2[i] - s2[i];
        }
    }
}

int evaluate(const Accumulator& acc, int stm) {
    const int16_t* us = acc.v[stm];
    const int16_t* them = acc.v[stm ^ 1];
    const int16_t* w0 = outw.data();
    const int16_t* w1 = outw.data() + H;
    i64 sum = 0;
#if defined(__AVX2__)
    // clamp to [0, QA], v * w fits in int16 because |w| <= 127, then madd with v gives v*v*w in int32
    __m256i zero = _mm256_setzero_si256(), qa = _mm256_set1_epi16(QA);
    __m256i s = _mm256_setzero_si256();
    for (int i = 0; i < H; i += 16) {
        __m256i a = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i*)(us + i)), zero), qa);
        __m256i w = _mm256_loadu_si256((const __m256i*)(w0 + i));
        s = _mm256_add_epi32(s, _mm256_madd_epi16(_mm256_mullo_epi16(a, w), a));
        __m256i b = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i*)(them + i)), zero), qa);
        __m256i w2 = _mm256_loadu_si256((const __m256i*)(w1 + i));
        s = _mm256_add_epi32(s, _mm256_madd_epi16(_mm256_mullo_epi16(b, w2), b));
    }
    __m128i lo = _mm256_castsi256_si128(s), hi = _mm256_extracti128_si256(s, 1);
    __m128i t = _mm_add_epi32(lo, hi);
    t = _mm_add_epi32(t, _mm_shuffle_epi32(t, 0x4E));
    t = _mm_add_epi32(t, _mm_shuffle_epi32(t, 0xB1));
    sum = _mm_cvtsi128_si32(t);
#else
    for (int i = 0; i < H; i++) {
        int a = std::min(std::max<int>(us[i], 0), QA);
        int b = std::min(std::max<int>(them[i], 0), QA);
        sum += a * a * w0[i] + b * b * w1[i];
    }
#endif
    // same integer order as the python reference: truncate toward zero at each step
    i64 t1 = sum / QA;
    t1 += outb;
    return int(t1 * SCALE / (QA * QB));
}

// accumulator of the child from the parent, using what make() left in the child's state
static void apply(const State& s, const Accumulator& parent, Accumulator& child) {
    Move m = s.mv;
    if (m == NOMOVE) {   // null move: same pieces
        for (int p = 0; p < 2; p++) std::memcpy(child.v[p], parent.v[p], H * 2);
        return;
    }
    int from = mfrom(m), to = mto(m), flag = mflag(m), pc = s.moved, us = pcolor(pc);
    if (flag == MOO || flag == MOOO) {
        int rf = flag == MOO ? to + 1 : to - 2, rt = flag == MOO ? to - 1 : to + 1, rk = mkpiece(us, ROOK);
        update(parent, child, pc, to, pc, from, rk, rt, rk, rf);
        return;
    }
    int newp = ispromo(m) ? mkpiece(us, promotype(m)) : pc;
    int capsq = flag == MEP ? to ^ 8 : to;
    update(parent, child, newp, to, pc, from, NOPIECE, 0, s.captured, s.captured == NOPIECE ? 0 : capsq);
}

void AccStack::reset(const Position& pos) {
    rootsp = pos.sp;
    for (auto& k : key) k = 0;
    if (!loaded) return;
    refresh(pos, acc[0]);
    key[0] = pos.key();
}

int AccStack::eval(const Position& pos) {
    int idx = pos.sp - rootsp;
    if (idx < 0 || idx >= MAXPLY + 8) {   // shouldn't happen, but never index out of range
        thread_local Accumulator tmp;
        refresh(pos, tmp);
        return evaluate(tmp, pos.stm);
    }
    int j = idx;
    while (j > 0 && key[j] != pos.st[rootsp + j].key) j--;
    if (j == 0 && key[0] != pos.st[rootsp].key) {
        // root slot is stale only if someone forgot reset(); rebuild from the current board then
        refresh(pos, acc[idx]);
        key[idx] = pos.key();
        j = idx;
    }
    for (int k = j + 1; k <= idx; k++) {
        apply(pos.st[rootsp + k], acc[k - 1], acc[k]);
        key[k] = pos.st[rootsp + k].key;
    }
    int v = evaluate(acc[idx], pos.stm);
    // fade towards a draw as the 50-move counter runs up
    return v * (200 - pos.cur().rule50) / 200;
}

} // namespace nnue

