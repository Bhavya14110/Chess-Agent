#include "position.h"
#include "movegen.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

const int seevalue[7] = {100, 320, 330, 500, 950, 20000, 0};

namespace zob {
u64 psq[12][64];
u64 castle[16];
u64 ep[8];
u64 side;

void init() {
    // splitmix64 with a fixed seed so keys are the same on every run
    u64 s = 0x2545F4914F6CDD1DULL;
    auto next = [&]() {
        u64 z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    };
    for (auto& p : psq)
        for (auto& k : p) k = next();
    for (auto& c : castle) c = next();
    for (auto& e : ep) e = next();
    side = next();
}
} // namespace zob

static int castlemask[64];
static struct CastleMaskInit {
    CastleMaskInit() {
        for (int& m : castlemask) m = 15;
        castlemask[0] &= ~WQ;
        castlemask[7] &= ~WK;
        castlemask[4] &= ~(WK | WQ);
        castlemask[56] &= ~BQ;
        castlemask[63] &= ~BK;
        castlemask[60] &= ~(BK | BQ);
    }
} castlemaskinit;

static const char* piecechars = "PNBRQKpnbrqk";

std::string sqname(int sq) {
    std::string s;
    s += char('a' + filex(sq));
    s += char('1' + rankx(sq));
    return s;
}

std::string movestr(Move m) {
    if (m == NOMOVE) return "0000";
    std::string s = sqname(mfrom(m)) + sqname(mto(m));
    if (ispromo(m)) s += "nbrq"[promotype(m) - KNIGHT];
    return s;
}

void Position::putpiece(int p, int sq) {
    pcs[p] |= sqbb(sq);
    occ[pcolor(p)] |= sqbb(sq);
    all |= sqbb(sq);
    board[sq] = u8(p);
}

void Position::delpiece(int sq) {
    int p = board[sq];
    pcs[p] &= ~sqbb(sq);
    occ[pcolor(p)] &= ~sqbb(sq);
    all &= ~sqbb(sq);
    board[sq] = NOPIECE;
}

void Position::movepiece(int from, int to) {
    int p = board[from];
    u64 ft = sqbb(from) | sqbb(to);
    pcs[p] ^= ft;
    occ[pcolor(p)] ^= ft;
    all ^= ft;
    board[to] = u8(p);
    board[from] = NOPIECE;
}

u64 Position::attackersto(int sq, u64 o) const {
    return (bb::pawnatt[BLACK][sq] & pcs[mkpiece(WHITE, PAWN)]) |
           (bb::pawnatt[WHITE][sq] & pcs[mkpiece(BLACK, PAWN)]) |
           (bb::knightatt[sq] & typebb(KNIGHT)) |
           (bb::kingatt[sq] & typebb(KING)) |
           (bb::batt(sq, o) & (typebb(BISHOP) | typebb(QUEEN))) |
           (bb::ratt(sq, o) & (typebb(ROOK) | typebb(QUEEN)));
}

void Position::setcheckinfo() {
    State& s = st[sp];
    int us = stm, them = stm ^ 1;
    int ksq = kingsq(us);
    s.checkers = attackersto(ksq, all) & occ[them];
    s.pinned = 0;
    u64 snipers = (bb::ratt(ksq, 0) & (bbof(them, ROOK) | bbof(them, QUEEN))) |
                  (bb::batt(ksq, 0) & (bbof(them, BISHOP) | bbof(them, QUEEN)));
    while (snipers) {
        int s2 = poplsb(snipers);
        u64 b = bb::between[ksq][s2] & all;
        if (b && !several(b) && (b & occ[us])) s.pinned |= b;
    }
}

