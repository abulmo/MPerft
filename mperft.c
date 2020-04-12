/*
 * mperft.c
 *
 * perft using magic bitboard
 *
 * © 2020 Richard Delorme
 * version 1.0
 */

/* Includes */
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef _ISOC11_SOURCE
void* aligned_alloc(size_t, size_t);
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#include <sys/time.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef USE_PEXT
    #include <immintrin.h>
#endif

/* Types */
typedef enum {GAME_SIZE = 4096, MOVE_SIZE = 256} Limits;

typedef uint64_t Bitboard;

typedef uint64_t Random;

typedef enum { WHITE, BLACK, COLOR_SIZE } Color;

typedef enum
{
	A1, B1, C1, D1, E1, F1, G1, H1,
	A2, B2, C2, D2, E2, F2, G2, H2,
	A3, B3, C3, D3, E3, F3, G3, H3,
	A4, B4, C4, D4, E4, F4, G4, H4,
	A5, B5, C5, D5, E5, F5, G5, H5,
	A6, B6, C6, D6, E6, F6, G6, H6,
	A7, B7, C7, D7, E7, F7, G7, H7,
	A8, B8, C8, D8, E8, F8, G8, H8,
	BOARD_SIZE, ENPASSANT_NONE = BOARD_SIZE, BOARD_OUT = -1
} Square;

typedef enum { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, PIECE_SIZE } Piece;

typedef enum { EMPTY, WPAWN, BPAWN, WKNIGHT, BKNIGHT, WBISHOP, BBISHOP, WROOK, BROOK, WQUEEN, BQUEEN, WKING, BKING, CPIECE_SIZE } CPiece;

typedef enum { KNIGHT_PROMOTION = 0x1000, BISHOP_PROMOTION = 0x2000, ROOK_PROMOTION = 0x3000, QUEEN_PROMOTION = 0x4000 } Promotion;

typedef uint16_t Move;

typedef struct Key {
	uint64_t code;
	uint32_t index;
} Key;

typedef struct Attack {
	Bitboard mask;
	Bitboard magic;
	Bitboard shift;
	Bitboard *attack;
} Attack;

typedef struct Mask {
	Bitboard between[BOARD_SIZE];
	int direction[BOARD_SIZE];
	Bitboard diagonal;
	Bitboard antidiagonal;
	Bitboard file;
	Bitboard rank;
	Bitboard pawn_attack[COLOR_SIZE];
	Bitboard pawn_push[COLOR_SIZE];
	Bitboard enpassant;
	Bitboard knight;
	Bitboard king;
	Attack bishop;
	Attack rook;
} Mask;

typedef struct BoardStack {
	Bitboard pinned;
	Bitboard checkers;
	uint8_t castling;
	uint8_t enpassant;
	uint8_t victim;
	Key key;
} BoardStack;

typedef struct Board {
	uint8_t cpiece[BOARD_SIZE];
	Bitboard piece[PIECE_SIZE];
	Bitboard color[COLOR_SIZE];
	BoardStack stack_[GAME_SIZE],*stack;
	Square x_king[COLOR_SIZE];
	int ply;
	Color player;
} Board;

typedef struct MoveArray {
	Move move[MOVE_SIZE];
	int n;
	int i;
} MoveArray;

typedef struct {
	uint64_t code;
	uint64_t count:56;
	int depth:8;
} Hash;

typedef struct {
	Hash *hash;
	uint64_t mask;
} HashTable;

/* Constants */
const Bitboard RANK[] =  {
	0x00000000000000ffULL, 0x000000000000ff00ULL, 0x0000000000ff0000ULL, 0x00000000ff000000ULL,
	0x000000ff00000000ULL, 0x0000ff0000000000ULL, 0x00ff000000000000ULL, 0xff00000000000000ULL,
};
const Bitboard COLUMN[] = {
	0x0101010101010101ULL, 0x0202020202020202ULL, 0x0404040404040404ULL, 0x0808080808080808ULL,
	0x1010101010101010ULL, 0x2020202020202020ULL, 0x4040404040404040ULL, 0x8080808080808080ULL,
};
const int PUSH[] = {8, -8};
const uint8_t MASK_CASTLING[BOARD_SIZE] = {
	13,15,15,15,12,15,15,14,
	15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,
	15,15,15,15,15,15,15,15,
	 7,15,15,15, 3,15,15,11
};
const int CAN_CASTLE_KINGSIDE[COLOR_SIZE] = {1, 4};
const int CAN_CASTLE_QUEENSIDE[COLOR_SIZE] = {2, 8};
const Bitboard PROMOTION_RANK[] = {0xff00000000000000ULL, 0x00000000000000ffULL};
const Random MASK48 = 0xFFFFFFFFFFFFull;
const int BUCKET_SIZE = 4;

/* Globals */
Mask MASK[BOARD_SIZE];
Key KEY_PLAYER[COLOR_SIZE];
Key KEY_SQUARE[BOARD_SIZE][CPIECE_SIZE];
Key KEY_CASTLING[16];
Key KEY_ENPASSANT[BOARD_SIZE + 1];
Key KEY_PLAY;

/* Count moves from a bitboard */
int count_moves(Bitboard b) {
#if defined(POPCOUNT) && defined(USE_MSVC_X64)
		return __popcnt64(b);
#elif defined(POPCOUNT) && defined(USE_GCC_X64)
		return __builtin_popcountll(b);
#else
	Bitboard c = b
		- ((b >> 1) & 0x7777777777777777ull)
		- ((b >> 2) & 0x3333333333333333ull)
		- ((b >> 3) & 0x1111111111111111ull);
	c = ((c + (c >> 4)) & 0x0f0f0f0f0f0f0f0full) * 0x0101010101010101ull;

	return  (int)(c >> 56);
#endif
}

/* Verify if only one bit is set. */
bool is_single(Bitboard b) {
	return (b & (b - 1)) == 0;
}

/* Byte swap (= vertical mirror) */
Bitboard bit_bswap(Bitboard b) {
#if defined(USE_MSVC_X64)
	return _byteswap_uint64(b);
#elif defined(USE_GCC_X64)
	return __builtin_bswap64(b);
#else
	b = ((b >>  8) & 0x00ff00ff00ff00ffull) | ((b <<  8) & 0xff00ff00ff00ff00ull);
	b = ((b >> 16) & 0x0000ffff0000ffffull) | ((b << 16) & 0xffff0000ffff0000ull);
	b = ((b >> 32) & 0x00000000ffffffffull) | ((b << 32) & 0xffffffff00000000ull);
	return b;
#endif
}


/* Time in seconds */
double chrono(void) {
	#if defined(__unix__) || defined(__APPLE__)
		#if _POSIX_TIMERS > 0
			struct timespec t;
			clock_gettime(CLOCK_MONOTONIC, &t);
			return 0.000000001 * t.tv_nsec + t.tv_sec;
		#else
			struct timeval t;
			gettimeofday(&t, NULL);
			return 0.000001 * t.tv_usec + t.tv_sec;
		#endif
	#elif defined(_WIN32)
		return 0.001 * GetTickCount();
	#endif
}

/* Memory error */
void memory_error(const char *function) {
	fprintf(stderr, "Fatal Error: memory allocation failure in %s\n", function);
	exit(EXIT_FAILURE);
}

