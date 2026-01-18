# MPerft
Fast bitboard chess move generation based on magic bitboards

## Usage:
```
mperft [--fen|-f <fen>] [--depth|-d <depth>] [--hash|-h <size>] [--bulk|-b] [--capture] [[--div] | [--repeat|-r] [--loop|-l]] | [--help|-?] | [--test|-t] 
Enumerate moves.
	--help|-?            Print this message.
	--fen|-f <fen>       Use the position indicated in FEN format (default=starting position).
	--kiwipete|-k        Use the kiwipete position.
	--depth|-d <depth>   Test up to this depth (default=6).
	--bulk|-b            Do fast bulk counting at the last ply.
	--hash|-h <size>     Use a hashtable with <size> Megabytes (default 0, no hashtable).
	--capture|-c         Generate only captures, promotions & check evasions.
	--div                Print a node count for each move.
	--seed <seed>        Change the seed of the pseudo move generator to <seed>.
	--loop|-l            Loop from depth 1 to <depth>.
	--repeat|-r <n>      Repeat the test <n> time (default = 1).
	--test|-t            Run an internal test to check the move generator.
```

## Compilation
You can compile mperft for your own CPU using:
CC=clang make pgo

## Example
To run perft at depth 8 with bulk counting and an hashtable of 256 Mbytes, you can type:

```
$ mperft -d 8 -b -h 256

Magic Perft (c) version 2.0 Richard Delorme - 2026
Bitboard move generation based on magic (pext) bitboards
Perft setting: hashtable size: 256 Mbytes; with bulk counting;
  a b c d e f g h
8 r n b q k b n r 8
7 p p p p p p p p 7
6 . . . . . . . . 6
5 . . . . . . . . 5
4 . . . . . . . . 4
3 . . . . . . . . 3
2 P P P P P P P P 2
1 R N B Q K B N R 1
  a b c d e f g h
w, KQkq
perft  8 :     84998978956 leaves in      9.766 s   8703464369 leaves/s
full time:      9.894 s
```

 