bool Position::setfen(const std::string& fen) {
    std::memset(pcs, 0, sizeof(pcs));
    occ[0] = occ[1] = all = 0;
    for (auto& b : board) b = NOPIECE;
    sp = 0;
    State& s = st[0];
    s = State{};
    s.ep = NOSQ;
    s.mv = NOMOVE;
    s.captured = NOPIECE;

    std::istringstream in(fen);
    std::string placement, side, castling, eps;
    int hm = 0, fm = 1;
    in >> placement >> side >> castling >> eps;
    if (!(in >> hm)) hm = 0;
    if (!(in >> fm)) fm = 1;
    if (placement.empty()) return false;

    int r = 7, f = 0;
    for (char c : placement) {
        if (c == '/') {
            r--;
            f = 0;
        } else if (c >= '1' && c <= '8') {
            f += c - '0';
        } else {
            const char* p = std::strchr(piecechars, c);
            if (!p || f > 7 || r < 0) return false;
            putpiece(int(p - piecechars), mksq(f, r));
            f++;
        }
    }
    if (popcnt(pcs[mkpiece(WHITE, KING)]) != 1 || popcnt(pcs[mkpiece(BLACK, KING)]) != 1) return false;
    stm = (side == "b") ? BLACK : WHITE;

    s.castle = 0;
    for (char c : castling) {
        if (c == 'K') s.castle |= WK;
        else if (c == 'Q') s.castle |= WQ;
        else if (c == 'k') s.castle |= BK;
        else if (c == 'q') s.castle |= BQ;
    }
    // drop rights that can't be real (pieces not on their home squares)
    if (board[4] != mkpiece(WHITE, KING)) s.castle &= ~(WK | WQ);
    if (board[7] != mkpiece(WHITE, ROOK)) s.castle &= ~WK;
    if (board[0] != mkpiece(WHITE, ROOK)) s.castle &= ~WQ;
    if (board[60] != mkpiece(BLACK, KING)) s.castle &= ~(BK | BQ);
    if (board[63] != mkpiece(BLACK, ROOK)) s.castle &= ~BK;
    if (board[56] != mkpiece(BLACK, ROOK)) s.castle &= ~BQ;

    s.ep = NOSQ;
    if (eps.size() == 2 && eps[0] >= 'a' && eps[0] <= 'h' && (eps[1] == '3' || eps[1] == '6')) {
        int e = mksq(eps[0] - 'a', eps[1] - '1');
        // only keep it if a pawn can actually take, so hashes match make()
        if (bb::pawnatt[stm ^ 1][e] & bbof(stm, PAWN)) s.ep = e;
    }
    s.rule50 = hm;
    s.nullply = 0;
    fullmove0 = fm;
    startstm = stm;
    gameply = 0;

    s.key = s.pawnkey = 0;
    for (int sq = 0; sq < 64; sq++)
        if (board[sq] != NOPIECE) {
            s.key ^= zob::psq[board[sq]][sq];
            if (ptype(board[sq]) == PAWN) s.pawnkey ^= zob::psq[board[sq]][sq];
        }
    s.key ^= zob::castle[s.castle];
    if (s.ep != NOSQ) s.key ^= zob::ep[filex(s.ep)];
    if (stm == BLACK) s.key ^= zob::side;
    setcheckinfo();
    // side not to move can't be in check
    if (attacked(kingsq(stm ^ 1), stm)) return false;
    return true;
}

std::string Position::fen() const {
    std::string s;
    for (int r = 7; r >= 0; r--) {
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            int p = board[mksq(f, r)];
            if (p == NOPIECE) {
                empty++;
                continue;
            }
            if (empty) s += char('0' + empty), empty = 0;
            s += piecechars[p];
        }
        if (empty) s += char('0' + empty);
        if (r) s += '/';
    }
    s += stm == WHITE ? " w " : " b ";
    const State& c = cur();
    if (!c.castle) s += '-';
    if (c.castle & WK) s += 'K';
    if (c.castle & WQ) s += 'Q';
    if (c.castle & BK) s += 'k';
    if (c.castle & BQ) s += 'q';
    s += ' ';
    s += c.ep == NOSQ ? "-" : sqname(c.ep);
    s += ' ' + std::to_string(c.rule50) + ' ' + std::to_string(fullmove0 + (gameply + startstm) / 2);
    return s;
}

void Position::print() const {
    for (int r = 7; r >= 0; r--) {
        std::string line;
        for (int f = 0; f < 8; f++) {
            int p = board[mksq(f, r)];
            line += p == NOPIECE ? '.' : piecechars[p];
            line += ' ';
        }
        std::fprintf(stderr, "%s\n", line.c_str());
    }
    std::fprintf(stderr, "fen: %s\nkey: %016llx\n", fen().c_str(), (unsigned long long)key());
}