/* Parse error. */
void parse_error(const char *string, const char *done, const char *msg) {
	int n;

	fprintf(stderr, "\nError in %s '%s'\n", msg, string);
	n = 11 + strlen(msg) + done - string;
	if (n > 0 && n < 256) {
		while (n--) putc('-', stderr);
		putc('^', stderr); putc('\n', stderr); putc('\n', stderr);
	}
	exit(EXIT_FAILURE);
}

/* Skip spaces */
char *parse_next(const char *s) {
	while (isspace((int)*s)) ++s;
	return (char*) s;
}

/* Get a random number */
uint64_t random_get(Random *random) {
	const uint64_t A = 0x5deece66dull;
	const uint64_t B = 0xbull;
	register uint64_t r;

	*random = ((A * *random + B) & MASK48);
	r = *random >> 16;
	*random = ((A * *random + B) & MASK48);
	return (r << 32) | (*random >> 16);
}

/* Init the random generator */
void random_seed(Random *random, const uint64_t seed) {
	*random = (seed & MASK48);
}

/* Opponent color */
Color opponent(const Color c) {
	return !c;
}

/* Convert a char to a Color */
Color color_from_char(const char c) {
	switch (tolower(c)) {
		case 'b': return BLACK;
		case 'w': return WHITE;
		default: return COLOR_SIZE;
	}
}

/* Loop over each color */
#define foreach_color(c) for ((c) = WHITE; (c) < COLOR_SIZE; ++(c))

/* Make a square from file & rank */
Square square(const int f, const int r) {
	return (r << 3) + f;
}

/* Make a square from file & rank if inside the board */
Square square_safe(const int f, const int r) {
	if (0 <= f && f < 8 && 0 <= r && r < 8) return square(f, r);
	else return BOARD_OUT;
}

/* Get square rank */
Square rank(const Square x) {
	return x >> 3;
}

/* Get square file */
Square file(const Square x) {
	return x & 7;
}

/* Create a bitboard with one bit (square) set */
Bitboard square_to_bit(const int x) {
	return 1ULL << x;
}

/* Create a bitboard with one bit set from file/rank if inside the board */
Bitboard file_rank_to_bit(const int f, const int r) {
	if (0 <= f && f < 8 && 0 <= r && r < 8) return square_to_bit(square(f, r));
	else return 0;
}

/* Parse a square from a string. */
bool square_parse(char **string, Square *x) {
	const char *s = *string;
	if ('a' <= s[0] && s[0] <= 'h' && '1' <= s[1] && s[1] <= '8') {
		*x = square(s[0] - 'a', s[1] - '1');
		*string += 2;
		return true;
	} else return false;
}

/* Get the first occupied square from a bitboard */
Square square_first(Bitboard b) {
#if defined(USE_MSVC_X64)
	unsigned long index;
	_BitScanForward64(&index, b);
	return (int) index;
#elif defined(USE_GCC_X64)
	return __builtin_ctzll(b);
#else
	const int magic[64] = {
		63,  0, 58,  1, 59, 47, 53,  2,
		60, 39, 48, 27, 54, 33, 42,  3,
		61, 51, 37, 40, 49, 18, 28, 20,
		55, 30, 34, 11, 43, 14, 22,  4,
		62, 57, 46, 52, 38, 26, 32, 41,
		50, 36, 17, 19, 29, 10, 13, 21,
		56, 45, 25, 31, 35, 16,  9, 12,
		44, 24, 15,  8, 23,  7,  6,  5
	};
	return magic[((b & (-b)) * 0x07edd5e59a4e28c2ull) >> 58];
#endif
}

/* Get the next occupied square from a bitboard */
Square square_next(Bitboard *b) {
	int i = square_first(*b);
	*b &= *b - 1;
	return i;
}

/* Loop over each square */
#define foreach_square(x) for ((x) = A1; (x) < BOARD_SIZE; ++(x))

/* Check if square 'x' is on 7th rank */
bool is_on_seventh_rank(const Square x, const Color c) {
	return c ? rank(x) == 1 : rank(x) == 6;
}

/* Check if square 'x' is on 2nd rank */
bool is_on_second_rank(const Square x, const Color c) {
	return c ? rank(x) == 6 : rank(x) == 1;
}

/* Convert a char to a piece */
Piece piece_from_char(const char c) {
	Piece p;

	for (p = PAWN; p < PIECE_SIZE; ++p) if ("pnbrqk"[p] == tolower(c)) break;
	return p;
}

/* Loop over each cpiece */
#define foreach_cpiece(cp) for ((cp) = WPAWN; (cp) < CPIECE_SIZE; ++(cp))

/* make a colored piece */
CPiece cpiece_make(Piece p, const Color c) {
	return (p << 1) + c + 1;
}

/* Get the Piece of a CPiece*/
Piece cpiece_piece(CPiece p) {
	return (p - 1) >> 1;
}

/* Get the color of a CPiece */
Color cpiece_color(CPiece p) {
	return (p - 1) & 1;
}

/* Convert a char to a colored piece */
CPiece cpiece_from_char(const char c) {
	CPiece p;

	foreach_cpiece(p) if ("#PpNnBbRrQqKk"[p] == c) break;
	return p;
}

/* Convert a char to a castling flag */
int castling_from_char(const char c) {
	switch (c) {
		case 'K': return 1;
		case 'Q': return 2;
		case 'k': return 4;
		case 'q': return 8;
		default: return 0;
	}
}

/* Get the origin square of a move */
Square move_from(const Move move) {
	return move & 63;
}

/* Get the destination square of a move */
Square move_to(const Move move) {
	return (move >> 6) & 63;
}

/* Get the promoted piece of a move */
Piece move_promotion(const Move move) {
	return move >> 12;
}

/* Convert a move to a string */
char* move_to_string(const Move move, char *s) {
	static char string[8];

	if (s == NULL) s = string;
	if (move) {
		s[0] = (move & 7) + 'a';
		s[1] = (move >> 3 & 7) + '1';
		s[2] = (move >> 6 & 7) + 'a';
		s[3] = (move >> 9 & 7) + '1';
		s[4] = "\0NBRQ"[move_promotion(move)];
		s[5] = '\0';
	} else {
		strcpy(s, "null");
	}

	return s;
}

/* Generate attack index using the magic bitboard or pext approach */
Bitboard magic_index(const Bitboard pieces, const Attack *attack) {
#ifdef USE_PEXT
	return _pext_u64(pieces, attack->mask);
#else
	return ((pieces & attack->mask) * attack->magic) >> attack->shift;
#endif
}

/* Generate pawn attack (capture) */
Bitboard pawn_attack(const Square x, const Color c, const Bitboard target) {
	return MASK[x].pawn_attack[c] & target;
}

/* Generate knight attack */
Bitboard knight_attack(const Square x, const Bitboard target) {
	return MASK[x].knight & target;
}

/* Generate bishop attack */
Bitboard bishop_attack(const Bitboard pieces, const Square x, const Bitboard target) {
	return MASK[x].bishop.attack[magic_index(pieces, &MASK[x].bishop)] & target;
}

/* Generate rook attack */
Bitboard rook_attack(const Bitboard pieces, const Square x, const Bitboard target) {
	return MASK[x].rook.attack[magic_index(pieces, &MASK[x].rook)] & target;
}

/* Generate king attack */
Bitboard king_attack(const Square x, const Bitboard target) {
	return MASK[x].king & target;
}

/* Init key to a random value */
void key_init(Key *key, Random *r) {
	key->code = random_get(r);
	key->index = (uint32_t) random_get(r);
}

