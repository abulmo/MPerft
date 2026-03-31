# MPerft

Fast bitboard chess move generation based on magic bitboards, optional bulking or nullmove<sup>new</sup>count,
transposition table & multithreading.

## Usage:
```
mperft <args> 
Enumerate moves. The following options are available:
	--bulk|-b               Do fast bulk counting at the last ply.
	--nullmove|-n           Do fast nullmove counting at the penultimate ply.
	[--depth|-d] <depth>    Test up to this depth (default = 6).
	--div                   Print a node count for each move.
	--fast                  Automatically set highest settings.
	--fen|-f <fen>          Use the position indicated in FEN format (default = starting position).
	--hash|-h <size>        Use a hashtable with <size> Megabytes (default = 0, no hashtable).
	--help|-?               Print this message.
	--kiwipete|-k           Use the kiwipete position.
	--moves|-m <moves>      Play a serie of moves to build the position to use.
	--loop|-l               Loop from depth 1 to <depth>.
	--quiet|-q              Disable verbose output.
	--repeat|-r <n>         Repeat the test <n> time (default = 1).
	--seed|-s <seed>        Change the seed of the pseudo move generator to <seed>.
	--test                  Run an internal test to check the move generator.
	--threads|-t <threads>  Use <threads> threads for parallel processing (default = 1).
```

## Limitation:
*Mperft* is limited to a maximal depth of 64 plies.
The hash table is limited to the available memory.
The number of threads is limited to the number of logical cores available. 
On 64 bits versions, the leaf counter is limited to 2^58 = 288,230,376,151,711,744 leaves, which is enough for executing
mperft up to depth 13. On 128 bits versions, the leaf counter is limited to 
2^122 = 5,316,911,983,139,663,491,615,228,241,121,378,304 leaves.

## Binary Release
You can download the binary release for your platform from the [releases page](https://github.com/abulmo/mperft/releases). 
Executable are given for Linux & Windows for intel64/amd64 cpu in 3 flavours:
 - x86-64 
 - x86-64-v2 support AVX extension, including the popcount instruction.
 - x86-64-v3 support AVX2 extension, including the PEX instruction.
 - x86-64-v3-128 support AVX2 extension, including the PEX instruction and use 128 bits counter & Zobrist's hash keys. 

## Compilation
You can compile mperft for your own CPU using the makefile or the .bat file provided for clang & icx (intel C compiler).
For example under Linux, using the clang compiler:
```
CC=clang make
```
or under MS-Windows, after opening a terminal windows:
```
build-clang.bat native
```

## Example

```
$ mperft -d 12 --fast -l
Magic Perft version 5.0 (c) Richard Delorme 2020 - 2026
Bitboard move generation based on magic (pext) bitboards
Perft setting: hashtable size: 32768 Mbytes (2147483652 entries); with 32 threads; with nullmove counting.
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
perft  1:                   20 leaves in      0.000 s        8388608 leaves/s
perft  2:                  400 leaves in      0.000 s      209715200 leaves/s
perft  3:                 8902 leaves in      0.000 s      484905120 leaves/s
perft  4:               197281 leaves in      0.000 s      878403914 leaves/s
perft  5:              4865609 leaves in      0.001 s     7810119897 leaves/s
perft  6:            119060324 leaves in      0.003 s    34828790152 leaves/s
perft  7:           3195901860 leaves in      0.034 s    93999312462 leaves/s
perft  8:          84998978956 leaves in      0.407 s   208810741269 leaves/s
perft  9:        2439530234167 leaves in      4.807 s   507469426465 leaves/s
perft 10:       69352859712417 leaves in     52.443 s  1322442452178 leaves/s
perft 11:     2097651003696806 leaves in    692.291 s  3030012648995 leaves/s
perft 12:    62854969236701747 leaves in  13964.894 s  4500927164567 leaves/s
total   :    65024500949358489 leaves in  14714.880 s  4418962235419 leaves/s
full time:  14762.732 s
```

## History
- Version 1.0 
- Version 1.1 More compact hash table using bitfields to store the counter & the depth.
- Version 2.0 Use copy/make instead of update/restore after each move. Replace bitfields with shift/mask.
- Version 3.0 Add support for multithreading.
- Version 3.1 Remove some OS/platform dependencies. Alphabetically sort the move with the --div command.
- Version 3.2 Remove more OS/platform dependencies. Fix a bug (free() incompatibility with _aligned_malloc()). Faster executable for Windows.
- Version 4.0 Code fix & optimisation. Add --fast & --moves option. remove --capture option.
- Version 4.1 Code fix & optimisation. Add optionally compiled 128 bit support for crunching big numbers.
- Version 5.0 Add nullmove counting at the penultimate remaining depth (=2). Significantly faster.

## Performance
Time to compute perft 7 with bulk counting on a ryzen 9 5950x @ 4.2 Ghz under Linux :

|---------|----------|---------|----------|
| Version |   Time   | Options | Progress |
|---------|----------|---------|----------|
|  1.1    |  3.865 s | -b 7    |          | 
|  2.0    |  3.787 s | -b 7    |  +2.1%   |
|  3.2    |  3.811 s | -b 7    |  -0.6%   |
|  4.1    |  3.006 s | -b 7    | +26.8%   |
|  5.0    |  3.314 s | -b 7    |  -9.3%   |
|  5.0    |  2.070 s | -n 7    | +60.1%   |
|  5.0-128|  2.089 s | -n 7    |  -0.9%   |
|---------|----------|---------|----------|

Time to compute perft 8 with bulk counting and 256 MB of hash table:

|---------|----------|-------------|----------|
| Version |   Time   | Options     | Progress |
|---------|----------|-------------|----------|
|  1.1    |  9.985 s | -b 8 -H 24  |          |
|  2.0    |  9.709 s | -b 8 -h 256 |  +2.8%   |
|  3.2    | 10.138 s | -b 8 -h 256 |  -4.2%   |
|  4.1    |  8.420 s | -b 8 -h 256 | +20.4%   |
|  5.0    |  6.655 s | -n 8 -h 256 | +26.5%   |
|  5.0-128|  6.949 s | -n 8 -h 256 |  -4.2%   |
|---------|----------|-------------|----------|

Time to compute perft 9 with bulk counting, 32 threads and 1 GB of hash table:

|---------|----------|--------------------|----------|
| Version |   Time   | Options            | Progress |
|---------|----------|--------------------|----------|
|  2.0    |118.197 s | -b 9 -h 1024       |          |
|  3.2    |  6.249 s | -b 9 -t 32 -h 1024 |+1791.4%  |
|  4.1    |  5.386 s | -b 9 -t 32 -h 1024 |  +16.0%  |
|  5.0    |  4.374 s | -n 9 -t 32 -h 1024 |  +23.1%  |
|  5.0-128|  5.350 s | -n 9 -t 32 -h 1024 |  -18.2%  |
|  5.0-128|  4.835 s | -n 9 -t 32 -h 2048 |  +10.7%  |
|---------|----------|--------------------|----------|