void Position::make(Move m) {
    const State& prev = st[sp];
    State& s = st[++sp];
    s.key = prev.key ^ zob::side;
    s.pawnkey = prev.pawnkey;
    s.castle = prev.castle;
    s.rule50 = prev.rule50 + 1;
    s.nullply = prev.nullply + 1;
    s.ep = NOSQ;
    s.mv = m;
    s.captured = NOPIECE;
    if (prev.ep != NOSQ) s.key ^= zob::ep[filex(prev.ep)];

    int us = stm, them = us ^ 1;
    int from = mfrom(m), to = mto(m), flag = mflag(m);
    int pc = board[from];

    if (flag == MEP) {
        int capsq = to ^ 8;
        s.captured = board[capsq];
        s.key ^= zob::psq[s.captured][capsq];
        s.pawnkey ^= zob::psq[s.captured][capsq];
        delpiece(capsq);
        s.rule50 = 0;
    } else if (iscap(m)) {
        s.captured = board[to];
        s.key ^= zob::psq[s.captured][to];
        if (ptype(s.captured) == PAWN) s.pawnkey ^= zob::psq[s.captured][to];
        delpiece(to);
        s.rule50 = 0;
    }

    if (flag == MOO || flag == MOOO) {
        int rf = flag == MOO ? to + 1 : to - 2;
        int rt = flag == MOO ? to - 1 : to + 1;
        int rk = board[rf];
        s.key ^= zob::psq[rk][rf] ^ zob::psq[rk][rt];
        movepiece(rf, rt);
    }

    s.key ^= zob::psq[pc][from] ^ zob::psq[pc][to];
    movepiece(from, to);

    if (ptype(pc) == PAWN) {
        s.rule50 = 0;
        s.pawnkey ^= zob::psq[pc][from] ^ zob::psq[pc][to];
        if (flag == MDOUBLE) {
            int e = (from + to) / 2;
            if (bb::pawnatt[us][e] & bbof(them, PAWN)) {
                s.ep = e;
                s.key ^= zob::ep[filex(e)];
            }
        } else if (ispromo(m)) {
            int np = mkpiece(us, promotype(m));
            s.key ^= zob::psq[pc][to] ^ zob::psq[np][to];
            s.pawnkey ^= zob::psq[pc][to];
            delpiece(to);
            putpiece(np, to);
        }
    }

    if (s.castle && (castlemask[from] & castlemask[to]) != 15) {
        s.key ^= zob::castle[s.castle];
        s.castle &= castlemask[from] & castlemask[to];
        s.key ^= zob::castle[s.castle];
    }

    stm = them;
    gameply++;
    setcheckinfo();
}

void Position::unmake() {
    const State& s = st[sp];
    Move m = s.mv;
    stm ^= 1;
    gameply--;
    int us = stm;
    int from = mfrom(m), to = mto(m), flag = mflag(m);

    if (ispromo(m)) {
        delpiece(to);
        putpiece(mkpiece(us, PAWN), to);
    }
    movepiece(to, from);
    if (flag == MOO || flag == MOOO) {
        int rf = flag == MOO ? to + 1 : to - 2;
        int rt = flag == MOO ? to - 1 : to + 1;
        movepiece(rt, rf);
    }
    if (flag == MEP) putpiece(s.captured, to ^ 8);
    else if (s.captured != NOPIECE) putpiece(s.captured, to);
    sp--;
}

void Position::makenull() {
    const State& prev = st[sp];
    State& s = st[++sp];
    s = prev;
    s.key ^= zob::side;
    if (prev.ep != NOSQ) s.key ^= zob::ep[filex(prev.ep)];
    s.ep = NOSQ;
    s.rule50 = prev.rule50 + 1;
    s.nullply = 0;
    s.mv = NOMOVE;
    s.captured = NOPIECE;
    stm ^= 1;
    gameply++;
    setcheckinfo();
}

void Position::unmakenull() {
    stm ^= 1;
    gameply--;
    sp--;
}

bool Position::cancastle(int right) const {
    if (!(cur().castle & right) || incheck()) return false;
    int them = stm ^ 1;
    // squares that must be empty, and squares the king walks over
    switch (right) {
    case WK: return !(all & 0x60ULL) && !attacked(5, them) && !attacked(6, them);
    case WQ: return !(all & 0x0EULL) && !attacked(3, them) && !attacked(2, them);
    case BK: return !(all & (0x60ULL << 56)) && !attacked(61, them) && !attacked(62, them);
    case BQ: return !(all & (0x0EULL << 56)) && !attacked(59, them) && !attacked(58, them);
    }
    return false;
}

