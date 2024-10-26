#include "ekjson.h"

// Makes a u32 literal out of a list of characters (little endian)
#define STR2U32(A, B, C, D) ((A) | ((B) << 8) | ((C) << 16) | ((D) << 24))

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
	long long: __builtin_saddll_overflow(x, y, (long long *)out), \
	long: __builtin_saddl_overflow(x, y, (long *)out), \
	int: __builtin_sadd_overflow(x, y, (int *)out))
// Returns true if the signed multiplication overflowed or underflowed
#define mul_overflow(x, y, out) _Generic(x + y, \
	long long: __builtin_smulll_overflow(x, y, (long long *)out), \
	long: __builtin_smull_overflow(x, y, (long *)out), \
	int: __builtin_smul_overflow(x, y, (int *)out))
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
#if EKJSON_SPACE_EFFICENT
	// Space compact tables
	static const uint8_t groups[256] = {
		['\\'] = 1,
		['u'] = 2,
		['0'] = 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
		['a'] = 3, 3, 3, 3, 3, 3,
		['A'] = 3, 3, 3, 3, 3, 3,
		['"'] = 4,
		['\0'] = 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
		5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	};
	static const uint8_t transitions[][8] = {
		{ 0, 1, 0, 0, 7, 6 }, // Normal string
		{ 0, 0, 2, 0, 0, 6 }, // Found escape char '\\'
		{ 6, 6, 6, 3, 6, 6 }, // utf hex
		{ 6, 6, 6, 4, 6, 6 }, // utf hex
		{ 6, 6, 6, 5, 6, 6 }, // utf hex
		{ 6, 6, 6, 0, 6, 6 }, // utf hex
	};
#else
	// Big tables, but with 1 less level of indirection
	// Every array in the table is a state, and every byte in those arrays
	// specify transitions to be made depending on a source character to
	// another state
	static const uint8_t transitions[][256] = {
		// Normal string
		{
			['\0'] = 6,
			['\\'] = 1,
			['"'] = 7,
		},
		// Escape
		{
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 0, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 0,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 0, 6, 6, 6,
			6, 6, 0, 6, 6, 6, 0, 6, 6, 6, 6, 6, 6, 6, 0, 6,
			6, 6, 0, 6, 0, 2, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			FILLCODESPACE(6)
		},
		// UTF Hex Digit 1
		{
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 6, 6, 6, 6, 6, 6,
			6, 3, 3, 3, 3, 3, 3, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 3, 3, 3, 3, 3, 3, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			FILLCODESPACE(6)
		},
		// UTF Hex Digit 2
		{
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 6, 6, 6, 6,
			6, 4, 4, 4, 4, 4, 4, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 4, 4, 4, 4, 4, 4, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			FILLCODESPACE(6)
		},
		// UTF Hex Digit 3
		{
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6,
			6, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			FILLCODESPACE(6)
		},
		// UTF Hex Digit 4
		{
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 6, 6, 6, 6, 6,
			6, 0, 0, 0, 0, 0, 0, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 0, 0, 0, 0, 0, 0, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			FILLCODESPACE(6)
		},
	};
#endif

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
	int s = 0;
	do {
#if EKJSON_SPACE_EFFICENT
		s = transitions[s][groups[(uint8_t)(*src++)]];
#else
		s = transitions[s][(uint8_t)(*src++)];
#endif
	} while (s < 6);

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
	return s == 7 ? tok : NULL;
}