/* Xor a key with another one */
void key_xor(Key *key, const Key *k) {
	key->code ^= k->code;
	key->index ^= k->index;
}

/* Set a key from a board */
void key_set(Key *key, const Board *board) {
	int x;

	*key = KEY_PLAYER[board->player];
	foreach_square (x) {
		key_xor(key, &KEY_SQUARE[x][board->cpiece[x]]);
	}
	key_xor(key, &KEY_CASTLING[board->stack->castling]);
	key_xor(key, &KEY_ENPASSANT[board->stack->enpassant]);
}

/* Update the key after a move is made */
void key_update(Key *key, const Board *board, const Move move) {
	const Square from = move_from(move);
	const Square to = move_to(move);
	CPiece cp = board->cpiece[from];
	Piece p = cpiece_piece(cp);
	const Color c = cpiece_color(cp);
	const CPiece victim = board->cpiece[to];
	const BoardStack *stack = board->stack;
	Square x, enpassant = ENPASSANT_NONE;

	*key = stack->key;

	// move the piece
	key_xor(key, &KEY_SQUARE[from][cp]);
	key_xor(key, &KEY_SQUARE[to][cp]);
	// capture
	if (victim) key_xor(key, &KEY_SQUARE[to][victim]);
	// pawn move
	if (p == PAWN) {
		if ((p = move_promotion(move))) {
			key_xor(key, &KEY_SQUARE[to][cp]);
			key_xor(key, &KEY_SQUARE[to][cpiece_make(p, c)]);
		} else if (stack->enpassant == to) {
			x = square(file(to), rank(from));
			key_xor(key, &KEY_SQUARE[x][cpiece_make(PAWN, opponent(c))]);
		} else if (abs(to - from) == 16 && (MASK[to].enpassant & (board->color[opponent(c)] & board->piece[PAWN]))) enpassant = (from + to) / 2;
	// castling
	} else if (p == KING) {
		if (to == from + 2) {
			cp = board->cpiece[from + 3];
			key_xor(key, &KEY_SQUARE[from + 3][cp]);
			key_xor(key, &KEY_SQUARE[from + 1][cp]);
		} else if (to == from - 2) {
			cp = board->cpiece[from - 4];
			key_xor(key, &KEY_SQUARE[from - 4][cp]);
			key_xor(key, &KEY_SQUARE[from - 1][cp]);
		}
	}
	// miscellaneous
	key_xor(key, &KEY_CASTLING[stack->castling]);
	key_xor(key, &KEY_CASTLING[stack->castling & MASK_CASTLING[from] & MASK_CASTLING[to]]);
	key_xor(key, &KEY_ENPASSANT[stack->enpassant]);
	key_xor(key, &KEY_ENPASSANT[enpassant]);
	key_xor(key, &KEY_PLAY);
}


/* compute slider attack to feed array accessed by magic index */
Bitboard compute_slider_attack(const int x, const Bitboard pieces, const int d[4][2]) {
	Bitboard a = 0, b;
	int i, r, f;

	for (i = 0; i < 4; i++) {
		for (r = rank(x) + d[i][0], f = file(x) + d[i][1]; 0 <= r && r < 8 && 0 <= f && f < 8; r += d[i][0], f += d[i][1]) {
			b = 1ull << square(f, r);
			a |= b;
			if ((pieces & b) != 0) break;
		}
	}

	return a;
}

