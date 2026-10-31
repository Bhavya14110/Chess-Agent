"""Check 3: review the live lichess-bot config.yml against the competition requirements and the repo template.
The token is never printed: the `token` key is dropped right after loading, and the raw diff skips token lines.

usage: python check3_config.py [LIVE_CONFIG] [TEMPLATE]
"""
import difflib
import os
import re
import sys

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
live_path = sys.argv[1] if len(sys.argv) > 1 else "C:/Users/Bhavya/Downloads/lichess-bot-src/config.yml"
tmpl_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(REPO, "lichess-bot", "config.example.yml")
TOKEN_LINE = re.compile(r"^\s*token\s*:|lip_", re.I)
ALLOWED_DIFF = {"token", "engine.uci_options.Threads", "engine.uci_options.Hash"}


def load(path):
    with open(path, encoding="utf-8") as f:
        d = yaml.safe_load(f)
    d.pop("token", None)          # never keep the secret in memory longer than needed
    return d


def flat(d, pre=""):
    out = {}
    for k, v in d.items():
        key = f"{pre}{k}"
        if isinstance(v, dict):
            out.update(flat(v, key + "."))
        else:
            out[key] = v
    return out


live, tmpl = load(live_path), load(tmpl_path)
L = flat(live)
results = []   # (status, item, detail)


def need(item, cond, detail, status_if_bad="FAIL"):
    results.append(("PASS" if cond else status_if_bad, item, detail))


g = lambda k, default=None: L.get(k, default)  # noqa: E731
need("protocol uci, ponder off", g("engine.protocol") == "uci" and g("engine.ponder") is False,
     f"protocol={g('engine.protocol')} ponder={g('engine.ponder')}")
need("engine is our Corvus.exe", g("engine.name") == "Corvus.exe"
     and os.path.isfile(os.path.join(g("engine.dir", ""), "Corvus.exe")),
     f"dir={g('engine.dir')} name={g('engine.name')} working_dir={g('engine.working_dir')}")
need("polyglot book disabled", g("engine.polyglot.enabled") is False and not g("engine.polyglot.book.standard"),
     f"enabled={g('engine.polyglot.enabled')} book={g('engine.polyglot.book.standard')} max_depth={g('engine.polyglot.max_depth')}")
for src in ("chessdb_book", "lichess_cloud_analysis", "lichess_opening_explorer", "online_egtb"):
    need(f"online_moves.{src} disabled", g(f"engine.online_moves.{src}.enabled") is False,
         f"enabled={g(f'engine.online_moves.{src}.enabled')}")
for tb in ("syzygy", "gaviota"):
    need(f"{tb} disabled", g(f"engine.lichess_bot_tbs.{tb}.enabled") is False and not g(f"engine.lichess_bot_tbs.{tb}.paths"),
         f"enabled={g(f'engine.lichess_bot_tbs.{tb}.enabled')} paths={g(f'engine.lichess_bot_tbs.{tb}.paths')}")
need("no go_commands (engine manages its own clock)", not g("engine.uci_options.go_commands"),
     f"go_commands={g('engine.uci_options.go_commands')}")
need("standard only", g("challenge.variants") == ["standard"], f"variants={g('challenge.variants')}")
need("casual only", g("challenge.modes") == ["casual"], f"modes={g('challenge.modes')}")
tc_ok = (g("challenge.min_base") == 180 and g("challenge.max_base") == 600 and g("challenge.min_increment") == 2
         and g("challenge.max_increment") == 5 and set(g("challenge.time_controls", [])) <= {"blitz", "rapid"})
need("only 3+2 .. 10+5 accepted", tc_ok,
     f"base {g('challenge.min_base')}-{g('challenge.max_base')} s, inc {g('challenge.min_increment')}-"
     f"{g('challenge.max_increment')} s, speeds {g('challenge.time_controls')}")
