/**
 * @file mperft.c
 *
 * @brief perft using magic bitboard, nullmove bulk counting, multithreading & transposition table.
 *
 * @author Richard Delorme
 * @copyright 2020-2026
 * @version 5.1
 */

/* includes */
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#if defined(_WIN32)
	#include <intrin.h>
	#include <windows.h>
#endif

#if defined(__linux__)
	#if defined(__x86_64__)
		#include <x86intrin.h>
	#endif
	#include <unistd.h>
	#include <sys/sysinfo.h>
#endif

#if defined(__APPLE__) || defined(__FreeBSD__)
	#include <sys/types.h>
	#include <sys/sysctl.h>
	#include <os/proc.h>
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
		static inline unsigned long long stdc_bit_floor_ull(const unsigned long long x) { return x ? 1ULL << (63 - __lzcnt64(x)) : x; }
	#elif defined(__GNUC__)
		static inline unsigned int stdc_count_ones_ull(const unsigned long long int x) { return __builtin_popcountll(x); }
		static inline unsigned int stdc_count_zeros_ull(const unsigned long long int x) { return __builtin_popcountll(~x); }
		static inline unsigned int stdc_trailing_zeros_ull(const unsigned long long x) { return x ? __builtin_ctzll(x) : 64; }
		static inline unsigned long long stdc_bit_floor_ull(const unsigned long long x) { return x ? 1ULL << (63 - __builtin_clzll(x)) : x; }
	#endif
	static inline unsigned long long stdc_has_single_bit(const unsigned long long x) { return x && (x - 1) & x == 0; }
#endif

// fast PEXT availability
#if (defined(__BMI2__) && !defined(__znver1__) && !defined(__znver2__))
	#define HAS_PEXT true
#else
	#define HAS_PEXT false
#endif

/** Counter: a 128 bit unsigned integer */
#if defined(__SIZEOF_INT128__) && defined(USE_INT128)
	typedef __int128 int128_t;
	typedef unsigned __int128 uint128_t;
	typedef uint128_t Counter;
#else
	#undef USE_INT128
	typedef uint64_t Counter;
#endif

/** statistics info */
#define NULLMOVE_STATS(x)
#define SPLIT_STATS(x)
#define HASH_STATS(x)

NULLMOVE_STATS(static atomic_llong nullmove_counter = 0;)
NULLMOVE_STATS(static atomic_llong plain_counter = 0;)

SPLIT_STATS(static atomic_llong split_tries = 0;)
SPLIT_STATS(static atomic_llong split_successes = 0;)

HASH_STATS(static atomic_llong probe_tries = 0;)
HASH_STATS(static atomic_llong probe_successes = 0;)
HASH_STATS(static atomic_llong stores = 0;)


/** Types */
/** Move size
 * Note that a legal position can't have more than 218 moves:
 * https://lichess.org/@/Tobs40/blog/why-a-reachable-position-can-have-at-most-218-playable-moves/a5xdxeqs
 */
enum : size_t { MOVE_SIZE = 256, MAP_SIZE = 16 };

/** Bitboard: 64-bit unsigned integer representing a bitboard */
typedef uint64_t Bitboard;

/** Random: 64-bit unsigned integer representing a random number */
typedef uint64_t Random;

/** Color: Enum representing the color of a piece */
typedef enum : int { WHITE, BLACK, COLOR_SIZE } Color;

/** PerftLimits: Enum representing the limits to split the search or to probe the hash */
enum : int { MAX_SPLIT = 16, MIN_SPLIT_DEPTH = 4, MIN_SPLIT_REMAINING_MOVES = 3, MIN_HASH_DEPTH = 3 };

/** Square: Enum representing the squares on the board */
typedef enum : int {
	A1, B1, C1, D1, E1, F1, G1, H1,
	A2, B2, C2, D2, E2, F2, G2, H2,
	A3, B3, C3, D3, E3, F3, G3, H3,
	A4, B4, C4, D4, E4, F4, G4, H4,
	A5, B5, C5, D5, E5, F5, G5, H5,
	A6, B6, C6, D6, E6, F6, G6, H6,
	A7, B7, C7, D7, E7, F7, G7, H7,
	A8, B8, C8, D8, E8, F8, G8, H8,
	BOARD_SIZE, SQUARE_NONE = BOARD_SIZE, BOARD_OUT = -1
} Square;

/** Enum representing the squares on the board as bitboard values */
enum : uint64_t {
	B_A1 = 1ULL << A1, B_C1 = 1ULL << C1, B_G1 = 1ULL << G1, B_H1 = 1ULL << H1,
	B_A8 = 1ULL << A8, B_C8 = 1ULL << C8, B_G8 = 1ULL << G8, B_H8 = 1ULL << H8,
};

/** Piece: Enum representing the pieces on the board */
typedef enum : int { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, PIECE_SIZE } Piece;

/** CPiece: Enum representing the pieces on the board */
typedef enum : int { EMPTY, WPAWN, BPAWN, WKNIGHT, BKNIGHT, WBISHOP, BBISHOP, WROOK, BROOK, WQUEEN, BQUEEN, WKING, BKING, CPIECE_SIZE } CPiece;

/** Enum representing precomputed promotions */
enum : uint32_t { KNIGHT_PROMOTION = KNIGHT << 15, BISHOP_PROMOTION = BISHOP << 15, ROOK_PROMOTION = ROOK << 15, QUEEN_PROMOTION = QUEEN << 15 };

/** Move: Enum representing the moves on the board */
typedef uint32_t Move;

/** Key: Struct representing a key for a hash table */
typedef struct {
#ifdef USE_INT128
	uint128_t code; ///< Hash code for the key
#else
	uint64_t code; ///< Hash code for the key
#endif
	uint64_t index; ///< Index for the key
} Key;

/** Attack: Struct to compute the target squares of a sliding piece on the board */
typedef struct {
#if	HAS_PEXT == false
	Bitboard magic; ///< Bitboard magic for the attack
	Bitboard shift; ///< Bitboard shift for the attack
#endif
	Bitboard mask; ///< Bitboard mask for the attack
	Bitboard *attack; ///< Bitboard attacks for the attack
} Attack;

/** Mask: Struct to compute various bitboard mask on the board */
typedef struct {
	Bitboard between[BOARD_SIZE]; ///< Bitboard mask for the squares between two squares
	Bitboard between_in[BOARD_SIZE]; ///< Bitboard mask for the squares between two squares including the mask's square
	int direction[BOARD_SIZE]; ///< Bitboard direction
	Bitboard diagonal; ///< Bitboard mask for the diagonal squares
	Bitboard antidiagonal; ///< Bitboard mask for the antidiagonal squares
	Bitboard file; ///< Bitboard mask for the file squares
	Bitboard rank; ///< Bitboard mask for the rank squares
	Bitboard pawn_attack[COLOR_SIZE]; ///< Bitboard mask for the pawn attack squares
	Bitboard pawn_push[COLOR_SIZE]; ///< Bitboard mask for the pawn push squares
	Bitboard enpassant; ///< Bitboard mask for the enpassant squares
	Bitboard knight; ///< Bitboard mask for the knight squares
	Bitboard bishop; ///< Bitboard mask for both diagonals
	Bitboard rook; ///< Bitboard mask for the rank & files
	Bitboard queen; ///< Bitboard mask for the rank & file & diagonals
	Bitboard king; ///< Bitboard mask for the king squares
} Mask;

/** Board: Struct to represent the chess board */
typedef struct {
	Bitboard piece[PIECE_SIZE]; ///< Bitboard mask for the pieces
	Bitboard color[COLOR_SIZE]; ///< Bitboard mask for the pieces' colors
	Bitboard pins; ///< Bitboard mask for the pins squares
	Bitboard checkers; ///< Bitboard mask for the checkers squares
	Bitboard castling; ///< Bitboard mask for the castling squares
	Key key; ///< Zobrist key
	uint8_t x_king[COLOR_SIZE]; ///< The king squares
	uint8_t player; ///< Current player color
	uint8_t enpassant; ///< The enpassant square
} Board;

/** MoveArray: Struct to represent an array of moves */
typedef struct {
	Move move[MOVE_SIZE]; ///< Array of moves
	int n; ///< Number of moves in the array
	int i; ///< Index of the current move
} MoveArray;

/** Map: Struct to represent a map of moves */
typedef struct {
	Bitboard to; ///< A bitboard with all the to squares
	Square from; ///< The from square
	Piece piece; ///< The piece type
} Map;

/** PawnMap: Struct to represent a map of pawn moves */
typedef struct {
	Bitboard left_capture; ///< A bitboard with all the to squares for all capture to the left side
	Bitboard right_capture; ///< A bitboard with all the to squares for all capture to the right side
	Bitboard enpassant; ///< A bitboard with all the to squares for all en passant captures
	Bitboard push; ///< A bitboard with all the to squares for all push moves
	Bitboard double_push; ///< A bitboard with all the to squares for all double push moves
} PawnMap;

/** MapArray: Struct to represent an array of maps */
typedef struct {
	Map map[MAP_SIZE]; ///< Array of maps
	PawnMap pawn_map; ///< pawn map
	int n; ///< Number of maps in the array
} MapArray;

/** Hash: Struct to represent an entry in the transposition table */
typedef struct {
#ifdef USE_INT128
	uint128_t code; ///< Hash code
	uint128_t data; ///< Hash data : count (122 bits) | depth (6 bits)
#else
	uint64_t code; ///< Hash code
	uint64_t data; ///< Hash data : count (56 bits) | depth (6 bits)
#endif
} Hash;

/** HashTable: Struct to represent a hash table */
typedef struct {
	Hash *hash; ///< Array of hash entries
	atomic_int *spin; ///< Array of spin locks
	uint64_t mask; ///< Hash table mask
} HashTable;

