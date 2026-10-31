"""Scripted game for check 4: white shuffles a knight, black mates on f2 with its 12th move."""
GAME = [
    "b1a3", "h7h6", "a3b1", "h6h5", "b1a3", "a7a6", "a3b1", "a6a5",
    "b1a3", "b7b6", "a3b1", "b6b5", "b1a3", "g7g6", "a3b1", "g6g5",
    "b1a3", "e7e5", "a3b1", "f8c5", "b1a3", "d8f6", "a3b1", "f6f2",
]

if __name__ == "__main__":
    import chess
    b = chess.Board()
    for m in GAME:
        b.push_uci(m)
    print(b.is_checkmate(), b.outcome())