// is a move (e.g. from the TT or a killer) a valid pseudo-legal move here?
bool Position::pseudolegal(Move m) const {
    if (m == NOMOVE) return false;
    int us = stm, them = us ^ 1;
    int from = mfrom(m), to = mto(m), flag = mflag(m);
    int pc = board[from];
    if (pc == NOPIECE || pcolor(pc) != us) return false;
    if (occ[us] & sqbb(to)) return false;
    int cap = board[to];
    int t = ptype(pc);

    if (flag == MEP) {
        return t == PAWN && to == cur().ep && (bb::pawnatt[us][from] & sqbb(to));
    }
    bool capflag = iscap(m);
    if (capflag != (cap != NOPIECE)) return false;
    if (cap != NOPIECE && ptype(cap) == KING) return false;

    if (t == PAWN) {
        bool lastrank = relrank(us, to) == 7;
        if (ispromo(m) != lastrank) return false;
        int push = us == WHITE ? 8 : -8;
        if (capflag) return bb::pawnatt[us][from] & sqbb(to);
        if (flag == MDOUBLE)
            return relrank(us, from) == 1 && to == from + 2 * push && !(all & (sqbb(from + push) | sqbb(to)));
        if (flag != MQUIET && !ispromo(m)) return false;
        return to == from + push && !(all & sqbb(to));
    }

    if (ispromo(m) || flag == MDOUBLE) return false;
    if (flag == MOO || flag == MOOO) {
        if (t != KING) return false;
        if (us == WHITE) {
            if (from != 4) return false;
            if (flag == MOO) return to == 6 && cancastle(WK);
            return to == 2 && cancastle(WQ);
        }
        if (from != 60) return false;
        if (flag == MOO) return to == 62 && cancastle(BK);
        return to == 58 && cancastle(BQ);
    }
    return bb::attacks(t, from, all) & sqbb(to);
}

// assumes pseudolegal
bool Position::legal(Move m) const {
    int us = stm, them = us ^ 1;
    int from = mfrom(m), to = mto(m);
    int ksq = kingsq(us);

    if (mflag(m) == MEP) {
        int capsq = to ^ 8;
        u64 o = (all ^ sqbb(from) ^ sqbb(capsq)) | sqbb(to);
        return !(attackersto(ksq, o) & occ[them] & ~sqbb(capsq));
    }
    if (ptype(board[from]) == KING) {
        if (iscastle(m)) return true;   // checked in cancastle
        return !(attackersto(to, all ^ sqbb(from)) & occ[them]);
    }
    u64 chk = cur().checkers;
    if (chk) {
        if (several(chk)) return false;
        int c = lsb(chk);
        if (!((bb::between[ksq][c] | chk) & sqbb(to))) return false;
    }
    if (cur().pinned & sqbb(from)) return bb::line[ksq][from] & sqbb(to);
    return true;
}

bool Position::givescheck(Move m) const {
    int us = stm, them = us ^ 1;
    int from = mfrom(m), to = mto(m), flag = mflag(m);
    int ksq = kingsq(them);
    u64 o = (all ^ sqbb(from)) | sqbb(to);
    u64 movers = sqbb(from);
    if (flag == MEP) o ^= sqbb(to ^ 8);
    int pt = ispromo(m) ? promotype(m) : ptype(board[from]);

    if (pt == PAWN) {
        if (bb::pawnatt[us][to] & sqbb(ksq)) return true;
    } else if (pt != KING && (bb::attacks(pt, to, o) & sqbb(ksq))) {
        return true;
    }
    if (flag == MOO || flag == MOOO) {
        int rf = flag == MOO ? to + 1 : to - 2;
        int rt = flag == MOO ? to - 1 : to + 1;
        o = (o ^ sqbb(rf)) | sqbb(rt);
        movers |= sqbb(rf);
        if (bb::ratt(rt, o) & sqbb(ksq)) return true;
    }
    // discovered checks from our other sliders
    u64 diag = (bbof(us, BISHOP) | bbof(us, QUEEN)) & ~movers;
    u64 orth = (bbof(us, ROOK) | bbof(us, QUEEN)) & ~movers;
    return (bb::batt(ksq, o) & diag) || (bb::ratt(ksq, o) & orth);
}