/** Task: Struct to represent a task in the search tree */
typedef struct Task {
	Board board; ///< Board state at this task
	SPLIT_STATS(uint32_t count;) ///< Count of run for this task
	struct Node *node; ///< Pointer to the node associated with this task
	int depth; ///< Perft depth
	int id; ///< Task ID

	thrd_t thread; ///< Thread ID
	mtx_t mutex; ///< Mutex for the task
	cnd_t condition; ///< Condition variable for the task
	bool loop; ///< Loop flag for the task
	bool run; ///< Run flag for the task
} Task;

/** Node: Struct to represent a node where the perft is splitted among several tasks in the search tree */
typedef struct Node {
	Counter count; ///< Count of moves at this node
	atomic_int spin; ///< Spin lock for the node
	int n_split; ///< Number of tasks used at this node
} Node;

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
static const Bitboard RANK[] =  {
	0x00000000000000FFULL, 0x000000000000FF00ULL, 0x0000000000FF0000ULL, 0x00000000FF000000ULL,
	0x000000FF00000000ULL, 0x0000FF0000000000ULL, 0x00FF000000000000ULL, 0xFF00000000000000ULL,
};

/** COLUMN: Bitboard representing a column mask */
static const Bitboard COLUMN[] = {
	0x0101010101010101ULL, 0x0202020202020202ULL, 0x0404040404040404ULL, 0x0808080808080808ULL,
	0x1010101010101010ULL, 0x2020202020202020ULL, 0x4040404040404040ULL, 0x8080808080808080ULL,
};

/** PUSH: Array representing the push values for each pawn color */
static const int PUSH[] = {8, -8};

/** PROMOTION_RANK: Array representing the promotion rank for each color */
static const Bitboard PROMOTION_RANK[] = {0xff00000000000000ULL, 0x00000000000000ffULL};

static const Bitboard MASK_CASTLING[BOARD_SIZE] = {
	B_C1, 0, 0, 0, B_C1 | B_G1, 0, 0, B_G1,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	B_C8, 0, 0, 0, B_C8 | B_G8, 0, 0, B_G8,
};

/** CAN_CASTLE_KINGSIDE: Array representing the ability to castle kingside for each color */
static const Bitboard CAN_CASTLE_KINGSIDE[COLOR_SIZE] = {B_G1, B_G8};

/** CAN_CASTLE_QUEENSIDE: Array representing the ability to castle queenside for each color */
static const Bitboard CAN_CASTLE_QUEENSIDE[COLOR_SIZE] = {B_C1, B_C8};

/** MASK48: Mask for 48-bit random number generation */
static const Random MASK48 = 0xFFFFFFFFFFFFULL;

/** BUCKET_SIZE: Size of the bucket */
static const int BUCKET_SIZE = 4;

/** SPINLOCK STATE Enum representing the state of a spin lock */
enum {SL_FREE = 0, SL_BUSY = 1};

/* Globals: shared value & struicture all over the code */
static Attack ATTACK_BISHOP[BOARD_SIZE]; ///< Attack by bishop for each square of the board
static Attack ATTACK_ROOK[BOARD_SIZE]; ///< Attack by rook for each square of the board
static Mask MASK[BOARD_SIZE]; ///< Mask for each square of the board
static Mask MASK[BOARD_SIZE]; ///< Mask for each square of the board
static Key KEY_PLAYER[COLOR_SIZE]; ///< Key for each player
static Key KEY_SQUARE[BOARD_SIZE][CPIECE_SIZE]; ///< Key for each square and piece
static Key KEY_CASTLING[BOARD_SIZE]; ///< Key for each castling state
static Key KEY_ENPASSANT[BOARD_SIZE + 1]; ///< Key for each en passant state
static Key KEY_PLAY; ///< Key to switch players
static HashTable *hash_table = NULL;
static TaskPool *task_pool = NULL;
static bool bulk = false;
static bool nullmove = false;

/* function declaration */
static inline Piece board_get_piece(const Board*, const Square);
static Counter perft(const Board*, const uint32_t);

/**
 * @brief Counter to a string, with thousands separated by a comma
 * Limitations:
 *  - Assume that the counter is less than 10^36
 *  - this function is not reentrant.
 *  - do not use the locale separator (on purpose)
 *
 * @param count Counter to convert
 * @return Pointer to the buffer
 */
static char* pretty_counter(Counter count) {
	static char buf[64];
	char tmp[64];
	int i, j, digit;

	for (i = 0, j = 0; count > 0; ++j, ++i) {
		digit = count % 10;
		count /= 10;
		tmp[j] = '0' + digit;
		if (count > 0 && i % 3 == 2) tmp[++j] = ',';
	}
	if (j == 0) tmp[j++] = '0';

	for (i = 0; i < j; ++i) buf[j - i - 1] = tmp[i];
	buf[j] = '\0';

	return buf;
}

/**
 * @brief print a double with a metric prefix
 * Limitations:
 *  - this function is not reentrant.
 *  - do not use the locale decimal point (on purpose)
 *
 * @param n double to print out
 * @return Pointer to the buffer
 */
static const char* pretty_speed(double n) {
	char unit[] = " kMGTPEZYRQ";
	static char buf[64];
	int u = 0;

	if (isinf(n)) return "inf ";
	if (isnan(n)) return "nan ";

	while (n >= 1000 && u < 10) {
		n /= 1000;
		++u;
	}

	sprintf(buf, "%7.3f %c", n, unit[u]);

	return buf;
}


/**
 * @brief print a time as d:hh:mm:ss.ms
 * Limitations:
 *  - this function is not reentrant.
 *  - do not use the locale decimal point (on purpose)
 *
 * @param t time to print out
 * @return Pointer to the buffer
 */
static const char* pretty_time(double t) {
	int d, h, m, s, ms;
	int sign;
	const char *space = "   ";
	static char buf[64];

	if (t < 0) { sign = -1; t = -t; } else sign = +1;
	d = t / 86400; t -= d * 86400LL;
	h = t / 3600; t -= h * 3600;
	m = t / 60; t -= m * 60;
	s = t; t -= s;
	ms = t * 1000;

	if (d) sprintf(buf, "%2d:%02d:%02d:%02d.%03d", sign*d, h, m, s, ms);
	else if (h) sprintf(buf, "%s%2d:%02d:%02d.%03d", space, sign*h, m, s, ms);
	else if (m) sprintf(buf, "%s%s%2d:%02d.%03d", space, space, sign*m, s, ms);
	else sprintf(buf, "%s%s%s%2d.%03d", space, space, space, sign*s, ms);

	return buf;
}

/**
 * @brief Get a 64 bit random number
 * @param random Random number generator
 * @return Random number
 */
static uint64_t random_get_u64(Random *random) {
	const uint64_t A = 0x5DEECE66DULL;
	const uint64_t B = 0xBULL;
	uint64_t r;

	*random = ((A * *random + B) & MASK48);
	r = *random >> 16;
	*random = ((A * *random + B) & MASK48);
	return (r << 32) | (*random >> 16);
}

/**
 * @brief Get a 128 bit random number
 * @param random Random number generator
 * @return Random number
 */
#ifdef USE_INT128
static uint128_t random_get_u128(Random *random) {
	uint128_t r = random_get_u64(random);
	return (r << 64) | random_get_u64(random);
}
#endif

/**
 * @brief Init the random generator
 * @param random Random number generator
 * @param seed Seed for random number generator
 */
static inline void random_seed(Random *random, const uint64_t seed) {
	*random = (seed & MASK48);
}

/**
 * @brief Get available memory in Megabytes
 * @return Available memory in MB
 */
static inline uint64_t get_available_memory(void) {
#if defined(__linux__)
	struct sysinfo info;
	if (sysinfo(&info) == 0) {
		return (info.freeram * info.mem_unit) >> 20;
	}
	return 1024; // 1GB by default expected to be enough for most systems
#elif defined(_WIN32)
	MEMORYSTATUSEX statex;
	statex.dwLength = sizeof (statex);
	GlobalMemoryStatusEx (&statex);
	return statex.ullAvailPhys >> 20;
#elif defined __APPLE__
	return os_proc_available_memory() >> 20;
#else
	return 1024; // 1GB by default expected to be enough for most systems
#endif
}

/**
 * @brief Get the number of (logical) processors
 * @return Number of processors
 */
