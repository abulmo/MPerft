# MPerft
Fast bitboard chess move generation based on magic bitboards

Usage:
```
mperft [--fen|-f <fen>] [--depth|-d <depth>] [--hash|-H <size>] [--bulk|-b] [--div] [--capture] [--help|-h] 
Enumerate moves.
	--help|-h            Print this message.
	--fen|-f <fen>       Test the position indicated in FEN format (default=starting position).
	--depth|-d <depth>   Test up to this depth (default=6).
	--bulk|-b            Do fast bulk counting at the last ply.
	--hash|-H <size>     Use a hashtable with <size> bits entries (default 0, no hashtable).
	--capture|-c         Generate only captures.
	--loop|-l            Loop from depth 1 to <depth>
	--div|-r             Print a node count for each move.
```
