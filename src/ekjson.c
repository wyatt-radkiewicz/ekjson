#include "ekjson.h"

// Makes a u32 literal out of a list of characters (little endian)
#define STR2U32(A, B, C, D) ((A) | ((B) << 8) | ((C) << 16) | ((D) << 24))
#define ARRLEN(A) (sizeof(A) / sizeof((A)[0]))

// Always inline macros
#ifdef __GNUC__
#define EKJSON_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define EKJSON_ALWAYS_INLINE
#endif

// (except for in space efficient mode)
#if EKJSON_SPACE_EFFICENT
#	define EKJSON_INLINE
#else
#	define EKJSON_INLINE EKJSON_ALWAYS_INLINE
#endif

// Bit twiddling hacks - https://graphics.stanford.edu/~seander/bithacks.html
// These macros check to see if a word has a byte that matches the condition
#if !EKJSON_NO_BITWISE
#define haszero(v) (((v) - 0x0101010101010101ull) & ~(v) \
			& 0x8080808080808080ull)
#define hasvalue(x,n) (haszero((x) ^ (~0ull/255 * (n))))
#define hasless(x,n) (((x)-~0UL/255*(n))&~(x)&~0UL/255*128)
#endif

#if !EKJSON_SPACE_EFFICIENT // No big tables in space efficient mode
// Used to make tables slightly smaller since everything after the ascii range
// is treated the same since it needs to be valid UTF-8 for ekjson
#define FILLCODESPACE(I) \
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, \
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, \
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, \
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, \
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, \
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, \
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, \
	I, I, I, I, I, I, I, I, I, I, I, I, I, I, I, I,
#endif

// Bit trick functions and macros
#if !EKJSON_NO_BITWISE

// Bitwise operations
#if __GNUC__ // Implement using GNU compiler compatible extensions
// Counts the number of trailing zeros in the number (starts from 0th bit)
#define ctz(x) _Generic(x, \
	unsigned long long: __builtin_ctzll(x), \
	unsigned long: __builtin_ctzl(x), \
	unsigned int: __builtin_ctz(x))
// Counts number of leading zeros in the number (starts from nth bit)
#define clz(x) _Generic(x, \
	unsigned long long: __builtin_clzll(x), \
	unsigned long: __builtin_clzl(x), \
	unsigned int: __builtin_clz(x))
#else // Generic compiler implementations of these bitwise functions
// Counts the number of trailing zeros in the number (starts from 0th bit)
static EKJSON_ALWAYS_INLINE uint64_t ctz(uint64_t x) {
	x &= -x;	// Isolate the first 1 so we can get its position
	uint64_t n = 0;	// Number of trailing zeros
	
	// Divide and concore approach
	n += x & 0xFFFFFFFF00000000 ? 32 : 0;
	n += x & 0xFFFF0000FFFF0000 ? 16 : 0;
	n += x & 0xFF00FF00FF00FF00 ? 8 : 0;
	n += x & 0xF0F0F0F0F0F0F0F0 ? 4 : 0;
	n += x & 0xCCCCCCCCCCCCCCCC ? 2 : 0;
	n += x & 0xAAAAAAAAAAAAAAAA ? 1 : 0;
	return n;
}
#endif // __GNUC__

// Wrappers for loading u16/u32/u64 on unaligned addresses
static EKJSON_ALWAYS_INLINE uint64_t ldu64_unaligned(const void *const buf) {
	const uint8_t *const bytes = buf;
	return (uint64_t)bytes[0] | (uint64_t)bytes[1] << 8
		| (uint64_t)bytes[2] << 16 | (uint64_t)bytes[3] << 24
		| (uint64_t)bytes[4] << 32 | (uint64_t)bytes[5] << 40
		| (uint64_t)bytes[6] << 48 | (uint64_t)bytes[7] << 56;
}
static EKJSON_ALWAYS_INLINE void stu64_unaligned(void *const buf,
						const uint64_t x) {
    uint8_t *const bytes = buf;
    bytes[0] = x & 0xFF, bytes[1] = x >> 8 & 0xFF;
    bytes[2] = x >> 16 & 0xFF, bytes[3] = x >> 24 & 0xFF;
    bytes[4] = x >> 32 & 0xFF, bytes[5] = x >> 40 & 0xFF;
    bytes[6] = x >> 48 & 0xFF, bytes[7] = x >> 56 & 0xFF;
}
#endif // EKJSON_NO_BITWISE

// Load unsigned 32 bit value on an unaligned address
static EKJSON_ALWAYS_INLINE uint32_t ldu32_unaligned(const void *const buf) {
	const uint8_t *const bytes = buf;
	return (uint32_t)bytes[0]
		| (uint32_t)bytes[1] << 8
		| (uint32_t)bytes[2] << 16
		| (uint32_t)bytes[3] << 24;
}

// Overflow detecting math ops
#if __GNUC__
// Returns true if the signed addition overflowed or underflowed
#define add_overflow(x, y, out) _Generic(x + y, \
	unsigned long long: __builtin_uaddll_overflow(x, y, \
				(unsigned long long *)out), \
	long long: __builtin_saddll_overflow(x, y, (long long *)out), \
	unsigned long: __builtin_uaddl_overflow(x, y, \
				(unsigned long *)out), \
	long: __builtin_saddl_overflow(x, y, (long *)out))
// Returns true if the signed multiplication overflowed or underflowed
#define mul_overflow(x, y, out) _Generic(x + y, \
	unsigned long long: __builtin_umulll_overflow(x, y, \
				(unsigned long long *)out), \
	long long: __builtin_smulll_overflow(x, y, (long long *)out), \
	unsigned long: __builtin_umull_overflow(x, y, \
				(unsigned long *)out), \
	long: __builtin_smull_overflow(x, y, (long *)out))
