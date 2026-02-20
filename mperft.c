/**
 * @file mperft.c
 *
 * @brief perft using magic bitboard, bulk counting, multithreading & transposition table.
 *
 * @author Richard Delorme
 * @copyright 2020-2026
 * @version 4.0
 */

/* includes */
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#if defined(_WIN32)
	#include <intrin.h>
#elif defined(__x86_64__)
	#include <x86intrin.h>
#endif

#if defined(__linux__)
	#include <unistd.h>
	#include <sys/sysinfo.h>
#endif

// include stdbit.h if available, otherwise partially implement it.
#if __has_include(<stdbit.h>)
	#include <stdbit.h>
#else
	static inline bool stdc_has_single_bit_ull(const unsigned long long x) { return x && !(x & (x - 1));}
	#if defined(_MSC_VER)
		static inline unsigned int stdc_count_ones_ull(const unsigned long long int x) { return __popcnt64(x); }
		static inline unsigned int stdc_count_zeros_ull(const unsigned long long int x) { return __popcnt64(~x); }
		static inline unsigned int stdc_trailing_zeros_ull(const unsigned long long x) { return x ? _tzcnt_u64(x) : 64; }
		static inline unsigned long long stdc_bit_floor_ull(const unsigned long long x) { return x ? 1ull << (63 - __lzcnt64(x)) : x;}
	#elif defined(__GNUC__)
		static inline unsigned int stdc_count_ones_ull(const unsigned long long int x) { return __builtin_popcountll(x); }
		static inline unsigned int stdc_count_zeros_ull(const unsigned long long int x) { return __builtin_popcountll(~x); }
		static inline unsigned int stdc_trailing_zeros_ull(const unsigned long long x) { return x ? __builtin_ctzll(x) : 64; }
		static inline unsigned long long stdc_bit_floor_ull(const unsigned long long x) { return x ? 1ull << (63 - __builtin_clzll(x)) : x;}
	#endif
#endif

// fast PEXT availability
#if (defined(__BMI2__) && !defined(__znver1__) && !defined(__znver2__))
	#define HAS_PEXT 1
#endif

/** Types */
/** Limis: Game size & Move size */
typedef enum {GAME_SIZE = 4096, MOVE_SIZE = 256} Limits;

/** Bitboard: 64-bit unsigned integer representing a bitboard */
typedef uint64_t Bitboard;

/** Random: 64-bit unsigned integer representing a random number */
typedef uint64_t Random;

/** Color: Enum representing the color of a piece */
typedef enum { WHITE, BLACK, COLOR_SIZE } Color;

/** PerftLimits: Enum representing the limits to split the search or to probe the hash */
typedef enum { MAX_SPLIT = 16, MIN_SPLIT_DEPTH = 4, MIN_SPLIT_REMAINING_MOVES = 3, MIN_HASH_DEPTH = 3 } PerftLimits;

/** Square: Enum representing the squares on the board */
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

/** Piece: Enum representing the pieces on the board */
typedef enum { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, PIECE_SIZE } Piece;

/** CPiece: Enum representing the pieces on the board */
typedef enum { EMPTY, WPAWN, BPAWN, WKNIGHT, BKNIGHT, WBISHOP, BBISHOP, WROOK, BROOK, WQUEEN, BQUEEN, WKING, BKING, CPIECE_SIZE } CPiece;

/** Promotion: Enum representing the promotions */
typedef enum { KNIGHT_PROMOTION = KNIGHT << 15, BISHOP_PROMOTION = BISHOP << 15, ROOK_PROMOTION = ROOK << 15, QUEEN_PROMOTION = QUEEN << 15 } Promotion;

/** Move: Enum representing the moves on the board */
typedef uint32_t Move;

/** Key: Struct representing a key for a hash table */
typedef struct {
	uint64_t code; ///< Hash code for the key
	uint32_t index; ///< Index for the key
} Key;

/** Attack: Struct to compute the target squares of a sliding piece on the board */
typedef struct {
	Bitboard mask; ///< Bitboard mask for the attack
	Bitboard magic; ///< Bitboard magic for the attack
	Bitboard shift; ///< Bitboard shift for the attack
	Bitboard *attack; ///< Bitboard attacks for the attack
} Attack;

/** Mask: Struct to compute various bitboard mask on the board */
typedef struct {
	Bitboard between[BOARD_SIZE]; ///< Bitboard mask for the squares between two squares
	int direction[BOARD_SIZE]; ///< Bitboard direction
	Bitboard diagonal; ///< Bitboard mask for the diagonal squares
	Bitboard antidiagonal; ///< Bitboard mask for the antidiagonal squares
	Bitboard file; ///< Bitboard mask for the file squares
	Bitboard rank; ///< Bitboard mask for the rank squares
	Bitboard pawn_attack[COLOR_SIZE]; ///< Bitboard mask for the pawn attack squares
	Bitboard pawn_push[COLOR_SIZE]; ///< Bitboard mask for the pawn push squares
	Bitboard enpassant; ///< Bitboard mask for the enpassant squares
	Bitboard knight; ///< Bitboard mask for the knight squares
	Bitboard king; ///< Bitboard mask for the king squares
	Attack bishop; ///< Bitboard mask for the bishop squares
	Attack rook; ///< Bitboard mask for the rook squares
} Mask;

/** Board: Struct to represent the chess board */
typedef struct {
	Bitboard piece[PIECE_SIZE]; ///< Bitboard mask for the pieces
	Bitboard color[COLOR_SIZE]; ///< Bitboard mask for the pieces' colors
	Bitboard pinned; ///< Bitboard mask for the pinned squares
	Bitboard checkers; ///< Bitboard mask for the checkers squares
	Key key; ///< Zobrist key
	int ply; ///< ply counter
	Square x_king[COLOR_SIZE]; ///< The king squares
	Color player; ///< Current player color
	uint8_t castling; ///< Bitboard mask for the castling squares
	uint8_t enpassant; ///< The enpassant square
} Board;

/** MoveArray: Struct to represent an array of moves */
typedef struct {
	Move move[MOVE_SIZE]; ///< Array of moves
	int n; ///< Number of moves in the array
	int i; ///< Index of the current move
} MoveArray;

/** Hash: Struct to represent an entry in the transposition table */
typedef struct {
	uint64_t code; ///< Hash code
	uint64_t data; ///< Hash data : count (58 bits) | depth (6 bits)
} Hash;

/** HashTable: Struct to represent a hash table */
typedef struct {
	Hash *hash; ///< Array of hash entries
	atomic_int *spin; ///< Array of spin locks
	uint64_t mask; ///< Hash table mask
} HashTable;


/** Options: Struct to represent options to perform perft */
typedef struct {
	bool bulk; ///< Use bulking count (directly count the number of moves at the last ply without generating them)
	bool do_quiet; ///< Also count the quiet moves (non capture & non promtion)
} Option;

/** Node: Struct to represent a node where the perft is splitted among several tasks in the search tree */
typedef struct {
	uint64_t count; ///< Count of moves at this node
	int task_id[MAX_SPLIT]; ///< List of tasks used at this node
	atomic_int spin; ///< Spin lock for the node
	int n_split; ///< Number of tasks used at this node
} Node;

/** Task: Struct to represent a task in the search tree */
typedef struct {
	Board board; ///< Board state at this task
	Node *node; ///< Pointer to the node associated with this task
	HashTable *hash_table; ///< Pointer to the shared hash table
	const Option *option; ///< Pointer to the perft options
	struct TaskPool *task_pool; ///< Pointer to the task pool
	int depth; ///< Perft depth
	int id; ///< Task ID

	thrd_t thread; ///< Thread ID
	mtx_t mutex; ///< Mutex for the task
	cnd_t condition; ///< Condition variable for the task
	bool loop; ///< Loop flag for the task
	bool run; ///< Run flag for the task
} Task;

/** TaskPool: Struct to represent a task pool, ie a set of tasks */
typedef struct TaskPool {
	Task *tasks; ///< Pointer to the tasks array
	int n_tasks; ///< Number of tasks in the pool
	Task **idle_tasks; ///< Pointer to the idle tasks array
	int n_idle; ///< Number of idle tasks in the pool
	atomic_int spin; ///< Spin lock for the task pool
} TaskPool;

/* Constants */

/** RANK: Bitboard representing a rank mask */
const Bitboard RANK[] =  {
	0x00000000000000ffULL, 0x000000000000ff00ULL, 0x0000000000ff0000ULL, 0x00000000ff000000ULL,
	0x000000ff00000000ULL, 0x0000ff0000000000ULL, 0x00ff000000000000ULL, 0xff00000000000000ULL,
};

/** COLUMN: Bitboard representing a column mask */
const Bitboard COLUMN[] = {
	0x0101010101010101ULL, 0x0202020202020202ULL, 0x0404040404040404ULL, 0x0808080808080808ULL,
	0x1010101010101010ULL, 0x2020202020202020ULL, 0x4040404040404040ULL, 0x8080808080808080ULL,
};

/** PUSH: Array representing the push values for each pawn color */
const int PUSH[] = {8, -8};

/** MASK_CASTLING: Array representing the mask for castling */
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

/** CAN_CASTLE_KINGSIDE: Array representing the ability to castle kingside for each color */
const int CAN_CASTLE_KINGSIDE[COLOR_SIZE] = {1, 4};

/** CAN_CASTLE_QUEENSIDE: Array representing the ability to castle queenside for each color */
const int CAN_CASTLE_QUEENSIDE[COLOR_SIZE] = {2, 8};

/** PROMOTION_RANK: Array representing the promotion rank for each color */
const Bitboard PROMOTION_RANK[] = {0xff00000000000000ULL, 0x00000000000000ffULL};

/** MASK48: Mask for 48-bit random number generation */
const Random MASK48 = 0xFFFFFFFFFFFFull;

