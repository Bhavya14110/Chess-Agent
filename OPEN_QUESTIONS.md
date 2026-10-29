# Open questions for the organisers

1. **Submission date** was not given to us in the brief — please confirm the deadline.
2. **Pondering**: the rules don't mention thinking on the opponent's time. We keep it disabled to be safe. Is it allowed?
3. **Contempt / draw score**: we use draw = 0. Is a small non-zero draw score considered fine (it's still our own eval)?
4. **Random opening plies in self-play data generation**: we play 8-10 uniformly random legal moves from the start position to diversify training games (nothing stored, nothing chosen by quality). We consider this not an opening book. Please confirm.
5. **Hardware for organiser rebuilds**: our binary is built with `-march=native` for our host; a portable build is also shipped. Is a Windows x64 executable acceptable?
