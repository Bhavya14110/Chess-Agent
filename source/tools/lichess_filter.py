"""Stream a Lichess monthly standard-rated .pgn.zst over HTTP and keep only strong,
normally-terminated, human games. Nothing compressed is ever written to disk: the file is
fetched as consecutive HTTP Range segments (a few in parallel, kept in RAM in a small sliding
window), fed in order to a streaming zstd decompressor, split into games and filtered.

Filters (all must hold):
  * WhiteElo >= --min-elo and BlackElo >= --min-elo (default 2200)
  * base time from TimeControl ("180+2" -> 180) >= --min-base (default 180 s);
    "-" (correspondence) is rejected
  * Termination == "Normal" (rejects "Time forfeit", "Abandoned", "Rules infraction", ...)
  * Result in {1-0, 0-1, 1/2-1/2}
  * Variant missing or "Standard", and no SetUp/FEN header (game starts from the initial position)

Kept games are written with their tag pairs and a re-built movetext that contains ONLY move
numbers, SAN moves and the result: every {...} comment ([%eval], [%clk], ...), every NAG /
annotation glyph (!, ?, !?, ?!, $n -- on Lichess these come from computer analysis), and any
(...) variation are removed.

Robustness: each Range segment is retried with exponential backoff (default 12 attempts) on
any network error, short read, or bad status, so connection drops are resumed transparently
at the exact byte offset. If the server does not advertise byte ranges, the script falls back
to one plain streaming GET (no resume possible -- a network error then ends the run, but all
games kept so far are already flushed to --out). A run cannot be resumed across processes
(the zstd frame state lives in memory); just re-run it.

usage:
  python source/tools/lichess_filter.py --url https://database.lichess.org/standard/lichess_db_standard_rated_2026-09.pgn.zst \
      --out data/lichess/filtered.pgn --max-games 1000000 [--max-seconds 9000] [--stats data/lichess/filter_stats.json]
--url may also be a local .pgn.zst path (handy for tests). Create the file given by --stop-file
to make a running job stop cleanly (flush + stats) at the next game boundary.
"""
import argparse, io, json, os, re, sys, threading, time
from collections import Counter, deque
from concurrent.futures import ThreadPoolExecutor

import zstandard

try:
    import requests
except ImportError:  # urllib fallback
    requests = None
import urllib.request


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, file=sys.stderr, flush=True)


# ----------------------------------------------------------------------------- HTTP input
class _Resp:
    """minimal wrapper so urllib and requests look the same"""
    def __init__(self, status, headers, body):
        self.status, self.headers, self.body = status, headers, body


def _http_get(url, headers, timeout, sess=None):
    if requests is not None:
        r = (sess or requests).get(url, headers=headers, timeout=timeout)
        return _Resp(r.status_code, {k.lower(): v for k, v in r.headers.items()}, r.content)
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=timeout[1]) as r:
        return _Resp(r.status, {k.lower(): v for k, v in r.headers.items()}, r.read())


def _http_head(url):
    if requests is not None:
        r = requests.head(url, allow_redirects=True, timeout=30)
        return r.status_code, {k.lower(): v for k, v in r.headers.items()}, r.url
    req = urllib.request.Request(url, method="HEAD")
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.status, {k.lower(): v for k, v in r.headers.items()}, r.geturl()