/* Initialize some global constants */
void init(void) {
	Bitboard o, inside;
	int r, f, i, j, c;
	int x, y, z;
	static int d[64][64];
	Mask *mask;
	Random random[1];
	CPiece p;
	static const Bitboard rook_magic[BOARD_SIZE] = {
		0x808000645080c000, 0x208020001480c000, 0x4180100160008048, 0x8180100018001680, 0x4200082010040201, 0x8300220400010008, 0x3100120000890004, 0x4080004500012180,
		0x01548000a1804008, 0x4881004005208900, 0x0480802000801008, 0x02e8808010008800, 0x08cd804800240080, 0x8a058002008c0080, 0x0514000c480a1001, 0x0101000282004d00,
		0x2048848000204000, 0x3020088020804000, 0x4806020020841240, 0x6080420008102202, 0x0010050011000800, 0xac00808004000200, 0x0000010100020004, 0x1500020004004581,
		0x0004c00180052080, 0x0220028480254000, 0x2101200580100080, 0x0407201200084200, 0x0018004900100500, 0x100200020008e410, 0x0081020400100811, 0x0000012200024494,
		0x8006c002808006a5, 0x0004201000404000, 0x0005402202001180, 0x0000081001002100, 0x0000100801000500, 0x4000020080800400, 0x4005050214001008, 0x810100118b000042,
		0x0d01020040820020, 0x000140a010014000, 0x0420001500210040, 0x0054210010030009, 0x0004000408008080, 0x0002000400090100, 0x0000840200010100, 0x0000233442820004,
		0x800a42002b008200, 0x0240200040009080, 0x0242001020408200, 0x4000801000480480, 0x2288008044000880, 0x000a800400020180, 0x0030011002880c00, 0x0041110880440200,
		0x0002001100442082, 0x01a0104002208101, 0x080882014010200a, 0x0000100100600409, 0x0002011048204402, 0x0012000168041002, 0x080100008a000421, 0x0240022044031182
	};

	static const Bitboard bishop_magic[BOARD_SIZE] = {
		0x88b030028800d040, 0x018242044c008010, 0x0010008200440000, 0x4311040888800a00, 0x001910400000410a, 0x2444240440000000, 0x0cd2080108090008, 0x2048242410041004,
		0x8884441064080180, 0x00042131420a0240, 0x0028882800408400, 0x204384040b820200, 0x0402040420800020, 0x0000020910282304, 0x0096004b10082200, 0x4000a44218410802,
		0x0808034002081241, 0x00101805210e1408, 0x9020400208010220, 0x000820050c010044, 0x0024005480a00000, 0x0000200200900890, 0x808040049c100808, 0x9020202200820802,
		0x0410282124200400, 0x0090106008010110, 0x8001100501004201, 0x0104080004030c10, 0x0080840040802008, 0x2008008102406000, 0x2000888004040460, 0x00d0421242410410,
		0x8410100401280800, 0x0801012000108428, 0x0000402080300b04, 0x0c20020080480080, 0x40100e0201502008, 0x4014208200448800, 0x4050020607084501, 0x1002820180020288,
		0x800610040540a0c0, 0x0301009014081004, 0x2200610040502800, 0x0300442011002800, 0x0001022009002208, 0x0110011000202100, 0x1464082204080240, 0x0021310205800200,
		0x0814020210040109, 0xc102008208c200a0, 0xc100702128080000, 0x0001044205040000, 0x0001041002020000, 0x4200040408021000, 0x004004040c494000, 0x2010108900408080,
		0x0000820801040284, 0x0800004118111000, 0x0203040201108800, 0x2504040804208803, 0x0228000908030400, 0x0010402082020200, 0x00a0402208010100, 0x30c0214202044104
	};
    static const int pawn_dir[2][2] = {{-1, 1}, {1, 1}};
    static const int knight_dir[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1}};
    static const int bishop_dir[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
    static const int rook_dir[4][2] = {{-1, 0}, {0, -1}, {0, 1}, {1, 0}};
    static const int king_dir[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};

	// MASK initialisations
	MASK->bishop.attack = aligned_alloc(64, sizeof (Bitboard) * 0x1480);
	if (MASK->bishop.attack == NULL) memory_error(__func__);
	MASK->rook.attack = aligned_alloc(64, sizeof (Bitboard) * 0x19000);
	if (MASK->rook.attack == NULL) memory_error(__func__);
	for (x = 0; x < 64; ++x) {
		f = file(x);
		r = rank(x);
		mask = MASK + x;

		for (y = 0; y < 64; ++y) d[x][y] = 0;
		// directions & between
		for (i = 0; i < 8; ++i) 
		for (j = 1; j < 8; ++j) {
			y = square_safe(f + king_dir[i][0] * j, r + king_dir[i][1] * j);
			if (y != BOARD_OUT) {
				d[x][y] = king_dir[i][0] + 8 * king_dir[i][1];
				mask->direction[y] = abs(d[x][y]);
				for (z = x + d[x][y]; z != y; z += d[x][y]) mask->between[y] |= square_to_bit(z);
			}
		}			

		// diagonal / antidiagonal / rank / file
		for (y = x - 9; y >= 0 && d[x][y] == -9; y -= 9) mask->diagonal |= square_to_bit(y);
		for (y = x + 9; y < 64 && d[x][y] == 9; y += 9) mask->diagonal |= square_to_bit(y);
		for (y = x - 7; y >= 0 && d[x][y] == -7; y -= 7) mask->antidiagonal |= square_to_bit(y);
		for (y = x + 7; y < 64 && d[x][y] == 7; y += 7) mask->antidiagonal |= square_to_bit(y);
		mask->file = (COLUMN[f] ^ square_to_bit(x));
		mask->rank = (RANK[r] ^ square_to_bit(x));

		// pawns
		for (i = 0; i < 2; ++i) {
			mask->pawn_attack[WHITE] |= file_rank_to_bit(f + pawn_dir[i][0], r + pawn_dir[i][1]);
			mask->pawn_attack[BLACK] |= file_rank_to_bit(f - pawn_dir[i][0], r - pawn_dir[i][1]);
		}
		mask->pawn_push[WHITE] |= file_rank_to_bit(f - 1, r);
		mask->pawn_push[BLACK] |= file_rank_to_bit(f + 1, r);
		if (r == 3 || r == 4) {
			if (f > 0) mask->enpassant |= square_to_bit(x - 1);
			if (f < 7) mask->enpassant |= square_to_bit(x + 1);
		}

		// knight & king
		for (i = 0; i < 8; ++i) {
			mask->knight |= file_rank_to_bit(f + knight_dir[i][0], r + knight_dir[i][1]);
			mask->king |= file_rank_to_bit(f + king_dir[i][0], r + king_dir[i][1]);
		}


		inside = ~(((RANK[0] | RANK[7]) & ~RANK[r]) | ((COLUMN[0] | COLUMN[7]) & ~COLUMN[f]));

		//magic bishop
		mask->bishop.mask = (mask->diagonal | mask->antidiagonal) & inside;
		mask->bishop.shift = 64 - count_moves(mask->bishop.mask);
		mask->bishop.magic = bishop_magic[x];
		if (x) mask->bishop.attack = mask[-1].bishop.attack + (1u << count_moves(mask[-1].bishop.mask));
		o = 0; do {
			mask->bishop.attack[magic_index(o, &mask->bishop)] = compute_slider_attack(x, o, bishop_dir);
			o = (o - mask->bishop.mask) & mask->bishop.mask;
		} while (o);

		// magic rook
		mask->rook.mask = (mask->rank | mask->file) & inside;
		mask->rook.shift = 64 - count_moves(mask->rook.mask);
		mask->rook.magic = rook_magic[x];
		if (x) mask->rook.attack = mask[-1].rook.attack + (1u << count_moves(mask[-1].rook.mask));
		o = 0; do {
			mask->rook.attack[magic_index(o, &mask->rook)] = compute_slider_attack(x, o, rook_dir);
			o = (o - mask->rook.mask) & mask->rook.mask;
		} while (o);
	}

	// Hash key
	random_seed(random, 0xA170EBA);

	foreach_color (c) key_init(KEY_PLAYER + c, random);

	KEY_PLAY = KEY_PLAYER[WHITE];
	key_xor(&KEY_PLAY, &KEY_PLAYER[BLACK]);

	foreach_square (x)
	foreach_cpiece (p)
		key_init(&KEY_SQUARE[x][p], random);

	for (c = 1; c < 16; ++c) key_init(KEY_CASTLING + c, random);

	foreach_square (x) key_init(KEY_ENPASSANT + x, random);
	key_init(KEY_ENPASSANT + BOARD_SIZE, random);
}

/* check if an enpassant move is possible */
bool board_enpassant(const Board *board) {
	return board->stack->enpassant != ENPASSANT_NONE;
}

/* deplace a piece on the board */
void board_deplace_piece(Board *board, const Square from, const Square to) {
	const Bitboard b = square_to_bit(from) ^ square_to_bit(to);
	const CPiece cp = board->cpiece[from];
	const Piece p = cpiece_piece(cp);
	const Color c = cpiece_color(cp);

	board->piece[p] ^= b;
	board->color[c] ^= b;
	board->cpiece[to] = cp;
	board->cpiece[from] = EMPTY;
}

/* generate checker & pinned pieces */
void generate_checkers(Board *board) {
	const Color c = board->player;
	const Color o = opponent(c);
	const Square k = board->x_king[c];
	const Bitboard bq = (board->piece[BISHOP] + board->piece[QUEEN]) & board->color[o];
	const Bitboard rq = (board->piece[ROOK] + board->piece[QUEEN]) & board->color[o];
	const Bitboard pieces = board->color[WHITE] + board->color[BLACK];
	Bitboard partial_checkers;
	Bitboard b;
	Bitboard *pinned = &board->stack->pinned;
	Bitboard *checkers = &board->stack->checkers;
	Square x;

	*pinned = 0;

	// bishop or queen: all square reachable from the king square.
	b = bishop_attack(pieces, k, -1ull);

	//checkers
	*checkers = partial_checkers = b & bq;

	// pinned square
	b &= board->color[c];
	if (b) {
		b = bishop_attack(pieces ^ b, k, bq ^ partial_checkers);
		while (b) {
			x = square_next(&b);
			*pinned |= MASK[x].between[k] & board->color[c];
		}
	}

	// rook or queen: all square reachable from the king square.
	b = rook_attack(pieces, k, -1ull);

	// checkers = opponent rook or queen
	*checkers |= partial_checkers = b & rq;

	// pinned square
	b &= board->color[c];
	if (b) {
		b = rook_attack(pieces ^ b, k, rq ^ partial_checkers);
		while (b) {
			x = square_next(&b);
			*pinned |= MASK[x].between[k] & board->color[c];
		}
	}

	// other pieces (no more pins)
	*checkers |= knight_attack(k, board->piece[KNIGHT]);
	*checkers |= pawn_attack(k, c, board->piece[PAWN]);
	*checkers &= board->color[o];

	return;
}

/* Clear the board. Set all of its content to zeroes. */
void board_clear(Board *board) {
	memset(board, 0, sizeof (Board));
	board->stack = board->stack_;
}

