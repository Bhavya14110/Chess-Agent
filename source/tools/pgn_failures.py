"""Counts how games ended in a fastchess PGN and flags anything that would lose a real game
(time forfeit, illegal move, crash/disconnect, stalls). usage: python pgn_failures.py games.pgn"""
import collections, re, sys

text = open(sys.argv[1], encoding="utf-8", errors="ignore").read()
games = text.count("[Event ")
terms = collections.Counter(re.findall(r'\[Termination "([^"]*)"\]', text))
results = collections.Counter(re.findall(r'\[Result "([^"]*)"\]', text))
# fastchess writes the reason in the closing comment, e.g. {A loses on time}
reasons = collections.Counter(m.strip() for m in re.findall(r"\{([^{}]*(?:loses|illegal|disconnect|stall|crash)[^{}]*)\}", text, re.I))
bad_words = ("time", "illegal", "disconnect", "stall", "crash", "abandon")
bad = sum(n for t, n in terms.items() if any(w in t.lower() for w in bad_words))
bad += sum(n for r, n in reasons.items() if any(w in r.lower() for w in ("on time", "illegal", "disconnect", "stall", "crash")))
print(f"games: {games}")
print(f"results: {dict(results)}")
print(f"terminations: {dict(terms)}")
if reasons:
    print(f"failure comments: {dict(reasons)}")
print(f"FAILURES (time/illegal/crash/stall): {bad}")
print("PASS" if bad == 0 and games > 0 else "FAIL")