class RangeStream(io.RawIOBase):
    """Sequential reader over an HTTP resource built from parallel, retried Range requests."""

    def __init__(self, url, size, conns=4, seg=8 << 20, ahead=3, retries=12):
        self.url, self.size, self.seg, self.retries = url, size, seg, retries
        self.pool = ThreadPoolExecutor(conns)
        self.window = conns * ahead
        self.futs = deque()
        self.next_start = 0
        self.buf, self.off = b"", 0
        self.pos = 0  # compressed bytes handed to the decompressor
        self.tls = threading.local()
        self.retried = 0
        self._fill()

    def _sess(self):
        if requests is None:
            return None
        s = getattr(self.tls, "s", None)
        if s is None:
            s = self.tls.s = requests.Session()
        return s

    def _fetch(self, start, end):
        last = None
        for attempt in range(self.retries):
            try:
                r = _http_get(self.url, {"Range": f"bytes={start}-{end}"}, (20, 60), self._sess())
                if r.status != 206:
                    raise IOError(f"HTTP {r.status} for range {start}-{end}")
                cr = r.headers.get("content-range", "")
                if not cr.startswith(f"bytes {start}-"):
                    raise IOError(f"bad Content-Range {cr!r}")
                if len(r.body) != end - start + 1:
                    raise IOError(f"short read {len(r.body)} of {end - start + 1}")
                return r.body
            except Exception as e:  # network hiccup: back off and retry this segment
                last = e
                self.retried += 1
                wait = min(60, 2 ** attempt)
                log(f"segment {start}-{end} attempt {attempt + 1} failed ({e}); retry in {wait}s")
                time.sleep(wait)
        raise IOError(f"giving up on segment {start}-{end}: {last}")

    def _fill(self):
        while len(self.futs) < self.window and self.next_start < self.size:
            s = self.next_start
            e = min(self.size, s + self.seg) - 1
            self.futs.append(self.pool.submit(self._fetch, s, e))
            self.next_start = e + 1

    def readable(self):
        return True

    def readinto(self, b):
        if self.off >= len(self.buf):
            if not self.futs:
                return 0
            self.buf, self.off = self.futs.popleft().result(), 0
            self._fill()
        n = min(len(b), len(self.buf) - self.off)
        b[:n] = self.buf[self.off:self.off + n]
        self.off += n
        self.pos += n
        return n

    def close(self):
        for f in self.futs:
            f.cancel()
        self.pool.shutdown(wait=False, cancel_futures=True)
        super().close()


class PlainStream(io.RawIOBase):
    """Fallback: one streaming GET, no resume. Also used for local files."""

    def __init__(self, fobj):
        self.f, self.pos, self.retried = fobj, 0, 0

    def readable(self):
        return True

    def readinto(self, b):
        data = self.f.read(len(b))
        n = len(data)
        b[:n] = data
        self.pos += n
        return n


def open_source(url, conns, seg):
    if os.path.exists(url):
        return PlainStream(open(url, "rb")), os.path.getsize(url)
    status, hdr, final = _http_head(url)
    if status != 200:
        raise SystemExit(f"HEAD {url} -> HTTP {status}")
    size = int(hdr.get("content-length", 0))
    if hdr.get("accept-ranges", "").lower() == "bytes" and size > 0:
        log(f"{final}: {size / 1e9:.2f} GB, byte ranges supported -> {conns} parallel segment fetchers")
        return RangeStream(final, size, conns=conns, seg=seg), size
    log("server does not advertise byte ranges: plain streaming, no resume on network errors")
    if requests is not None:
        r = requests.get(final, stream=True, timeout=(20, 120))
        r.raise_for_status()
        return PlainStream(r.raw), size
    return PlainStream(urllib.request.urlopen(final, timeout=120)), size


# ----------------------------------------------------------------------------- PGN filtering
TAG_RE = re.compile(rb'^\[(\w+)\s+"(.*)"\]\s*$')
COMMENT_RE = re.compile(rb"\{[^}]*\}")
SEMI_RE = re.compile(rb";[^\n]*")
MOVENUM_RE = re.compile(rb"^\d+\.(\.\.)?$")
SAN_RE = re.compile(rb"^([NBRQK]?[a-h]?[1-8]?x?[a-h][1-8](=[NBRQ])?|O-O(-O)?)[+#]?")
RESULTS = {b"1-0", b"0-1", b"1/2-1/2"}


def strip_variations(s):
    if b"(" not in s:
        return s
    out, depth = bytearray(), 0
    for ch in s:
        if ch == 40:      # (
            depth += 1
        elif ch == 41:    # )
            depth = max(0, depth - 1)
        elif depth == 0:
            out.append(ch)
    return bytes(out)


