# Running Corvus on Lichess with lichess-bot

Corvus is connected to Lichess through the official bridge
[lichess-bot](https://github.com/lichess-bot-devs/lichess-bot) (guidelines, section 3/5).
This folder only holds our **template** configuration, `config.example.yml`.
lichess-bot itself is installed in a separate folder and is not part of this repo.

`config.example.yml` was written against lichess-bot commit
`afa430da25cb6d506c878d3ca2fa155be8da625d` (version `2026.10.27.3`). If you install a
newer lichess-bot, compare its `config.yml.default` with our file first, because key
names sometimes change.

> **Never commit a `config.yml` that contains a token.** `config.yml` and `*.token` are
> in `.gitignore`, but check `git status` before every commit anyway. The only config
> file that belongs in git is `config.example.yml`, which holds a placeholder.

## What the config does

- Launches `Corvus.exe` from our `engine/` folder over UCI with `Threads 15`, `Hash 256` and `Move Overhead 150`. These values are provisional and will be tuned.
- Disables every source of moves that Corvus does not choose itself: the polyglot opening book, local Syzygy and Gaviota tablebases, and all of `online_moves` (ChessDB, Lichess cloud analysis, Lichess opening explorer and online EGTB).
- Turns pondering off.
- Accepts standard chess only, casual games only, at blitz and rapid speeds, with a 3 to 10 minute base and a 2 to 5 second increment. That range covers both 3+2 and 10+5. It plays one game at a time and accepts challenges automatically.
- Keeps matchmaking disabled.
- Allows resigning and draw offers, based only on Corvus's own evaluation (see the comments in the file).
- Contains an `allow_list` that is commented out. **Fill it in before the competition.**

## 1. Install lichess-bot (in a virtual environment)

You need **Python 3.11 or newer**. lichess-bot marks Python 3.10 as deprecated, and its
releases from 2026-10-31 refuse to start on 3.10. On our Windows host, use `py -3.12`.

### Windows (PowerShell)

```powershell
cd C:\Users\<you>\Downloads
git clone https://github.com/lichess-bot-devs/lichess-bot lichess-bot-src
cd lichess-bot-src
py -3.12 -m venv venv
.\venv\Scripts\Activate.ps1      # if blocked: Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
python -m pip install --upgrade pip
pip install -r requirements.txt
```

### Linux

```bash
git clone https://github.com/lichess-bot-devs/lichess-bot lichess-bot-src
cd lichess-bot-src
python3 -m venv venv            # Debian/Ubuntu: sudo apt install python3-venv
source venv/bin/activate
python -m pip install --upgrade pip
pip install -r requirements.txt
```

Do not put the lichess-bot folder inside our repo. If you have to, use
`lichess-bot/lichess-bot-src/`, which is git-ignored.

## 2. Create `config.yml`

Copy our template into the lichess-bot folder under the name `config.yml`:

```powershell
# Windows
copy "C:\Users\<you>\Downloads\chess agent\lichess-bot\config.example.yml" config.yml
```

```bash
# Linux
cp /path/to/chess-agent/lichess-bot/config.example.yml config.yml
```

Then edit the copy in the lichess-bot folder. Never edit the template to hold real values.

1. **`token`**: replace `PUT_BOT_PLAY_TOKEN_HERE` with the bot account's `bot:play` token.
   You can also leave the placeholder and set the token as an environment variable, which lichess-bot uses instead:
   - Windows: `$env:LICHESS_BOT_TOKEN = "lip_..."` (current PowerShell session only)
   - Linux: `export LICHESS_BOT_TOKEN=lip_...`
2. **`engine.dir`** and **`engine.working_dir`**: set both to the **absolute** path of our repo's
   `engine/` folder on the hosting machine. For example:
   - `"C:/Users/<you>/Downloads/chess agent/engine/"` (forward slashes work on Windows)
   - `"/home/<you>/chess-agent/engine/"`
   On Linux the engine must be a Linux executable with execute permission
   (`chmod +x`). `engine.name` must then be that file's name.
3. **`challenge.allow_list`**: before the competition, uncomment it and list the
   organisers' accounts, all competing bots and the chess-club members in the final stage.
   Spelling must match Lichess exactly, including capitalisation. For testing, add your
   personal account or leave the list commented out.

## 3. Run

The account must already be a BOT account (see `MY_TODO.md`; the `-u` upgrade is a
one-time, permanent step done by a human).

```powershell
# Windows, inside lichess-bot-src with the venv activated
python lichess-bot.py
# with a log file:
python lichess-bot.py -l corvus-bot.log
```

```bash
# Linux, inside lichess-bot-src with the venv activated
python lichess-bot.py
python lichess-bot.py -l corvus-bot.log
```

At startup lichess-bot prints the whole configuration, with the token masked. Check
that `polyglot`, `online_moves`, `lichess_bot_tbs` and `matchmaking` all show
`enabled: false` / `allow_matchmaking: false`.

`quit_after_all_games_finish` is `true`, so pressing Ctrl-C once waits for the current
game to finish. Do not stop or restart the bot during a game.

## 4. Re-validate the config offline (optional)

From inside `lichess-bot-src`, with the venv active, this loads `config.yml` through
lichess-bot's own validator without contacting Lichess:

```bash
python -c "from lib.config import load_config; load_config('config.yml'); print('config OK')"
```
