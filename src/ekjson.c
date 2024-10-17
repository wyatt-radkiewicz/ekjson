#include "ekjson.h"

// Makes a u32 literal out of a list of characters (little endian)
#define STR2U32(A, B, C, D) ((A) | ((B) << 8) | ((C) << 16) | ((D) << 24))

// Always inline macros
#ifdef __GNUC__
#define EKJSON_ALWAYS_INLINE __attribute__((always_inline))
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

// Wrappers for loading u32/u64 on unaligned addresses
static EKJSON_ALWAYS_INLINE uint32_t ldu32_unaligned(const void *const buf) {
	const uint8_t *const bytes = buf;
	return (uint32_t)bytes[0]
		| (uint32_t)bytes[1] << 8
		| (uint32_t)bytes[2] << 16
		| (uint32_t)bytes[3] << 24;
}
static EKJSON_ALWAYS_INLINE uint64_t ldu64_unaligned(const void *const buf) {
	const uint8_t *const bytes = buf;
	return (uint64_t)bytes[0] | (uint64_t)bytes[1] << 8
		| (uint64_t)bytes[2] << 16 | (uint64_t)bytes[3] << 24
		| (uint64_t)bytes[4] << 32 | (uint64_t)bytes[5] << 40
		| (uint64_t)bytes[6] << 48 | (uint64_t)bytes[7] << 56;
}

static uint8_t hex2num(const uint8_t hex) {
	static const uint8_t table[] = {
		['0'] = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
		['a'] = 0xA, 0xB, 0xC, 0xD, 0xE, 0xF,
		['A'] = 0xA, 0xB, 0xC, 0xD, 0xE, 0xF,
	};
	return table[hex];
}

static uint32_t str2hex(const char *src) {
	return (uint32_t)hex2num(src[0]) << 12
		| (uint32_t)hex2num(src[1]) << 8
		| (uint32_t)hex2num(src[2]) << 4
		| (uint32_t)hex2num(src[3]);
}

static size_t hex2utf8(const char *src, char out[static const 4]) {
	const uint32_t hi = str2hex(src);
	if (hi < 0x80) {
		out[0] = hi;
		return 1;
	} else if (hi < 0x800) {
		out[0] = 0xC0 | (hi >> 6);
		out[1] = 0x80 | (hi & 0x3F);
		return 2;
	} else if (hi < 0xD800 || hi > 0xDFFF) {
		out[0] = 0xE0 | (hi >> 12);
		out[1] = 0x80 | (hi >> 6 & 0x3F);
		out[2] = 0x80 | (hi & 0x3F);
		return 3;
	} else {
		if (hi > 0xDBFF) return 0;
		if (src[4] != '\\' && src[5] != 'u') return 0;

		const uint32_t lo = str2hex(src + 6);
		if (lo < 0xDC00 || lo > 0xDFFF) return 0;

		const uint32_t final =
			((hi - 0xD800) << 10) + (lo - 0xDC00) + 0x10000;
		out[0] = 0xF0 | (final >> 18);
		out[1] = 0x80 | (final >> 12 & 0x3F);
		out[2] = 0x80 | (final >> 6 & 0x3F);
		out[3] = 0x80 | (final & 0x3F);
		return 4;
	}
}

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
		{ 10,  9,  5,  5,  9,  6,  9 }, // Fraction (second part)

		{  9,  9,  7,  7,  9,  9,  7 }, // Exponent (+/-)
		{  9,  9,  8,  8,  9,  9,  9 }, // Exponent (first digit)
		{ 10,  9,  8,  8,  9,  9,  9 }, // Exponent (rest of digits)
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
			0xA, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0xA, 0xA, 0x9, 0x9, 0xA, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0xA, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0xA, 0x9, 0x4, 0x9,
			0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5, 0x5,
			0x5, 0x5, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
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
			0xA, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0xA, 0xA, 0x9, 0x9, 0xA, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0xA, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0xA, 0x9, 0x9, 0x9,
			0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8, 0x8,
			0x8, 0x8, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0xA, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9, 0x9,
			0x9, 0x9, 0x9, 0x9, 0x9, 0xA, 0x9, 0x9,
			FILLCODESPACE(0x9)
		},
	};
#endif

	// Add token
	ejtok_t *const tok = addtok(state, EJNUM);

	// Create local copy for speed
	const char *src = state->src;

	// Use dfa to quickly validate the number without having to parse it
	// fully and correctly
	int s = 0;
	while (s < 9) {
#if EKJSON_SPACE_EFFICENT
		s = transitions[s][groups[(uint8_t)(*src++)]];
#else
		s = transitions[s][(uint8_t)(*src++)];
#endif
	}

	// Restore the source pointer to the first different char
	state->src = src - 1;

	// Return error code if dfa state is in the invalid (9) state
	return s == 10 ? tok : NULL;
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

static bool escape(const char **src, char **out,
		char **end, size_t *len) {
	if (*++*src != 'u') {
		if (*out != *end) *(*out)++ = **src;
		++*src, ++*len;
		return true;
	}

	char utf8[4];
	const size_t u8len = hex2utf8(++*src, utf8);
	if (u8len == 0) {
		return 0;
	} else if (*out + u8len < *end) {
		char *tmp = utf8;
		switch (u8len) {
		case 4: *(*out)++ = *tmp++;
		case 3: *(*out)++ = *tmp++;
		case 2: *(*out)++ = *tmp++;
		case 1: *(*out)++ = *tmp++;
		}
	} else {
		*end = *out;
	}

	*src += u8len == 4 ? 6+4 : 4;
	*len += u8len;
	return true;
}

size_t ejstr(const char *src, char *out, const size_t outlen) {
	char *end = outlen ? (out ? out + outlen - 1 : NULL) : out;

	++src;
	size_t len = 1;
	while (*src != '"') {
		if (*src == '\\') {
			if (!escape(&src, &out, &end, &len)) return 0;
		} else {
			if (out < end) *out++ = *src;
			++src, ++len;
		}
	}

	if (out) *out = '\0';
	return len;
}