def clean_movetext(mt, result):
    """-> b'1. e4 e5 2. Nf3 ... 1-0' with nothing but SAN moves; None if movetext is unusable"""
    mt = COMMENT_RE.sub(b" ", mt)
    mt = SEMI_RE.sub(b" ", mt)
    mt = strip_variations(mt)
    sans = []
    for tok in mt.split():
        if MOVENUM_RE.match(tok) or tok in RESULTS or tok == b"*" or tok.startswith(b"$"):
            continue
        # a token may still carry a move number glued on ("12.Nf3") or glyphs ("Nf3?!")
        tok = re.sub(rb"^\d+\.+", b"", tok)
        if not tok:
            continue
        m = SAN_RE.match(tok)
        if not m:
            return None
        sans.append(m.group(0))
    if not sans:
        return None
    parts = []
    for i, s in enumerate(sans):
        if i % 2 == 0:
            parts.append(b"%d. %s" % (i // 2 + 1, s))
        else:
            parts.append(s)
    parts.append(result)
    # wrap lines at ~80 columns like most PGN writers
    lines, cur = [], b""
    for p in parts:
        if cur and len(cur) + 1 + len(p) > 79:
            lines.append(cur)
            cur = p
        else:
            cur = cur + b" " + p if cur else p
    lines.append(cur)
    return b"\n".join(lines)


def _int(v):
    try:
        return int(v)
    except ValueError:
        return None


def filter_game(g, a, why):
    """g: one game as bytes (tags + blank line + movetext). Returns cleaned bytes or None."""
    # fast reject on Elo before parsing anything else (most games fail here)
    for key in (b'[WhiteElo "', b'[BlackElo "'):
        i = g.find(key)
        if i < 0:
            why["no_elo"] += 1
            return None
        j = g.find(b'"', i + len(key))
        e = _int(g[i + len(key):j])
        if e is None or e < a.min_elo:
            why["elo"] += 1
            return None
    sep = g.find(b"\n\n")
    if sep < 0:
        why["malformed"] += 1
        return None
    head, mt = g[:sep], g[sep + 2:]
    tags, tag_lines = {}, []
    for line in head.split(b"\n"):
        line = line.strip()
        if not line:
            continue
        m = TAG_RE.match(line)
        if not m:
            why["malformed"] += 1
            return None
        tags[m.group(1)] = m.group(2)
        tag_lines.append(line)
    tc = tags.get(b"TimeControl", b"-")
    base = _int(tc.split(b"+")[0]) if tc not in (b"-", b"?") else None
    if base is None or base < a.min_base:
        why["time_control"] += 1
        return None
    if tags.get(b"Termination") != b"Normal":
        why["termination"] += 1
        return None
    res = tags.get(b"Result")
    if res not in RESULTS:
        why["result"] += 1
        return None
    if tags.get(b"Variant", b"Standard") != b"Standard" or b"FEN" in tags or b"SetUp" in tags:
        why["variant"] += 1
        return None
    clean = clean_movetext(mt, res)
    if clean is None:
        why["bad_movetext"] += 1
        return None
    return b"\n".join(tag_lines) + b"\n\n" + clean + b"\n\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--url", required=True, help="http(s) URL of a Lichess .pgn.zst (or a local path)")
    ap.add_argument("--out", required=True, help="output PGN")
    ap.add_argument("--max-games", type=int, default=0, help="stop after N kept games (0 = no limit)")
    ap.add_argument("--max-seconds", type=float, default=0, help="stop cleanly after this wall time (0 = no limit)")
    ap.add_argument("--min-elo", type=int, default=2200)
    ap.add_argument("--min-base", type=int, default=180, help="minimum base time in seconds")
    ap.add_argument("--conns", type=int, default=4, help="parallel HTTP range fetchers")
    ap.add_argument("--segment-mb", type=int, default=8)
    ap.add_argument("--log-every", type=int, default=100_000, help="progress line every N scanned games")
    ap.add_argument("--stats", help="write final counts as JSON here")
    ap.add_argument("--stop-file", help="if this file appears, stop cleanly")
    a = ap.parse_args()

    t0 = time.time()
    raw, size = open_source(a.url, a.conns, a.segment_mb << 20)
    dctx = zstandard.ZstdDecompressor(max_window_size=1 << 31)
    reader = dctx.stream_reader(raw, read_size=1 << 20, read_across_frames=True)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    out = open(a.out, "wb")

    scanned = kept = 0
    why = Counter()
    pending = b""
    next_log = a.log_every
    stop_reason = "end of file"
    marker = b"\n[Event "

    def progress(final=False):
        el = time.time() - t0
        mb = raw.pos / 1e6
        pct = 100.0 * raw.pos / size if size else 0.0
        log(f"{'DONE ' if final else ''}scanned {scanned:,} kept {kept:,} ({100.0 * kept / max(1, scanned):.2f}%) "
            f"| {mb:,.0f} MB compressed ({pct:.1f}%) {mb / max(el, 1e-9):.2f} MB/s | {el / 60:.1f} min"
            f" | retries {raw.retried}")

    def handle(g):
        nonlocal scanned, kept
        if not g.strip():
            return
        scanned += 1
        r = filter_game(g, a, why)
        if r is not None:
            out.write(r)
            kept += 1

    exit_code = 0
    try:
        done = False
        while not done:
            chunk = reader.read(4 << 20)
            if not chunk:
                if pending.strip():
                    handle(pending.replace(b"\r\n", b"\n"))
                break
            data = (pending + chunk).replace(b"\r\n", b"\n")
            # games start with "[Event "; everything before the last game start is complete
            last = data.rfind(marker)
            if last < 0:
                pending = data
                continue
            body, pending = data[:last + 1], data[last + 1:]
            start = 0
            while True:
                nxt = body.find(marker, start)
                end = len(body) if nxt < 0 else nxt + 1
                handle(body[start:end])
                if nxt < 0:
                    break
                start = nxt + 1
                if a.max_games and kept >= a.max_games:
                    stop_reason = f"--max-games {a.max_games} reached"
                    done = True
                    break
            if a.max_games and kept >= a.max_games:
                stop_reason = f"--max-games {a.max_games} reached"
                done = True
            if scanned >= next_log:
                progress()
                out.flush()
                next_log = (scanned // a.log_every + 1) * a.log_every
            if a.max_seconds and time.time() - t0 > a.max_seconds:
                stop_reason = f"--max-seconds {a.max_seconds:g} reached"
                done = True
            if a.stop_file and os.path.exists(a.stop_file):
                stop_reason = "stop file"
                done = True
    except KeyboardInterrupt:
        stop_reason = "interrupted"
    except Exception as e:  # report what we have; non-zero exit code
        stop_reason = f"error: {type(e).__name__}: {e}"
        exit_code = 1
    finally:
        out.close()
        progress(final=True)
        log(f"stop reason: {stop_reason}; rejects: {dict(why)}")
        stats = {
            "url": a.url, "out": a.out, "games_scanned": scanned, "games_kept": kept,
            "compressed_bytes_read": raw.pos, "compressed_size": size,
            "fraction_of_file": raw.pos / size if size else None,
            "rejects": dict(why), "stop_reason": stop_reason,
            "filters": {"min_elo_both": a.min_elo, "min_base_seconds": a.min_base,
                        "termination": "Normal", "results": ["1-0", "0-1", "1/2-1/2"],
                        "variant": "Standard, no SetUp/FEN"},
            "wall_seconds": round(time.time() - t0, 1),
            "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(t0)),
        }
        if a.stats:
            with open(a.stats, "w") as f:
                json.dump(stats, f, indent=2)
        try:
            raw.close()
        except Exception:
            pass
        os._exit(exit_code)  # don't wait for in-flight range fetches


if __name__ == "__main__":
    main()