need("matchmaking off", g("matchmaking.allow_matchmaking") is False, f"allow_matchmaking={g('matchmaking.allow_matchmaking')}")
need("concurrency 1", g("challenge.concurrency") == 1, f"concurrency={g('challenge.concurrency')}")
greet = {k: g(f"greeting.{k}") for k in ("hello", "goodbye", "hello_spectators", "goodbye_spectators")}
rude = re.compile(r"\b(noob|idiot|stupid|loser|gg ez|ez|trash|lol)\b", re.I)
need("chat messages courteous", all(isinstance(v, str) and not rude.search(v) for v in greet.values()), str(greet))
rs, rm = g("engine.draw_or_resign.resign_score"), g("engine.draw_or_resign.resign_moves")
need("resign not too eager", not g("engine.draw_or_resign.resign_enabled") or (rs <= -800 and rm >= 5),
     f"resign_enabled={g('engine.draw_or_resign.resign_enabled')} score<={rs}cp for {rm} own moves", "RISK")
ds, dm, dp = (g(f"engine.draw_or_resign.offer_draw_{k}") for k in ("score", "moves", "pieces"))
need("draw offers not too eager", not g("engine.draw_or_resign.offer_draw_enabled") or (abs(ds) <= 25 and dm >= 5 and dp <= 16),
     f"offer_draw_enabled={g('engine.draw_or_resign.offer_draw_enabled')} |eval|<={ds}cp for {dm} moves, <= {dp} men", "RISK")
need("no tablebase resign/draw", not g("engine.draw_or_resign.resign_for_egtb_minus_two")
     and not g("engine.draw_or_resign.offer_draw_for_egtb_zero"), "egtb-based resign/draw off")
need("no fake think time / takebacks", g("fake_think_time") is False and g("max_takebacks_accepted") == 0
     and g("rate_limiting_delay") == 0,
     f"fake_think_time={g('fake_think_time')} takebacks={g('max_takebacks_accepted')} rate_limiting_delay={g('rate_limiting_delay')}")
mo, emo = g("move_overhead"), g("engine.uci_options.Move Overhead")
need("lichess-bot move_overhead sane", mo is not None and 0 <= mo <= 3000,
     f"lichess-bot move_overhead={mo} ms is subtracted from our clock before every go; Corvus Move Overhead={emo} ms on top", "RISK")
al = live.get("challenge", {}).get("allow_list")
results.append(("REMINDER" if not al else "PASS", "allow_list",
                f"{'missing/empty: ANY user can challenge and occupy the single game slot - fill it on competition day' if not al else f'{len(al)} names'}"))

# structural diff against the template (token, Threads, Hash excluded)
T = flat(tmpl)
diffs = [(k, T.get(k, "<absent>"), L.get(k, "<absent>")) for k in sorted(set(T) | set(L))
         if k not in ALLOWED_DIFF and T.get(k, "<absent>") != L.get(k, "<absent>")]
need("live config == template (except token/Threads/Hash)", not diffs,
     "no value differences" if not diffs else "; ".join(f"{k}: template={a!r} live={b!r}" for k, a, b in diffs))
info = [f"Threads: template={T.get('engine.uci_options.Threads')} live={L.get('engine.uci_options.Threads')}",
        f"Hash: template={T.get('engine.uci_options.Hash')} live={L.get('engine.uci_options.Hash')}"]


def lines(path):
    with open(path, encoding="utf-8") as f:
        return [ln for ln in f.read().splitlines() if not TOKEN_LINE.search(ln)]


raw = [d for d in difflib.unified_diff(lines(tmpl_path), lines(live_path), "template", "live", n=0, lineterm="")]

for st, item, detail in results:
    print(f"{st:<8} {item}: {detail}")
print("info    " + "; ".join(info))
print("raw line diff (token lines removed):")
print("\n".join("    " + d for d in raw) if raw else "    (none)")
bad = [r for r in results if r[0] == "FAIL"]
risk = [r for r in results if r[0] == "RISK"]
print("check3:", "FAIL" if bad else ("PASS (with RISK notes)" if risk else "PASS"))
sys.exit(1 if bad else 0)