/* Initialize the board to the starting position. */
void board_init(Board *board) {
	static const uint8_t cpiece[BOARD_SIZE] = {
		WROOK, WKNIGHT, WBISHOP, WQUEEN, WKING, WBISHOP, WKNIGHT, WROOK,
		WPAWN,WPAWN,WPAWN,WPAWN,WPAWN,WPAWN,WPAWN,WPAWN,
		EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,
		EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,
		EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,
		EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,EMPTY,
		BPAWN,BPAWN,BPAWN,BPAWN,BPAWN,BPAWN,BPAWN,BPAWN,
		BROOK, BKNIGHT, BBISHOP, BQUEEN, BKING, BBISHOP, BKNIGHT, BROOK
	};

	board_clear(board);
	memcpy(board->cpiece, cpiece, BOARD_SIZE);
	board->piece[PAWN] =   0x00ff00000000ff00ull;
	board->piece[KNIGHT] = 0x4200000000000042ull;
	board->piece[BISHOP] = 0x2400000000000024ull;
	board->piece[ROOK] =   0x8100000000000081ull;
	board->piece[QUEEN] =  0x0800000000000008ull;
	board->piece[KING] =   0x1000000000000010ull;
	board->color[WHITE] =  0x000000000000ffffull;
	board->color[BLACK] =  0xffff000000000000ull;
	board->stack->pinned = board->stack->checkers = 0;
	board->stack->castling = 15;
	board->stack->enpassant = ENPASSANT_NONE; // illegal enpassant square
	board->x_king[WHITE] = E1;
	board->x_king[BLACK] = E8;
	board->ply = 1;
	board->player = WHITE;

	key_set(&board->stack->key, board);
}

/* parse a FEN board description */
void board_set(Board *board, char *string) {
	char *s = string;
	Square x;
	int r, f;
	CPiece p;

	if (!s || *s == '\0') return;
	board_clear(board);
	// board
	r = 7, f = 0;
	do {
		if (*s == '/') {
			if (r <= 0) parse_error(string, s, "FEN: too many ranks");
			if (f != 8) parse_error(string, s, "FEN: missing square");
			f = 0; r--;
		} else if (isdigit((int)*s)) {
			f += (Square) (*s - '0');
			if (f > 8) parse_error(string, s, "FEN: file overflow");
		} else {
			if (f > 8) parse_error(string, s, "FEN: file overflow");
			x = square(f, r);
			board->cpiece[x] = p = cpiece_from_char(*s);
			if (board->cpiece[x] == CPIECE_SIZE) parse_error(string, s, "FEN: bad piece");
			board->piece[cpiece_piece(p)] |= square_to_bit(x);
			board->color[cpiece_color(p)] |= square_to_bit(x);
			if (cpiece_piece(p) == KING) board->x_king[cpiece_color(p)] = x;
			f++;
		}
		++s;
	} while (*s && *s != ' ');
	if (r < 0 || f != 8) parse_error(string, s, "FEN: missing square");
	// turn
	if (*s++ != ' ') parse_error(string, s, "FEN: missing space before player's turn");
	board->player = (uint8_t) color_from_char(*s);
	if (board->player == COLOR_SIZE) parse_error(string, s, "FEN: bad player's turn");
	++s;
	// castling
	s = parse_next(s);
	if (*s == '-') s++;
	else {
		while (*s && *s != ' ') {
			board->stack->castling |= castling_from_char(*s);
			s++;
		}
	}
	// correct castling
	if (board->cpiece[E1] == WKING) {
		if (board->cpiece[H1] != WROOK) board->stack->castling &= ~1;
		if (board->cpiece[A1] != WROOK) board->stack->castling &= ~2;
	} else board->stack->castling &= ~3;
	if (board->cpiece[E8] == BKING) {
		if (board->cpiece[H8] != BROOK) board->stack->castling &= ~4;
		if (board->cpiece[A8] != BROOK) board->stack->castling &= ~8;
	} else board->stack->castling &= ~12;
	// en passant
	x = 64;
	s = parse_next(s);
	if (*s == '-') s++;
	else if (!square_parse(&s, &x)) parse_error(string, s, "FEN: bad enpassant square");
	board->stack->enpassant = x;
	// update other chess board structure
	key_set(&board->stack->key, board);
	generate_checkers(board);
}

/* Create a board structure */
Board* board_create(void) {
	Board *board = aligned_alloc(64, sizeof (Board));
	if (board == NULL) memory_error(__func__);
	board_init(board);
	return board;
}

/* Destroy a board structure */
void board_destroy(Board* board) {
	free(board);
}

/* Play a move on the board. */
void board_update(Board *board, const Move move) {
	const Square from = move_from(move);
	const Square to = move_to(move);
	CPiece cp = board->cpiece[from];
	Piece p = cpiece_piece(cp);
	const Color c = cpiece_color(cp);
	const Bitboard b_from = square_to_bit(from);
	const Bitboard b_to = square_to_bit(to);
	const CPiece victim = board->cpiece[to];
	const BoardStack *current = board->stack;
	BoardStack *next = board->stack + 1;
	Square x;
	Bitboard b;

	// update chess board informations
	next->castling = current->castling;
	next->enpassant = 64;
	next->victim = 0;
	next->castling &= MASK_CASTLING[from] & MASK_CASTLING[to];
	// move the piece
	board->piece[p] ^= b_from;
	board->piece[p] ^= b_to;
	board->color[c] ^= b_from | b_to;
	board->cpiece[from] = EMPTY;
	board->cpiece[to] = cp;
	// capture
	if (victim) {
		board->piece[cpiece_piece(victim)] ^= b_to;
		board->color[cpiece_color(victim)] ^= b_to;
		next->victim = victim;
	}
	// special pawn move
	if (p == PAWN) {
		if ((p = move_promotion(move))) {
			cp = cpiece_make(p, c);
			board->piece[PAWN] ^= b_to;
			board->piece[p] ^= b_to;
			board->cpiece[to] = cp;
		} else if (current->enpassant == to) {
			x = square(file(to), rank(from));
			b = square_to_bit(x);
			board->piece[PAWN] ^= b;
			board->color[opponent(c)] ^= b;
			board->cpiece[x] = EMPTY;
		} else if (abs(to - from) == 16 && (MASK[to].enpassant & (board->color[opponent(c)] & board->piece[PAWN]))) {
			next->enpassant = (from + to) / 2;
		}
	// king move
	} else if (p == KING) {
		board->x_king[c] = to;
		if (to == from + 2) board_deplace_piece(board, from + 3, from + 1);
		else if (to == from - 2) board_deplace_piece(board, from - 4, from - 1);
	}

	++board->stack;
	++board->ply;
	board->player = opponent(board->player);

	generate_checkers(board);
}

/* Undo a move from the board.*/
void board_restore(Board *board, const Move move) {
	const Square from = move_from(move);
	const Square to = move_to(move);
	CPiece cp = board->cpiece[to];
	Piece p = cpiece_piece(cp);
	const Color c = cpiece_color(cp);
	const Bitboard b_from = square_to_bit(from);
	const Bitboard b_to = square_to_bit(to);
	const CPiece victim = board->stack->victim;

	// miscellaneous
	--board->stack;
	--board->ply;
	board->player = opponent(board->player);

	// move the piece (with promotion)
	board->piece[p] ^= b_to;
	if (move_promotion(move)) {
		p = PAWN;
		cp = cpiece_make(PAWN, c);
	}
	board->piece[p] ^= b_from;
	board->color[c] ^= b_from | b_to;
	board->cpiece[to] = EMPTY;
	board->cpiece[from] = cp;
	// capture
	if (victim) {
		board->piece[cpiece_piece(victim)] ^= b_to;
		board->color[cpiece_color(victim)] ^= b_to;
		board->cpiece[to] = victim;
	}
	// enpassant capture
	if (p == PAWN && board->stack->enpassant == to) {
		const Square x = square(file(to), rank(from));
		const Bitboard b = square_to_bit(x);
		board->piece[PAWN] ^= b;
		board->color[opponent(c)] ^= b;
		board->cpiece[x] = cpiece_make(PAWN, opponent(c));
	}
	// king move
	if (p == KING) {
		board->x_king[c] = from;
		if (to == from + 2) board_deplace_piece(board, from + 1, from + 3);
		else if (to == from - 2) board_deplace_piece(board, from - 1, from - 4);
	}
}