/** BUCKET_SIZE: Size of the bucket */
const int BUCKET_SIZE = 4;

/** SPINLOCK STATE Enum representing the state of a spin lock */
enum {SL_FREE = 0, SL_BUSY = 1};

/* Globals */
Mask MASK[BOARD_SIZE]; ///< Mask for each square of the board
Key KEY_PLAYER[COLOR_SIZE]; ///< Key for each player
Key KEY_SQUARE[BOARD_SIZE][CPIECE_SIZE]; ///< Key for each square and piece
Key KEY_CASTLING[16]; ///< Key for each castling state
Key KEY_ENPASSANT[BOARD_SIZE + 1]; ///< Key for each en passant state
Key KEY_PLAY; ///< Key to switch players

/**
 * @brief Get a random number
 * @param random Random number generator
 * @return Random number
 */
static uint64_t random_get(Random *random) {
	const uint64_t A = 0x5deece66dull;
	const uint64_t B = 0xbull;
	uint64_t r;

	*random = ((A * *random + B) & MASK48);
	r = *random >> 16;
	*random = ((A * *random + B) & MASK48);
	return (r << 32) | (*random >> 16);
}

/**
 * @brief Init the random generator
 * @param random Random number generator
 * @param seed Seed for random number generator
 */
static inline void random_seed(Random *random, const uint64_t seed) {
	*random = (seed & MASK48);
}

/**
 * @brief Get available memory
 * @return Available memory in MB
 */
static inline uint64_t get_available_memory() {
#if defined(__linux__)
	struct sysinfo info;
	if (sysinfo(&info) == 0) {
		return (info.freeram * info.mem_unit) >> 20;
	}
#endif
	return 1024; // 1GB by default expected to be enough for most systems
}

/**
 * @brief Get number of processors
 * @return Number of processors
 */
static inline uint64_t get_available_processors() {
#if defined(__linux__)
	 return get_nprocs();
#endif
	return 1;
}

/**
 * @brief Initialize a spinlock
 * @param spin spinlock to initialize
 */
static inline void spin_init(atomic_int *spin) {
	atomic_init(spin, SL_FREE);
}

/**
 * @brief Lock a spinlock
 * @param spin spinlock to lock
 */
static inline void spin_lock(atomic_int *spin) {
	for(;;) {
		if (atomic_exchange_explicit(spin, SL_BUSY, memory_order_acquire) == SL_FREE) return;
		while (atomic_load_explicit(spin, memory_order_relaxed)) thrd_yield();
	}
}

/**
 * @brief Unlock a spinlock
 * @param spin spinlock to unlock
 */
static inline void spin_unlock(atomic_int *spin) {
	atomic_store_explicit(spin, SL_FREE, memory_order_release);
}

/**
 * @brief Get opponent color
 * @param color Color to get opponent of
 * @return Opponent color
 */
static inline Color opponent(const Color color) {
	return !color;
}

/**
 * @brief Convert a char to a Color
 * @param c Char to convert to Color
 * @return Color corresponding to char
 */
static inline Color color_from_char(const char c) {
	switch (tolower(c)) {
		case 'b': return BLACK;
		case 'w': return WHITE;
		default: return COLOR_SIZE;
	}
}

/**
 * @brief Loop over each color
 * @param c Color variable to iterate over
 */
#define foreach_color(c) for ((c) = WHITE; (c) < COLOR_SIZE; ++(c))

/**
 * @brief Loop over each piece
 * @param p Piece variable to iterate over
 */
#define foreach_piece(p) for ((p) = PAWN; (p) < PIECE_SIZE; ++(p))

/**
 * @brief Convert a char to a piece
 * @param c Char to convert
 * @return a Piece
 */
static inline Piece piece_from_char(const char c) {
	Piece p;

	foreach_piece(p) if ("PNBRQK"[p] == toupper(c)) break;
	return p;
}

/**
 * @brief Make a square from file & rank
 * @param f File of square
 * @param r Rank of square
 * @return Square corresponding to file & rank
 */
static inline Square square(const int f, const int r) {
	return (r << 3) + f;
}

/**
 * @brief Make a square from file & rank if inside the board
 * @param f File of square
 * @param r Rank of square
 * @return Square corresponding to file & rank if inside the board, BOARD_OUT otherwise
 */
static inline Square square_safe(const int f, const int r) {
	if (0 <= f && f < 8 && 0 <= r && r < 8) return square(f, r);
	else return BOARD_OUT;
}

/**
 * @brief Get square rank
 * @param x Square to get rank from
 * @return Rank of square
 */
static inline Square rank(const Square x) {
	return x >> 3;
}

/**
 * @brief Get square file
 * @param x Square to get file from
 * @return File of square
 */
static inline Square file(const Square x) {
	return x & 7;
}

/**
 * @brief Create a bitboard with one bit (square) set
 * @param x Square to set bit for
 * @return Bitboard with one bit set
 */
static inline Bitboard square_to_bit(const int x) {
	assert(0 <= x && x < 64);
	return 1ULL << x;
}

/**
 * @brief Create a bitboard with one bit set from file/rank if inside the board
 * @param f File to set bit for
 * @param r Rank to set bit for
 * @return Bitboard with one bit set
 */
static inline Bitboard file_rank_to_bit(const int f, const int r) {
	if (0 <= f && f < 8 && 0 <= r && r < 8) return square_to_bit(square(f, r));
	else return 0;
}

/**
 * @brief Parse a square from a string.
 * @param string String to parse
 * @param x Square to set bit for
 * @return True if successful, false otherwise
 */
static inline bool square_parse(char **string, Square *x) {
	const char *s = *string;
	if ('a' <= s[0] && s[0] <= 'h' && '1' <= s[1] && s[1] <= '8') {
		*x = square(s[0] - 'a', s[1] - '1');
		*string += 2;
		return true;
	} else return false;
}

/**
 * @brief Get the first occupied square from a bitboard.
 * @param b Bitboard to get first occupied square from
 * @return Square of the first occupied square, or BOARD_SIZE if none
 */
static inline Square square_first(Bitboard b) {
	return stdc_trailing_zeros_ull(b);
}

/**
 * @brief Get the next occupied square from a bitboard.
 * @param b Bitboard to get next occupied square from
 * @return Square of the next occupied square, or BOARD_SIZE if none
 */
static inline Square square_next(Bitboard *b) {
	int i = square_first(*b);
	*b &= *b - 1;
	return i;
}

/**
 * @brief Macro to loop over each square
 * @param x Square to iterate over
 */
#define foreach_square(x) for ((x) = A1; (x) < BOARD_SIZE; ++(x))

/**
 * @brief Check if square 'x' is on 7th rank
 * @param x Square to check
 * @param c Color of the square
 * @return True if square is on 7th rank, false otherwise
 */
static inline bool is_on_seventh_rank(const Square x, const Color c) {
	return c ? rank(x) == 1 : rank(x) == 6;
}

/**
 * @brief Check if square 'x' is on 2nd rank
 * @param x Square to check
 * @param c Color of the square
 * @return True if square is on 2nd rank, false otherwise
 */
static inline bool is_on_second_rank(const Square x, const Color c) {
	return c ? rank(x) == 6 : rank(x) == 1;
}

/**
 * @brief Macro to loop over each colored piece.
 * @param cp Colored piece to iterate over
 */
#define foreach_cpiece(cp) for ((cp) = WPAWN; (cp) < CPIECE_SIZE; ++(cp))

/**
 * @brief make a colored piece from a piece & a color
 * @param p Piece
 * @param c Color of the piece
 * @return Colored piece
 */
static inline CPiece cpiece_make(Piece p, const Color c) {
	return (p << 1) + c + 1;
}

/**
 * @brief Get the Piece part of a CPiece
 * @param p Colored piece
 * @return Piece
 */
static inline Piece cpiece_piece(CPiece p) {
	return (p - 1) >> 1;
}

/**
 * @brief Get the color of a CPiece
 * @param p Colored piece
 * @return Color
 */
static inline Color cpiece_color(CPiece p) {
	return (p - 1) & 1;
}

/**
 * @brief Convert a char to a colored piece
 * @param c Char to convert
 * @return Colored piece
 */
static inline CPiece cpiece_from_char(const char c) {
	CPiece p;

	foreach_cpiece(p) if ("#PpNnBbRrQqKk"[p] == c) break;
	return p;
}

/**
 * Convert a char to a castling flag
 * @param c Char to convert
 * @return Castling flag
 */
static inline int castling_from_char(const char c) {
	switch (c) {
		case 'K': return 1;
		case 'Q': return 2;
		case 'k': return 4;
		case 'q': return 8;
		default: return 0;
	}
}

/**
 * @brief Get the moving piece
 * @param move Move
 * @return Moving piece
 */
static inline Piece move_piece(const Move move) {
	return move & 7;
}

/**
 * @brief Get the source square of a move
 * @param move Move
 * @return Source square
 */
static inline Square move_from(const Move move) {
	return (move >> 3) & 63;
}

/**
 * @brief Get the destination square of a move
 * @param move Move
 * @return Destination square
 */
static inline Square move_to(const Move move) {
	return (move >> 9) & 63;
}

/**
 * @brief Get the promoted piece of a move
 * @param move Move
 * @return Promoted piece
 */
static inline Piece move_promotion(const Move move) {
	return move >> 15;
}

/**
 * @brief Convert a move to a string
 * @param move Move
 * @param s String buffer
 * @return String buffer
 */
static inline char* move_to_string(const Move move, char *s) {
	static char string[8];

	if (s == NULL) s = string;
	if (move) {
		s[0] = (move >>  3 & 7) + 'a';
		s[1] = (move >>  6 & 7) + '1';
		s[2] = (move >>  9 & 7) + 'a';
		s[3] = (move >> 12 & 7) + '1';
		s[4] = "\0NBRQ"[move_promotion(move)];
		s[5] = '\0';
	} else {
		strcpy(s, "null");
	}

	return s;
}