static inline int get_available_processors(void) {
#if defined(__linux__)
	 return get_nprocs();
#elif defined(_WIN32)
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors;
#elif defined(__APPLE__) || defined(__FreeBSD__)
	int count;
	size_t size = sizeof(count);
	if (!sysctlbyname("hw.ncpu", &count, &size, NULL, 0)) count = 1;
	return count;
#else
	return 1;
#endif
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
static inline bool square_parse(const char ** string, Square *x) {
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
static inline Bitboard castling_from_char(const char c) {
	switch (c) {
		case 'K': return B_G1;
		case 'Q': return B_C1;
		case 'k': return B_G8;
		case 'q': return B_C8;
		default: return 0;
	}
}

/**
 * @brief Get the moving piece
 * @param move Move
 * @return Moving piece
 */
static inline Piece move_piece(const Move move) {
	return move & 7U;
}

/**
 * @brief Get the source square of a move
 * @param move Move
 * @return Source square
 */
static inline Square move_from(const Move move) {
	return (move >> 3U) & 63U;
}

/**
 * @brief Get the destination square of a move
 * @param move Move
 * @return Destination square
 */
static inline Square move_to(const Move move) {
	return (move >> 9U) & 63U;
}

/**
 * @brief Get the promoted piece of a move
 * @param move Move
 * @return Promoted piece
 */
static inline Piece move_promotion(const Move move) {
	return move >> 15U;
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
	return strcmp(move_to_string(*(const Move*)a, string_a), move_to_string(*(const Move*)b, string_b));
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
static const char *parse_next(const char *string) {
	while (isspace(*string)) ++string;
	return (const char*) string;
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
static const char* parse_word(const char *string, char *word, size_t n)
{
	string = parse_next(string);
	while(*string && !isspace(*string) && --n) *word++ = *string++;
	*word = '\0';
	return string;
}

/**
 * @brief Parse a move.
 *
 * @param string String to parse
 * @return The remaining of the input string.
 */
static const char *parse_move(const char *string, const Board *board, Move *move) {
	char word[8];
	const char *w = word;
	Square from, to;
	Piece promotion;

	// get next word
	string = parse_word(string, word, 8);
	if (*word == '\0') return NULL;

	// convert it to move
	if (square_parse(&w, &from) && square_parse(&w, &to)) {
		promotion = piece_from_char(*w);
		if (promotion == PIECE_SIZE) promotion = PAWN;
	} else {
		parse_error(word, "move", w);
		return NULL;
	}

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
#if HAS_PEXT == true
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
static inline Bitboard pawn_attack(const Square x, const Color c, const Bitboard target)  {
	return MASK[x].pawn_attack[c] & target;
}

/**
 * @brief Generate knight attack
 * @param x Knight's square
 * @param target Bitboard of target squares
 * @return Bitboard of knight attack
 */
static inline Bitboard knight_attack(const Square x, const Bitboard target)  {
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
	return ATTACK_BISHOP[x].attack[magic_index(pieces, ATTACK_BISHOP + x)] & target;
}

/**
 * @brief Generate rook attack
 * @param pieces Bitboard of pieces
 * @param x Rook's square
 * @param target Bitboard of target squares
 * @return Bitboard of rook attack
 */
static inline Bitboard rook_attack(const Bitboard pieces, const Square x, const Bitboard target) {
	return ATTACK_ROOK[x].attack[magic_index(pieces, ATTACK_ROOK + x)] & target;
}

/**
 * @brief Generate Queen attack
 * @param pieces Bitboard of pieces
 * @param x Queen's square
 * @param target Bitboard of target squares
 * @return Bitboard of rook attack
 */
static inline Bitboard queen_attack(const Bitboard pieces, const Square x, const Bitboard target)  {
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
static inline Piece board_get_piece_bb(const Board *board, const Bitboard b)  {
	Piece p;

	foreach_piece(p) {
		if (b & board->piece[p]) return p;
	}
	return PIECE_SIZE;
}

/**
 * @brief Get a piece located at a specified square
 * @param board The board
 * @param x square
 * @return A Piece
 */
static inline Piece board_get_piece(const Board *board, const Square x)  {
	return board_get_piece_bb(board, square_to_bit(x));
}

/**
 * @brief Get the color of a piece located at a specified square
 * @param board The board
 * @param x square
 * @return A Color.
 */
static inline Color board_get_color(const Board *board, const Square x)  {
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
static inline CPiece board_get_cpiece(const Board *board, const Square x)  {
	Piece p = board_get_piece(board, x);
	Color c = board_get_color(board, x);
	return c == COLOR_SIZE ? EMPTY : cpiece_make(p, c);
}

/**
 * @brief Initialize key to a random value
 * @param key Key to initialize
 * @param r Random number generator
 */
static inline void key_init(Key *key, Random *r)  {
	#ifdef USE_INT128
		key->code = random_get_u128(r);
	#else
		key->code = random_get_u64(r);
	#endif
	key->index = random_get_u64(r);
}

/**
 * @brief Xor a key with another one
 * @param key Key to xor
 * @param k Key to xor with
 */
static inline void key_xor(Key *key, const Key *k)  {
	key->code ^= k->code;
	key->index ^= k->index;
}

/**
 * @brief Set a key from a board
 * @param key Key to set
 * @param board Board to set key from
 */
static void key_set(Key *key, const Board *board) {
	Bitboard castling = board->castling;
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

	while (castling) key_xor(key, &KEY_CASTLING[square_next(&castling)]);
	key_xor(key, &KEY_ENPASSANT[board->enpassant]);
}

/**
 * @brief Update the key after a move is made
 * @param key Key to update
 * @param board Board to update key from
 * @param move Move to update key with
 */
static void key_update(Key *key, const Board *board, const Move move)  {
	const Square from = move_from(move);
	const Square to = move_to(move);
	const Color c = board->player;
	const Color o = opponent(c);
	const Bitboard occupied = board->color[WHITE] | board->color[BLACK];
	const Bitboard b_to = square_to_bit(to);
	Piece p = move_piece(move);
	CPiece cp = cpiece_make(p, c);
	Square enpassant = SQUARE_NONE;

	*key = board->key;

	// move the piece
	key_xor(key, &KEY_SQUARE[from][cp]);
	key_xor(key, &KEY_SQUARE[to][cp]);
	// capture
	if (occupied & b_to) {
		const CPiece victim = cpiece_make(board_get_piece_bb(board, b_to), o);
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
	Bitboard castling = (board->castling & ~MASK_CASTLING[from] & ~MASK_CASTLING[to]) ^ board->castling;
	while (castling) key_xor(key, &KEY_CASTLING[square_next(&castling)]);
	key_xor(key, &KEY_ENPASSANT[board->enpassant]);
	key_xor(key, &KEY_ENPASSANT[enpassant]);
	// side to move change
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
			b = 1ULL << square(f, r);
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
	Attack *bishop, *rook;
	Random random[1];
	CPiece p;
	#if HAS_PEXT == false
		static const Bitboard rook_magic[BOARD_SIZE] = {
			0x808000645080c000U, 0x208020001480c000U, 0x4180100160008048U, 0x8180100018001680U, 0x4200082010040201U, 0x8300220400010008U, 0x3100120000890004U, 0x4080004500012180U,
			0x01548000a1804008U, 0x4881004005208900U, 0x0480802000801008U, 0x02e8808010008800U, 0x08cd804800240080U, 0x8a058002008c0080U, 0x0514000c480a1001U, 0x0101000282004d00U,
			0x2048848000204000U, 0x3020088020804000U, 0x4806020020841240U, 0x6080420008102202U, 0x0010050011000800U, 0xac00808004000200U, 0x0000010100020004U, 0x1500020004004581U,
			0x0004c00180052080U, 0x0220028480254000U, 0x2101200580100080U, 0x0407201200084200U, 0x0018004900100500U, 0x100200020008e410U, 0x0081020400100811U, 0x0000012200024494U,
			0x8006c002808006a5U, 0x0004201000404000U, 0x0005402202001180U, 0x0000081001002100U, 0x0000100801000500U, 0x4000020080800400U, 0x4005050214001008U, 0x810100118b000042U,
			0x0d01020040820020U, 0x000140a010014000U, 0x0420001500210040U, 0x0054210010030009U, 0x0004000408008080U, 0x0002000400090100U, 0x0000840200010100U, 0x0000233442820004U,
			0x800a42002b008200U, 0x0240200040009080U, 0x0242001020408200U, 0x4000801000480480U, 0x2288008044000880U, 0x000a800400020180U, 0x0030011002880c00U, 0x0041110880440200U,
			0x0002001100442082U, 0x01a0104002208101U, 0x080882014010200aU, 0x0000100100600409U, 0x0002011048204402U, 0x0012000168041002U, 0x080100008a000421U, 0x0240022044031182U
		};

		static const Bitboard bishop_magic[BOARD_SIZE] = {
			0x88b030028800d040U, 0x018242044c008010U, 0x0010008200440000U, 0x4311040888800a00U, 0x001910400000410aU, 0x2444240440000000U, 0x0cd2080108090008U, 0x2048242410041004U,
			0x8884441064080180U, 0x00042131420a0240U, 0x0028882800408400U, 0x204384040b820200U, 0x0402040420800020U, 0x0000020910282304U, 0x0096004b10082200U, 0x4000a44218410802U,
			0x0808034002081241U, 0x00101805210e1408U, 0x9020400208010220U, 0x000820050c010044U, 0x0024005480a00000U, 0x0000200200900890U, 0x808040049c100808U, 0x9020202200820802U,
			0x0410282124200400U, 0x0090106008010110U, 0x8001100501004201U, 0x0104080004030c10U, 0x0080840040802008U, 0x2008008102406000U, 0x2000888004040460U, 0x00d0421242410410U,
			0x8410100401280800U, 0x0801012000108428U, 0x0000402080300b04U, 0x0c20020080480080U, 0x40100e0201502008U, 0x4014208200448800U, 0x4050020607084501U, 0x1002820180020288U,
			0x800610040540a0c0U, 0x0301009014081004U, 0x2200610040502800U, 0x0300442011002800U, 0x0001022009002208U, 0x0110011000202100U, 0x1464082204080240U, 0x0021310205800200U,
			0x0814020210040109U, 0xc102008208c200a0U, 0xc100702128080000U, 0x0001044205040000U, 0x0001041002020000U, 0x4200040408021000U, 0x004004040c494000U, 0x2010108900408080U,
			0x0000820801040284U, 0x0800004118111000U, 0x0203040201108800U, 0x2504040804208803U, 0x0228000908030400U, 0x0010402082020200U, 0x00a0402208010100U, 0x30c0214202044104U
		};
	#endif
    static const int pawn_dir[2][2] = {{-1, 1}, {1, 1}};
    static const int knight_dir[8][2] = {{-2, -1}, {-2, 1}, {-1, -2}, {-1, 2}, {1, -2}, {1, 2}, {2, -1}, {2, 1}};
    static const int bishop_dir[4][2] = {{-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
    static const int rook_dir[4][2] = {{-1, 0}, {0, -1}, {0, 1}, {1, 0}};
    static const int king_dir[8][2] = {{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};

	// MASK initialisations
	ATTACK_BISHOP->attack = malloc(sizeof (Bitboard) * 0x1480);
	if (ATTACK_BISHOP->attack == NULL) memory_error(__func__);
	ATTACK_ROOK->attack = malloc(sizeof (Bitboard) * 0x19000);
	if (ATTACK_ROOK->attack == NULL) memory_error(__func__);
	for (x = 0; x < 64; ++x) {
		f = file(x);
		r = rank(x);
		mask = MASK + x;
		bishop = ATTACK_BISHOP + x;
		rook = ATTACK_ROOK + x;

		for (y = 0; y < 64; ++y) d[x][y] = 0;
		// directions & between
		for (i = 0; i < 8; ++i) {
			for (j = 1; j < 8; ++j) {
				y = square_safe(f + king_dir[i][0] * j, r + king_dir[i][1] * j);
				if (y != BOARD_OUT) {
					d[x][y] = king_dir[i][0] + 8 * king_dir[i][1];
					mask->direction[y] = abs(d[x][y]);
					for (z = x + d[x][y]; z != y; z += d[x][y]) mask->between[y] |= square_to_bit(z);
					mask->between_in[y] = mask->between[y] | square_to_bit(y);
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
		mask->bishop = mask->diagonal | mask->antidiagonal;
		bishop->mask = mask->bishop & inside;
		#if HAS_PEXT == false
			bishop->shift = stdc_count_zeros_ull(bishop->mask);
			bishop->magic = bishop_magic[x];
		#endif
		if (x) bishop->attack = bishop[-1].attack + (1ULL << stdc_count_ones_ull(bishop[-1].mask));
		o = 0; do {
			bishop->attack[magic_index(o, bishop)] = compute_slider_attack(x, o, bishop_dir);
			o = (o - bishop->mask) & bishop->mask;
		} while (o);

		// magic rook
		mask->rook = mask->rank | mask->file;
		rook->mask = mask->rook & inside;
		#if HAS_PEXT == false
			rook->shift = stdc_count_zeros_ull(rook->mask);
			rook->magic = rook_magic[x];
		#endif
		if (x) rook->attack = rook[-1].attack + (1ULL << stdc_count_ones_ull(rook[-1].mask));
		o = 0; do {
			rook->attack[magic_index(o, rook)] = compute_slider_attack(x, o, rook_dir);
			o = (o - rook->mask) & rook->mask;
		} while (o);

		// queen
		mask->queen = mask->bishop | mask->rook;
	}

	// Hash key
	random_seed(random, seed);

	foreach_color (c) key_init(KEY_PLAYER + c, random);

	KEY_PLAY = KEY_PLAYER[WHITE];
	key_xor(&KEY_PLAY, &KEY_PLAYER[BLACK]);

	foreach_square (x)
	foreach_cpiece (p)
		key_init(&KEY_SQUARE[x][p], random);

	key_init(KEY_CASTLING + C1, random);
	key_init(KEY_CASTLING + G1, random);
	key_init(KEY_CASTLING + C8, random);
	key_init(KEY_CASTLING + G8, random);

	foreach_square (x) key_init(KEY_ENPASSANT + x, random);
	key_init(KEY_ENPASSANT + SQUARE_NONE, random);
}

/**
 * @brief Generate pinned pieces for the opponent side.
 * @param board Board to generate and pins for
 * @return pins pieces for the opponent side
 */
static Bitboard get_opponent_pins(const Board *board) {
	const Color o = board->player;
	const Color c = opponent(o);
	const Square k = board->x_king[c];
	const Bitboard bq = (board->piece[BISHOP] + board->piece[QUEEN]) & board->color[o];
	const Bitboard rq = (board->piece[ROOK] + board->piece[QUEEN]) & board->color[o];
	const Bitboard pieces = board->color[WHITE] + board->color[BLACK];
	Bitboard sliders, between, pins = 0;
	Square x;

	// bishop or queen: all square reachable from the king square.
	sliders = MASK[k].bishop & bq;
	while (sliders) {
		x = square_next(&sliders);
		between = MASK[k].between[x] & pieces;
		if (stdc_has_single_bit(between)) pins |= between;
	}

	// rook or queen: all square reachable from the king square.
	sliders = MASK[k].rook & rq;
	while (sliders) {
		x = square_next(&sliders);
		between = MASK[k].between[x] & pieces;
		if (stdc_has_single_bit(between)) pins |= between;
	}

	return pins & board->color[c];
}

/**
 * @brief Generate checker & pinned pieces for the current player.
 * @param board Board to generate checkers and pins pieces for
 */
static void generate_checkers(Board *board) {
	const Color c = board->player;
	const Color o = opponent(c);
	const Square k = board->x_king[c];
	const Bitboard bq = (board->piece[BISHOP] + board->piece[QUEEN]) & board->color[o];
	const Bitboard rq = (board->piece[ROOK] + board->piece[QUEEN]) & board->color[o];
	const Bitboard pieces = board->color[WHITE] + board->color[BLACK];
	Bitboard sliders, between, pins = 0, checkers = 0;
	Square x;

	// bishop or queen
	sliders = MASK[k].bishop & bq;
	while (sliders) {
		x = square_next(&sliders);
		between = MASK[k].between[x] & pieces;
		if (between == 0) checkers |= square_to_bit(x);
		else if (stdc_has_single_bit(between)) pins |= between;
	}

	// rook or queen
	sliders = MASK[k].rook & rq;
	while (sliders) {
		x = square_next(&sliders);
		between = MASK[k].between[x] & pieces;
		if (between == 0) checkers |= square_to_bit(x);
		else if (stdc_has_single_bit(between)) pins |= between;
	}

	// other pieces
	checkers |= knight_attack(k, board->piece[KNIGHT]);
	checkers |= pawn_attack(k, c, board->piece[PAWN]);

	board->pins = pins & board->color[c];
	board->checkers = checkers & board->color[o];
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
	board->piece[PAWN] =   0x00FF00000000FF00ULL;
	board->piece[KNIGHT] = 0x4200000000000042ULL;
	board->piece[BISHOP] = 0x2400000000000024ULL;
	board->piece[ROOK] =   0x8100000000000081ULL;
	board->piece[QUEEN] =  0x0800000000000008ULL;
	board->piece[KING] =   0x1000000000000010ULL;
	board->color[WHITE] =  0x000000000000FFFFULL;
	board->color[BLACK] =  0xFFFF000000000000ULL;
	board->pins = board->checkers = 0;
	board->castling = B_C1 | B_G1 | B_C8 | B_G8;
	board->enpassant = SQUARE_NONE; // illegal enpassant square
	board->x_king[WHITE] = E1;
	board->x_king[BLACK] = E8;
	board->player = WHITE;

	key_set(&board->key, board);
}

/**
 * @brief parse a FEN board description
 * @param board Board to parse FEN string into
 * @param string FEN string to parse
 */
static void board_set(Board *board, const char *string) {
	const char *s = string;
	Square x;
	int r, f;
	CPiece p;

	if (!s || *s == '\0') return;
	board_clear(board);

	// board
	r = 7, f = 0;
	do {
		if (*s == '/') {
			if (r <= 0) parse_error(string, s, "FEN: rank overflow");
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
		if ((rooks & B_H1) == 0) board->castling &= ~B_G1;
		if ((rooks & B_A1) == 0) board->castling &= ~B_C1;
	} else board->castling &= ~(B_G1 | B_C1);
	if (board->x_king[BLACK] == E8) {
		uint64_t rooks = board->color[BLACK] & board->piece[ROOK];
		if ((rooks & B_H8) == 0) board->castling &= ~B_G8;
		if ((rooks & B_A8) == 0) board->castling &= ~B_C8;
	} else board->castling &= ~(B_G8 | B_C8);

	// en passant
	x = SQUARE_NONE;
	s = parse_next(s);
	if (*s != '-' && !square_parse(&s, &x)) parse_error(string, s, "FEN: bad enpassant square");
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
static void board_copymake(const Board *board, const Move move, Board *next) {
	const Square from = move_from(move);
	const Square to = move_to(move);
	const Square enpassant = board->enpassant;
	const Bitboard b_from = square_to_bit(from);
	const Bitboard b_to = square_to_bit(to);
	const Bitboard occupied = board->color[WHITE] | board->color[BLACK];
	const Color c = board->player;
	const Color o = opponent(c);
	const Piece p = move_piece(move);
	Bitboard *piece = next->piece;
	Bitboard *color = next->color;
	Square x;
	Bitboard b;

	*next = *board;

	// update chess board informations
	next->enpassant = SQUARE_NONE;
	next->castling &= ~MASK_CASTLING[from] & ~MASK_CASTLING[to];
	// move the piece
	piece[p] ^= b_from;
	piece[p] ^= b_to;
	color[c] ^= b_from | b_to;

	// capture
	if (occupied & b_to) {
		const Piece victim = board_get_piece_bb(board, b_to);
		piece[victim] ^= b_to;
		color[o] ^= b_to;
	}

	// special pawn move
	if (p == PAWN) {
		const Piece promotion = move_promotion(move);
		if (promotion) {
			piece[PAWN] ^= b_to;
			piece[promotion] ^= b_to;
		} else if (enpassant == to) {
			x = square(file(to), rank(from));
			b = square_to_bit(x);
			piece[PAWN] ^= b;
			color[o] ^= b;
		} else if (abs(to - from) == 16 && (MASK[to].enpassant & (next->color[o] & next->piece[PAWN]))) {
			next->enpassant = (from + to) / 2;
		}

	// king move
	} else if (p == KING) {
		next->x_king[c] = to;
		if (to == from + 2) {
			const uint64_t b_rook = (square_to_bit(from + 3) ^ square_to_bit(from + 1));
			piece[ROOK] ^= b_rook;
			color[c] ^= b_rook;
		} else if (to == from - 2) {
			const uint64_t b_rook = (square_to_bit(from - 4) ^ square_to_bit(from - 1));
			piece[ROOK] ^= b_rook;
			color[c] ^= b_rook;
		}
	}

	next->player = o;
	generate_checkers(next);
}

/**
 * Print the board.
 * @param board Board to printC & (board->piece[ROOK] | board->piece[QUEEN]
 * @param output File to print board to
 */
static void board_print(const Board *board, FILE *output) {
	Square x;
	int f, r;
	const char p[] = ".PpNnBbRrQqKk#";
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
	fprintf(output, "%c, ", "wb"[board->player]);
	if (board->castling & B_G1) fputc('K', output);
	if (board->castling & B_C1) fputc('Q', output);
	if (board->castling & B_G8) fputc('k', output);
	if (board->castling & B_C8) fputc('q', output);
	if (board->enpassant != SQUARE_NONE) fprintf(output, ", ep: %c%c", file(ep) + 'a', rank(ep) + '1');
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
static bool board_is_square_attacked(const Board *board, const Square x, const Color c, const Bitboard pieces) {
	const Bitboard C = board->color[c];
	Bitboard sliders;

	return ((sliders = C & (board->piece[BISHOP] | board->piece[QUEEN])) && bishop_attack(pieces, x, sliders))
	    || ((sliders = C & (board->piece[ROOK] | board->piece[QUEEN])) && rook_attack(pieces, x, sliders))
	    ||  knight_attack(x, C & board->piece[KNIGHT])
	    ||  pawn_attack(x, opponent(c), C & board->piece[PAWN])
	    ||  king_attack(x, C & board->piece[KING]);
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
static inline Move* push_moves(Move *move, const Map *map) {
	Square to;
	uint64_t attack = map->to;
	const Piece piece = map->piece;
	const Square from = map->from;

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
static inline Move* push_pawns(Move *move, Bitboard attack, const int dir) {
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
 * @brief push map
 */
static inline Map* map_push(Map *map, const Bitboard attack, const Square from, const Piece piece) {
	if (attack) {
		map->to =attack;
		map->from = from;
		map->piece = piece;
		++map;
	}
	return map;
}

/**
 * @brief generate all moves' maps.
 * @param board Board state
 * @param map Array of maps to fill
 * @param pawn_map Pawn map to fill
 * @param c Color of the player to generate moves for
 * @return Number of moves generated
 */
static int generate_maps(const Board *board, Map *map, PawnMap *pawn_map, const Color c) {
	const Color o = opponent(c);
	const Square k = board->x_king[c];
	const Bitboard occupied = board->color[WHITE] + board->color[BLACK];
	const Bitboard occupied_no_king = occupied ^ square_to_bit(k);
	const Bitboard pins = c == board->player ? board->pins : get_opponent_pins(board);
	const Bitboard unpins = board->color[c] & ~pins;
	const Bitboard checkers = c == board->player ? board->checkers : 0;
	const Square enpassant = c == board->player ? board->enpassant : SQUARE_NONE;
	const int pawn_left = PUSH[c] - 1;
	const int pawn_right = PUSH[c] + 1;
	const int pawn_push = PUSH[c];
	const int *dir = MASK[k].direction;
	Bitboard target, piece, attack, k_attack = 0;
	Bitboard empty = ~occupied;
	Bitboard enemy = board->color[o];
	Square from, to, ep, x_checker = SQUARE_NONE;
	const Map *start = map;
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
		target = enemy | empty;

	// not in check: castling & pins pieces moves
	} else {
		target = enemy | empty;

		// castling
		if ((board->castling & CAN_CASTLE_KINGSIDE[c])
			&& (occupied & MASK[k].between[k + 3]) == 0
			&& !board_is_square_attacked(board, k + 1, o, occupied)
			&& !board_is_square_attacked(board, k + 2, o, occupied)) {
				k_attack |= square_to_bit(k + 2);
		}
		if ((board->castling & CAN_CASTLE_QUEENSIDE[c])
			&& (occupied & MASK[k].between[k - 4]) == 0
			&& !board_is_square_attacked(board, k - 1, o, occupied)
			&& !board_is_square_attacked(board, k - 2, o, occupied)) {
				k_attack |= square_to_bit(k - 2);
		}

		// pins pieces
		// pawn (pins)
		piece = board->piece[PAWN] & pins;
		while (piece) {
			from = square_next(&piece);
			d = dir[from];
			if (d == abs(pawn_left) && (square_to_bit(to = from + pawn_left) & pawn_attack(from, c, enemy))) pawn_map->left_capture = square_to_bit(to);
			else if (d == abs(pawn_right) && (square_to_bit(to = from + pawn_right) & pawn_attack(from, c, enemy))) pawn_map->right_capture = square_to_bit(to);
			if (d == abs(pawn_push) && (square_to_bit(to = from + pawn_push) & empty)) {
				pawn_map->push |= square_to_bit(to);
				if (is_on_second_rank(from, c) && (square_to_bit(to += pawn_push) & empty)) pawn_map->double_push = square_to_bit(to);
			}
		}
		// bishop (pins)
		piece = board->piece[BISHOP] & pins;
		while (piece) {
			from = square_next(&piece);
			d = dir[from];
			if (d == 9) map = map_push(map, bishop_attack(occupied, from, target & MASK[from].diagonal), from, BISHOP);
			else if (d == 7) map = map_push(map, bishop_attack(occupied, from, target & MASK[from].antidiagonal), from, BISHOP);
		}

		// rook (pins)
		piece = board->piece[ROOK] & pins;
		while (piece) {
			from = square_next(&piece);
			d = dir[from];
			if (d == 1) map = map_push(map, rook_attack(occupied, from, target & MASK[from].rank), from, ROOK);
			else if (d == 8) map = map_push(map, rook_attack(occupied, from, target & MASK[from].file), from, ROOK);
		}

		// queen (pins)
		piece = board->piece[QUEEN] & pins;
		while (piece) {
			from = square_next(&piece);
			d = dir[from];
			if (d == 1) map = map_push(map, rook_attack(occupied, from, target & MASK[from].rank), from, QUEEN);
			else if (d == 8) map = map_push(map, rook_attack(occupied, from, target & MASK[from].file), from, QUEEN);
			else if (d == 9) map = map_push(map, bishop_attack(occupied, from, target & MASK[from].diagonal), from, QUEEN);
			else if (d == 7) map = map_push(map, bishop_attack(occupied, from, target & MASK[from].antidiagonal), from, QUEEN);
		}

	}

	// enpassant capture
	if (enpassant != SQUARE_NONE && (!checkers || x_checker == enpassant - pawn_push)) {
		const Bitboard bq = (board->piece[BISHOP] | board->piece[QUEEN]) & board->color[o];
		const Bitboard rq = (board->piece[ROOK] | board->piece[QUEEN]) & board->color[o];
		to = board->enpassant;
		ep = to - pawn_push;
		if (file(to) > 0 && (board->piece[PAWN] & board->color[c] & (piece = square_to_bit(ep - 1)))) {
			attack = occupied ^ piece  ^ square_to_bit(ep) ^ square_to_bit(to);
			if (!bishop_attack(attack, k, bq) && !rook_attack(attack, k, rq)) pawn_map->enpassant |= piece;
		}
		if (file(to) < 7 && (board->piece[PAWN] & board->color[c] & (piece = square_to_bit(ep + 1)))) {
			attack = occupied ^ piece  ^ square_to_bit(ep) ^ square_to_bit(to);
			if (!bishop_attack(attack, k, bq) && !rook_attack(attack, k, rq)) pawn_map->enpassant |= piece;
		}
	}

	// pawn
	piece = board->piece[PAWN] & unpins;
	if (c) {
		pawn_map->left_capture |= ((piece & ~COLUMN[0]) >> 9) & enemy;
		pawn_map->right_capture |= ((piece & ~COLUMN[7]) >> 7) & enemy;
		pawn_map->push |= (piece >> 8) & empty;
		pawn_map->double_push |= ((((piece & RANK[6]) >> 8) & ~occupied) >> 8) & empty;
	} else {
		pawn_map->left_capture |= ((piece & ~COLUMN[0]) << 7) & enemy;
		pawn_map->right_capture |= ((piece & ~COLUMN[7]) << 9) & enemy;
		pawn_map->push |= (piece << 8) & empty;
		pawn_map->double_push |= ((((piece & RANK[1]) << 8) & ~occupied) << 8) & empty;
	}

	// knight
	piece = board->piece[KNIGHT] & unpins;
	while (piece) {
		from = square_next(&piece);
		map = map_push(map, knight_attack(from, target), from, KNIGHT);
	}

	// bishop
	piece = board->piece[BISHOP] & unpins;
	while (piece) {
		from = square_next(&piece);
		map = map_push(map, bishop_attack(occupied, from, target), from, BISHOP);
	}

	// rook
	piece = board->piece[ROOK] & unpins;
	while (piece) {
		from = square_next(&piece);
		map = map_push(map, rook_attack(occupied, from, target), from, ROOK);
	}

	// queen
	piece = board->piece[QUEEN] & unpins;
	while (piece) {
		from = square_next(&piece);
		map = map_push(map, queen_attack(occupied, from, target), from, QUEEN);
	}

	// king
	target = ~board->color[c];
	attack = king_attack(k, target);
	// TODO check with a bitboard of attacked squares ? attack = king_attack(k, target) & ~attacked;
	while (attack) {
		to = square_next(&attack);
		if (!board_is_square_attacked(board, to, o, occupied_no_king)) k_attack |= square_to_bit(to);
	}
	map = map_push(map, k_attack, k, KING);

	return map - start;
}

static inline void maparray_generate(MapArray *map_array, const Board *board, const Color c) {
	memset(&map_array->pawn_map, 0, sizeof(PawnMap));
	map_array->n = generate_maps(board, map_array->map, &map_array->pawn_map, c);
}

static inline int maparray_to_count(const MapArray *map_array, const Color c) {
	const Bitboard promotion = PROMOTION_RANK[c];
	const Map *map = map_array->map;
	const PawnMap *pawn_map = &map_array->pawn_map;
	const int n = map_array->n;
	int count = 0;

	for (int i = 0; i < n; ++i) count += stdc_count_ones_ull(map[i].to);
	count += 4 * stdc_count_ones_ull(pawn_map->left_capture & promotion) + stdc_count_ones_ull(pawn_map->left_capture & ~promotion);
	count += 4 * stdc_count_ones_ull(pawn_map->right_capture & promotion) + stdc_count_ones_ull(pawn_map->right_capture & ~promotion);
	count += 4 * stdc_count_ones_ull(pawn_map->push & promotion) + stdc_count_ones_ull(pawn_map->push & ~promotion);
	count += stdc_count_ones_ull(pawn_map->enpassant);
	count += stdc_count_ones_ull(pawn_map->double_push);

	return count;
}

/**
 * @brief Generate all legal moves or captures
 * @param ma Move array to fill
 * @param board Board state
 */
static inline void maparray_to_movearray(const MapArray *map_array, MoveArray *ma, const Color c, const Square enpassant) {
	const Bitboard promotion = PROMOTION_RANK[c];
	const Map *map = map_array->map;
	const PawnMap *pawn_map = &map_array->pawn_map;
	const int n = map_array->n;
	const int pawn_left = PUSH[c] - 1;
	const int pawn_right = PUSH[c] + 1;
	const int pawn_push = PUSH[c];
	Move *move = ma->move;

	for (int i = 0; i < n; ++i) move = push_moves(move, map + i);

	move = push_promotions(move, pawn_map->left_capture  &  promotion, pawn_left);
	move = push_pawns     (move, pawn_map->left_capture  & ~promotion, pawn_left);
	move = push_promotions(move, pawn_map->right_capture &  promotion, pawn_right);
	move = push_pawns     (move, pawn_map->right_capture & ~promotion, pawn_right);
	move = push_promotions(move, pawn_map->push          &  promotion, pawn_push);
	move = push_pawns     (move, pawn_map->push          & ~promotion, pawn_push);
	move = push_pawns     (move, pawn_map->double_push               , pawn_push * 2);
	Bitboard from = pawn_map->enpassant;
	while (from) move = push_move(move, PAWN, square_next(&from), enpassant);
	*move = 0; // sentinel

	ma->n = move - ma->move;
	ma->i = 0;
}

/**
 * @brief count all legal moves
 * @param board Board state
 * @return Number of moves generated
 */
static inline int count_moves(const Board *board) {
	MapArray map_array;

	maparray_generate(&map_array, board, board->player);
	return maparray_to_count(&map_array, board->player);
}

/**
 * @brief Generate all legal moves or captures
 * @param ma Move array to fill
 * @param board Board state
 */
static inline void movearray_generate(MoveArray *move_array, const Board *board) {
	MapArray map_array;

	maparray_generate(&map_array, board, board->player);
	maparray_to_movearray(&map_array, move_array, board->player, board->enpassant);
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
	size_t i;
	memset(hash_table->hash, 0, (hash_table->mask + BUCKET_SIZE + 1) * sizeof(Hash));
	for (i = 0; i <= hash_table->mask + BUCKET_SIZE - 3; i += 4) {
		spin_init(hash_table->spin + i);
		spin_init(hash_table->spin + i + 1);
		spin_init(hash_table->spin + i + 2);
		spin_init(hash_table->spin + i + 3);
	}
	for (; i <= hash_table->mask + BUCKET_SIZE; ++i) {
		spin_init(hash_table->spin + i);
	}
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

	HASH_STATS(atomic_fetch_add(&probe_tries, 1);)

	for (int i = 0; i < BUCKET_SIZE; ++i) {
		if (hash[i].code == key->code && (hash[i].data & 0x3f) == depth) {
			spin_lock(spin + i);
			if (hash[i].code == key->code && (hash[i].data & 0x3f) == depth) {
				uint64_t count = hash[i].data >> 6;
				HASH_STATS(atomic_fetch_add(&probe_successes, 1);)
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

	HASH_STATS(atomic_fetch_add(&stores, 1);)

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
 * @brief Run a perft search in parallel.
 * @param task Task
 */
static void task_run(Task *task) {
	uint64_t count;
	Node *node = task->node;
	const Board *board = &task->board;
	const Key *key = &board->key;
	const uint32_t depth  = task->depth;

	// do perft
	count = perft(board, depth);
	if (hash_table) hash_store(hash_table, key, depth, count);

	// store the result & release the thread
	spin_lock(&node->spin);
		SPLIT_STATS(++task->count;)
		node->count += count;
		node->n_split--;
	spin_unlock(&node->spin);

	// put the task back to the pool
	task->run = false;
	taskpool_put_idle_task(task_pool, task);
}

/**
 * Task loop.
 * Loop waiting for a task to run.
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
 * @param bulk do bulk counting
 */
static void task_init(Task *task, const int id) {
	task->run = false;
	task->loop = false;
	task->id = id;
	task->depth = 0;
	SPLIT_STATS(task->count = 0;)
	mtx_init(&task->mutex, mtx_plain);
	cnd_init(&task->condition);
	thrd_create(&task->thread, task_loop, task);
}

/**
 * Initialize a pool of tasks
 * @param task_pool Task pool
 * @param n_workers Number of parallel threads (total threads - 1)
 * @param hash_table Hash table
 * @param bulk do bulk counting
 */
static TaskPool* taskpool_create(const int n_workers) {
	static const struct timespec ONE_MS = {.tv_sec = 0, .tv_nsec = 1000};
	task_pool = malloc(sizeof(TaskPool));
	if (task_pool == NULL) memory_error(__func__);
	task_pool->n_tasks = task_pool->n_idle = n_workers;
	task_pool->tasks = n_workers ? malloc(n_workers * sizeof (Task)) : NULL;
	if (n_workers && task_pool->tasks == NULL) memory_error(__func__);
	task_pool->idle_tasks = n_workers ? malloc(n_workers * sizeof (Task*)) : NULL;
	if (n_workers && task_pool->idle_tasks == NULL) memory_error(__func__);
	spin_init(&task_pool->spin);
	for (int i = 0 ; i < n_workers; ++i) {
		Task *task = task_pool->tasks + i;
		task_init(task, i);
		task_pool->idle_tasks[n_workers - i - 1] = task;
	}
	// hack: wait for all tasks to be initialized...
	if (n_workers) {
		while(task_pool->idle_tasks[0]->loop == false) thrd_sleep(&ONE_MS, NULL);
	}
	return task_pool;
}

/**
 * @brief Free ressources of a pool of tasks
 * @param task_pool Task pool
 */
static void taskpool_destroy(TaskPool *task_pool) {
	for (int i = 0 ; i < task_pool->n_tasks; ++i) {
		task_free(task_pool->tasks + i);
	}
	free(task_pool->tasks);
	free(task_pool->idle_tasks);
	free(task_pool);
}

/**
 * @brief Initialize a node
 * @param node Node to initialize
 */
static inline void node_init(Node *node) {
	node->count = 0;
	node->n_split = 0;
	spin_init(&node->spin);
}

/**
 * @brief Split the search
 *
 * This function looks if there is an idle task available in the task pool.
 * If so, it assigns the task to the node and returns true. The task will
 * then perform a perft with the given board and depth.
 *
 * @param node Node to split
 * @param board Board to split
 * @param task_pool Task pool
 * @param depth Depth of the search
 * @param moves_todo Number of moves remaining
 * @return True if the search was split, false otherwise
 */
static bool node_split(Node *node, const Board *board, const int depth, const int moves_todo) {
	if (depth < MIN_SPLIT_DEPTH || moves_todo < MIN_SPLIT_REMAINING_MOVES) return false;
	Task *task = NULL;

	SPLIT_STATS(atomic_fetch_add(&split_tries, 1);)

	// look for an available task in the task_pool
	if (task_pool->n_idle > 0) {
		spin_lock(&task_pool->spin);
			if (task_pool->n_idle > 0) {
				spin_lock(&node->spin);
					if (node->n_split < MAX_SPLIT) {
						task = task_pool->idle_tasks[--task_pool->n_idle];
						node->n_split++;
					}
				spin_unlock(&node->spin);
			}
		spin_unlock(&task_pool->spin);
	}

	if (task == NULL) return false;

	SPLIT_STATS(atomic_fetch_add(&split_successes, 1);)

	mtx_lock(&task->mutex);
		task->run = true;
		task->board = *board;
		task->node = node;
		task->depth = depth;
		cnd_signal(&task->condition);
	mtx_unlock(&task->mutex);

	return true;
}

/**
 * @brief Wait for all child tasks to terminate
 * @param node Node to wait for
 * @return Total count of nodes
 */
static inline uint64_t node_wait(const Node *node) {
	while (node->n_split > 0) thrd_yield();

	return node->count;
}

/**
 * @brief Perft with nullmove pruning
 *
 * Performs a perft search with nullmove pruning. The idea is to precompute the move count of the next player playing at the last ply,
 * and then to search the moves of the current player that do not affect the next player's moves. The resulting perft
 * the product of the number of harmless moves of the current player and the perft of the next player. For the other moves,
 * that affect the next player's moves, the perft is computed recursively.
 *
 * @param board Board to perft
 * @return Total count of nodes
 */
static int perft_nullmove(const Board *board) {
	const Color player = board->player; // Current player
	const Color enemy = opponent(player); // Next player
	const Square k = board->x_king[enemy]; // Enemy's king square
	const Bitboard empty = ~(board->color[BLACK] | board->color[WHITE]); // Bitboard mask of empty squares
	const Bitboard promotion = PROMOTION_RANK[player]; // Bitboard mask of promotion squares
	const Bitboard enemy_pawn = board->piece[PAWN] & board->color[enemy]; // Bitboard mask of enemy pawns
	const Bitboard pawn = board->piece[PAWN] & board->color[player]; // Bitboard mask of player's pawns
	const Bitboard knight = board->piece[KNIGHT] & board->color[player]; // Bitboard mask of player's knights
	const Bitboard bishop_queen = (board->piece[BISHOP] | board->piece[QUEEN]) & board->color[player]; // Bitboard mask of player's bishops/queens
	const Bitboard rook_queen = (board->piece[ROOK] | board->piece[QUEEN]) & board->color[player]; // Bitboard mask of player's rooks/queens
	const Bitboard enemy_slider = (board->piece[BISHOP] | board->piece[ROOK] | board->piece[QUEEN]) & board->color[enemy]; // Bitboard mask of player's sliders
	const Bitboard king = (board->piece[KING]) & board->color[player]; // Bitboard mask of player's king
	const Bitboard castling_enemy = board->castling & (CAN_CASTLE_KINGSIDE[enemy] | CAN_CASTLE_QUEENSIDE[enemy]);
	const Bitboard castling_player = board->castling & (CAN_CASTLE_KINGSIDE[player] | CAN_CASTLE_QUEENSIDE[player]);
	const Bitboard border = 0xFF818181818181FFULL, rank_border = 0xFF000000000000FFULL, file_border = 0x8181818181818181ULL;
	Board next;    // Next board state after a move is made
	int count = 0; // Number of legal moves for the next 2 plies (current player & opponent player).
	Move move;     // a single move
	MoveArray move_array;    // Array of legal moves for the current player
	MapArray player_array;   // Map array of player's legal moves
	MapArray enemy_array;    // Map array of enemy's legal moves (after a nullmove)
	MapArray harmful_array;  // Map array of player's moves that affect enemy's move generation
	Bitboard pawn_potential_move = 0; // pawn push by the enemy that can be blocked or unblocked by a player's move
	Bitboard to_empty = 0;   // Empty squares where the enemy can move: pieces going to here affect the next move generation
	Bitboard to_capture = 0; // Squares where the enemy can capture: pieces from here cannot move without affecting the next move generation
	Bitboard blocking = 0;   // Square between a slider & the enemy's king. Playing from or to here can affect the move generation
	Bitboard harmless;       // Bitboard mask of destination square not affecting the enemy's moves.
	Bitboard king_to = 0;    // Bitboard mask of the enemy's king destination square
 	Bitboard to;             // Bitboard mask of destination squares
	Bitboard from;           // Bitboard mask of source squares
	Bitboard from_mask;      // Bitboard mask of harmless source squares for the current player
	Bitboard to_mask;        // Bitboard mask of harmless destination squares for the current player
	Bitboard king_threat[] = {
		MASK[k].pawn_attack[enemy], // squares from where pawns cannot check
		MASK[k].knight,             // squares from where knights cannot check
		MASK[k].bishop,   // squares from where bishops cannot check or pin
		MASK[k].rook,     // squares from where rooks cannot check or pin
		MASK[k].queen, // squares from where a queen cannot check or pin
		MASK[k].king,               // squares from where the king cannot touch the opponent king
	};
	int i, j; // indices for iterating over move arrays

	// compute map for both players
	maparray_generate(&player_array, board, player);
	maparray_generate(&enemy_array, board, enemy);

	// perft for the next player, should be invariant after some moves
	const int enemy_count = maparray_to_count(&enemy_array, enemy);

	// compute to_empty: empty squares where the next player can move and
	// to_capture, where the next player's sliders & pawns can capture
	for (i = 0; i < enemy_array.n; i++) {
		const Map *map = enemy_array.map + i;
		const Bitboard b_from = square_to_bit(map->from);;
		to_empty |= map->to & empty;
		if (enemy_slider & b_from) {
			Bitboard inside = ~border;
			if (b_from & ~board->piece[BISHOP] & border) { // rook or queen on an edge
				if (b_from & rank_border) inside |= rank_border;
				if (b_from & file_border) inside |= file_border;
			}
			to_capture |= map->to & ~empty & inside;
		}
	}
	to_empty |= enemy_array.pawn_map.push | enemy_array.pawn_map.double_push;
	to_capture |= (enemy_array.pawn_map.left_capture | enemy_array.pawn_map.right_capture);
	if (enemy) {
		to_empty |= (((enemy_pawn & ~COLUMN[0]) >> 9) | ((enemy_pawn & ~COLUMN[7]) >> 7)) & empty;
		pawn_potential_move = enemy_pawn >> 8;
		pawn_potential_move |= (pawn_potential_move & RANK[5] & empty) >> 8;
	} else {
		to_empty |= (((enemy_pawn & ~COLUMN[0]) << 7) | ((enemy_pawn & ~COLUMN[7]) << 9)) & empty;
		pawn_potential_move = enemy_pawn << 8;
		pawn_potential_move |= (pawn_potential_move & RANK[2] & empty) << 8;
	}

	// king: moves should not prevent the king to move (a king cannot move into check).
	king_to = (MASK[k].king & ~board->color[enemy]) | square_to_bit(k) | castling_enemy;
	to = king_to;
	while (to) {
		const Square nk = square_next(&to);
		blocking |= MASK[nk].pawn_attack[enemy] & pawn;
		blocking |= MASK[nk].knight & knight;
		Bitboard bq = bishop_queen;
		while (bq) {
			const Square x = square_next(&bq);
			const int dir = MASK[nk].direction[x];
			if (dir == 7 || dir == 9) blocking |= MASK[nk].between_in[x];
		}
		Bitboard rq = rook_queen;
		while (rq) {
			const Square x = square_next(&rq);
			const int dir = MASK[nk].direction[x];
			if (dir == 1 || dir == 8) blocking |= MASK[nk].between_in[x];
		}
		blocking |= MASK[nk].king & king;

		king_threat[PAWN]   |= MASK[nk].pawn_attack[enemy];
		king_threat[KNIGHT] |= MASK[nk].knight;
		king_threat[BISHOP] |= MASK[nk].bishop;
		king_threat[ROOK]   |= MASK[nk].rook;
		king_threat[QUEEN]  |= MASK[nk].queen;
		king_threat[KING]   |= MASK[nk].king;
	}

	// Mask of squares that can be moved from, excluding squares:
	//   - blocking king move threats
	//   - where the enemy can capture a piece
	//   - blocking an enemy pawn push
	from_mask = ~(blocking | to_capture | pawn_potential_move);
	// Mask of squares that can be moved to, excluding moves:
	//   - blocking or a king move threat
	//   - that capture a piece
	//   - to a square where the enemy can move to
	to_mask = ~to_empty & empty & ~blocking;

	// (non pawn) moves
	harmful_array.n = 0;
	for (i = j = 0; i < player_array.n; i++) {
		const Map *map = player_array.map + i;
		from = square_to_bit(map->from);

		harmless = 0;
		if ((from & from_mask) && (map->to & king_to) == 0) { // keep moves from squares that we can move from & that do not threat the king
			harmless = map->to & to_mask;             // keep moves to squares that can be moved to
			harmless &= ~king_threat[map->piece];     // exclude the one that can threat the king
			if (map->piece == KING) harmless &= ~castling_player; // exclude castling moves
			count += stdc_count_ones_ull(harmless); // total of harmless moves
		}

		harmful_array.map[j].to = map->to ^ harmless;
		if (harmful_array.map[j].to) {
			harmful_array.map[j].from = map->from;
			harmful_array.map[j].piece = map->piece;
			j++;
		}
	}
	harmful_array.n = j;

	// pawn push
	harmful_array.pawn_map = player_array.pawn_map;
	to_mask &= ~king_threat[PAWN];
	from = pawn & from_mask; // harmless source squares
	harmless = (player ? from >> 8 : from << 8) & harmful_array.pawn_map.push; // legal destination square from harmless source squares
	harmless &= to_mask & ~promotion; // harmless destination squares
	harmful_array.pawn_map.push &= ~harmless; // remove harmless destination squares from the harmful array
	count += stdc_count_ones_ull(harmless); // total of harmless moves

	// pawn double push
	harmless = (player ? harmless >> 8 : harmless << 8) & harmful_array.pawn_map.double_push; // legal destination square from harmless source squares
	harmless &= to_mask;  // harmless destination squares
	harmful_array.pawn_map.double_push &= ~harmless; // remove harmless destination squares from the harmful array
	count += stdc_count_ones_ull(harmless); // total of harmless moves

	count *= enemy_count; // multiply harmless moves by enemy count

	NULLMOVE_STATS(int nm_count = count;)
	NULLMOVE_STATS(atomic_fetch_add(&nullmove_counter, count);)

	// compute perft for the remaining moves
	maparray_to_movearray(&harmful_array, &move_array, player, board->enpassant);

	while ((move = movearray_next(&move_array)) != 0) {
		board_copymake(board, move, &next);
		count += count_moves(&next);
	}

	NULLMOVE_STATS(atomic_fetch_add(&plain_counter, count - nm_count);)

	return count;
}

/**
 * @brief Perft depth 2 (non-recursive)
 *
 * @param board Board to analyze
 * @return Total count of nodes
 */
static int perft_2(const Board *board) {
	Board next;
	int count = 0;
	Move move;
	MoveArray ma;

	movearray_generate(&ma, board);
	while ((move = movearray_next(&ma)) != 0) {
		board_copymake(board, move, &next);
		count += count_moves(&next);
	}
	return count;
}


/**
 * @brief Recursive Perft with optional hashtable, pool of tasks, bulk counting & capture only generation.
 * @param board Board to analyze
 * @param depth Depth to analyze
 * @return Total count of nodes
 */
static Counter perft(const Board *board, const uint32_t depth) {
	const bool use_hash = (hash_table && depth >= MIN_HASH_DEPTH);
	Board next;
	Counter count = 0, hash_count;
	Move move;
	MoveArray ma;
	Node node;
	Key key;

	if (depth == 2) {
		if (nullmove) return perft_nullmove(board);
		else if (bulk) return perft_2(board);
	}

	movearray_generate(&ma, board);
	node_init(&node);

	while ((move = movearray_next(&ma)) != 0) {
		if (use_hash) {
			key_update(&key, board, move);
			hash_prefetch(hash_table, &key);
		}
		board_copymake(board, move, &next);
		if (depth == 1) ++count;
		else {
			if (use_hash) {
				hash_count = hash_probe(hash_table, &key, depth - 1);
				if (hash_count == 0) {
					next.key = key;
					if (!node_split(&node, &next, depth - 1, movearray_todo(&ma))) {
						hash_count = perft(&next, depth - 1);
						hash_store(hash_table, &key, depth - 1, hash_count);
					}
				}
				count += hash_count;
			} else if (!node_split(&node, &next, depth - 1, movearray_todo(&ma))) {
				count += perft(&next, depth - 1);
			}
		}
	}
	count += node_wait(&node);

	return count;
}

static void stats_print(FILE *f) {
	(void) f;
	NULLMOVE_STATS(fprintf(f, "nullmove counter: %lld, plain_counter: %lld, ratio: %.2f%%\n", nullmove_counter, plain_counter, 100.0 * nullmove_counter / (plain_counter + nullmove_counter));)
	HASH_STATS(fprintf(f, "hash table stats: %lld tries, %lld successes  (%.2f%%)\n", probe_tries, probe_successes, 100.0 * probe_successes / probe_tries);)
	HASH_STATS(fprintf(f, "hash table stats: %lld stores\n", stores);)
	SPLIT_STATS(fprintf(f, "split tries: %lld, successes: %lld (%.2f%%)\n", split_tries, split_successes, 100.0 * split_successes / split_tries);)
	SPLIT_STATS(fprintf(f, "run per task:\n");)
	SPLIT_STATS(for (int id = 0; id < task_pool->n_tasks; ++id) fprintf(f, "task %3d: %12u\n", id, task_pool->tasks[id].count));
	SPLIT_STATS(fprintf(f, "master  : %12u\n", 1U);)
}

/** Test */
static void test() {
	Board board;
	typedef struct TestBoard {
		char *comments, *fen;
		Counter result;
		uint32_t depth;
	} TestBoard;
	const TestBoard tests[] = {
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
	for (const TestBoard *t = tests; t->fen != NULL; ++t) {
		printf("Test %s %s", t->comments, t->fen); fflush(stdout);
		board_set(&board, t->fen);
		Counter count = perft(&board, t->depth);
		if (count == t->result) printf(" passed\n");
		else {
			printf(" FAILED ! %s", pretty_counter(count));
			printf("!= %s\n", pretty_counter(t->result));
		}
	}
}

/**
 * Benchmark the perft function on a set of positions.
 * this bench is the same as the one in Gigantua perft program.
 */
void bench() {
	Board board;
	typedef struct BenchBoard {
		char *comment, *fen;
		uint32_t depth;
	} BenchBoard;
	const BenchBoard bench[] = {
		{"Initial Position", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 7},
		{"Kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", 6},
		{"Pos6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 6},
		{"Endgame", "5nk1/pp3pp1/2p4p/q7/2PPB2P/P5P1/1P5K/3Q4 w - - 1 28", 6},
		{NULL, NULL, 0}
	};
	double partial_time = 0.0, total_time = 0.0;
	Counter count, total = 0;

	for (const BenchBoard *t = bench; t->comment; t++) {
		board_set(&board, t->fen);
		if (hash_table) hash_clear(hash_table);

		for (uint32_t d = 1; d <= t->depth; ++d) {
			partial_time = -chrono();
				count = perft(&board, d);
			partial_time += chrono();
			printf("%16s: %26s %s %spos/s\n", t->comment, pretty_counter(count), pretty_time(partial_time), pretty_speed(count / partial_time));
			total_time += partial_time;
			total += count;
		}
		puts("");
	}
	printf("Total: %26s, time: %s %spos/s\n", pretty_counter(total), pretty_time(total_time), pretty_speed(total / total_time));
	stats_print(stdout);
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
int main(int argc, char ** argv) {
	const size_t MAX_HASH_SIZE = get_available_memory();
	const uint32_t N_PROCESSORS = get_available_processors();
	double full_time= -chrono(), partial_time = 0.0, total_time = 0.0;
	Board board, next;
	MoveArray ma;
	Counter count, total = 0;
	uint64_t seed = 0xA170EBA;
	const char *fen = NULL, *moves = NULL;
	uint32_t depth = 6, n_threads = 1, n_repetition = 1;
	size_t hash_size = 0;
	bool div = false, loop = false, verbose = true, do_test = false, do_bench = false;
	Move move;

	// argument
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--bench")) do_bench = true;
		else if (!strcmp(argv[i], "--bulk") || !strcmp(argv[i], "-b")) bulk = true;
		else if (!strcmp(argv[i], "--nullmove") || !strcmp(argv[i], "-n")) nullmove = true;
		else if (!strcmp(argv[i], "--depth") || !strcmp(argv[i], "-d")) depth = atoi(argv[++i]);
		else if (isdigit((int) argv[i][0])) depth = atoi(argv[i]);
		else if (!strcmp(argv[i], "--div")) div = true;
		else if (!strcmp(argv[i], "--fast")) {
			bulk = true;
			nullmove = true;
			hash_size = MAX_HASH_SIZE;
			n_threads = N_PROCESSORS;
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
			puts("\t--bench                 Run a small benchmark.");
			puts("\t--bulk|-b               Do fast bulk counting at the last ply.");
			puts("\t[--depth|-d] <depth>    Test up to this depth (default = 6).");
			puts("\t--div                   Print a node count for each move.");
			puts("\t--fast                  Automatically set highest settings.");
			puts("\t--fen|-f <fen>          Use the position indicated in FEN format (default = starting position).");
			puts("\t--hash|-h <size>        Use a hashtable with <size> Megabytes (default = 0, no hashtable).");
			puts("\t--help|-?               Print this message.");
			puts("\t--kiwipete|-k           Use the kiwipete position.");
			puts("\t--loop|-l               Loop from depth 1 to <depth>.");
			puts("\t--moves|-m <moves>      Play a serie of moves to build the position to use.");
			puts("\t--nullmove|-n           Do fast nullmove counting at the penultimate ply.");
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
	if (hash_size > MAX_HASH_SIZE) hash_size = MAX_HASH_SIZE; // max size set to available memory
	hash_table = hash_create(hash_size);

	// thread initialization
	if (n_threads < 1) n_threads = 1;
	if (n_threads > N_PROCESSORS) n_threads = N_PROCESSORS;
	task_pool = taskpool_create(n_threads - 1);

	// Test
	if (do_test) {
		test();
		return 0;
	}

	// bench
	if (do_bench) {
		bench();
		return 0;
	}

	// board initialization
	board_init(&board);
	if (fen) board_set(&board, fen);
	if (moves) {
		while ((moves = parse_move(moves, &board, &move)) != NULL) {
			board_copymake(&board, move, &next);
			board = next;
			key_update(&board.key, &board, move);
		}
	}

	// depth and repetition bounds
	if (depth < 1) depth = 1;
	if (depth > 63) depth = 63;
	if (n_repetition < 1) n_repetition = 1;

	// Header output
	if (verbose) {
		puts("Magic Perft version 5.1 (c) Richard Delorme 2020 - 2026");
		if (HAS_PEXT) puts("Bitboard move generation based on magic (pext) bitboards.");
		else puts("Bitboard move generation based on magic bitboards.");
		if (sizeof (Counter) == 16) puts("Using 128 bits counter & Zobrist's key.");
		printf("Perft setting: ");
		if (hash_size == 0) printf("no hashing; ");
		else printf("hashtable size: %u Mbytes (%" PRIu64 " entries); ",
				(unsigned) (sizeof (Hash) * (hash_table->mask + BUCKET_SIZE + 1) >> 20), hash_table->mask + BUCKET_SIZE + 1);
		if (n_threads > 1) printf("with %u threads; ", n_threads); else printf("no multithreading; ");
		if (nullmove) printf("with nullmove counting.");
		else if (bulk) printf("with bulk counting.");
		else printf("no nullmove/bulk counting.");
		puts("");
		board_print(&board, stdout);
	}
	// root search
	if (div) {
		for (uint32_t r = 1; r <= n_repetition; ++r) {
			for (uint32_t d = (loop ? 1 : depth); d <= depth; ++d) {
				if (n_repetition > 1) printf("repetition: %u\n", r);
				printf("depth: %u\n", d);
				movearray_generate(&ma, &board);
				movearray_sort(&ma);
				while ((move = movearray_next(&ma)) != 0) {
					partial_time = -chrono();
					board_copymake(&board, move, &next);
					key_update(&next.key, &board, move);
					if (d == 1) count = 1;
					else if (bulk && d == 2) count = count_moves(&next);
					else count = perft(&next, depth - 1);
					total += count;
					partial_time += chrono();
					total_time += partial_time;
					printf("%5s %26s positions in %s %spos/s\n",
						move_to_string(move, NULL), pretty_counter(count), pretty_time(partial_time), pretty_speed(count / partial_time));
				}
			}
		}
	} else {
		for (uint32_t r = 1; r <= n_repetition; ++r) {
			for (uint32_t d = (loop ? 1 : depth); d <= depth; ++d) {
				if (hash_table) hash_clear(hash_table);
				partial_time = -chrono();
				count = perft(&board, d);
				total += count;
				partial_time += chrono();
				total_time += partial_time;
				printf("perft %2d: %26s positions in %s %spos/s\n",
					d, pretty_counter(count), pretty_time(partial_time), pretty_speed(count / partial_time));
			}
		}
	}
	if (div || loop || n_repetition > 1) {
		printf("total   : %26s positions in %s %spos/s\n",
			pretty_counter(total), pretty_time(total_time), pretty_speed(total / total_time));
	}

	stats_print(stdout);

	// cleanup
	hash_destroy(hash_table);
	taskpool_destroy(task_pool);
	free(ATTACK_BISHOP->attack);
	free(ATTACK_ROOK->attack);

	full_time += chrono();
	if (verbose) printf("full time: %s s\n", pretty_time(full_time));

	return 0;
}