// Parse number
// Adds token to state variable
// Leaves state source pointer at the first non-num character
// Returns NULL if error occurred
static EKJSON_INLINE ejtok_t *number(state_t *const state) {
#if EKJSON_SPACE_EFFICENT
	// Same kind of table as described in the string parsing function
	static const uint8_t groups[256] = {
		['-'] = 1, ['0'] = 2, ['1'] = 3, 3, 3, 3, 3, 3, 3, 3, 3,
		['.'] = 4, ['e'] = 5, ['E'] = 5, ['+'] = 6,
	};
	static const uint8_t transitions[][8] = {
		{  9,  1,  2,  3,  9,  9,  9 }, // Initial checks

		{  9,  9,  2,  3,  9,  9,  9 }, // Negative sign
		{ 10,  9,  9,  9,  4,  6,  9 }, // Initial zero
		{ 10,  9,  3,  3,  4,  6,  9 }, // Digits

		{  9,  9,  5,  5,  9,  9,  9 }, // Fraction (first part)
		{ 11,  9,  5,  5,  9,  6,  9 }, // Fraction (second part)

		{  9,  9,  7,  7,  9,  9,  7 }, // Exponent (+/-)
		{  9,  9,  8,  8,  9,  9,  9 }, // Exponent (first digit)
		{ 11,  9,  8,  8,  9,  9,  9 }, // Exponent (rest of digits)
	};
#else
	// Same kind of table as described in the string parsing function
	static const uint8_t transitions[][256] = {
		// Initial checks
		{
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x1, 0x9, 0x9,
			0x2, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3,
			0x3, 0x3, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			FILLCODESPACE(0x9)
		},
		// Negative sign
		{
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x2, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3,
			0x3, 0x3, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			FILLCODESPACE(0x9)
		},
		// Initial zero
		{
			0xA, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0xA, 0xA, 0x9, 0x9, 0xA, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0xA, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0xA, 0x9, 0x4, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x6, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0xA, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0xA, 0x9, 0x9,
			FILLCODESPACE(0x9)
		},
		// Digits
		{
			0xA, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0xA, 0xA, 0x9, 0x9, 0xA, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0xA, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0xA, 0x9, 0x4, 0x9,
			0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3,
			0x3, 0x3, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x6, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0xA, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x6, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0xA, 0x9, 0x9,
			FILLCODESPACE(0x9)
		},
		// Fraction (first part)
		{
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5,
			0x5, 0x5, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			FILLCODESPACE(0x9)
		},
		// Fraction (second part)
		{
			0xB, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0xB, 0xB, 0x9, 0x9, 0xB, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0xB, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0xB, 0x9, 0x4, 0x9,
			0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5,
			0x5, 0x5, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x6, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0xB, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x6, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0xB, 0x9, 0x9,
			FILLCODESPACE(0x9)
		},
		// Exponent (+/-)
		{
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x7, 0x9, 0x7, 0x9, 0x9,
			0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8,
			0x8, 0x8, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			FILLCODESPACE(0x9)
		},
		// Exponent (first digit)
		{
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8,
			0x8, 0x8, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			FILLCODESPACE(0x9)
		},
		// Exponent (rest of digits)
		{
			0xB, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0xB, 0xB, 0x9, 0x9, 0xB, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0xB, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0xB, 0x9, 0x9, 0x9,
			0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8,
			0x8, 0x8, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0xB, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0xB, 0x9, 0x9,
			FILLCODESPACE(0x9)
		},
	};
#endif

	// Add token
	ejtok_t *const tok = addtok(state, EJINT);

	// Create local copy for speed
	const char *src = state->src;

	// Use dfa to quickly validate the number without having to parse it
	// fully and correctly
	int s = 0;
	while (s < 9) {
		// Get next state using current state
#if EKJSON_SPACE_EFFICENT
		s = transitions[s][groups[(uint8_t)(*src++)]];
#else
		s = transitions[s][(uint8_t)(*src++)];
#endif
	}

	// Update token type if it is a float
	tok->type = s == 0xB ? EJFLT : tok->type;

	// Restore the source pointer to the first different char
	state->src = src - 1;

	// Return error code if dfa state is in the invalid (9) state
	return s == 0x9 ? NULL : tok;
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

double ejflt(const char *src) {
	return 0.0;
}

// Returns the number token parsed as an int64_t. If there are decimals, it
// just returns the number truncated towards 0. If the number is outside of
// the int64_t range, it will saturate it to the closest limit.
int64_t ejint(const char *src) {
#if EKJSON_NO_BITWISE
	// Get the sign of the number and skip the negative char if nessesary
	int64_t sign = *src == '-' ? -1 : 1, x = 0;
	src += *src == '-';

	// Loop through all the digits
	while (*src >= '0' && *src <= '9') {
		const int64_t d = *src++ - '0'; // Convert char to number

		// Shift current value up by a decimal place
		if (mul_overflow(x, 10, &x)) goto overflow; // Check overflow

		// Add the new ones place we found
		if (add_overflow(x, d * sign, &x)) goto overflow;
	}

	return x;
overflow: // Check are sign to see what limit to saturate to
	return sign == -1 ? INT64_MIN : INT64_MAX;
#else
	// Precalculate all the powers of 10 needed for float/int parsers
	static const int64_t pows[9] = {
		1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000,
	};

	// Get the sign of the number
	const int64_t sign = *src == '-' ? -1 : 1;
	int64_t x, tmp; // Our num. we are making, and current number bit (tmp)
	int n;	// The number of right chars in the 8-byte sequence we parsed

	src += *src == '-';		// Skip the negative char if nessesary
	
	// Parse first 1-8 bytes of the number. If the number is 7 bytes or
	// less, then we can be sure that we are done.
	if (parsedigits8(src, (uint64_t *)&x) < 8) {
		return x * sign;	// Make sure to apply sign
	}

	// Parse next 8 byte section (this also accounts for the case that
	// the number is truely a 8 byte number. So this section might have
	// no number in it at all). We also apply the sign in this section
	src += 8;			// Skip past the 8 bytes we parsed
	n = parsedigits8(src, (uint64_t *)&tmp); // Put next 8 bytes into tmp
	
	// Make 'room' for the new digits we are adding by shifting the old
	// ones by n number of decimal places
	x *= pows[n] * sign;		// Apply sign
	x += tmp * sign; // Add the numbers we parsed and apply sign to them
	
	if (n < 8) return x; // Return if we're sure we're at the end

	// Since int64_t can hold 16 digit values easily, we have to now check
	// for overflow since we're going over that.
	src += 8;			// Skip past the 8 bytes we parsed
	n = parsedigits8(src, (uint64_t *)&tmp); // Put next 8 bytes into tmp
	
	// Do the same as above but check if we overflowed
	if (mul_overflow(x, pows[n], &x) || add_overflow(x, sign * tmp, &x)) {
		// Saturate to the limit corresponding to the sign
		return sign == -1 ? INT64_MIN : INT64_MAX;
	} else {
		return x;	// Otherwise our x is already good
	}
#endif
}

// Returns whether the boolean is true or false
bool ejbool(const char *tok_start) {
	// Since tokens are already validated, this is all that is needed
	return *tok_start == 't';
}