/**
 * @brief Compare two moves
 * @param a Move
 * @param b Move
 * @return Comparison result
 */
static int move_compare(const void *a, const void *b) {
	char string_a[8], string_b[8];
	return strcmp(move_to_string(*(Move*)a, string_a), move_to_string(*(Move*)b, string_b));
}

/**
 *  @brief Measure time in seconds as a double.
 *  @return Elapsed Time in seconds
 */
static inline double chrono(void) {
	struct timespec t;
	timespec_get(&t, TIME_UTC);
	return 0.000000001 * t.tv_nsec + t.tv_sec;
}

/**
 *  @brief Memory error.
 *  @param function Function name
 */
static void memory_error(const char *function) {
	fprintf(stderr, "Fatal Error: memory allocation failure in %s\n", function);
	exit(EXIT_FAILURE);
}

/**
 *  @brief Parse error.
 *  @param string Input string
 *  @param done   Done string
 *  @param msg    Message string
 */
static void parse_error(const char *string, const char *done, const char *msg) {
	size_t n;

	fprintf(stderr, "\nError in %s '%s'\n", msg, string);
	n = 11 + strlen(msg) + done - string;
	if (n > 0 && n < 256) {
		while (n--) putc('-', stderr);
		putc('^', stderr); putc('\n', stderr); putc('\n', stderr);
	}
	exit(EXIT_FAILURE);
}

/**
 *  @brief Skip spaces.
 *  @param string String to skip spaces in
 *  @return Pointer to first non-space character
 */
static char *parse_next(const char *string) {
	while (isspace((int)*string)) ++string;
	return (char*) string;
}

/**
 * @brief Parse a word.
 *
 * @param string String to parse
 * @param word String receiving a copy of the parsed word.
 * @param n word string capacity.
 * NOTE: It is assumed that w is big enough to contains the word copy.
 * @return The remaining of the input string.
 */
char* parse_word(const char *string, char *word, size_t n)
{
	string = parse_next(string);
	while(*string && !isspace(*string) && --n) *word++ = *string++;
	*word = '\0';
	return (char*) string;
}

static inline Piece board_get_piece(const Board*, const Square);

/**
 * @brief Parse a move.
 *
 * @param string String to parse
 * @return The remaining of the input string.
 */
char *parse_move(char *string, const Board *board, Move *move) {
	char word[8];
	Square from, to;
	Piece promotion;

	string = parse_word(string, word, 8);
	if (*word == '\0') return NULL;

	from = square(word[0] - 'a', word[1] - '1');
	to = square(word[2] - 'a', word[3] - '1');
	promotion = word[4] ? piece_from_char(word[4]) : PAWN;

    //
/* 	if (!board->chess960 && cpiece_piece(board->cpiece[from]) == KING) {
		if (to == from + 2) to = from + 3;
		if (to == from - 2) to = from - 4;
	}
*/
	*move = board_get_piece(board, from) | (from << 3) | (to << 9) | (promotion << 15);

	return string;
}

/**
 * @brief Generate attack index using the magic bitboard or pext approach
 * @param pieces Bitboard of pieces
 * @param attack Attack structure
 * @return Attack index
 */
static inline Bitboard magic_index(const Bitboard pieces, const Attack *attack) {
#ifdef HAS_PEXT
	return _pext_u64(pieces, attack->mask);
#else
	return ((pieces & attack->mask) * attack->magic) >> attack->shift;
#endif
}

/**
 * @brief Generate pawn attack (capture)
 * @param x Pawn's quare
 * @param c Pawn's color
 * @param target Bitboard of target squares
 * @return Bitboard of pawn attack (capture)
 */
static inline Bitboard pawn_attack(const Square x, const Color c, const Bitboard target) {
	return MASK[x].pawn_attack[c] & target;
}

/**
 * @brief Generate knight attack
 * @param x Knight's square
 * @param target Bitboard of target squares
 * @return Bitboard of knight attack
 */
static inline Bitboard knight_attack(const Square x, const Bitboard target) {
	return MASK[x].knight & target;
}

/**
 * @brief Generate bishop attack
 * @param pieces Bitboard of pieces
 * @param x Bishop's square
 * @param target Bitboard of target squares
 * @return Bitboard of bishop attack
 */
static inline Bitboard bishop_attack(const Bitboard pieces, const Square x, const Bitboard target) {
	return MASK[x].bishop.attack[magic_index(pieces, &MASK[x].bishop)] & target;
}

/**
 * @brief Generate rook attack
 * @param pieces Bitboard of pieces
 * @param x Rook's square
 * @param target Bitboard of target squares
 * @return Bitboard of rook attack
 */
static inline Bitboard rook_attack(const Bitboard pieces, const Square x, const Bitboard target) {
	return MASK[x].rook.attack[magic_index(pieces, &MASK[x].rook)] & target;
}

/**
 * @brief Generate Queen attack
 * @param pieces Bitboard of pieces
 * @param x Queen's square
 * @param target Bitboard of target squares
 * @return Bitboard of rook attack
 */
static inline Bitboard queen_attack(const Bitboard pieces, const Square x, const Bitboard target) {
	return rook_attack(pieces, x, target) | bishop_attack(pieces, x, target);
}

/**
 * @brief Generate king attack
 * @param x King's square
 * @param target Bitboard of target squares
 * @return Bitboard of king attack
 */
static inline Bitboard king_attack(const Square x, const Bitboard target) {
	return MASK[x].king & target;
}

/**
 * @brief Get a piece located at a specified square
 * @param board The board
 * @param x square
 * @return A Piece
 */
static inline Piece board_get_piece(const Board *board, const Square x) {
	Bitboard b = square_to_bit(x);
	Piece p;

	foreach_piece(p) {
		if (b & board->piece[p]) return p;
	}
	return PIECE_SIZE;
}

/**
 * @brief Get the color of a piece located at a specified square
 * @param board The board
 * @param x square
 * @return A Color.
 */
static inline Color board_get_color(const Board *board, const Square x) {
	Bitboard b = square_to_bit(x);
	Color c;

	foreach_color(c) {
		if (b & board->color[c]) return c;
	}
	return COLOR_SIZE;
}


/**
 * @brief Get a colored piece located at a specified square
 * @param board The board
 * @param x square
 * @return A CPiece
 */
static inline CPiece board_get_cpiece(const Board *board, const Square x) {
	Piece p = board_get_piece(board, x);
	Color c = board_get_color(board, x);
	return c == COLOR_SIZE ? EMPTY : cpiece_make(p, c);
}

/**
 * @brief Initialize key to a random value
 * @param key Key to initialize
 * @param r Random number generator
 */
static inline void key_init(Key *key, Random *r) {
	key->code = random_get(r);
	key->index = (uint32_t) random_get(r);
}

/**
 * @brief Xor a key with another one
 * @param key Key to xor
 * @param k Key to xor with
 */
static inline void key_xor(Key *key, const Key *k) {
	key->code ^= k->code;
	key->index ^= k->index;
}

/**
 * @brief Set a key from a board
 * @param key Key to set
 * @param board Board to set key from
 */
static void key_set(Key *key, const Board *board) {
	*key = KEY_PLAYER[board->player];
	Color c;
	Piece p;

	foreach_color (c)
	foreach_piece (p) {
		Bitboard b = (board->color[c] & board->piece[p]);
		CPiece cp = cpiece_make(p, c);
		while (b) {
			Square x = square_next(&b);
			key_xor(key, &KEY_SQUARE[x][cp]);
		}
	}
	key_xor(key, &KEY_CASTLING[board->castling]);
	key_xor(key, &KEY_ENPASSANT[board->enpassant]);
}

/**
 * @brief Update the key after a move is made
 * @param key Key to update
 * @param board Board to update key from
 * @param move Move to update key with
 */
static void key_update(Key *key, const Board *board, const Move move) {
	const Bitboard occupied = board->color[WHITE] | board->color[BLACK];
	const Square from = move_from(move);
	const Square to = move_to(move);
	const Color c = board->player;
	const Color o = opponent(c);
	Piece p = move_piece(move);
	CPiece cp = cpiece_make(p, c);
	Square enpassant = ENPASSANT_NONE;

	*key = board->key;

	// move the piece
	key_xor(key, &KEY_SQUARE[from][cp]);
	key_xor(key, &KEY_SQUARE[to][cp]);
	// capture
	if (occupied & square_to_bit(to)) {
		const CPiece victim = cpiece_make(board_get_piece(board, to), o);
		key_xor(key, &KEY_SQUARE[to][victim]);
	}
	// pawn move
	if (p == PAWN) {
		if ((p = move_promotion(move))) {
			key_xor(key, &KEY_SQUARE[to][cp]);
			key_xor(key, &KEY_SQUARE[to][cpiece_make(p, c)]);
		} else if (board->enpassant == to) {
			Square x = square(file(to), rank(from));
			key_xor(key, &KEY_SQUARE[x][cpiece_make(PAWN, o)]);
		} else if (abs(to - from) == 16 && (MASK[to].enpassant & (board->color[o] & board->piece[PAWN]))) enpassant = (from + to) / 2;
	// castling
	} else if (p == KING) {
		const CPiece rook = cpiece_make(ROOK, c);
		if (to == from + 2) {
			key_xor(key, &KEY_SQUARE[from + 3][rook]);
			key_xor(key, &KEY_SQUARE[from + 1][rook]);
		} else if (to == from - 2) {
			key_xor(key, &KEY_SQUARE[from - 4][rook]);
			key_xor(key, &KEY_SQUARE[from - 1][rook]);
		}
	}
	// miscellaneous
	key_xor(key, &KEY_CASTLING[board->castling]);
	key_xor(key, &KEY_CASTLING[board->castling & MASK_CASTLING[from] & MASK_CASTLING[to]]);
	key_xor(key, &KEY_ENPASSANT[board->enpassant]);
	key_xor(key, &KEY_ENPASSANT[enpassant]);
	key_xor(key, &KEY_PLAY);
}