// static exchange: does this move win at least `threshold` material?
bool Position::see(Move m, int threshold) const {
    if (iscastle(m)) return 0 >= threshold;
    int from = mfrom(m), to = mto(m);
    int capval = mflag(m) == MEP ? seevalue[PAWN]
               : board[to] == NOPIECE ? 0 : seevalue[ptype(board[to])];
    int moverval = seevalue[ptype(board[from])];
    if (ispromo(m)) {
        capval += seevalue[promotype(m)] - seevalue[PAWN];
        moverval = seevalue[promotype(m)];
    }

    int swap = capval - threshold;
    if (swap < 0) return false;
    swap = moverval - swap;
    if (swap <= 0) return true;

    u64 o = all ^ sqbb(from) ^ sqbb(to);
    if (mflag(m) == MEP) o ^= sqbb(to ^ 8);
    u64 att = attackersto(to, o) & o;
    u64 diag = typebb(BISHOP) | typebb(QUEEN);
    u64 orth = typebb(ROOK) | typebb(QUEEN);
    int side = stm;
    int res = 1;

    while (true) {
        side ^= 1;
        att &= o;
        u64 mine = att & occ[side];
        if (!mine) break;
        res ^= 1;

        u64 b;
        if ((b = mine & typebb(PAWN))) {
            if ((swap = seevalue[PAWN] - swap) < res) break;
            o ^= sqbb(lsb(b));
            att |= bb::batt(to, o) & diag;
        } else if ((b = mine & typebb(KNIGHT))) {
            if ((swap = seevalue[KNIGHT] - swap) < res) break;
            o ^= sqbb(lsb(b));
        } else if ((b = mine & typebb(BISHOP))) {
            if ((swap = seevalue[BISHOP] - swap) < res) break;
            o ^= sqbb(lsb(b));
            att |= bb::batt(to, o) & diag;
        } else if ((b = mine & typebb(ROOK))) {
            if ((swap = seevalue[ROOK] - swap) < res) break;
            o ^= sqbb(lsb(b));
            att |= bb::ratt(to, o) & orth;
        } else if ((b = mine & typebb(QUEEN))) {
            if ((swap = seevalue[QUEEN] - swap) < res) break;
            o ^= sqbb(lsb(b));
            att |= (bb::batt(to, o) & diag) | (bb::ratt(to, o) & orth);
        } else {
            // only the king is left: it can take only if the other side has nothing more
            return (att & ~occ[side]) ? res ^ 1 : res;
        }
    }
    return res;
}

bool Position::isrepetition(int searchply) const {
    const State& s = st[sp];
    int n = std::min(s.rule50, s.nullply);
    if (n > sp) n = sp;
    int hits = 0;
    for (int i = 4; i <= n; i += 2) {
        if (st[sp - i].key == s.key) {
            // a repeat inside the search tree counts straight away, older ones need two
            if (i < searchply) return true;
            if (++hits == 2) return true;
        }
    }
    return false;
}

bool Position::insufficient() const {
    if (typebb(PAWN) | typebb(ROOK) | typebb(QUEEN)) return false;
    int minors = popcnt(typebb(KNIGHT) | typebb(BISHOP));
    if (minors <= 1) return true;
    // just bishops, all on the same colour squares
    if (!typebb(KNIGHT)) {
        u64 b = typebb(BISHOP);
        constexpr u64 dark = 0xAA55AA55AA55AA55ULL;
        if (!(b & dark) || !(b & ~dark)) return true;
    }
    return false;
}

bool Position::isdraw(int searchply) const {
    if (cur().rule50 >= 100) {
        if (!incheck()) return true;
        // checkmate on the 100th half-move still counts as mate
        MoveList ml;
        genlegal(*this, ml);
        return ml.n > 0;
    }
    return insufficient() || isrepetition(searchply);
}

Move Position::parsemove(const std::string& s) const {
    MoveList ml;
    genlegal(*this, ml);
    for (int i = 0; i < ml.n; i++)
        if (movestr(ml.mv[i]) == s) return ml.mv[i];
    return NOMOVE;
}