/* Print the board. */
void board_print(const Board *board, FILE *output) {
	Square x;
	int f, r;
	const char p[] = ".PpNnBbRrQqKk#";
	const char c[] = "wb";
	const Square ep = board->stack->enpassant;

	fputs("  a b c d e f g h\n", output);
	for (r = 7; r >= 0; --r)
	for (f = 0; f <= 7; ++f) {
		x = square(f, r);
		if (f == 0) fprintf(output, "%1d ", r + 1);
		fputc(p[board->cpiece[x]], output); fputc(' ', output);
		if (f == 7) fprintf(output, "%1d\n", r + 1);
	}
	fputs("  a b c d e f g h\n", output);
	fprintf(output, "%c, ", c[board->player]);
	if (board->stack->castling & CAN_CASTLE_KINGSIDE[WHITE]) fputc('K', output);
	if (board->stack->castling & CAN_CASTLE_QUEENSIDE[WHITE]) fputc('Q', output);
	if (board->stack->castling & CAN_CASTLE_KINGSIDE[BLACK]) fputc('k', output);
	if (board->stack->castling & CAN_CASTLE_QUEENSIDE[BLACK]) fputc('q', output);
	if (board_enpassant(board))	fprintf(output, ", ep: %c%c", file(ep) + 'a', rank(ep) + '1');
	fprintf(output, "\n");
}


/* Check if a square is attacked.*/
bool board_is_square_attacked(const Board *board, const Square x, const Color c) {
	const Bitboard occupied = board->color[WHITE] + board->color[BLACK];
	const Bitboard C = board->color[c];

	return bishop_attack(occupied, x, C & (board->piece[BISHOP] | board->piece[QUEEN]))
	    || rook_attack(occupied, x, C & (board->piece[ROOK] | board->piece[QUEEN]))
	    || knight_attack(x, C & board->piece[KNIGHT])
	    || pawn_attack(x, opponent(c), C & board->piece[PAWN])
	    || king_attack(x, C & board->piece[KING]);
}

/* Append a move to an array of moves */
Move* push_move(Move *move, const Square from, const Square to) {
	*move++ = from | (to << 6);
	return move;
}

/* Append promotions from the same move */
Move* push_promotion(Move *move, const Square from, const Square to) {
	const Move m = from | (to << 6);

	*move++ = m | QUEEN_PROMOTION;
	*move++ = m | KNIGHT_PROMOTION;
	*move++ = m | ROOK_PROMOTION;
	*move++ = m | BISHOP_PROMOTION;
	return move;
}

/* Append all moves from a square */
Move* push_moves(Move *move, Bitboard attack, const Square from) {
	Square to;

	while (attack) {
		to = square_next(&attack);
		move = push_move(move, from, to);
	}
	return move;
}

/* Append all pawn moves from a direction */
Move* push_pawn_moves(Move *move, Bitboard attack, const int dir) {
	Square to;

	while (attack) {
		to = square_next(&attack);
		move = push_move(move, to - dir, to);
	}
	return move;
}

/* Append all promotions from a direction */
Move *push_promotions(Move *move, Bitboard attack, const int dir) {
	Square to;

	while (attack) {
		to = square_next(&attack);
		move = push_promotion(move, to - dir, to);
	}
	return move;
}