#else // Generic implementations
static EKJSON_ALWAYS_INLINE bool add_overflow(int64_t x, int64_t y,
						int64_t *out) {
	*out = x + y;
	return x > 0 && y > INT64_MAX - x || x < 0 && y < INT64_MIN - x;
}
static EKJSON_ALWAYS_INLINE bool mul_overflow(int64_t x, int64_t y,
						int64_t *out) {
	*out = x * y;
	y = y < 0 ? -y : y;
	return x > 0 && x > INT64_MAX / y || x < 0 && x < INT64_MIN / y;
}
#endif // __GNUC__

// Unicode escape helper functions

// Converts a utf8 char to a hexadecimal number
// If the character is invalid, it simply returns 0
static uint8_t hex2num(const uint8_t hex) {
	static const uint8_t table[] = {
		['0'] = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
		['a'] = 0xA, 0xB, 0xC, 0xD, 0xE, 0xF,
		['A'] = 0xA, 0xB, 0xC, 0xD, 0xE, 0xF,
	};
	return table[hex];
}

// Converts valid hex 4 digit strings (without prefix) to a uint32_t
static uint32_t str2hex(const char *src) {
	return (uint32_t)hex2num(src[0]) << 12
		| (uint32_t)hex2num(src[1]) << 8
		| (uint32_t)hex2num(src[2]) << 4
		| (uint32_t)hex2num(src[3]);
}

// Returns the length of the json unicode escape sequence
// Returns 0 if there was an error
// Handles utf16 surrogates for code points above the BMP
// Always writes to out
static size_t hex2utf8(const char *src, char out[static const 4]) {
	// Get the code point (or high surrogate if it is one)
	const uint32_t hi = str2hex(src);

	if (hi < 0x80) {
		// Ascii char
		out[0] = hi;
		return 1;
	} else if (hi < 0x800) {
		// UTF-8 2 byte sequence
		out[0] = 0xC0 | (hi >> 6);
		out[1] = 0x80 | (hi & 0x3F);
		return 2;
	} else if (hi < 0xD800 || hi > 0xDFFF) {
		// UTF-8 3 byte sequence (NOT in surrogate range)
		out[0] = 0xE0 | (hi >> 12);
		out[1] = 0x80 | (hi >> 6 & 0x3F);
		out[2] = 0x80 | (hi & 0x3F);
		return 3;
	} else {
		// This is a utf16 surrogate

		// Check if we got the low surrogate first (big no no)
		if (hi > 0xDBFF) return 0;

		// Make sure that there is a \uXXXX after this one
		if (src[4] != '\\' && src[5] != 'u') return 0;

		// No need to check for eof since this will be a valid
		// JSON string by definition of ejparse
		const uint32_t lo = str2hex(src + 6);

		// Make sure this codepoint is a low surrogate
		if (lo < 0xDC00 || lo > 0xDFFF) return 0;

		// Combine the two surrogates to get codepoint
		const uint32_t final =
			((hi - 0xD800) << 10) + (lo - 0xDC00) + 0x10000;

		// Write 4-byte code point (no need for high level checking)
		out[0] = 0xF0 | (final >> 18);
		out[1] = 0x80 | (final >> 12 & 0x3F);
		out[2] = 0x80 | (final >> 6 & 0x3F);
		out[3] = 0x80 | (final & 0x3F);
		return 4;
	}
}

// State used for ejstr function and escape function
typedef struct ejstr_state {
	const char *src;	// Where we are in the source
	char *out;		// Where we are in the output buffer
	char *end;		// Last valid byte of the buffer
	
	// Length of the string in bytes irrespective of whether or not
	// the output buffer is present or has already been filled
	size_t len;
} ejstr_state_t;

// Main state for the parser (used by most parser functions)
typedef struct state {
	// Start of the source code (doesn't change)
	const char *base;

	// Pointer to where we are currently parsing
	const char *src;

	// Start of the token buffer
	ejtok_t *tbase;
	
	// 1 before the end of the token buffer
	ejtok_t *tend;

	// Next place to allocate a token
	ejtok_t *t;
} state_t;

// Consumes whitespace and returns a pointer to the first non-whitespace char
static EKJSON_ALWAYS_INLINE const char *whitespace(const char *src) {
	for (; *src == ' ' || *src == '\t'
		|| *src == '\r' || *src == '\n'; src++);
	return src;
}

// Adds a token with the specified type and increments the pointer if there
// is space
static EKJSON_INLINE ejtok_t *addtok(state_t *const state, const int type) {
	*state->t = (ejtok_t){
		.type = type,
		.len = 1,
		.start = state->src - state->base,
	};
	ejtok_t *const t = state->t;
	state->t += state->t != state->tend;
	return t;
}

