# MPerft
Fast bitboard chess move generation based on magic bitboards, optional bulking count, transposition table & multithreading.

## Usage:
```
mperft <args> 
Enumerate moves. The following options are available:
	--bulk|-b               Do fast bulk counting at the last ply.
	--capture|-c            Generate only captures, promotions & check evasions.
	[--depth|-d] <depth>    Test up to this depth (default = 6).
	--div                   Print a node count for each move.
	--fen|-f <fen>          Use the position indicated in FEN format (default=starting position).
	--hash|-h <size>        Use a hashtable with <size> Megabytes (default = 0, no hashtable).
	--help|-?               Print this message.
	--kiwipete|-k           Use the kiwipete position.
	--loop|-l               Loop from depth 1 to <depth>.
	--quiet|-q              Disable verbose output.
	--repeat|-r <n>         Repeat the test <n> time (default = 1).
	--seed|-s <seed>        Change the seed of the pseudo move generator to <seed>.
	--test                  Run an internal test to check the move generator.
	--threads|-t <threads>  Use <threads> threads for parallel processing (default = 1).
```

## Limitation:
mperft is limited to a maximum depth of 64 plies.
The hash table is limited to 64 GB (65536 MB).
The leaf counter is limited to 2^58 = 288,230,376,151,711,744 leaves.

## Binary Release

You can download the binary release for your platform from the [releases page](https://github.com/abulmo/mperft/releases). 
Executable are given for Linux & Windows for intel64/amd64 cpu in 3 flavours:
 - x86-64 
 - x86-64-v2 support AVX extension, including the popcount instruction.
 - x86-64-v3 support AVX2 extension, including the PEX instruction.

## Compilation
You can compile mperft for your own CPU using the makefile or the .bat file provided for clang & icx (intel C compiler). For example under Linux, using the intel C compiler:
```
CC=icx make
```
or under MS-Windows:
```
build-icx.bat native
```

## Example
To run perft at depth 9 with bulk counting, a hashtable of 256 Mbytes and 32 threads, you can type:

```
$ mperft -d 9 -b -h 256 -t 32
Magic Perft version 3.2 (c) Richard Delorme 2020 - 2026
Bitboard move generation based on magic (pext) bitboards
Perft setting: hashtable size: 256 Mbytes (16777220 entries); with 32 threads; with bulk counting;
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
perft  9 :   2439530234167 leaves in      8.452 s 288633848809 leaves/s
full time:      8.629 s
```

## History
- Version 1.0 
- Version 1.1 More compact hash table using bitfields to store the counter & the depth.
- Version 2.0 Use copy/make instead of update/restore after each move. Replace bitfields with shift/mask.
- Version 3.0 Add support for multithreading.
- Version 3.1 Remove some OS/platform dependencies. Alphabetically sort the move with the --div command.
- Version 3.2 Remove more OS/platform dependencies. Fix a bug (free() incompatibility with _aligned_malloc()). Faster executable for Windows.