/* Generate all legal moves */
int generate_moves(Board *board, Move *move, const bool generate, const bool do_quiet) {
	const Color c = board->player;
	const Color o = opponent(c);
	const Bitboard occupied = board->color[WHITE] + board->color[BLACK];
	const Bitboard bq = board->piece[BISHOP] | board->piece[QUEEN];
	const Bitboard rq = board->piece[ROOK] | board->piece[QUEEN];
	const Bitboard pinned = board->stack->pinned;
	const Bitboard unpinned = board->color[c] & ~pinned;
	const Bitboard checkers = board->stack->checkers;
	const Square k = board->x_king[c];
	const int pawn_left = PUSH[c] - 1;
	const int pawn_right = PUSH[c] + 1;
	const int pawn_push = PUSH[c];
	const int *dir = MASK[k].direction;
	const Move *start = move;
	Bitboard target, piece, attack;
	Bitboard empty = ~occupied;
	Bitboard enemy = board->color[o];
	Square from, to, ep, x_checker = ENPASSANT_NONE;
	int d, count = 0;

	// in check: capture or block the (single) checker if any;
	if (checkers) {
		if (is_single(checkers)) {
			x_checker = square_first(checkers);
			empty = MASK[k].between[x_checker];
			enemy = checkers;
		} else {
			empty = enemy  = 0;
		}

	// not in check: castling & pinned pieces moves
	} else {
		target = enemy; if (do_quiet) target |= empty;
		// castling
		if (do_quiet) {
			if ((board->stack->castling & CAN_CASTLE_KINGSIDE[c])
				&& (occupied & MASK[k].between[k + 3]) == 0
				&& !board_is_square_attacked(board, k + 1, o)
				&& !board_is_square_attacked(board, k + 2, o)) {
					if (generate) move = push_move(move, k, k + 2); else ++count;
			}
			if ((board->stack->castling & CAN_CASTLE_QUEENSIDE[c])
				&& (occupied & MASK[k].between[k - 4]) == 0
				&& !board_is_square_attacked(board, k - 1, o)
				&& !board_is_square_attacked(board, k - 2, o)) {
					if (generate) move = push_move(move, k, k - 2); else ++count;
			}
		}
		// pawn (pinned)
		piece = board->piece[PAWN] & pinned;
		while (piece) {
			from = square_next(&piece);
			d = dir[from];
			if (d == abs(pawn_left) && (square_to_bit(to = from + pawn_left) & pawn_attack(from, c, enemy))) {
				if (generate) move = is_on_seventh_rank(from, c) ? push_promotion(move, from, to) : push_move(move,from, to);
				else count += is_on_seventh_rank(from, c) ? 4 : 1;

			} else if (d == abs(pawn_right) && (square_to_bit(to = from + pawn_right) & pawn_attack(from, c, enemy))) {
				if (generate) move = is_on_seventh_rank(from, c) ? push_promotion(move, from, to) : push_move(move,from, to);
				else count += is_on_seventh_rank(from, c) ? 4 : 1;
			}
			if (do_quiet && d == abs(pawn_push) && (square_to_bit(to = from + pawn_push) & empty)) {
				if (generate) move = push_move(move, from, to); else ++count;
				if (is_on_second_rank(from, c) && (square_to_bit(to += pawn_push) & empty)) {
					if (generate) move = push_move(move, from, to); else ++count;
				}
			}
		}
		// bishop or queen (pinned)
		piece = bq & pinned;
		while (piece) {
			from = square_next(&piece);
			d = dir[from];
			attack = 0;
			if (d == 9) attack = bishop_attack(occupied, from, target & MASK[from].diagonal);
			else if (d == 7) attack = bishop_attack(occupied, from, target & MASK[from].antidiagonal);
			if (generate) move = push_moves(move, attack, from); else count += count_moves(attack);
		}
		// rook or queen (pinned)
		piece = rq & pinned;
		while (piece) {
			from = square_next(&piece);
			d = dir[from];
			attack = 0;
			if (d == 1) attack = rook_attack(occupied, from, target & MASK[from].rank); 
			else if (d == 8) attack = rook_attack(occupied, from, target & MASK[from].file);
			if (generate) move = push_moves(move, attack, from); else count += count_moves(attack);			
		}
	}
	// common moves

	target = enemy; if (do_quiet) target |= empty;

	// enpassant capture
	if (board_enpassant(board) && (!checkers || x_checker == board->stack->enpassant - pawn_push)) {
		to = board->stack->enpassant;
		ep = to - pawn_push;
		from = ep - 1;
		if (file(to) > 0 && board->cpiece[from] == cpiece_make(PAWN, c)) {
			piece = occupied ^ square_to_bit(from) ^ square_to_bit(ep) ^ square_to_bit(to);
			if (!bishop_attack(piece, k, bq & board->color[o]) && !rook_attack(piece, k, rq & board->color[o])) {
				if (generate) move = push_move(move, from, to); else ++count;
			}
		}
		from = ep + 1;
		if (file(to) < 7 && board->cpiece[from] == cpiece_make(PAWN, c)) {
			piece = occupied ^ square_to_bit(from) ^ square_to_bit(ep) ^ square_to_bit(to);
			if (!bishop_attack(piece, k, bq & board->color[o]) && !rook_attack(piece, k, rq & board->color[o])) {
				if (generate) move = push_move(move, from, to); else ++count;
			}
		}
	}

	// pawn
	piece = board->piece[PAWN] & unpinned;
	attack = (c ? (piece & ~COLUMN[0]) >> 9 : (piece & ~COLUMN[0]) << 7) & enemy;
	if (generate) {
		move = push_promotions(move, attack & PROMOTION_RANK[c], pawn_left);
		move = push_pawn_moves(move, attack & ~PROMOTION_RANK[c], pawn_left);
	} else count += 4 * count_moves(attack & PROMOTION_RANK[c]) + count_moves(attack & ~PROMOTION_RANK[c]);

	attack = (c ? (piece & ~COLUMN[7]) >> 7 : (piece & ~COLUMN[7]) << 9) & enemy;
	if (generate) {
		move = push_promotions(move, attack & PROMOTION_RANK[c], pawn_right);
		move = push_pawn_moves(move, attack & ~PROMOTION_RANK[c], pawn_right);
	} else count += 4 * count_moves(attack & PROMOTION_RANK[c]) + count_moves(attack & ~PROMOTION_RANK[c]);

	attack = (c ? piece >> 8 : piece << 8) & empty;
	if (generate) {
		move = push_promotions(move, attack & PROMOTION_RANK[c], pawn_push);
	} else count += 4 * count_moves(attack & PROMOTION_RANK[c]);
	if (do_quiet) {
		if (generate) {
			move = push_pawn_moves(move, attack & ~PROMOTION_RANK[c], pawn_push);
		} else count += count_moves(attack & ~PROMOTION_RANK[c]);
		attack = (c ? (((piece & RANK[6]) >> 8) & ~occupied) >> 8 : (((piece & RANK[1]) << 8) & ~occupied) << 8) & empty;
		if (generate) move = push_pawn_moves(move, attack, 2 * pawn_push); else count += count_moves(attack);
	}
	
	// knight
	piece = board->piece[KNIGHT] & unpinned;
	while (piece) {
		from = square_next(&piece);
		attack = knight_attack(from, target);
		if (generate) move = push_moves(move, attack, from); else count += count_moves(attack);
	}

	// bishop or queen
	piece = bq & unpinned;
	while (piece) {
		from = square_next(&piece);
		attack = bishop_attack(occupied, from, target);
		if (generate) move = push_moves(move, attack, from); else count += count_moves(attack);
	}

	// rook or queen
	piece = rq & unpinned;
	while (piece) {
		from = square_next(&piece);
		attack = rook_attack(occupied, from, target);
		if (generate) move = push_moves(move, attack, from); else count += count_moves(attack);
	}

	// king
	board->color[c] ^= square_to_bit(k);
	target = board->color[o]; if (do_quiet) target |= ~occupied;
	attack = king_attack(k, target);
	while (attack) {
		to = square_next(&attack);
		if (!board_is_square_attacked(board, to, o)) {
			if (generate) move = push_move(move, k, to); else ++count;
		}
	}
	board->color[c] ^= square_to_bit(k);

	if (generate) count = move - start;

	return count;
}

/* Generate all legal moves or captures */
void movearray_generate(MoveArray *ma, Board *board,  const bool do_quiet) {
	ma->i = 0;
	ma->n = generate_moves(board, ma->move, true, do_quiet);
	ma->move[ma->n] = 0;
}

/* Get next move */
Move movearray_next(MoveArray *ma) {
	return ma->move[ma->i++];
}

/* Hash initialisation */
HashTable* hash_create(const int b) {
	const size_t n = 1ULL << b;
	size_t i;
	const Hash hash_init = {0, 0, 0};

	HashTable *hashtable = malloc(sizeof (HashTable));
	if (hashtable == NULL) memory_error(__func__);
	hashtable->hash = aligned_alloc(64, (n + BUCKET_SIZE) * sizeof (Hash));
	if (hashtable->hash == NULL) memory_error(__func__);
	hashtable->mask = (n - 1) & ~3;

	for (i = 0; i <= hashtable->mask + BUCKET_SIZE; ++i) hashtable->hash[i] = hash_init;

	return hashtable;
}

/* Hash free resources */
void hash_destroy(HashTable *hashtable) {
	if (hashtable) free(hashtable->hash);
	free(hashtable);
}

/* Hash probe */
uint64_t hash_probe(const HashTable *hashtable, const Key *key, const int depth) {
	Hash *hash = hashtable->hash + (key->index & hashtable->mask);

	for (int i = 0; i < BUCKET_SIZE; ++i) {
		if (hash[i].code == key->code && hash[i].depth == depth) return hash[i].count;
	}
	return 0;
}

/* Hash store */
void hash_store(const HashTable *hashtable, const Key *key, const int depth, const uint64_t count) {
	Hash *hash = (hashtable->hash + (key->index & hashtable->mask));
	int i, j;

	for (i = j = 0; i < BUCKET_SIZE; ++i) {
		if (hash[i].code == key->code && hash[i].depth == depth) return;
		if (hash[i].depth < hash[j].depth) j = i;
	}

	hash[j].code = key->code;
	hash[j].depth = depth;
	hash[j].count = count;
}

/* Prefetch */
void hash_prefetch(HashTable *hashtable, const Key *key) {
	#if defined(USE_GCC_X64)
		__builtin_prefetch(hashtable->hash + (key->index & hashtable->mask));
	#endif
}

