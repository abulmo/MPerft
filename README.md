# MPerft
Fast bitboard chess move generation based on magic bitboards, optional bulking count, transposition table & multithreading
## Usage:
```
mperft <args> 
Enumerate moves. The following options are available:
	--bulk|-b               Do fast bulk counting at the last ply.
	[--depth|-d] <depth>    Test up to this depth (default = 6).
	--div                   Print a node count for each move.
	--fast                  Automatically set highest settings.
	--fen|-f <fen>          Use the position indicated in FEN format (default = starting position).
	--hash|-h <size>        Use a hashtable with <size> Megabytes (default = 0, no hashtable).
	--help|-?               Print this message.
	--kiwipete|-k           Use the kiwipete position.
	--moves|-m <moves>      Play a series of moves to build the position to use.
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
You can compile mperft for your own CPU using the makefile or the .bat file provided for clang & icx (intel C compiler). For example under Linux, using the clang compiler:
```
CC=clang make
```
or under MS-Windows:
```
build-clang.bat native
```

## Example

```
$ mperft -d 12 --fast -l
Magic Perft version 4.0 (c) Richard Delorme 2020 - 2026
Bitboard move generation based on magic (pext) bitboards
Perft setting: hashtable size: 32768 Mbytes (2147483652 entries); with 32 threads; with bulk counting;
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
perft  1 :                 20 leaves in      0.000 s       11983726 leaves/s
perft  2 :                400 leaves in      0.000 s      209715200 leaves/s
perft  3 :               8902 leaves in      0.000 s      377148426 leaves/s
perft  4 :             197281 leaves in      0.000 s      573427919 leaves/s
perft  5 :            4865609 leaves in      0.001 s     7249677901 leaves/s
perft  6 :          119060324 leaves in      0.004 s    29868723799 leaves/s
perft  7 :         3195901860 leaves in      0.044 s    72115344852 leaves/s
perft  8 :        84998978956 leaves in      0.536 s   158707039166 leaves/s
perft  9 :      2439530234167 leaves in      5.897 s   413669140306 leaves/s
perft 10 :     69352859712417 leaves in     63.779 s  1087387524614 leaves/s
perft 11 :   2097651003696806 leaves in    837.672 s  2504143445218 leaves/s
perft 12 :  62854969236701747 leaves in  16914.459 s  3716049685686 leaves/s
total    :  65024500949358489 leaves in  17822.392 s  3648472073458 leaves/s
full time:  17860.911 s
```

## History
- Version 1.0 
- Version 1.1 More compact hash table using bitfields to store the counter & the depth.
- Version 2.0 Use copy/make instead of update/restore after each move. Replace bitfields with shift/mask.
- Version 3.0 Add support for multithreading.
- Version 3.1 Remove some OS/platform dependencies. Alphabetically sort the move with the --div command.
- Version 3.2 Remove more OS/platform dependencies. Fix a bug (free() incompatibility with _aligned_malloc()). Faster executable for Windows.
- Version 4.0 Code fix & optimisation. Add --fast & --moves option. remove --capture option.