/**
 * @brief Compute slider attack to feed array accessed by magic index
 * @param x Square to compute attack for
 * @param pieces Bitboard of pieces
 * @param d Direction array
 * @return Bitboard of attacks
 */
static Bitboard compute_slider_attack(const int x, const Bitboard pieces, const int d[4][2]) {
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

/**
 * @brief Initialize some global constants
 * @param seed Seed for random number generator
 */
static void init(const uint64_t seed) {
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
	MASK->bishop.attack = malloc(sizeof (Bitboard) * 0x1480);
	if (MASK->bishop.attack == NULL) memory_error(__func__);
	MASK->rook.attack = malloc(sizeof (Bitboard) * 0x19000);
	if (MASK->rook.attack == NULL) memory_error(__func__);
	for (x = 0; x < 64; ++x) {
		f = file(x);
		r = rank(x);
		mask = MASK + x;

		for (y = 0; y < 64; ++y) d[x][y] = 0;
		// directions & between
		for (i = 0; i < 8; ++i) {
			for (j = 1; j < 8; ++j) {
				y = square_safe(f + king_dir[i][0] * j, r + king_dir[i][1] * j);
				if (y != BOARD_OUT) {
					d[x][y] = king_dir[i][0] + 8 * king_dir[i][1];
					mask->direction[y] = abs(d[x][y]);
					for (z = x + d[x][y]; z != y; z += d[x][y]) mask->between[y] |= square_to_bit(z);
				}
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
		mask->bishop.shift = stdc_count_zeros_ull(mask->bishop.mask);
		mask->bishop.magic = bishop_magic[x];
		if (x) mask->bishop.attack = mask[-1].bishop.attack + (1ull << stdc_count_ones_ull(mask[-1].bishop.mask));
		o = 0; do {
			mask->bishop.attack[magic_index(o, &mask->bishop)] = compute_slider_attack(x, o, bishop_dir);
			o = (o - mask->bishop.mask) & mask->bishop.mask;
		} while (o);

		// magic rook
		mask->rook.mask = (mask->rank | mask->file) & inside;
		mask->rook.shift = stdc_count_zeros_ull(mask->rook.mask);
		mask->rook.magic = rook_magic[x];
		if (x) mask->rook.attack = mask[-1].rook.attack + (1ull << stdc_count_ones_ull(mask[-1].rook.mask));
		o = 0; do {
			mask->rook.attack[magic_index(o, &mask->rook)] = compute_slider_attack(x, o, rook_dir);
			o = (o - mask->rook.mask) & mask->rook.mask;
		} while (o);
	}

	// Hash key
	random_seed(random, seed);

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

/**
 * @brief Check if an enpassant move is possible
 * @param board Board to check
 * @return True if enpassant is possible, false otherwise
 */
static inline bool board_enpassant(const Board *board) {
	return board->enpassant != ENPASSANT_NONE;
}

/**
 * @brief Deplace a piece on the board.
 * @param board Board to modify
 * @param from Square to move from
 * @param to Square to move to
 */
static inline void board_deplace_piece(Board *board, const Square from, const Square to) {
	const Bitboard b = square_to_bit(from) ^ square_to_bit(to);
	const Piece p = board_get_piece(board, from);
	const Color c = board_get_color(board, from);

	board->piece[p] ^= b;
	board->color[c] ^= b;
}

/**
 * @brief Generate checker & pinned pieces.
 * @param board Board to generate checkers and pinned pieces for
 */
static void generate_checkers(Board *board) {
	const Color c = board->player;
	const Color o = opponent(c);
	const Square k = board->x_king[c];
	const Bitboard bq = (board->piece[BISHOP] + board->piece[QUEEN]) & board->color[o];
	const Bitboard rq = (board->piece[ROOK] + board->piece[QUEEN]) & board->color[o];
	const Bitboard pieces = board->color[WHITE] + board->color[BLACK];
	Bitboard partial_checkers;
	Bitboard b;
	Bitboard *pinned = &board->pinned;
	Bitboard *checkers = &board->checkers;
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

/**
 * @brief Clear the board. Set all of its content to zeroes.
 * @param board Board to clear
 */
static inline void board_clear(Board *board) {
	memset(board, 0, sizeof (Board));
}

/**
 * @brief Initialize the board to the starting position.
 * @param board Board to initialize
 */
static void board_init(Board *board) {
	board_clear(board);
	board->piece[PAWN] =   0x00ff00000000ff00ull;
	board->piece[KNIGHT] = 0x4200000000000042ull;
	board->piece[BISHOP] = 0x2400000000000024ull;
	board->piece[ROOK] =   0x8100000000000081ull;
	board->piece[QUEEN] =  0x0800000000000008ull;
	board->piece[KING] =   0x1000000000000010ull;
	board->color[WHITE] =  0x000000000000ffffull;
	board->color[BLACK] =  0xffff000000000000ull;
	board->pinned = board->checkers = 0;
	board->castling = 15;
	board->enpassant = ENPASSANT_NONE; // illegal enpassant square
	board->x_king[WHITE] = E1;
	board->x_king[BLACK] = E8;
	board->ply = 1;
	board->player = WHITE;

	key_set(&board->key, board);
}

/**
 * @brief parse a FEN board description
 * @param board Board to parse FEN string into
 * @param string FEN string to parse
 */
static void board_set(Board *board, char *string) {
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
			p = cpiece_from_char(*s);
			if (p == CPIECE_SIZE) parse_error(string, s, "FEN: bad piece");
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
			board->castling |= castling_from_char(*s);
			s++;
		}
	}
	// correct castling
	if (board->x_king[WHITE] == E1) {
		uint64_t rooks = board->color[WHITE] & board->piece[ROOK];
		if ((rooks & square_to_bit(H1)) == 0) board->castling &= ~1;
		if ((rooks & square_to_bit(A1)) == 0) board->castling &= ~2;
	} else board->castling &= ~3;
	if (board->x_king[BLACK] == E8) {
		uint64_t rooks = board->color[BLACK] & board->piece[ROOK];
		if ((rooks & square_to_bit(H8)) == 0) board->castling &= ~1;
		if ((rooks & square_to_bit(A8)) == 0) board->castling &= ~2;
	} else board->castling &= ~12;
	// en passant
	x = ENPASSANT_NONE;
	s = parse_next(s);
	if (*s == '-') s++;
	else if (!square_parse(&s, &x)) parse_error(string, s, "FEN: bad enpassant square");
	board->enpassant = x;
	// update other chess board structure
	key_set(&board->key, board);
	generate_checkers(board);
}

/**
 * @brief Play a move on the board.
 * @param board Board to play move on
 * @param move Move to play
 * @param key Key to use for hashing
 * @param next Board to store result in
 */
static void board_copymake(const Board *board, const Move move, const Key *key, Board *next) {
	const Square from = move_from(move);
	const Square to = move_to(move);
	const Square enpassant = board->enpassant;
	const Bitboard b_from = square_to_bit(from);
	const Bitboard b_to = square_to_bit(to);
	const Bitboard occupied = board->color[WHITE] | board->color[BLACK];
	Piece p = move_piece(move);
	const Color c = board->player;
	const Color o = opponent(c);
	Square x;
	Bitboard b;

	*next = *board;

	// update chess board informations
	next->enpassant = ENPASSANT_NONE;
	next->castling &= MASK_CASTLING[from] & MASK_CASTLING[to];
	// move the piece
	next->piece[p] ^= b_from;
	next->piece[p] ^= b_to;
	next->color[c] ^= b_from | b_to;

	// capture
	if (occupied & b_to) {
		const Piece victim = board_get_piece(board, to);
		next->piece[victim] ^= b_to;
		next->color[o] ^= b_to;
	}

	// special pawn move
	if (p == PAWN) {
		if ((p = move_promotion(move))) {
			next->piece[PAWN] ^= b_to;
			next->piece[p] ^= b_to;
		} else if (enpassant == to) {
			x = square(file(to), rank(from));
			b = square_to_bit(x);
			next->piece[PAWN] ^= b;
			next->color[o] ^= b;
		} else if (abs(to - from) == 16 && (MASK[to].enpassant & (next->color[o] & next->piece[PAWN]))) {
			next->enpassant = (from + to) / 2;
		}

	// king move
	} else if (p == KING) {
		next->x_king[c] = to;
		if (to == from + 2) board_deplace_piece(next, from + 3, from + 1);
		else if (to == from - 2) board_deplace_piece(next, from - 4, from - 1);
	}

	++next->ply;
	next->player = opponent(next->player);
	next->key = *key;
	generate_checkers(next);
}

/**
 * Print the board.
 * @param board Board to print
 * @param output File to print board to
 */
static void board_print(const Board *board, FILE *output) {
	Square x;
	int f, r;
	const char p[] = ".PpNnBbRrQqKk#";
	const char c[] = "wb";
	const Square ep = board->enpassant;

	fputs("  a b c d e f g h\n", output);
	for (r = 7; r >= 0; --r) {
		for (f = 0; f <= 7; ++f) {
			x = square(f, r);
			if (f == 0) fprintf(output, "%1d ", r + 1);
			fputc(p[board_get_cpiece(board, x)], output); fputc(' ', output);
			if (f == 7) fprintf(output, "%1d\n", r + 1);
		}
	}
	fputs("  a b c d e f g h\n", output);
	fprintf(output, "%c, ", c[board->player]);
	if (board->castling & CAN_CASTLE_KINGSIDE[WHITE]) fputc('K', output);
	if (board->castling & CAN_CASTLE_QUEENSIDE[WHITE]) fputc('Q', output);
	if (board->castling & CAN_CASTLE_KINGSIDE[BLACK]) fputc('k', output);
	if (board->castling & CAN_CASTLE_QUEENSIDE[BLACK]) fputc('q', output);
	if (board_enpassant(board))	fprintf(output, ", ep: %c%c", file(ep) + 'a', rank(ep) + '1');
	fputc('\n', output);
}


/**
 * @brief Check if a square is attacked.
 * @param board Board to check
 * @param x Square to check
 * @param c Color to check
 * @param occupied Bitboard of occupied squares
 * @return True if square is attacked, false otherwise
 */
static bool board_is_square_attacked(const Board *board, const Square x, const Color c, const Bitboard occupied) {
	const Bitboard C = board->color[c];

	return bishop_attack(occupied, x, C & (board->piece[BISHOP] | board->piece[QUEEN]))
	    || rook_attack(occupied, x, C & (board->piece[ROOK] | board->piece[QUEEN]))
	    || knight_attack(x, C & board->piece[KNIGHT])
	    || pawn_attack(x, opponent(c), C & board->piece[PAWN])
	    || king_attack(x, C & board->piece[KING]);
}

/**
 * Append a move to an array of moves
 * @param move Array of moves
 * @param piece Moving piece
 * @param from From square
 * @param to To square
 * @return Pointer to next move
 */
static inline Move* push_move(Move *move, const Piece piece, const Square from, const Square to) {
	assert(piece >= PAWN && piece < PIECE_SIZE);
	assert(from >= A1 && from <= H8);
	assert(to >= A1 && to <= H8);

	*move++ = piece | (from << 3) | (to << 9);
	return move;
}

/**
 * @brief Append promotions from the same move
 * @param move Array of moves
 * @param from From square
 * @param to To square
 * @return Pointer to next move
 */
static inline Move* push_promotion(Move *move, const Square from, const Square to) {
	const Move m = PAWN | (from << 3) | (to << 9);

	assert(from >= A1 && from <= H8);
	assert(to >= A1 && to <= H8);

	*move++ = m | QUEEN_PROMOTION;
	*move++ = m | KNIGHT_PROMOTION;
	*move++ = m | ROOK_PROMOTION;
	*move++ = m | BISHOP_PROMOTION;
	return move;
}

/**
 * @brief Append all moves from a square
 * @param move Array of moves
 * @param attack Bitboard of attacked squares
 * @param from From square
 * @return Pointer to next move
 */
static inline Move* push_moves(Move *move, Bitboard attack, const Piece piece, const Square from) {
	Square to;

	while (attack) {
		to = square_next(&attack);
		move = push_move(move, piece , from, to);
	}
	return move;
}

/**
 * @brief Append all pawn moves from a direction
 * @param move Array of moves
 * @param attack Bitboard of attacked squares
 * @param dir Direction
 * @return Pointer to next move
 */
static inline Move* push_pawn_moves(Move *move, Bitboard attack, const int dir) {
	while (attack) {
		const Square to = square_next(&attack);
		move = push_move(move, PAWN, to - dir, to);
	}
	return move;
}

/**
 * @brief Append all promotions from a direction
 * @param move Array of moves
 * @param attack Bitboard of attacked squares
 * @param dir Direction
 * @return Pointer to next move
 */
static inline Move *push_promotions(Move *move, Bitboard attack, const int dir) {
	while (attack) {
		const Square to = square_next(&attack);
		move = push_promotion(move, to - dir, to);
	}
	return move;
}

/**
 * @brief count all legal moves
 * @param board Board state
 * @param do_quiet Whether to count quiet moves
 * @return Number of moves generated
 */
static int count_moves(const Board *board, const bool do_quiet) {
	const Color c = board->player;
	const Color o = opponent(c);
	const Square k = board->x_king[c];
	const Bitboard occupied = board->color[WHITE] + board->color[BLACK];
	const Bitboard occupied_no_king = occupied ^ square_to_bit(k);
	const Bitboard bq = board->piece[BISHOP] | board->piece[QUEEN];
	const Bitboard rq = board->piece[ROOK] | board->piece[QUEEN];
	const Bitboard pinned = board->pinned;
	const Bitboard unpinned = board->color[c] & ~pinned;
	const Bitboard checkers = board->checkers;
	const int pawn_left = PUSH[c] - 1;
	const int pawn_right = PUSH[c] + 1;
	const int pawn_push = PUSH[c];
	const int *dir = MASK[k].direction;
	Bitboard target, piece, attack;
	Bitboard empty = ~occupied;
	Bitboard enemy = board->color[o];
	Square from, to, ep, x_checker = ENPASSANT_NONE;
	int d, count = 0;

	// in check: capture or block the (single) checker if any;
	if (checkers) {
		if (stdc_has_single_bit_ull(checkers)) {
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
			if ((board->castling & CAN_CASTLE_KINGSIDE[c])
				&& (occupied & MASK[k].between[k + 3]) == 0
				&& !board_is_square_attacked(board, k + 1, o, occupied)
				&& !board_is_square_attacked(board, k + 2, o, occupied)) {
					++count;
			}
			if ((board->castling & CAN_CASTLE_QUEENSIDE[c])
				&& (occupied & MASK[k].between[k - 4]) == 0
				&& !board_is_square_attacked(board, k - 1, o, occupied)
				&& !board_is_square_attacked(board, k - 2, o, occupied)) {
					++count;
			}
		}
		// pawn (pinned)
		piece = board->piece[PAWN] & pinned;
		while (piece) {
			from = square_next(&piece);
			d = dir[from];
			if (d == abs(pawn_left) && (square_to_bit(to = from + pawn_left) & pawn_attack(from, c, enemy))) count += is_on_seventh_rank(from, c) ? 4 : 1;
			else if (d == abs(pawn_right) && (square_to_bit(to = from + pawn_right) & pawn_attack(from, c, enemy))) count += is_on_seventh_rank(from, c) ? 4 : 1;
			if (do_quiet && d == abs(pawn_push) && (square_to_bit(to = from + pawn_push) & empty)) {
				++count;
				if (is_on_second_rank(from, c) && (square_to_bit(to += pawn_push) & empty)) ++count;
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
			count += stdc_count_ones_ull(attack);
		}
		// rook or queen (pinned)
		piece = rq & pinned;
		while (piece) {
			from = square_next(&piece);
			d = dir[from];
			attack = 0;
			if (d == 1) attack = rook_attack(occupied, from, target & MASK[from].rank);
			else if (d == 8) attack = rook_attack(occupied, from, target & MASK[from].file);
			count += stdc_count_ones_ull(attack);
		}
	}
	// common moves

	target = enemy; if (do_quiet) target |= empty;

	// enpassant capture
	if (board_enpassant(board) && (!checkers || x_checker == board->enpassant - pawn_push)) {
		to = board->enpassant;
		ep = to - pawn_push;
		from = ep - 1;
		attack = square_to_bit(from);
		if (file(to) > 0 && (board->piece[PAWN] & board->color[c] & attack)) {
			piece = occupied ^ square_to_bit(from) ^ square_to_bit(ep) ^ square_to_bit(to);
			if (!bishop_attack(piece, k, bq & board->color[o]) && !rook_attack(piece, k, rq & board->color[o])) ++count;
		}
		from = ep + 1;
		attack = square_to_bit(from);
		if (file(to) < 7 && (board->piece[PAWN] & board->color[c] & attack)) {
			piece = occupied ^ square_to_bit(from) ^ square_to_bit(ep) ^ square_to_bit(to);
			if (!bishop_attack(piece, k, bq & board->color[o]) && !rook_attack(piece, k, rq & board->color[o])) ++count;
		}
	}

	// pawn
	piece = board->piece[PAWN] & unpinned;
	attack = (c ? (piece & ~COLUMN[0]) >> 9 : (piece & ~COLUMN[0]) << 7) & enemy;
	count += 4 * stdc_count_ones_ull(attack & PROMOTION_RANK[c]) + stdc_count_ones_ull(attack & ~PROMOTION_RANK[c]);

	attack = (c ? (piece & ~COLUMN[7]) >> 7 : (piece & ~COLUMN[7]) << 9) & enemy;
	count += 4 * stdc_count_ones_ull(attack & PROMOTION_RANK[c]) + stdc_count_ones_ull(attack & ~PROMOTION_RANK[c]);

	attack = (c ? piece >> 8 : piece << 8) & empty;
	count += 4 * stdc_count_ones_ull(attack & PROMOTION_RANK[c]);
	if (do_quiet) {
		count += stdc_count_ones_ull(attack & ~PROMOTION_RANK[c]);
		attack = (c ? (((piece & RANK[6]) >> 8) & ~occupied) >> 8 : (((piece & RANK[1]) << 8) & ~occupied) << 8) & empty;
		count += stdc_count_ones_ull(attack);
	}

	// knight
	piece = board->piece[KNIGHT] & unpinned;
	while (piece) {
		from = square_next(&piece);
		attack = knight_attack(from, target);
		count += stdc_count_ones_ull(attack);
	}

	// bishop or queen
	piece = bq & unpinned;
	while (piece) {
		from = square_next(&piece);
		attack = bishop_attack(occupied, from, target);
		count += stdc_count_ones_ull(attack);
	}

	// rook or queen
	piece = rq & unpinned;
	while (piece) {
		from = square_next(&piece);
		attack = rook_attack(occupied, from, target);
		count += stdc_count_ones_ull(attack);
	}

	// king
	target = board->color[o]; if (do_quiet) target |= ~occupied;
	attack = king_attack(k, target);
	while (attack) {
		to = square_next(&attack);
		if (!board_is_square_attacked(board, to, o, occupied_no_king)) ++count;
	}

	return count;
}


/**
 * @brief Generate all legal moves
 * @param board Board state
 * @param move Array of moves
 * @param do_quiet Whether to generate quiet moves
 * @return Number of moves generated
 */
static int generate_moves(const Board *board, Move *move, const bool do_quiet) {
	const Color c = board->player;
	const Color o = opponent(c);
	const Square k = board->x_king[c];
	const Bitboard occupied = board->color[WHITE] + board->color[BLACK];
	const Bitboard occupied_no_king = occupied & ~square_to_bit(k);
	const Bitboard bq = board->piece[BISHOP] | board->piece[QUEEN];
	const Bitboard rq = board->piece[ROOK] | board->piece[QUEEN];
	const Bitboard pinned = board->pinned;
	const Bitboard unpinned = board->color[c] & ~pinned;
	const Bitboard checkers = board->checkers;
	const int pawn_left = PUSH[c] - 1;
	const int pawn_right = PUSH[c] + 1;
	const int pawn_push = PUSH[c];
	const int *dir = MASK[k].direction;
	const Move *start = move;
	Bitboard target, piece, attack;
	Bitboard empty = ~occupied;
	Bitboard enemy = board->color[o];
	Square from, to, ep, x_checker = ENPASSANT_NONE;
	int d;

	// in check: capture or block the (single) checker if any;
	if (checkers) {
		if (stdc_has_single_bit_ull(checkers)) {
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
			if ((board->castling & CAN_CASTLE_KINGSIDE[c])
				&& (occupied & MASK[k].between[k + 3]) == 0
				&& !board_is_square_attacked(board, k + 1, o, occupied)
				&& !board_is_square_attacked(board, k + 2, o, occupied)) {
					move = push_move(move, KING, k, k + 2);
			}
			if ((board->castling & CAN_CASTLE_QUEENSIDE[c])
				&& (occupied & MASK[k].between[k - 4]) == 0
				&& !board_is_square_attacked(board, k - 1, o, occupied)
				&& !board_is_square_attacked(board, k - 2, o, occupied)) {
					move = push_move(move, KING, k, k - 2);
			}
		}

		// pawn (pinned)
		piece = board->piece[PAWN] & pinned;
		while (piece) {
			from = square_next(&piece);
			d = dir[from];
			if (d == abs(pawn_left) && (square_to_bit(to = from + pawn_left) & pawn_attack(from, c, enemy))) {
				move = is_on_seventh_rank(from, c) ? push_promotion(move, from, to) : push_move(move, PAWN, from, to);
			} else if (d == abs(pawn_right) && (square_to_bit(to = from + pawn_right) & pawn_attack(from, c, enemy))) {
				move = is_on_seventh_rank(from, c) ? push_promotion(move, from, to) : push_move(move, PAWN, from, to);
			}
			if (do_quiet && d == abs(pawn_push) && (square_to_bit(to = from + pawn_push) & empty)) {
				move = push_move(move, PAWN, from, to);
				if (is_on_second_rank(from, c) && (square_to_bit(to += pawn_push) & empty)) {
					move = push_move(move, PAWN, from, to);
				}
			}
		}
		// bishop (pinned)
		piece = board->piece[BISHOP] & pinned;
		while (piece) {
			from = square_next(&piece);
			d = dir[from];
			attack = 0;
			if (d == 9) attack = bishop_attack(occupied, from, target & MASK[from].diagonal);
			else if (d == 7) attack = bishop_attack(occupied, from, target & MASK[from].antidiagonal);
			move = push_moves(move, attack, BISHOP, from);
		}
		// rook (pinned)
		piece = board->piece[ROOK] & pinned;
		while (piece) {
			from = square_next(&piece);
			d = dir[from];
			attack = 0;
			if (d == 1) attack = rook_attack(occupied, from, target & MASK[from].rank);
			else if (d == 8) attack = rook_attack(occupied, from, target & MASK[from].file);
			move = push_moves(move, attack, ROOK, from);
		}
		// queen (pinned)
		piece = board->piece[QUEEN] & pinned;
		while (piece) {
			from = square_next(&piece);
			d = dir[from];
			attack = 0;
			if (d == 9) attack = bishop_attack(occupied, from, target & MASK[from].diagonal);
			else if (d == 7) attack = bishop_attack(occupied, from, target & MASK[from].antidiagonal);
			else if (d == 1) attack = rook_attack(occupied, from, target & MASK[from].rank);
			else if (d == 8) attack = rook_attack(occupied, from, target & MASK[from].file);
			move = push_moves(move, attack, QUEEN, from);
		}
	}

	// common moves
	target = enemy; if (do_quiet) target |= empty;

	// enpassant capture
	if (board_enpassant(board) && (!checkers || x_checker == board->enpassant - pawn_push)) {
		to = board->enpassant;
		ep = to - pawn_push;
		from = ep - 1;
		attack = square_to_bit(from);
		if (file(to) > 0 && (board->piece[PAWN] & board->color[c] & attack)) {
			piece = occupied ^ square_to_bit(from) ^ square_to_bit(ep) ^ square_to_bit(to);
			if (!bishop_attack(piece, k, bq & board->color[o]) && !rook_attack(piece, k, rq & board->color[o])) {
				move = push_move(move, PAWN, from, to);
			}
		}
		from = ep + 1;
		attack = square_to_bit(from);
		if (file(to) < 7 && (board->piece[PAWN] & board->color[c] & attack)) {
			piece = occupied ^ square_to_bit(from) ^ square_to_bit(ep) ^ square_to_bit(to);
			if (!bishop_attack(piece, k, bq & board->color[o]) && !rook_attack(piece, k, rq & board->color[o])) {
				move = push_move(move, PAWN, from, to);
			}
		}
	}

	// pawn
	piece = board->piece[PAWN] & unpinned;
	attack = (c ? (piece & ~COLUMN[0]) >> 9 : (piece & ~COLUMN[0]) << 7) & enemy;
	move = push_promotions(move, attack & PROMOTION_RANK[c], pawn_left);
	move = push_pawn_moves(move, attack & ~PROMOTION_RANK[c], pawn_left);

	attack = (c ? (piece & ~COLUMN[7]) >> 7 : (piece & ~COLUMN[7]) << 9) & enemy;
	move = push_promotions(move, attack & PROMOTION_RANK[c], pawn_right);
	move = push_pawn_moves(move, attack & ~PROMOTION_RANK[c], pawn_right);

	attack = (c ? piece >> 8 : piece << 8) & empty;
	move = push_promotions(move, attack & PROMOTION_RANK[c], pawn_push);
	if (do_quiet) {
		move = push_pawn_moves(move, attack & ~PROMOTION_RANK[c], pawn_push);
		attack = (c ? (((piece & RANK[6]) >> 8) & ~occupied) >> 8 : (((piece & RANK[1]) << 8) & ~occupied) << 8) & empty;
		move = push_pawn_moves(move, attack, 2 * pawn_push);
	}

	// knight
	piece = board->piece[KNIGHT] & unpinned;
	while (piece) {
		from = square_next(&piece);
		attack = knight_attack(from, target);
		move = push_moves(move, attack, KNIGHT, from);
	}

	// bishop
	piece = board->piece[BISHOP] & unpinned;
	while (piece) {
		from = square_next(&piece);
		attack = bishop_attack(occupied, from, target);
		move = push_moves(move, attack, BISHOP, from);
	}

	// rook
	piece = board->piece[ROOK] & unpinned;
	while (piece) {
		from = square_next(&piece);
		attack = rook_attack(occupied, from, target);
		move = push_moves(move, attack, ROOK, from);
	}

	// queen
	piece = board->piece[QUEEN] & unpinned;
	while (piece) {
		from = square_next(&piece);
		attack = queen_attack(occupied, from, target);
		move = push_moves(move, attack, QUEEN, from);
	}

	// king
	target = board->color[o]; if (do_quiet) target |= ~occupied;
	attack = king_attack(k, target);
	while (attack) {
		to = square_next(&attack);
		if (!board_is_square_attacked(board, to, o, occupied_no_king)) {
			move = push_move(move, KING, k, to);
		}
	}

	return move - start;
}

/**
 * @brief Generate all legal moves or captures
 * @param ma Move array
 * @param board Board state
 * @param do_quiet Do quiet moves also
 */
static inline void movearray_generate(MoveArray *ma, const Board *board,  const bool do_quiet) {
	ma->i = 0;
	ma->n = generate_moves(board, ma->move, do_quiet);
	ma->move[ma->n] = 0;
}

/**
 * @brief Get next move
 * @param ma Move array
 * @return Next move
 */
static inline Move movearray_next(MoveArray *ma) {
	return ma->move[ma->i++];
}

/**
 * @brief Get number of moves left
 * @param ma Move array
 * @return Number of moves left
 */
static inline int movearray_todo(const MoveArray *ma) {
	return ma->n - ma->i;
}

/**
 * @brief Sort moves in the array
 * @param ma Move array
 */
static inline void movearray_sort(MoveArray *ma) {
	qsort(ma->move, ma->n, sizeof(Move), move_compare);
}

/**
 * @brief Hash creation
 * @param size Hash table size
 * @return Hash table
 */
static HashTable* hash_create(const size_t size) {
	if (size == 0) return NULL;

	const size_t n = stdc_bit_floor_ull(size << 20) / sizeof(Hash);

	HashTable *hash_table = malloc(sizeof (HashTable));
	if (hash_table == NULL) memory_error(__func__);
	hash_table->hash = malloc((n + BUCKET_SIZE) * sizeof (Hash));
	if (hash_table->hash == NULL) memory_error(__func__);
	hash_table->spin = malloc((n + BUCKET_SIZE) * sizeof (atomic_int));
	if (hash_table->spin == NULL) memory_error(__func__);
	hash_table->mask = n - 1;

	return hash_table;
}

/**
 * @brief Hash free resources
 * @param hash_table Hash table
 */
static void hash_destroy(HashTable *hash_table) {
	if (hash_table) {
		free(hash_table->hash);
		free(hash_table->spin);
		free(hash_table);
	}
}

/**
 * @brief Clear the Hash table.
 * @param hash_table Hash table
 */
static inline void hash_clear(HashTable *hash_table) {
	memset(hash_table->hash, 0, (hash_table->mask + BUCKET_SIZE + 1) * sizeof(Hash));
	memset(hash_table->spin, 0, (hash_table->mask + BUCKET_SIZE + 1) * sizeof(atomic_int));
}

/**
 * @brief Hash probe
 * @param hash_table Hash table
 * @param key Key
 * @param depth Depth
 * @return Count
 */
static uint64_t hash_probe(const HashTable *hash_table, const Key *key, const uint32_t depth) {
	Hash *hash = hash_table->hash + (key->index & hash_table->mask);
	atomic_int *spin = hash_table->spin + (key->index & hash_table->mask);

	for (int i = 0; i < BUCKET_SIZE; ++i) {
		if (hash[i].code == key->code && (hash[i].data & 0x3f) == depth) {
			spin_lock(spin + i);
			if (hash[i].code == key->code && (hash[i].data & 0x3f) == depth) {
				uint64_t count = hash[i].data >> 6;
				spin_unlock(spin + i);
				return count;
			}
			spin_unlock(spin + i);
		}
	}
	return 0;
}

/**
 * @brief Store a count result into the hash table.
 * @param hash_table Hash table
 * @param key Key
 * @param depth Depth
 * @param count Count
 */
static void hash_store(const HashTable *hash_table, const Key *key, const uint32_t depth, const uint64_t count) {
	Hash *hash = (hash_table->hash + (key->index & hash_table->mask));
	atomic_int *spin = hash_table->spin + (key->index & hash_table->mask);
	const uint64_t data = count << 6 | depth;
	int i, j;

	for (i = j = 0; i < BUCKET_SIZE; ++i) {
		if (hash[i].code == key->code && hash[i].data == data) {
			spin_lock(spin + i);
			if (hash[i].code == key->code && hash[i].data == data) {
				spin_unlock(spin + i);
				return;
			};
			spin_unlock(spin + i);
		}
		if (hash[i].data < hash[j].data) j = i; // here we don't care of the lock, a few bad chosen storage place is fine.
	}

	spin_lock(spin + j);
		hash[j].code = key->code;
		hash[j].data = data;
	spin_unlock(spin + j);
}

/**
 * @brief Prefetch a hash table entry (for faster access).
 * @param hashtable Hash table
 * @param key Key
 */
static inline void hash_prefetch(const HashTable *hashtable, const Key *key) {
#if defined(__x86_64__)
	_mm_prefetch((const char*) (hashtable->hash + (key->index & hashtable->mask)), _MM_HINT_T2);
	_mm_prefetch((const char*) (hashtable->spin + (key->index & hashtable->mask)), _MM_HINT_T2);
#elif defined __GNUC__
	__builtin_prefetch((const char*) (hashtable->hash + (key->index & hashtable->mask)));
	__builtin_prefetch((const char*) (hashtable->spin + (key->index & hashtable->mask)));
#endif
}

/**
 * @brief Put an idle task into the task pool.
 * @param task_pool Task pool
 * @param task Task
 */
static void taskpool_put_idle_task(TaskPool *task_pool, Task *task) {
	spin_lock(&task_pool->spin);
		task_pool->idle_tasks[task_pool->n_idle++] = task;
	spin_unlock(&task_pool->spin);
}

/**
 * @brief Run a perft search in parallel.
 * @param task Task
 */
static uint64_t perft(const Board *, HashTable*, TaskPool *, const uint32_t, const Option*);
static void task_run(Task *task) {
	uint64_t count;
	Node *node = task->node;
	HashTable *hash_table = task->hash_table;
	TaskPool *task_pool = task->task_pool;
	const Board *board = &task->board;
	const Key *key = &board->key;
	const uint32_t depth  = task->depth;
	const Option *option = task->option;

	// do perft
	if (hash_table) {
		count = hash_probe(hash_table, key, depth);
		if (count == 0) {
			count = perft(board, hash_table, task_pool, depth, option);
			hash_store(hash_table, key, depth, count);
		}
	} else count = perft(board, hash_table, task_pool, depth, option);

	// store the result & release the thread
	spin_lock(&node->spin);
		node->count += count;
		for (int i = 0; i < node->n_split; i++) {
			if (node->task_id[i] == task->id) {
				node->task_id[i] = node->task_id[--node->n_split];
				break;
			}
		}
	spin_unlock(&node->spin);

	task->run = false;
	taskpool_put_idle_task(task->task_pool, task);

}

/**
 * Free resources used by a task.
 * @param task Task
 */
static void task_free(Task *task) {
	task->loop = false;
	cnd_signal(&task->condition);
	thrd_join(task->thread, NULL);
	mtx_destroy(&task->mutex);
	cnd_destroy(&task->condition);
}

/**
 * Task loop.
 * @param param Task
 */
static int task_loop(void *param)
{
	Task *task = (Task*) param;

	mtx_lock(&task->mutex);
	task->loop = true;
	task->run = false;

	while (task->loop) {
		if (!task->run) cnd_wait(&task->condition, &task->mutex);
		if (task->run) task_run(task);
	}

	mtx_unlock(&task->mutex);

	return thrd_success;
}

/**
 * Initialize a task.
 * @param task Task
 * @param hash_table Hash table
 * @param task_pool Task pool
 * @param id Task ID
 * @param option Option
 */
static void task_init(Task *task, HashTable *hash_table, TaskPool *task_pool, const int id, const Option *option) {
	task->run = false;
	task->loop = false;
	task->id = id;
	task->depth = 0;
	task->hash_table = hash_table;
	task->task_pool = task_pool;
	task->option = option;
	mtx_init(&task->mutex, mtx_plain);
	cnd_init(&task->condition);
	thrd_create(&task->thread, task_loop, task);
}

/**
 * Initialize a pool of tasks
 * @param task_pool Task pool
 * @param n_workers Number of parallel threads (total threads - 1)
 * @param hash_table Hash table
 * @param option Option
 */
static void taskpool_init(TaskPool *task_pool, const int n_workers, HashTable *hash_table, const Option *option) {
	task_pool->n_tasks = task_pool->n_idle = n_workers;
	task_pool->tasks = n_workers ? malloc(n_workers * sizeof (Task)) : NULL;
	task_pool->idle_tasks = n_workers ? malloc(n_workers * sizeof (Task*)) : NULL;
	spin_init(&task_pool->spin);
	for (int i = 0 ; i < n_workers; ++i) {
		task_init(task_pool->tasks + i, hash_table, task_pool, i, option);
		task_pool->idle_tasks[i] = task_pool->tasks + i;
	}
	// wait for all tasks to be initialized...
	thrd_sleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 10000}, NULL);
}

/**
 * @breif Free ressources of a pool of tasks
 * @param task_pool Task pool
 */
static void taskpool_free(TaskPool *task_pool) {
	for (int i = 0 ; i < task_pool->n_tasks; ++i) {
		task_free(task_pool->tasks + i);
	}
	free(task_pool->tasks);
	free(task_pool->idle_tasks);
}

/**
 * @brief Initialize a node
 * @param node Node to initialize
 */
static inline void node_init(Node *node) {
	spin_init(&node->spin);
	node->n_split = 0;
	node->count = 0;
}

/**
 * @brief Split the search
 * @param node Node to split
 * @param board Board to split
 * @param task_pool Task pool
 * @param depth Depth of the search
 * @param moves_todo Number of moves remaining
 * @return True if the search was split, false otherwise
 */
static bool node_split(Node *node, const Board *board, TaskPool *task_pool, const int depth, const int moves_todo) {
	if (depth < MIN_SPLIT_DEPTH || moves_todo < MIN_SPLIT_REMAINING_MOVES) return false;

	spin_lock(&task_pool->spin);
		if (task_pool->n_idle == 0) {
			spin_unlock(&task_pool->spin);
			return false;
		}

		spin_lock(&node->spin);
			if (node->n_split >= MAX_SPLIT) {
				spin_unlock(&node->spin);
				spin_unlock(&task_pool->spin);
				return false;
			}
			Task *task = task_pool->idle_tasks[--task_pool->n_idle];
			node->task_id[node->n_split++] = task->id;
		spin_unlock(&node->spin);
	spin_unlock(&task_pool->spin);

	mtx_lock(&task->mutex);
		task->run = true;
		task->board = *board;
		task->node = node;
		task->depth = depth;
		// if (depth > 6) printf("Splitting node using task %d at depth %d\n", task->id, depth);
		cnd_signal(&task->condition);
	mtx_unlock(&task->mutex);

	return true;
}

/**
 * @brief Wait for all tasks to terminate
 * @param node Node to wait for
 * @return Total count of nodes
 */
static inline uint64_t node_wait(const Node *node) {
	while (node->n_split > 0) thrd_yield();

	return node->count;
}

/**
 * @brief Recursive Perft with optional hashtable, pool of tasks, bulk counting & capture only generation.
 * @param board Board to analyze
 * @param hashtable Hash table to use
 * @param task_pool Task pool to use
 * @param depth Depth to analyze
 * @param option Options to use
 * @return Total count of nodes
 */
static uint64_t perft(const Board *board, HashTable *hashtable, TaskPool *task_pool, const uint32_t depth, const Option *option) {
	const bool use_hash = (hashtable && depth >= MIN_HASH_DEPTH);
	const bool use_bulk_counting = (option->bulk && depth == 2);
	Board next;
	uint64_t count = 0, hash_count;
	Move move;
	MoveArray ma;
	Node node;
	Key key;

	movearray_generate(&ma, board, option->do_quiet || board->checkers);
	node_init(&node);

	while ((move = movearray_next(&ma)) != 0) {
		if (use_hash) {
			key_update(&key, board, move);
			hash_prefetch(hashtable, &key);
		}
		board_copymake(board, move, &key, &next);
		if (depth == 1) ++count;
		else if (use_bulk_counting) count += count_moves(&next, option->do_quiet || next.checkers);
		else {
			if (use_hash) {
				hash_count = hash_probe(hashtable, &key, depth - 1);
				if (hash_count == 0) {
					if (!node_split(&node, &next, task_pool, depth - 1, movearray_todo(&ma))) {
						hash_count += perft(&next, hashtable, task_pool, depth - 1, option);
						hash_store(hashtable, &key, depth - 1, hash_count);
					};
				}
				count += hash_count;
			} else if (!node_split(&node, &next, task_pool, depth - 1, movearray_todo(&ma))) {
				count += perft(&next, hashtable, task_pool, depth - 1, option);
			}
		}
	}
	count += node_wait(&node);

	return count;
}


/** Test */
static void test(HashTable *hashtable,TaskPool *task_pool, const bool bulk) {
	Board board;
	const Option option = { .bulk = bulk, .do_quiet = true };
	typedef struct TestBoard {
		char *comments, *fen;
		uint64_t result;
		uint32_t depth;
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
		{"20.", "rnbqkb1r/pp1p1ppp/2p5/4P3/2B5/8/PPP1NnPP/RNBQK2R w KQkq - 0 6", 94854874131, 7},
		{NULL, NULL, 0, 0}
	};

	printf("Testing the board generator\n");
	for (TestBoard *t = tests; t->fen != NULL; ++t) {
		printf("Test %s %s", t->comments, t->fen); fflush(stdout);
		board_set(&board, t->fen);
		uint64_t count = perft(&board, hashtable, task_pool, t->depth, &option);
		if (count == t->result) printf(" passed\n"); else printf(" FAILED ! %" PRIu64 " != %" PRIu64 "\n", count, t->result);
	}
}

/**
 * @brief main function: programme entry
 *
 * Read the command line & execute what it is asked for.
 *
 * @param argc Number of arguments
 * @param argv Arguments
 * @return 0
 */
int main(int argc, char **argv) {
	double full_time= -chrono(), partial_time = 0.0, total_time = 0.0;
	Board board, next;
	HashTable *hashtable = NULL;
	TaskPool task_pool = {.n_tasks = 0, .n_idle = 0};
	Key key;
	MoveArray ma;
	uint64_t count, total = 0;
	uint64_t seed = 0xA170EBA;
	char *fen = NULL, *moves = NULL;
	uint32_t depth = 6, hash_size = 0, n_threads = 1, n_repetition = 1;
	Move move;
	bool div = false, loop = false, verbose = true, do_test = false;
	Option option = {false, true};

	// argument
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--bulk") || !strcmp(argv[i], "-b")) option.bulk = true;
		else if (!strcmp(argv[i], "--capture") || !strcmp(argv[i], "-c")) option.do_quiet = false;
		else if (!strcmp(argv[i], "--depth") || !strcmp(argv[i], "-d")) depth = atoi(argv[++i]);
		else if (isdigit((int) argv[i][0])) depth = atoi(argv[i]);
		else if (!strcmp(argv[i], "--div")) div = true;
		else if (!strcmp(argv[i], "--fast")) {
			option.bulk = true;
			hash_size = get_available_memory();
			n_threads = get_available_processors();
		} else if (!strcmp(argv[i], "--fen") || !strcmp(argv[i], "-f")) fen = argv[++i];
		else if (i < argc - 1 && (!strcmp(argv[i], "--hash") || !strcmp(argv[i], "-h"))) hash_size = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--kiwipete") || !strcmp(argv[i], "-k")) fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
		else if (i < argc - 1 && (!strcmp(argv[i], "--moves") || !strcmp(argv[i], "-m"))) moves = argv[++i];
		else if (!strcmp(argv[i], "--loop") || !strcmp(argv[i], "-l")) loop = true;
		else if (!strcmp(argv[i], "--quiet") || !strcmp(argv[i], "-q")) verbose = false;
		else if (i < argc - 1 && (!strcmp(argv[i], "--repeat") || !strcmp(argv[i], "-r"))) n_repetition=atoi(argv[++i]);
		else if (i < argc - 1 && (!strcmp(argv[i], "--seed") || !strcmp(argv[i], "-s"))) seed = atoi(argv[++i]);
		else if (i < argc - 1 && (!strcmp(argv[i], "--threads") || !strcmp(argv[i], "-t"))) n_threads = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--test")) do_test = true;
		else {
			printf("%s <args> \n", argv[0]);
			puts("Enumerate moves. The following options are available:");
			puts("\t--bulk|-b               Do fast bulk counting at the last ply.");
			puts("\t--capture|-c            Generate only captures, promotions & check evasions.");
			puts("\t[--depth|-d] <depth>    Test up to this depth (default = 6).");
			puts("\t--div                   Print a node count for each move.");
			puts("\t--fast                  Automatically set highest settings.");
			puts("\t--fen|-f <fen>          Use the position indicated in FEN format (default = starting position).");
			puts("\t--hash|-h <size>        Use a hashtable with <size> Megabytes (default = 0, no hashtable).");
			puts("\t--help|-?               Print this message.");
			puts("\t--kiwipete|-k           Use the kiwipete position.");
			puts("\t--moves|-m              Play a series of moves to build the position to use.");
			puts("\t--loop|-l               Loop from depth 1 to <depth>.");
			puts("\t--quiet|-q              Disable verbose output.");
			puts("\t--repeat|-r <n>         Repeat the test <n> time (default = 1).");
			puts("\t--seed|-s <seed>        Change the seed of the pseudo move generator to <seed>.");
			puts("\t--test                  Run an internal test to check the move generator.");
			puts("\t--threads|-t <threads>  Use <threads> threads for parallel processing (default = 1).");
			return 0;
		}
	}

	// hash table initialization
	init(seed);
	if (hash_size > 65536) hash_size = 65536; // max size of 64G for a 32 bit index
	hashtable = hash_create(hash_size);

	// thread initialization
	if (n_threads < 1) n_threads = 1;
	if (n_threads > 256) n_threads = 256;
	taskpool_init(&task_pool, n_threads - 1, hashtable, &option);

	if (do_test) {
		test(hashtable, &task_pool, option.bulk);
		return 0;
	}

	// board initialization
	board_init(&board);
	if (fen) board_set(&board, fen);
	if (moves) {
		while ((moves = parse_move(moves, &board, &move)) != NULL) {
			key_update(&key, &board, move);
			board_copymake(&board, move, &key, &next);
			board = next;
		}
	}

	if (depth < 1) depth = 1;
	if (depth > 63) depth = 63;
	if (n_repetition < 1) n_repetition = 1;

	if (verbose) {
		puts("Magic Perft version 4.0 (c) Richard Delorme 2020 - 2026");
		#if HAS_PEXT
			puts("Bitboard move generation based on magic (pext) bitboards");
		#else
			puts("Bitboard move generation based on magic bitboards");
		#endif
		printf("Perft setting: ");
		if (hash_size == 0) printf("no hashing; ");
		else printf("hashtable size: %u Mbytes (%" PRIu64 " entries); ", (unsigned) (sizeof (Hash) * (hashtable->mask + BUCKET_SIZE + 1) >> 20), hashtable->mask + BUCKET_SIZE + 1);
		if (n_threads > 1) printf("with %u threads; ", n_threads); else printf("no multithreading; ");
		if (option.bulk) printf("with"); else printf("no"); printf(" bulk counting;");
		if (!option.do_quiet) printf(" capture only;");
		puts("");
		board_print(&board, stdout);
	}
	// root search
	if (div) {
		for (uint32_t r = 1; r <= n_repetition; ++r) {
			for (uint32_t d = (loop ? 1 : depth); d <= depth; ++d) {
				if (n_repetition > 1) printf("repetition: %u\n", r);
				printf("depth: %u\n", d);
				movearray_generate(&ma, &board, option.do_quiet || board.checkers);
				movearray_sort(&ma);
				while ((move = movearray_next(&ma)) != 0) {
					partial_time = -chrono();
					key_update(&key, &board, move);
					board_copymake(&board, move, &key, &next);
					if (d == 1) count = 1;
					else if (option.bulk && d == 2) count = count_moves(&next, option.do_quiet || next.checkers);
					else count = perft(&next, hashtable, &task_pool, depth - 1, &option);
					total += count;
					partial_time += chrono();
					total_time += partial_time;
					printf("%5s %18" PRIu64 " leaves in %10.3f s %14.0f leaves/s\n", move_to_string(move, NULL), count, partial_time, count / partial_time);
				}
			}
		}
	} else {
		for (uint32_t r = 1; r <= n_repetition; ++r) {
			for (uint32_t d = (loop ? 1 : depth); d <= depth; ++d) {
				if (hashtable) hash_clear(hashtable);
				partial_time = -chrono();
				count = perft(&board, hashtable, &task_pool, d, &option);
				total += count;
				partial_time += chrono();
				total_time += partial_time;
				printf("perft %2d : %18" PRIu64 " leaves in %10.3f s %14.0f leaves/s\n", d, count, partial_time, count / partial_time);
			}
		}
	}
	if (div || loop || n_repetition > 1) printf("total    : %18" PRIu64 " leaves in %10.3f s %14.0f leaves/s\n", total, total_time, total / total_time);


	hash_destroy(hashtable);
	taskpool_free(&task_pool);
	free(MASK->bishop.attack);
	free(MASK->rook.attack);

	full_time += chrono();
	if (verbose) printf("full time: %10.3f s\n", full_time);

	return 0;
}