/* Recursive Perft with optional hashtable, bulk counting & capture only generation */
uint64_t perft(Board *board, HashTable *hashtable, const int depth, const bool bulk, const bool do_quiet) {
	uint64_t count = 0, hash_count;
	Move move;
	MoveArray ma[1];
	const bool use_hash = (hashtable && depth > 2);
	Key *key = &board->stack[1].key;

	movearray_generate(ma, board, do_quiet | board->stack->checkers);

	while ((move = movearray_next(ma)) != 0) {
		if (use_hash) {
			key_update(key, board, move);
			hash_prefetch(hashtable, key);
		}
		board_update(board, move);
			if (depth == 1) ++count;
			else if (bulk && depth == 2) count += generate_moves(board, NULL, false, do_quiet);
			else {
				if (use_hash) {
					hash_count = hash_probe(hashtable, key, depth - 1);
					if (hash_count == 0) {
						hash_count = perft(board, hashtable, depth - 1, bulk, do_quiet);
						hash_store(hashtable, key, depth - 1, hash_count);
					}
					count += hash_count;
				} else count += perft(board, hashtable, depth - 1, bulk, do_quiet);
			}
		board_restore(board, move);
	}

	return count;
}

/* test */
void test(Board *board) {
	typedef struct TestBoard {
		char *comments, *fen;
		unsigned long long result;
		int depth;
	} TestBoard;
	TestBoard tests[] = {
		{"1. Initial position ", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 119060324, 6},
		{"2.", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", 193690690, 5},
		{"3.", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -", 178633661, 7},
		{"4.", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 706045033, 6},
		{"5.", "rnbqkb1r/pp1p1ppp/2p5/4P3/2B5/8/PPP1NnPP/RNBQK2R w KQkq - 0 6", 53392, 3},
		{"6.", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 6923051137, 6},
		{"7.", "8/5bk1/8/2Pp4/8/1K6/8/8 w - d6 0 1", 824064, 6},
		{"8. Enpassant capture gives check", "8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 1440467, 6},
		{"9. Short castling gives check", "5k2/8/8/8/8/8/8/4K2R w K - 0 1", 661072, 6},
		{"10. Long castling gives check", "3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", 803711, 6},
		{"11. Castling", "r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", 1274206, 4},
		{"12. Castling prevented", "r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1", 1720476, 4},
		{"13. Promote out of check", "2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1", 3821001, 6},
		{"14. Discovered check", "8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1", 1004658, 5},
		{"15. Promotion gives check", "4k3/1P6/8/8/8/8/K7/8 w - - 0 1", 217342, 6},
		{"16. Underpromotion gives check", "8/P1k5/K7/8/8/8/8/8 w - - 0 1", 92683, 6},
		{"17. Self stalemate", "K1k5/8/P7/8/8/8/8/8 w - - 0 1", 2217, 6},
		{"18. Stalemate/Checkmate", "8/k1P5/8/1K6/8/8/8/8 w - - 0 1", 567584, 7},
		{"19. Double check", "8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1", 23527, 4},
		{NULL, NULL, 0, 0}
	};

	printf("Testing the board generator\n");
	for (TestBoard *t = tests; t->fen != NULL; ++t) {
		printf("Test %s %s", t->comments, t->fen); fflush(stdout);
		board_set(board, t->fen);
		unsigned long long count = perft(board, NULL, t->depth, true, true);
		if (count == t->result) printf(" passed\n"); else printf(" FAILED ! %llu != %llu\n", count, t->result);
	}
	board_destroy(board);
}

/* main */
int main(int argc, char **argv) {
	double time = -chrono(), partial = 0.0;
	Board *board;
	HashTable *hashtable = NULL;
	Move move;
	MoveArray ma[1];
	unsigned long long count, total = 0;
	int i, d, depth = 6, hash_size = 0;
	bool div = false, capture = false, bulk = false, loop = false;
	char *fen = NULL;

	puts("Magic Perft (c) version 1.0 Richard Delorme - 2020");
#if USE_PEXT
	puts("Bitboard move generation based on magic (pext) bitboards");
#else
	puts("Bitboard move generation based on magic bitboards");
#endif

	// pre-initialisation
	init();
	board = board_create();

	// argument
	for (i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--fen") || !strcmp(argv[i], "-f")) fen = argv[++i];
		else if (!strcmp(argv[i], "--depth") || !strcmp(argv[i], "-d")) depth = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--bulk") || !strcmp(argv[i], "-b")) bulk = true;
		else if (!strcmp(argv[i], "--div") || !strcmp(argv[i], "-r")) div = true;
		else if (!strcmp(argv[i], "--capture") || !strcmp(argv[i], "-c")) capture = true;
		else if (!strcmp(argv[i], "--loop") || !strcmp(argv[i], "-l")) loop = true;
		else if (!strcmp(argv[i], "--hash") || !strcmp(argv[i], "-H")) hash_size = atoi(argv[++i]);
		else if (isdigit((int) argv[i][0])) depth = atoi(argv[i]);
		else if (!strcmp(argv[i], "--test") || !strcmp(argv[i], "-t")) {
			test(board);
			return 0;
		} else {
			printf("%s [--fen|-f <fen>] [--depth|-d <depth>] [--hash|-H <size>] [--bulk|-b] [--div] [--capture] | [--help|-h] | [--test|-t]\n", argv[0]);
			puts("Enumerate moves.");
			puts("\t--help|-h            Print this message.");
			puts("\t--fen|-f <fen>       Test the position indicated in FEN format (default=starting position).");
			puts("\t--depth|-d <depth>   Test up to this depth (default=6).");
			puts("\t--bulk|-b            Do fast bulk counting at the last ply.");
			puts("\t--hash|-H <size>     Use a hashtable with <size> bits entries (default 0, no hashtable).");
			puts("\t--capture|-c         Generate only captures, promotions & check evasions.");
			puts("\t--loop|-l            Loop from depth 1 to <depth>.");
			puts("\t--div|-r             Print a node count for each move.");
			puts("\t--test|-t            Run an internal test to check the move generator.");
			return 0;
		}
	}
	
	// post-initialisation
	if (hash_size > 32) hash_size = 32;
	if (hash_size > 0) hashtable = hash_create(hash_size);
	if (fen) board_set(board, fen);
	if (depth < 1) depth = 1;

	printf("Perft setting: ");
	if (hash_size == 0) printf("no hashing; ");
	else printf("hashtable size: %u Mbytes; ", (unsigned) (sizeof (Hash) * (hashtable->mask + BUCKET_SIZE + 1) >> 20));
	if (bulk) printf("with"); else printf("no"); printf(" bulk counting;");
	if (capture) printf(" capture only;");
	puts("");
	board_print(board, stdout);

	// root search
	if (div) {
		movearray_generate(ma, board, !capture);
		while ((move = movearray_next(ma)) != 0) {
			board_update(board, move);
				count = depth <= 1 ? 1 : perft(board, hashtable, depth - 1, bulk, !capture);
				total += count;
				printf("%5s %16llu\n", move_to_string(move, NULL), count);
			board_restore(board, move);
		}
	} else {
		for (d = (loop ? 1 : depth); d <= depth; ++d) {
			partial = -chrono();
			count = perft(board, hashtable, d, bulk, !capture);
			total += count;
			partial += chrono();
			printf("perft %2d : %15llu leaves in %10.3f s %12.0f leaves/s\n", d, count, partial, count / partial);
		}
	}
	time += chrono();
	if (div || loop) printf("total    : %15llu leaves in %10.3f s %12.0f leaves/s\n", total, time, total / time);

	board_destroy(board);
	hash_destroy(hashtable);
	free(MASK->bishop.attack);
	free(MASK->rook.attack);

	return 0;
}