// Parses a string
// Adds the string token with type 'type'
// Leaves the source sting at the character after the ending " or after the
// first error that occurred in the string
// Returns NULL if error occurred
static EKJSON_INLINE ejtok_t *string(state_t *const state, const int type) {
// Auto-generated by gendfa.py, don't touch, regenerate instead.
#if EKJSON_SPACE_EFFICIENT
	// Edge table
	static const uint8_t edges[256] = {
		[''] = 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
			1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		['"'] = 2, ['/'] = 3,
		['0'] = 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
		['A'] = 4, 4, 4, 4, 4, 4,
		['\'] = 5, ['a'] = 4, ['b'] = 6,
		['c'] = 4, 4, 4, ['f'] = 6, ['n'] = 3, ['r'] = 3,
		['t'] = 3, ['u'] = 7,
	};
	// State transition table (small)
	static const uint8_t trans[][8] = {
		{ 0 , 7 , 6 , 0 , 0 , 1 , 0 , 0  },
		{ 7 , 7 , 0 , 0 , 7 , 0 , 0 , 5  },
		{ 7 , 7 , 7 , 7 , 3 , 7 , 3 , 7  },
		{ 7 , 7 , 7 , 7 , 4 , 7 , 4 , 7  },
		{ 7 , 7 , 7 , 7 , 0 , 7 , 0 , 7  },
		{ 7 , 7 , 7 , 7 , 2 , 7 , 2 , 7  },
	};
#else // EKJSON_SPACE_EFFICIENT
	// State transition table (big)
	static const uint8_t trans[][256] = {
		{ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
		  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		  FILLCODESPACE(0) },
		{ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 0, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0, 7, 7, 7,
		  7, 7, 0, 7, 7, 7, 0, 7, 7, 7, 7, 7, 7, 7, 0, 7,
		  7, 7, 0, 7, 0, 5, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  FILLCODESPACE(7) },
		{ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 7, 7, 7, 7, 7, 7,
		  7, 3, 3, 3, 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 3, 3, 3, 3, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  FILLCODESPACE(7) },
		{ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 7, 7, 7, 7, 7, 7,
		  7, 4, 4, 4, 4, 4, 4, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 4, 4, 4, 4, 4, 4, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  FILLCODESPACE(7) },
		{ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7,
		  7, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 0, 0, 0, 0, 0, 0, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  FILLCODESPACE(7) },
		{ 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 7, 7, 7, 7, 7, 7,
		  7, 2, 2, 2, 2, 2, 2, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 2, 2, 2, 2, 2, 2, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		  FILLCODESPACE(7) },
	};
#endif // EKJSON_SPACE_EFFICIENT

#define STRACCEPT 0
#define STRFINISHSTATES 6
#define STRDONE 6
#define STRERR 7

	// Add the token and save a local copy of the source pointer for speed
	ejtok_t *const tok = addtok(state, type);
	const char *src = state->src + 1;

#if !EKJSON_NO_BITWISE
	// Eat 8-byte chunks for as long as we can
	uint64_t probe = ldu64_unaligned(src);
	while (!(hasless(probe, 0x20)
		|| hasvalue(probe, '"')
		|| hasvalue(probe, '\\'))) {
		src += 8;
		probe = ldu64_unaligned(src);
	}
#endif

	// Use a dfa to get through things relativly quickly
	int s = STRACCEPT;
	do {
#if EKJSON_SPACE_EFFICENT
		s = trans[s][edges[(uint8_t)(*src++)]];
#else
		s = trans[s][(uint8_t)(*src++)];
#endif
	} while (s < STRFINISHSTATES);

	// Update the normal state source pointer again
	// NOTE: Problem here that gets fixed at the ejparse wrapper:
	// For some reason fixing the src pointer when errors occur here fucks
	// up the code speed, so we don't actually ensure the src pointer is
	// pointing in the actual string.
	// TLDR: When an error occurs, the src pointer points to the char after
	// the error char meaning that it can point after the null-terminator,
	// this gets fixed in ejparse instead of here due to speed :/
	state->src = src;

	// Return error code if dfa state is in the invalid (6) state
	return s == STRDONE ? tok : NULL;
}

// Parse number
// Adds token to state variable
// Leaves state source pointer at the first non-num character
// Returns NULL if error occurred
static EKJSON_INLINE ejtok_t *number(state_t *const state) {
// Auto-generated by gendfa.py, don't touch, regenerate instead.
#if EKJSON_SPACE_EFFICIENT
	// Edge table
	static const uint8_t edges[256] = {
		['+'] = 1, ['-'] = 2, ['.'] = 3, ['0'] = 4,
		['1'] = 5, 5, 5, 5, 5, 5, 5, 5, 5,
		['E'] = 6, ['e'] = 6,
	};
	// State transition table (small)
	static const uint8_t trans[][7] = {
		{ 11, 11, 1 , 11, 2 , 3 , 11 },
		{ 11, 11, 11, 11, 2 , 3 , 11 },
		{ 10, 10, 10, 4 , 10, 10, 6  },
		{ 10, 10, 10, 4 , 3 , 3 , 6  },
		{ 11, 11, 11, 11, 5 , 5 , 11 },
		{ 9 , 9 , 9 , 9 , 5 , 5 , 6  },
		{ 11, 7 , 7 , 11, 8 , 8 , 11 },
		{ 11, 11, 11, 11, 8 , 8 , 11 },
		{ 9 , 9 , 9 , 9 , 8 , 8 , 9  },
	};
#else // EKJSON_SPACE_EFFICIENT
	// State transition table (big)
	static const uint8_t trans[][256] = {
		{ 11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 1 , 11, 11,
		  2 , 3 , 3 , 3 , 3 , 3 , 3 , 3 ,
		  3 , 3 , 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  FILLCODESPACE(11) },
		{ 11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  2 , 3 , 3 , 3 , 3 , 3 , 3 , 3 ,
		  3 , 3 , 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  FILLCODESPACE(11) },
		{ 10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 4 , 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 6 , 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 6 , 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  FILLCODESPACE(10) },
		{ 10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 4 , 10,
		  3 , 3 , 3 , 3 , 3 , 3 , 3 , 3 ,
		  3 , 3 , 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 6 , 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 6 , 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  10, 10, 10, 10, 10, 10, 10, 10,
		  FILLCODESPACE(10) },
		{ 11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  5 , 5 , 5 , 5 , 5 , 5 , 5 , 5 ,
		  5 , 5 , 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  FILLCODESPACE(11) },
		{ 9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  5 , 5 , 5 , 5 , 5 , 5 , 5 , 5 ,
		  5 , 5 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 6 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 6 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  FILLCODESPACE(9) },
		{ 11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 7 , 11, 7 , 11, 11,
		  8 , 8 , 8 , 8 , 8 , 8 , 8 , 8 ,
		  8 , 8 , 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  FILLCODESPACE(11) },
		{ 11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  8 , 8 , 8 , 8 , 8 , 8 , 8 , 8 ,
		  8 , 8 , 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  11, 11, 11, 11, 11, 11, 11, 11,
		  FILLCODESPACE(11) },
		{ 9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  8 , 8 , 8 , 8 , 8 , 8 , 8 , 8 ,
		  8 , 8 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  9 , 9 , 9 , 9 , 9 , 9 , 9 , 9 ,
		  FILLCODESPACE(9) },
	};
#endif // EKJSON_SPACE_EFFICIENT

#define NUMACCEPT 0
#define NUMFINISHSTATES 9
#define NUMFLTDONE 9
#define NUMINTDONE 10
#define NUMERR 11

	// Add token
	ejtok_t *const tok = addtok(state, EJINT);

	// Create local copy for speed
	const char *src = state->src;

	// Use dfa to quickly validate the number without having to parse it
	// fully and correctly
	int s = NUMACCEPT;
	while (s < NUMFINISHSTATES) {
		// Get next state using current state
#if EKJSON_SPACE_EFFICENT
		s = trans[s][edges[(uint8_t)(*src++)]];
#else
		s = trans[s][(uint8_t)(*src++)];
#endif
	}

	// Update token type if it is a float
	tok->type = s == NUMFLTDONE ? EJFLT : tok->type;

	// Restore the source pointer to the first different char
	state->src = src - 1;

	// Return error code if dfa state is in the invalid (9) state
	return s == NUMERR ? NULL : tok;
}

// Parses boolean values aka 'true'/'false'
// Adds a token
// Leaves state source pointer right after the 'true'/'false'
// Returns NULL if the source is not 'true'/'false'
static EKJSON_INLINE ejtok_t *boolean(state_t *const state) {
	// Add the token here
	ejtok_t *const tok = addtok(state, EJBOOL);

	// See if it is 'false'?
	const bool bfalse =
		(state->src[4] == 'e')
		& (ldu32_unaligned(state->src) == STR2U32('f', 'a', 'l', 's'));

	// Check if its either false or 'true'
	const bool bvalid = bfalse
		| (ldu32_unaligned(state->src) == STR2U32('t', 'r', 'u', 'e'));

	// A mask. 0xFFFFFFFFFFFFFFFF if valid, 0 if not
	const uint64_t valid = bvalid * (uint64_t)(-1);

	// Increment pointer
	state->src += 4 * bvalid + bfalse;

	// Return NULL if either token was NULL or it wasn't valid
	return (ejtok_t *)((uint64_t)tok & valid);
}

// Parses null value aka 'null'
// Adds a token
// Leaves state source pointer right after the 'null'
// Returns NULL if the source is not 'null'
static EKJSON_INLINE ejtok_t *null(state_t *const state) {
	// Add the token here
	ejtok_t *const tok = addtok(state, EJNULL);

	// See if it is 'null'?
	const bool bvalid =
		ldu32_unaligned(state->src) == STR2U32('n', 'u', 'l', 'l');

	// A mask. 0xFFFFFFFFFFFFFFFF if valid, 0 if not
	const uint64_t valid = bvalid * (uint64_t)(-1);

	// Increment pointer
	state->src += 4 * bvalid;

	// Return NULL if either token was NULL or it wasn't valid
	return (ejtok_t *)((uint64_t)tok & valid);
}

// Main heartbeat of the ekjson parser
// This will parse anything in a json document
// Takes in a depth parameter to make sure that no stack overflows can occur
static ejtok_t *value(state_t *const state, const int depth) {
	// The token that we are parsing (also the value)
	ejtok_t *tok = NULL;

	// Check if we are over the callstack limit
	if (depth >= EKJSON_MAX_DEPTH) return NULL;

	// Eat whitespace first as per spec
	state->src = whitespace(state->src);

	// Figure out what kind of value/token we are going to parse
	switch (*state->src) {
	case '{':
		// Parse an object, add the token first
		tok = addtok(state, EJOBJ);

		// Parse whitespace after initial '{'
		state->src = whitespace(state->src + 1);

		// Make sure we're not at the ending '}' already
		// If not then actually parse a key and value
		while (*state->src != '}') {
			// Get the key eg. "a"
			ejtok_t *const key = string(state, EJKV);

			// If the key had errors, exit now
			if (!key) return NULL;

			// Do an early check for : since most documents
			// have the : right after the key with no whitespace
			// (this is a situational optimization but doesn't
			//  hurt in terms of performance if the assumption is
			//  incorrect)
			if (*state->src != ':') {
				// Parse the whitespace after the key
				state->src = whitespace(state->src);
				if (*state->src++ != ':') return NULL;
			} else {
				state->src++;
			}

			// Now take the value. No need to parse whitespace
			// since values already do that initially
			const ejtok_t *const val = value(state, depth + 1);

			// If the value had errors, exit now
			if (!val) return NULL;

			// Update the key and object length
			key->len += val->len;
			tok->len += val->len + 1;
 			if (*state->src == ',') {
				// Make sure to parse whitespace for next key
				// and also skip the ','
				state->src = whitespace(state->src + 1);
			}
		}

		state->src++;	// Eat last '}' character
		break;
	case '[':
		// Parse an array, create the array token first
		tok = addtok(state, EJARR);

		// Parse the whitespace after the initial '['
		state->src = whitespace(state->src + 1);

		// Make sure we're not at the ending ']' already
		// If not then actually parse a key and value
		while (*state->src != ']') {
			// Parse array value (does whitespace before and after)
			const ejtok_t *const val = value(state, depth + 1);

			// About out right now if the value had an error
			if (!val) return NULL;
			tok->len += val->len;	// Update array length

			// Eat the ',' (no whitespace parsing needed,
			// value does it)
			if (*state->src == ',') state->src++;
		}

		state->src++;	// Eat the last ']'
		break;
	case '"':		// Parse and create string token
		tok = string(state, EJSTR);
		break;
	case '-': case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':	// Number token
		tok = number(state);
		break;
	case 't': case 'f':	// Parse and create boolean token
		tok = boolean(state);
		break;
	case 'n':		// Parse and create null token
		tok = null(state);
		break;
	case '\0':		// Parse '\0', error if not on top-level
		if (depth) return NULL;
		else return (void *)1;
		break;
	default:		// If its anything else, its an error
		return NULL;
	}

	// Parse final whitespace (like json spec)
	state->src = whitespace(state->src);
	return tok;
}

// This is just a wrapper around the value parser
// It just initializes the state and checks for error states
ejresult_t ejparse(const char *src, ejtok_t *t, size_t nt) {
	// Create initial state
	state_t state = {
		.base = src, .src = src,
		.tbase = t, .tend = t + nt, .t = t,
	};

	// See if the value parsed correctly
	const bool value_result = value(&state, 0);

	// BAD CODE WARNING (jk)
	// So since the error location is returned after an error occured the
	// source pointer must point inside the string. The string() function
	// will return the char after the error character which is a fine
	// enough trade-off except for the part where it will return the char
	// after the null terminator :/. This is a fix for that.
	if (!value_result			// Did we even get an error
		&& state.src > state.base	// Are we beyond first char?
		&& state.src[-1] == '\0') {	// Did we skip null-terminator?
		state.src--;			// Fix the fuckup
	}

	const bool okay = value_result	 // Was there a parsing error?
		&& state.t != state.tend // Did we take up all memory?
		&& *state.src == '\0';	 // Make sure we ended at end of string

	// Return ejresult_t value
	return okay ? (ejresult_t){
		.err = false,
		.loc = NULL,
		.ntoks = 0,
	} : (ejresult_t){
		.err = true,
		.loc = state.src,
		.ntoks = state.t - state.tbase,
	};
}

// Maps all 1-byte escape sequences. Used in escape function and compare func
static const uint8_t unescape[256] = {
	['"'] = '"', ['\\'] = '\\',
	['/'] = '/', ['b'] = '\b',
	['f'] = '\f', ['n'] = '\n',
	['r'] = '\r', ['t'] = '\t',
};

// Returns whether or not the escape charcter is valid (usually is)
// Updates state with the newly escaped character
static bool escape(ejstr_state_t *state) {
	// Check if its a normal escape charater
	if (*++state->src != 'u') {
		// Write out the byte if we can
		if (state->out < state->end) {
			// Use escapes table to map to unescaped char
			*state->out++ = unescape[*state->src];
		}

		// Increment src ptr, len and exit early
		++state->src, ++state->len;
		return true;
	}

	// We are parsing a unicode escape char
	char utf8[4];						// tmp buffer
	const size_t u8len = hex2utf8(++state->src, utf8);	// parse escape
	
	if (u8len == 0) {
		// It was an invalid escape, return error code
		return false;
	} else if (state->out + u8len < state->end) {
		// We have enough buffer room to copy it over
		char *tmp = utf8;		// Copy bytes over
		switch (u8len) {
		case 4: *state->out++ = *tmp++;
		case 3: *state->out++ = *tmp++;
		case 2: *state->out++ = *tmp++;
		case 1: *state->out++ = *tmp++;
		}
	} else {
		// Don't have enough room, so make sure that no more bytes
		// can be outputted.
		state->end = state->out;
	}

	// Go past the unicode escape, and add the utf-8 sequence length
	state->src += u8len == 4 ? 6+4 : 4;
	state->len += u8len;
	return true;
}

// Copies and escapes a json string/kv to a string buffer
// Takes in json source, token, and the out buffer and out length
// If out is non-null and outlen is greater than 0, it will write characters
// until outlen-1 and then output a null-terminator meaning the output buffer
// will always be null-terminated.
// Returns length of what the would be string would be (including null
// terminator so that length is always above 0 when there are no errors)
// If the string contains an invalid utf-8 codepoint or surrogate, it will
// return the length as 0 to signify error
size_t ejstr(const char *src, char *out, const size_t outlen) {
	// Initialize the escaping/copying state
	ejstr_state_t state = {
		.src = src + 1,	// Skip '"', and where we are in the string
		.out = out,	// Where we are in the output buffer

		// Last valid char of the buffer
		.end = outlen ? (out ? out + outlen - 1 : NULL) : out,
		.len = 1,	// How long the string is (irrespective of buf)
	};

#if !EKJSON_NO_BITWISE
	// Do everything in chunks of 8 bytes
	uint64_t probe = ldu64_unaligned(state.src);

	// If we go over this, then we should go to the slow route
	char *const end8 = outlen ? (out ? out + outlen - 8 : NULL) : out;

	// Keep adding length and outputting to buffer until we find ending '"'
	while (!hasvalue(probe, '"')) {
		if (hasvalue(probe, '\\')) {
			// No point in the slow headache of rewinding and doing
			// stuff that way for escaping... Too slow without SIMD
			break;
		} else {
			if (state.out < end8) {
				// Output 8 bytes
				stu64_unaligned(state.out, probe);
				// Skip past written data
				state.out += 8;
			} else if (state.out > state.end) {
				// Create temporary src pointer
				const char *tmp = state.src;

				// Output 1 to 7 bytes
				switch (state.end - state.out) {
				case 7: *state.out++ = *tmp++;
				case 6: *state.out++ = *tmp++;
				case 5: *state.out++ = *tmp++;
				case 4: *state.out++ = *tmp++;
				case 3: *state.out++ = *tmp++;
				case 2: *state.out++ = *tmp++;
				case 1: *state.out++ = *tmp++;
				}
			}

			// Add string length and skip past probed data
			state.len += 8, state.src += 8;
			// Get next 8-byte chunk
			probe = ldu64_unaligned(state.src);
		}
	}
#endif

	// Do it byte by byte here
	// Keep adding length and outputting to buffer until we find ending '"'
	while (*state.src != '"') {
		if (*state.src == '\\') {
			if (!escape(&state)) return 0; // Checks for errors
		} else {
			// Output byte (if there is room left)
			if (state.out < state.end) *state.out++ = *state.src;
			++state.src, ++state.len; // 1 byte for len and src
		}
	}

	// Add null terminator always if the user supplied a buffer
	if (state.out) *state.out = '\0';

	// Return what the string length is regardless of buffer length
	return state.len;
}

// Compares the string token to a normal c string, escaping characters as
// needed and returning whether or not they are equal. Passing in null for
// tok_start or cstr is undefined.
// Renamed tok_start to src here since its used as the source pointer in this
// implemenation
bool ejcmp(const char *src, const char *cstr) {
	// Skip past the first '"' at the start of string token
	src++;

#if !EKJSON_NO_BITWISE
	// Initialize 8-byte probe
	uint64_t probe = ldu64_unaligned(src);

	// Continue comparing 8-byte chunks until we are less than 8 away from
	// the end or we hit and escape
	while (!hasvalue(probe, '"')) {
		if (hasvalue(probe, '\\')) {
			// We fall back to the sequential way of doing things
			// when we see an escape. It's too slow without SIMD
			break;
		} else {
			// Compare 8 bytes
			if (probe != ldu64_unaligned(cstr)) return false;
			src += 8, cstr += 8;	// If successful, goto next 8
			probe = ldu64_unaligned(src);	// Load this row of 8
		}
	}
#endif

	// Go byte by byte until we reach the end of the string
	while (*src != '"') {
		if (*src == '\\') {
			// Escape this character
			if (*++src == 'u') {
				// Write the utf8 bytes into a temporary buffer
				char buf[4];	// Temporary buffer
				const size_t len = hex2utf8(++src, buf);

				// Check to see if the next 32 bits, when
				// masked off to fit the length of the
				// outputted utf-8 are equal
				if (len == 0	// Output len is 0 if error
					|| ldu32_unaligned(buf) << len * 8
					!= ldu32_unaligned(cstr) << len * 8) {
					return false;
				}
				
				// The bytes are equal so skip past the utf-8
				// bytes in the c string and skip past the
				// escape in the source string
				cstr += len, src += len == 4 ? 10 : 4;
			} else {
				// Compare the next byte with the unescaped src
				if (*cstr++ != unescape[*src++]) return false;
			}
		} else {
			if (*src++ != *cstr++) return false; // Simple compare
		}
	}

	// Make sure the end of the string token is at the same place as the
	// end of the c string
	return *cstr == '\0';
}

#if !EKJSON_NO_BITWISE
// Parses up to 8 digits and writes it to the out pointer. It how many of the
// 8 bytes in this part of the string make up the number starting at the string
static int parsedigits8(const char *const src, uint64_t *const out) {
	// Load up 8 bytes of the source and set default amount of right bytes
	// A 'right' byte is a byte that is a digit between 0 - 9
	uint64_t val = ldu64_unaligned(src), nright = 8;

	// The high nibble of each byte that is not between 0-9 is set
	const uint64_t wrong_bytes = (val ^ 0x3030303030303030)
		+ 0x0606060606060606 & 0xF0F0F0F0F0F0F0F0;

	// Skip the shifting code if there are no wrong bytes. Also we do this
	// because getting the leading zero count on 0 depends on what
	// architecture you're specifically on
	if (!wrong_bytes) goto convert;

	// The index of the first wrong byte is the number of right bytes
	// Due to the UB of shifting by the int width, exit early here.
	// It also doubles up as an early exit optimization
	if (!(nright = ctz(wrong_bytes) / 8)) {
		*out = 0;
		return 0;
	}

	// Shift out the invalid bytes and since the conversion down here only
	// works when the msb is the ones place digit, shift the ones place
	// to the msb
	val <<= (8 - nright) * 8;

convert:
	// Convert the chars in val to a digit. Found this at this amazing talk
	// https://lemire.me/en/talk/gosystems2020/
	// This is also a variation of the algorithm found here:
	// https://kholdstare.github.io/technical/2020/05/26/faster-integer-parsing.html
	val = (val & 0x0F0F0F0F0F0F0F0F) * (0x100 * 10 + 1) >> 8;
	val = (val & 0x00FF00FF00FF00FF) * (0x10000 * 100 + 1) >> 16;
	val = (val & 0x0000FFFF0000FFFF) * (0x100000000 * 10000 + 1) >> 32;
	*out = val;
	return nright;
}
#endif

// Precalculate all the powers of 10 for the integer parser
static const uint64_t u64pows[] = {
	1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000,
	1000000000, 10000000000, 100000000000, 1000000000000, 10000000000000,
	100000000000000, 1000000000000000, 10000000000000000,
	100000000000000000, 1000000000000000000,
};

// Parse base10 integer while rounding when overflow occurs
// This goes byte by byte so it can see the last byte and round if
// nessesary. Returns number of chars parsed. Stores overflow bool in out param
static int parsebase10_round(const char *src, uint64_t *out, bool *overflow) {
	const char *const start = src; // First digit
	uint64_t last; // Save last digit we found
	
	*overflow = false; // Make sure overflow is false

	// Loop through all the digits
	for (*out = 0; *src >= '0' && *src <= '9'; last = *out) {
		const int64_t d = *src++ - '0'; // Convert char to number

		// Shift current value up by a decimal place
		if (mul_overflow(*out, 10, out)) goto overflow;

		// Add the new ones place we found
		if (add_overflow(*out, d, out)) goto overflow;
	}

	return src - start;
overflow:
	// Add 1 more to output to round up if the last digit was 5 or over
	*overflow = true;
	*out = last + (*--src >= '5');
	return src - start;
}

// Parses a stream of base10 digits
// Returns number of chars parsed. Stores if overflow occurred in overflow
// flag
// The value does NOT saturate
static int parsebase10(const char *src, uint64_t *const out, bool *overflow) {
#if EKJSON_NO_BITWISE
	return parsebase10_round(src, out, overflow);
#else
	*overflow = false;

	uint64_t tmp; // Our num we are making, and curr number part (tmp)
	int n;	// The number of right chars in the 8-byte sequence we parsed
	
	// Parse first 1-8 bytes of the number. If the number is 7 bytes or
	// less, then we can be sure that we are done.
	if ((n = parsedigits8(src, out)) < 8) return n;

	// Parse next 8 byte section (this also accounts for the case that
	// the number is truely a 8 byte number. So this section might have
	// no number in it at all). We also apply the sign in this section
	n = parsedigits8(src + 8, &tmp); // Put next 8 bytes into tmp
	
	// Make 'room' for the new digits we are adding by shifting the old
	// ones by n number of decimal places and add the new digits
	*out = *out * u64pows[n] + tmp;
	if (n < 8) return n + 8; // Return if we're sure we're at the end

	// Since uint64_t can hold 16 digit values easily, we have to now check
	// for overflow since we're going over that.
	n = parsedigits8(src + 16, &tmp); // Put next 8 bytes into tmp
	
	// Do the same as above but check if we overflowed
	*overflow = mul_overflow(*out, u64pows[n], out);
	*overflow |= add_overflow(*out, tmp, out);
	return n + 16; // Return number of digits parsed
#endif
}

// Returns the number token parsed as an int64_t. If there are decimals, it
// just returns the number truncated towards 0. If the number is outside of
// the int64_t range, it will saturate it to the closest limit.
int64_t ejint(const char *const src) {
	// What the sign of the number is
	const bool sign = *src == '-';
	bool overflow;	// parsebase10 stores overflow flag here

	// The bound for the sign of the number
	uint64_t bound = (uint64_t)INT64_MAX + sign, x;
	
	// Parse int and also increment source pointer if sign is negative
	parsebase10(src + sign, &x, &overflow);

	// Make sure it didn't also overflow the i64/u64 range,
	// otherwise just return the correct sign
	if (overflow || x > bound) return (int64_t)bound;
	else return sign ? -(int64_t)x : (int64_t)x; // Apply sign
}

// Auto-generated by gentbl.py, don't touch, regenerate instead.
// Fine table
static const uint64_t ten2e_fine[] = {
	0x8000000000000000, 0xA000000000000000,	// 1e0,  1e1
	0xC800000000000000, 0xFA00000000000000,	// 1e2,  1e3
	0x9C40000000000000, 0xC350000000000000,	// 1e4,  1e5
	0xF424000000000000, 0x9896800000000000,	// 1e6,  1e7
	0xBEBC200000000000, 0xEE6B280000000000,	// 1e8,  1e9
	0x9502F90000000000, 0xBA43B74000000000,	// 1e10, 1e11
	0xE8D4A51000000000, 0x9184E72A00000000,	// 1e12, 1e13
	0xB5E620F480000000, 0xE35FA931A0000000,	// 1e14, 1e15
	0x8E1BC9BF04000000, 0xB1A2BC2EC5000000,	// 1e16, 1e17
	0xDE0B6B3A76400000, 0x8AC7230489E80000,	// 1e18, 1e19
	0xAD78EBC5AC620000, 0xD8D726B7177A8000,	// 1e20, 1e21
	0x878678326EAC9000, 0xA968163F0A57B400,	// 1e22, 1e23
	0xD3C21BCECCEDA100, 0x84595161401484A0,	// 1e24, 1e25
	0xA56FA5B99019A5C8, 0xCECB8F27F4200F3A,	// 1e26, 1e27
};


// Coarse table
static const uint64_t ten2e_coarse[] = {
	0xD953E8624B85DD78, 0xDB71E91432B1A24A,	// 1e-330, 1e-302
	0xDD95317F31C7FA1D, 0xDFBDCECE67006AC9,	// 1e-274, 1e-246
	0xE1EBCE4DC7F16DFB, 0xE41F3D6A7377EECA,	// 1e-218, 1e-190
	0xE65829B3046B0AFA, 0xE896A0D7E51E1566,	// 1e-162, 1e-134
	0xEADAB0ABA3B2DBE5, 0xED246723473E3813,	// 1e-106, 1e-78
	0xEF73D256A5C0F77C, 0xF1C90080BAF72CB1,	// 1e-50,  1e-22
	0xF424000000000000, 0xF684DF56C3E01BC6,	// 1e6,    1e34
	0xF8EBAD2B84E0D58B, 0xFB5878494ACE3A5F,	// 1e62,   1e90
	0xFDCB4FA002162A63, 0x802221226BE55A64,	// 1e118,  1e146
	0x8161AFB94B44F57D, 0x82A45B450226B39C,	// 1e174,  1e202
	0x83EA2B892091E44D, 0x8533285C936B35DE,	// 1e230,  1e258
	0x867F59A9D4BED6C0,			// 1e286
};

typedef struct float_inf {
	uint64_t sig;
	int32_t exp;
	bool sign, overflow;
} float_inf_t;

static float_inf_t hpfmul(const float_inf_t lhs, const float_inf_t rhs) {
	const __uint128_t x = (__uint128_t)lhs.sig * rhs.sig;
	const bool carry = x >> 127;

	return (float_inf_t){
		.sig = x >> (63 + carry),
		.exp = lhs.exp + rhs.exp + carry,
		.sign = lhs.sign,
	};
}

static float_inf_t ten2e(int e) {
#define ten2e_get_exp(e10) (((e10) * 217706) >> 16)
	e += 330;
	int fine = e % ARRLEN(ten2e_fine),
		coarse = e / ARRLEN(ten2e_fine);
	return hpfmul((float_inf_t){
		.sig = ten2e_fine[fine],
		.exp = ten2e_get_exp(fine),
	}, (float_inf_t){
		.sig = ten2e_coarse[coarse],
		.exp = ten2e_get_exp((coarse * ARRLEN(ten2e_fine)) - 330),
	});
}

static double hpf2dbl(float_inf_t f) {
	union { double d; uint64_t u; } cvt;
	cvt.u = (uint64_t)f.sign << 63;
	cvt.u |= ((uint64_t)f.exp + 1023) << 52;
	cvt.u |= (f.sig << 1) >> 12;
	return cvt.d;
}

static double testflt(const float_inf_t *f, int error) {
	// Normalize float info
	const int lz = clz(f->sig);
	float_inf_t out = hpfmul((float_inf_t){
		.sig = f->sig << lz,
		.exp = 63 - lz,
		.sign = f->sign,
	}, ten2e(f->exp));
	const int error_area = out.sig & 0x7FF;

	if (error_area - error <= 0x400 && error_area + error >= 0x400) {
		return 0.0;
	} else if (error_area > 0x400) {
		out.sig = (out.sig & ~0x7FF) + 0x800;
		out.exp += out.sig == 0;
	}

	return hpf2dbl(out);
}

static int parsebase10_all(const char **src, uint64_t *out, bool *overflow) {
	int ndigits = parsebase10(*src, out, overflow);
	
	if (*overflow) {
		*src += ndigits = parsebase10_round(*src, out, overflow);
		for (;**src >= '0' && **src <= '9'; ++*src, ++ndigits);
		return ndigits;
	} else {
		*src += ndigits;
		return ndigits;
	}
}

static void parsefrac(const char **src, float_inf_t *f) {
	uint64_t frac, sig_backup = f->sig;
	const char *src_backup = *src;
	bool overflow;

	++*src;
	const int n = parsebase10_all(src, &frac, &overflow);
	f->overflow |= overflow;

	if (n < ARRLEN(u64pows)
		&& !mul_overflow(f->sig, u64pows[n], &f->sig)
		&& !add_overflow(f->sig, frac, &f->sig)) {
		f->exp -= n;
		return;
	}
	
	// Slow path: multiply by 10 just before we overflow and then round
	f->sig = sig_backup;
	*src = src_backup;
	
	for (int64_t digit = *(*src)++ - '0';
		!mul_overflow(f->sig, 10, &f->sig)
		&& !add_overflow(f->sig, digit, &f->sig);
		digit = *(*src)++ - '0', sig_backup = f->sig, --f->exp);

	f->sig = sig_backup;
	f->sig += (*src)[-1] >= '5';
	for (;**src >= '0' && **src <= '9'; ++*src, --f->exp);
}

static float_inf_t parsefloatinfo(const char *src) {
	float_inf_t f;

	src += f.sign = *src == '-';
	f.exp = 0;

	parsebase10_all(&src, &f.sig, &f.overflow);
	if (*src == '.') parsefrac(&src, &f);
	
	if ((*src & 0x4F) == 'E') {
		bool overflow, sign = *++src == '-';
		uint64_t exp;

		src += sign || *src == '+';
		parsebase10(src, &exp, &overflow);

		if (overflow) f.exp = sign ? INT32_MIN : INT32_MAX;
		else f.exp += sign ? -exp : exp;
	}

	return f;
}

double ejflt(const char *src) {
	static const double exact[23] = {
		1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,
		1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19,
		1e20, 1e21, 1e22,
	};

	float_inf_t f = parsefloatinfo(src);

	if (f.sig == 0 || f.exp < -308) return 0.0;
	else if (f.exp > 308) return f.sign ? -1.0 / 0.0 : 1.0 / 0.0;

	if (f.overflow || f.sig >> 53) {
		if (f.exp >= 0) return testflt(&f, 1);
		return testflt(&f, 4);
	}

	if (f.exp > -23 && f.exp < 23) {
		double computed;

		if (f.exp < 0) computed = (double)f.sig / exact[-f.exp];
		else computed = (double)f.sig * exact[f.exp];

		return f.sign ? -computed : computed;
	} else if (f.exp >= 0) {
		return testflt(&f, 0);
	} else {
		return testflt(&f, 3);
	}
}

// Returns whether the boolean is true or false
bool ejbool(const char *tok_start) {
	// Since tokens are already validated, this is all that is needed
	return *tok_start == 't';
}

