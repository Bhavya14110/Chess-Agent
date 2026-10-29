# MY_TODO: manual steps for a human team member

A team member has to do these steps by hand. They involve accounts, tokens and
permanent changes, which are not automated. They follow section 5 of `docs/guidelines.pdf`.
Technical details are in `lichess-bot/README.md`.

**Golden rules**

- Never commit a token or a filled-in `config.yml`.
- Never paste a token into chat, an issue or a document that others can read.
- Never make a move for the bot yourself.
- Never touch the bot while a competition game is running.

---

## 1. Create the bot's Lichess account

1. Log out of your personal account, or use a private browser window, and go to <https://lichess.org/signup>.
2. Pick a username that identifies the team. Suggestion: **`CorvusChessBot`**. If it is taken, try `CorvusBot` or `TeamCorvusBot`.
3. Use a team email address that someone will still have access to after the competition, and a strong unique password. Store both in the team's password manager.
4. **Do not play any game with this account**: no casual game, no puzzle game, no game against the computer. Only an account that has never played a game can become a bot account.

## 2. Create the `bot:play` token

1. While logged in to the bot account, open **Preferences → API access tokens** (<https://lichess.org/account/oauth/token>) and click **+**.
2. Description: `lichess-bot play`. Tick **only** **"Play games with the bot API" (`bot:play`)** and leave every other box unticked.
3. Click **Create** and copy the token (it starts with `lip_`). **Lichess shows it only once.**
4. Store it in the team password manager. Do not store it in the repo, in e-mail or in chat. This token stays private to the team and is never submitted.

## 3. Install lichess-bot and fill in `config.yml`

Follow `lichess-bot/README.md`, sections 1 and 2:

1. Clone <https://github.com/lichess-bot-devs/lichess-bot> **outside** our repo, create a venv with **Python 3.11 or newer** (`py -3.12` on our Windows host), and run `pip install -r requirements.txt`.
2. Copy `lichess-bot/config.example.yml` to `<lichess-bot folder>/config.yml`.
3. In that copy, replace `PUT_BOT_PLAY_TOKEN_HERE` with the `bot:play` token, or set the `LICHESS_BOT_TOKEN` environment variable instead.
4. Check that `engine.dir` and `engine.working_dir` are the absolute path to our repo's `engine/` folder on this machine.

## 4. Upgrade the account to a BOT account (PERMANENT, run only once)

From the lichess-bot folder, with the venv active:

```
python lichess-bot.py -u
```

This cannot be undone: the account can never be used by a human player again. Check
that it worked: the profile page at `https://lichess.org/@/<botname>` shows the **BOT** tag.

## 5. Start the bot and play a test game

1. Start the bot with `python lichess-bot.py -l corvus-bot.log`. Check that the printed config shows `enabled: false` for `polyglot`, every `online_moves` source, `syzygy` and `gaviota`, plus `allow_matchmaking: false`.
2. If `allow_list` is already filled in, add your personal account to it first.
3. From your **personal** account, open `https://lichess.org/@/<botname>`, click **Challenge to a game** and choose **Standard, Real time, 3+2, Casual**.
4. Check all of the following:
   - the bot accepts the challenge;
   - it plays legal moves for the whole game and does not flag;
   - the chat shows only "Good luck and have fun!" and "Thanks for the game!".
5. Optionally also test that **10+5 casual** is accepted, and that **rated** or **1+0** challenges are declined.

## 6. Create the `challenge:write` token, for submission day

1. On the bot account, open **Preferences → API access tokens** again and create a **second** token.
2. Description: `competition challenges`. Tick **only** **"Create, accept, decline challenges" (`challenge:write`)**.
3. Copy it once and store it in the password manager, separately from the `bot:play` token. You will submit it to the organisers on competition day. It cannot make moves for the bot.

## 7. Fill in the allow-list (before the competition)

In the local `config.yml` (not the template), uncomment `challenge.allow_list` and list
the following. Spell each name exactly as on Lichess, including capitalisation.

- the organisers' account(s);
- every competing bot;
- the chess-club members taking part in the final stage, or update the list again before that stage.

Restart the bot **between** rounds (never during a game) for the change to take effect.
Ask the organisers for the exact list of usernames.

## 8. Competition day

1. **Submit** through the channel the organisers specify, before the deadline. Late submissions are refused.
   - the GitHub repository link (make sure the organisers can access it);
   - the bot username;
   - the `challenge:write` token.
2. **Keep the bot online** with the submitted engine from the announced start time of each round until all of its games are finished. A bot that is not online and accepting challenges within 10 minutes of the start time forfeits.
3. **Do not intervene** during games: do not stop, restart, reconfigure or advise. Never make moves manually. If the host crashes **between** games, restart lichess-bot.
4. For the final stage against the chess club (10+5, casual), use **exactly** the same engine version. Make no changes.
5. Keep the host machine awake for the whole event. Turn off sleep and hibernation, and pause Windows Update restarts.

---

## Pre-flight checklist (before submitting and before each round)

- [ ] The engine starts from its own folder with the launch command alone:
  `cd engine` then `Corvus.exe`. Type `uci`, then check that `uciok` appears; type `isready`, then check that `readyok` appears; then type `quit`.
- [ ] The engine writes only UCI lines to stdout. Debug output goes to stderr.
- [ ] `config.yml` contains: protocol `uci`, `ponder: false`, `polyglot.enabled: false`, all four `online_moves` sources `enabled: false`, `syzygy` and `gaviota` `enabled: false`, and `matchmaking.allow_matchmaking: false`.
- [ ] `challenge` contains: `variants: [standard]`, `modes: [casual]`, `time_controls: [blitz, rapid]`, `concurrency: 1`, and an `allow_list` that is filled in.
- [ ] A test casual 3+2 game from a personal account has been played without problems.
- [ ] **No tokens in git.** Run these from the repo root; each must print nothing:
  ```bash
  git log -p --all | grep -nE "(^|[^A-Za-z0-9])lip_[A-Za-z0-9]{8,}"
  git log -p --all | grep -inE "^\+\s*token:" | grep -v PUT_BOT_PLAY_TOKEN_HERE
  git ls-files | grep -iE "(^|/)config\.yml$|\.token$"
  ```
  `config.example.yml` must still contain only `PUT_BOT_PLAY_TOKEN_HERE`.
- [ ] `README.md` at the repo root states the exact launch command (`Corvus.exe`, run from `engine/`) and the bot username.
- [ ] Both tokens are stored in the password manager, and the `bot:play` token has not been shared with anyone outside the team.
