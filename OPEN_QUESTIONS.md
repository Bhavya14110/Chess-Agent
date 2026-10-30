# Open questions for the organisers

1. **Submission date** was not given to us in the brief — please confirm the deadline.
2. **Pondering**: the rules don't mention thinking on the opponent's time. We keep it disabled to be safe. Is it allowed?
3. **Contempt / draw score**: we use draw = 0. Is a small non-zero draw score considered fine (it's still our own eval)?
4. **Starting positions for self-play data generation**: to diversify training games, 1 in 4 games starts with 8-10 uniformly random legal moves from the start position, and 3 in 4 start from a position taken from the human Lichess games (plus 1-2 random moves). These positions are only used offline as starting points for our own engine's training games; nothing from them is stored in or used by the engine during play. We consider this not an opening book. Please confirm.
6. **Hosting**: may the bot run on a cloud machine we rent instead of our own laptop (same engine, same rules)?
5. **Hardware for organiser rebuilds**: our binary is built with `-march=native` for our host; a portable build is also shipped. Is a Windows x64 executable acceptable?
