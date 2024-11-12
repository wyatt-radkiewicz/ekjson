#include <limits.h>

#include "ekjson.h"

// Makes a u32 literal out of a list of characters (little endian)
#define STR2U32(A, B, C, D) ((A) | ((B) << 8) | ((C) << 16) | ((D) << 24))
#define ARRLEN(A) (sizeof(A) / sizeof((A)[0]))

// GNUC specific macros
#ifdef __GNUC__
#define EKJSON_ALWAYS_INLINE inline __attribute__((always_inline))
#define EKJSON_NO_INLINE __attribute__((noinline))
#define EKJSON_EXPECT(X, Y) __builtin_expect((X), (Y))
#else
#define EKJSON_ALWAYS_INLINE
#define EKJSON_NO_INLINE
#define EKJSON_EXPECT(X, Y) (X)
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

#if !EKJSON_SPACE_EFFICENT // No big tables in space efficient mode
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
// Counts the number of leading zeros in the number (starts from 63rd bit)
static EKJSON_ALWAYS_INLINE uint64_t clz(uint64_t x) {
	// Set all bits after the most significant set bit too
	x |= x >> 1, x |= x >> 2,  x |= x >> 4;
	x |= x >> 8, x |= x >> 16, x |= x >> 32;
	
	// Flip the bits to get leading zeros
	x = ~x;

	// Count bits set
	// https://graphics.stanford.edu/~seander/bithacks.html
	uint64_t c = x - ((x >> 1) & 0x5555555555555555);
	c = ((c >> 1) & 0x3333333333333333) + (c & 0x3333333333333333);
	c = ((c >> 2) + c) & 0x0F0F0F0F0F0F0F0F;
	c = ((c >> 4) + c) & 0x00FF00FF00FF00FF;
	c = ((c >> 8) + c) & 0x0000FFFF0000FFFF;
	c = ((c >> 16) + c) & 0x00000000FFFFFFFF;
	return c;
}
#endif // __GNUC__

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

#if defined(BIGINT_MAXWIDTH) && BIGINT_MAXWIDTH >= EKJSON_MAX_SIG
typedef unsigned _BitInt(EKJSON_MAX_SIG) bigint_t;

// Shifts 'x' 'n' bits left. Returns true if an overflow occured
static bool bigint_shl(bigint_t *x, const uint32_t n) {
	// TODO: Check for overflow
	*x <<= n;
	return false;
}

// Gets most significant 64 bits where the integer returned has the most
// significant bit from the big int starting as the msb of the 64 bit int.
// Rounds up for lower bits. Sets pos to what direction the result was shifted
// to to normalize it
static uint64_t bigint_ms64(const bigint_t *x, int32_t *pos) {
	bigint_t y = *x;
	
	*pos = 0;
	if (y == 0) {
		return 0;
	} else if (y >> 64) {
		while (y >> 64) {
			y >>= 1;
			++*pos;
		}
	} else {
		while (y & (1ull << 63)) {
			y <<= 1;
			--*pos;
		}
	}

	return (uint64_t)y;
}

// Compare 2 bit ints. (sign of x - y)
static int bigint_cmp(const bigint_t *x, const bigint_t *y) {
	if (*x == *y) return 0;
	else if (*x < *y) return -1;
	else return 1;
}

// Returns true if the bitint overflowed
static bool bigint_add32(bigint_t *x, uint64_t y) {
	// TODO: Check for overflow
	*x += y;
	return false;
}

// Raises out to the power of 10 and returns if an overflow occurred
static EKJSON_ALWAYS_INLINE bool bigint_pow10(bigint_t *out, uint32_t e) {
	// Representation of powers of 10
	static const uint64_t pows[] = {
		1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull,
		10000000ull, 100000000ull, 1000000000ull, 10000000000ull,
		100000000000ull, 1000000000000ull, 10000000000000ull,
		100000000000000ull, 1000000000000000ull, 10000000000000000ull,
		100000000000000000ull, 1000000000000000000ull,
		10000000000000000000ull,
	};
	while (e >= ARRLEN(pows) - 1) {
		// TODO: Check for overflow
		*out *= pows[ARRLEN(pows) - 1];
		e -= ARRLEN(pows) - 1;
	}
	*out *= pows[e];
	return false;
}

// Sets a big int to the data of a u64
static EKJSON_ALWAYS_INLINE void bigint_set64(bigint_t *out, uint64_t x) {
	*out = x;
}

// Returns true if the bigint is 0
static EKJSON_ALWAYS_INLINE bool bigint_iszero(const bigint_t *x) {
	return *x == 0;
}
#else
// Used in the slow path of ejflt parser to compare really big ints (> 2^1024)
typedef struct bigint {
	uint32_t len;
	uint32_t dgts[EKJSON_MAX_SIG / 32];
} bigint_t;

// Shift each digit in x left. Returns true if overflow occurred
static bool shiftdigits(bigint_t *x, const uint32_t n) {
	if (n == 0) return false;
	if ((x->len += n) > ARRLEN(x->dgts)) return true;
	uint32_t *to = x->dgts + x->len - 1;
	for (uint32_t *from = to - n; from >= x->dgts; *to-- = *from--);
	for (; to >= x->dgts; *to-- = 0);
	return false;
}

// Shifts 'x' 'n' bits left. Returns true if an overflow occured
static bool bigint_shl(bigint_t *x, uint32_t n) {
	if (x->len == 0 || n == 0) return false;	// Nothing to shift
	if (shiftdigits(x, n / 32)) return true;	// Overflow happened
	if ((n %= 32) == 0) return false;		// Shift was on bound
	
	// Shift every bit now
	uint64_t carry = 0;
	for (int i = 0; i < x->len; i++) {
		uint64_t res = x->dgts[i] << n;
		x->dgts[i] = res | carry;
		carry = res >> 32;
	}

	if (!carry) return false;
	if (x->len + 1 >= ARRLEN(x->dgts)) return true;
	x->dgts[x->len++] = carry;
	return false;
}

// Gets most significant 64 bits where the integer returned has the most
// significant bit from the big int starting as the msb of the 64 bit int.
// Rounds up for lower bits. Sets pos to what direction the result was shifted
// to to normalize it
static uint64_t bigint_ms64(const bigint_t *x, int32_t *pos) {
	if (x->len == 0) { // Return 0 if x is 0
		*pos = -63;
		return 0;
	}

	// Get the msb digit
	uint64_t y = (uint64_t)x->dgts[x->len - 1] << 32;

	// Find first 1 in the digit
	const int offs = clz(y);

	y <<= offs;	// Add the other (optional) parts
	if (x->len > 1) y |= (uint64_t)x->dgts[x->len - 2] << (offs);

	// Get rounding bit
	if (x->len > 2) {
		uint64_t last = (uint64_t)x->dgts[x->len - 3] >> (31 - offs);
		y |= last >> 1;	// Don't use 33rd bit
		y += last & 1;	// Roudn up if the bit after last is 1
	}

	// Get what we had to shift (shift right is positive) to normalize this
	*pos = x->len * 32 - 64 - offs;
	return y;
}

// Compare 2 bit ints. (sign of x - y)
static int bigint_cmp(const bigint_t *x, const bigint_t *y) {
	// If they have not equal lengths then just measure lengths
	if (x->len != y->len) return x->len > y->len ? 1 : -1;

	// Else we have to check each digit until there isnt a match
	for (uint32_t i = x->len - 1; i < x->len; i--) {
		if (x->dgts[i] != y->dgts[i]) {
			return x->dgts[i] > y->dgts[i] ? 1 : -1;
		}
	}

	return 0;	// Exact match, they're equal
}

// Returns true if the bitint overflowed
static bool bigint_add32(bigint_t *x, uint64_t y) {
	// Add to each current digit, being mindful of carrying
	for (uint32_t i = 0; i < x->len; i++) {
		y += x->dgts[i];
		x->dgts[i] = (uint32_t)y;
		y >>= 32;
	}

	if (y == 0) return false;			// No carry
	if (x->len == ARRLEN(x->dgts)) return true;	// Carry, but overflows
	x->dgts[x->len++] = y;				// Add carry
	return false;
}

// Multiplies against 1 digit
static bool bigint_mul32(bigint_t *x, const uint64_t y) {
	// Multiply each current digit, being mindful of carrying
	uint64_t carry = 0;
	for (uint32_t i = 0; i < x->len; i++) {
		carry += x->dgts[i] * y;
		x->dgts[i] = (uint32_t)carry;
		carry >>= 32;
	}

	if (carry == 0) return false;			// No carry
	if (x->len == ARRLEN(x->dgts)) return true;	// Carry, but overflows
	x->dgts[x->len++] = carry;			// Add carry
	return false;
}

// Raises out to the power of 10 and returns if an overflow occurred
static bool bigint_pow10(bigint_t *out, uint32_t e) {
	// Bigint representation of powers of 10 in 1 digit
	static const uint32_t pows[] = {
		1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u,
		100000000u, 1000000000u,
	};
	while (e >= ARRLEN(pows) - 1) {
		if (bigint_mul32(out, pows[ARRLEN(pows) - 1])) {
			return true;
		}
		e -= ARRLEN(pows) - 1;
	}
	if (bigint_mul32(out, pows[e])) return true;
	return false;
}

// Sets a big int to the data of a u64
static EKJSON_ALWAYS_INLINE void bigint_set64(bigint_t *out, uint64_t x) {
	out->dgts[0] = x;
	out->dgts[1] = x >> 32;
	out->len = out->dgts[1] ? 2 : !!out->dgts[0];
}

// Returns true if the bigint is 0
static EKJSON_ALWAYS_INLINE bool bigint_iszero(const bigint_t *x) {
	return x->len == 0;
}
#endif // BITINT_MAXWIDTH

#define FLTNAN (0.0 / 0.0) // Quiet nan
#define FLTINF (1.0 / 0.0) // Infinity
#define NOTNAN(X) ((X) == (X))

// Used to create doubles using ieee754 representation
typedef union bitdbl {
	double d;			// To get the double as a double
	struct {
		uint64_t m : 52;	// 52 bit (implicit 1) mantissa
		uint64_t e : 11;	// 11 bit (+1023 biased) exponent
		uint64_t s : 1;		// Sign (true if negative)
	} u;				// To set bits in the double
} bitdbl_t;

// Returns mantissa of bit double with implicit 1 added
static EKJSON_ALWAYS_INLINE uint64_t bitdbl_sig(const bitdbl_t x) {
	return x.u.m + (1ull << 52);
}

// Goes to closest 'previous' double
static EKJSON_ALWAYS_INLINE void bitdbl_prev(bitdbl_t *x) {
	x->u.e -= !x->u.m--;	// Decrement exponent if mantissa underflows
}

// Goes to closest 'next' double
static EKJSON_ALWAYS_INLINE void bitdbl_next(bitdbl_t *x) {
	x->u.e += !++x->u.m;	// Increment exponent if mantissa overflows
}

// High precision float used in ejflt. No sign information, will be passed
// in other arguments
typedef struct flt {
	uint64_t mant;	// Normalized mantissa
	int32_t e;	// Exponent
} flt_t;

// Convert 96 bit float to double with no error checking or tieing to even
// Requires a ulperr or error in units of last precision (of 64 bits that is)
// so that it can return false if it is an ambigious conversion
static bool flt_dbl(flt_t flt, const int ulperr,
		const bool sign, bitdbl_t *out) {
	// Shift least significant bits off for mantissa and remove leading 1
	out->u.m = flt.mant >> 11;
	out->u.e = flt.e + 1023;	// Exponent with added bias
	out->u.s = sign;

	// Now we get the bits after the last position and see if we
	// are within range of '0.5'. If we are, then we must go slow
	// path. (PS. 11 bits is how many we have after the lp)
	const int lowbits = flt.mant & 0x7FF;
	const int half = 0x400;			// 0.5 units in ulp
	const bool safe = lowbits - ulperr > half || lowbits + ulperr < half;
	
	// Round up if nessesary (don't if this is a half way case)
	const bool rnd = lowbits > half && safe;
	out->u.m += rnd;			// Actually add 1 to ulp
	flt.e += out->u.m == 0 && rnd;		// Normalize (inc exponent)
	
	// Return false if we are in the 1/2 range
	return safe;
}

// Multiple 2 64bit mantissas and get high 64 bits back.
// Since they are mantissas and normalized, they both have high bit set
#if defined(__GNUC__) && defined(__SIZEOF_INT128__)
static flt_t flt_mul(const flt_t x, const flt_t y) {
	// Use 128 bit int to multiply
	const __uint128_t a = (__uint128_t)x.mant * y.mant;

	// See if result carried so we can normalize the result
	const bool carried = a >> 127;
	const bool round = a & 1ull << (63 - carried);

	// Create unrounded result
	flt_t flt = (flt_t){
		.mant = a >> (63 + carried),
		.e = x.e + y.e + carried,
	};

	// Check for overflow when rounding up
	flt.e += (flt.mant += round) == 0;
	flt.mant |= (1ull << 63);
	return flt;
}
#else
static flt_t flt_mul(const flt_t x, const flt_t y) {
	// Get cross multiplies of each
	const uint64_t lx_ly = (x & 0xFFFFFFFF)	* (y & 0xFFFFFFFF);
	const uint64_t lx_hy = (x & 0xFFFFFFFF)	* (y >> 32);
	const uint64_t hx_ly = (x >> 32)	* (y & 0xFFFFFFFF);
	const uint64_t hx_hy = (x >> 32)	* (y >> 32);
	const bool carried = hx_ly >> 63; // See if we carried

	// Get high 64 bits
	uint64_t mant = hx_hy + (lx_hy >> 32) + (hx_ly >> 32);
	mant <<= !carried; // Normalize result

	// Shift in last bit (if we have to)
	mant |= (lx_ly + (lx_hy << 32) + (hx_ly << 32)) & carried;

	// Create unrounded result
	flt_t flt = (flt_t){
		.mant = mant,
		.e = x.e + y.e + carried,
	};

	// Check for overflow when rounding up
	flt.e += (flt.mant += round) == 0;
	flt.mant |= (1ull << 63);
	return flt;
}
#endif

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
#if EKJSON_SPACE_EFFICENT
	// Edge table
	static const uint8_t edges[256] = {
		[0] = 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
			1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		['"'] = 2, ['/'] = 3,
		['0'] = 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
		['A'] = 4, 4, 4, 4, 4, 4,
		['\\'] = 5, ['a'] = 4, ['b'] = 6,
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
#else // EKJSON_SPACE_EFFICENT
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
#endif // EKJSON_SPACE_EFFICENT

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
#if EKJSON_SPACE_EFFICENT
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
#else // EKJSON_SPACE_EFFICENT
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
#endif // EKJSON_SPACE_EFFICENT

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
	// Create initial state. Set end to 1 minus the end since the functions
	// in ejparse will overwrite at most 1 over the buffer given to it.
	// This is done because its faster. :/
	state_t state = {
		.base = src, .src = src,
		.tbase = t, .tend = t + nt - 1, .t = t,
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

// Parses up to 8 digits and writes it to the out pointer. It how many of the
// 8 bytes in this part of the string make up the number starting at the string
static int EKJSON_INLINE parsedigits8(const char *src, uint64_t *const out) {
#if EKJSON_NO_BITWISE
	*out = 0;
	if (*src < '0' || *src > '9') return 0;
	*out = *src++ - '0';
	if (*src < '0' || *src > '9') return 1;
	*out *= 10, *out += *src++ - '0';
	if (*src < '0' || *src > '9') return 2;
	*out *= 10, *out += *src++ - '0';
	if (*src < '0' || *src > '9') return 3;
	*out *= 10, *out += *src++ - '0';
	if (*src < '0' || *src > '9') return 4;
	*out *= 10, *out += *src++ - '0';
	if (*src < '0' || *src > '9') return 5;
	*out *= 10, *out += *src++ - '0';
	if (*src < '0' || *src > '9') return 6;
	*out *= 10, *out += *src++ - '0';
	if (*src < '0' || *src > '9') return 7;
	*out *= 10, *out += *src++ - '0';
	return 8;
#else
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
#endif
}

// Parses a stream of base10 digits
// Returns number of chars parsed, if overflow, returns 0 chars parsed
static EKJSON_INLINE int parsebase10(const char *src, uint64_t *const out) {
	// Powers of 10 to shift by
	static const uint64_t pows[9] = {
		1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull,
		1000000ull, 10000000ull, 100000000ull,
	};

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
	*out = *out * pows[n] + tmp;
	if (n < 8) return n + 8; // Return if we're sure we're at the end

	// Since uint64_t can hold 16 digit values easily, we have to now check
	// for overflow since we're going over that.
	n = parsedigits8(src + 16, &tmp); // Put next 8 bytes into tmp
	
	// Do the same as above but check if we overflowed
	bool ovf = mul_overflow(*out, pows[n], out);
	ovf |= add_overflow(*out, tmp, out);
	return (n + 16) * !ovf; // Return # of digits parsed or 0 if overflow
}

// Returns the number token parsed as an int64_t. If there are decimals, it
// just returns the number truncated towards 0. If the number is outside of
// the int64_t range, it will saturate it to the closest limit.
int64_t ejint(const char *const src) {
	// What the sign of the number is
	const bool sign = *src == '-';

	// The bound for the sign of the number
	uint64_t bound = (uint64_t)INT64_MAX + sign, x;

	// Make sure it didn't also overflow the i64/u64 range,
	// otherwise just return the correct sign
	if (!parsebase10(src + sign, &x) || x > bound) return (int64_t)bound;
	else return sign ? -(int64_t)x : (int64_t)x; // Apply sign
}

// Auto-generated by gentbl.py, don't touch, regenerate instead.
#define MANT_FINE_RANGE 16
#define MANT_COARSE_MIN -330
#define MANT_COARSE_MAX 310

// Finds approximate 10^(e10) for a high precision float
static flt_t ten2e(int32_t e10) {
// Auto-generated by gentbl.py, don't touch, regenerate instead.
#if EKJSON_SPACE_EFFICENT
	// Fine table
	static const uint64_t mant_fine[] = {
		0x8000000000000000, 0xA000000000000000,	// 1e0, 1e1
		0xC800000000000000, 0xFA00000000000000,	// 1e2, 1e3
		0x9C40000000000000, 0xC350000000000000,	// 1e4, 1e5
		0xF424000000000000, 0x9896800000000000,	// 1e6, 1e7
		0xBEBC200000000000, 0xEE6B280000000000,	// 1e8, 1e9
		0x9502F90000000000, 0xBA43B74000000000,	// 1e10, 1e11
		0xE8D4A51000000000, 0x9184E72A00000000,	// 1e12, 1e13
		0xB5E620F480000000, 0xE35FA931A0000000,	// 1e14, 1e15
	};
	
	// Coarse table
	static const uint64_t mant_coarse[] = {
		0xD953E8624B85DD78, 0xF148440A256E2C76,	// 1e-330, 1e-314
		0x85F0468293F0EB4E, 0x94B3A202EB1C3F39,	// 1e-298, 1e-282
		0xA5178FFF668AE0B6, 0xB749FAED14125D36,	// 1e-266, 1e-250
		0xCB7DDCDDA26DA268, 0xE1EBCE4DC7F16DFB,	// 1e-234, 1e-218
		0xFAD2A4B13D1B5D6C, 0x8B3C113C38F9F37E,	// 1e-202, 1e-186
		0x9A94DD3E8CF578B9, 0xAB9EB47C81F5114F,	// 1e-170, 1e-154
		0xBE89523386091465, 0xD389B47879823479,	// 1e-138, 1e-122
		0xEADAB0ABA3B2DBE5, 0x825ECC24C873782F,	// 1e-106, 1e-90
		0x90BD77F3483BB9B9, 0xA0B19D2AB70E6ED6,	// 1e-74, 1e-58
		0xB267ED1940F1C61C, 0xC612062576589DDA,	// 1e-42, 1e-26
		0xDBE6FECEBDEDD5BE, 0xF424000000000000,	// 1e-10, 1e6
		0x878678326EAC9000, 0x96769950B50D88F4,	// 1e22, 1e38
		0xA70C3C40A64E6C51, 0xB975D6B6EE39E436,	// 1e54, 1e70
		0xCDE6FD5E09ABCF26, 0xE498F455C38B997A,	// 1e86, 1e102
		0xFDCB4FA002162A63, 0x8CE2529E2734BB1D,	// 1e118, 1e134
		0x9C69A97284B578D7, 0xADA72CCC20054AE9,	// 1e150, 1e166
		0xC0CB28A98FCF3C7F, 0xD60B3BD56A5586F1,	// 1e182, 1e198
		0xEDA2EE1C7064130C, 0x83EA2B892091E44D,	// 1e214, 1e230
		0x92746B9BE2F8552C, 0xA298F2C501F45F42,	// 1e246, 1e262
		0xB484F9DC9641E9DA, 0xC86AB5C39FA63440,	// 1e278, 1e294
	};

	// Bias the exponent for the coarse range
	e10 -= MANT_COARSE_MIN;

	// Use fine and coarse because 10^(x*y) = 10^x*10^y
	int fine = e10 % MANT_FINE_RANGE, coarse = e10 / MANT_FINE_RANGE;

	// Eq. is 2^(e2) = 10^(e10), solve it urself, you'll get linear exp.
	return flt_mul(
		(flt_t){ .mant = mant_fine[fine],
			.e = (fine * 217706) >> 16 },
		(flt_t){ .mant = mant_coarse[coarse],
			.e = ((e10 - fine + MANT_COARSE_MIN) * 217706) >> 16 }
	);
#else // EKJSON_SPACE_EFFICENT
	static const uint64_t mant[] = {
		0xD953E8624B85DD78, 0x87D4713D6F33AA6B,	// 1e-330, 1e-329
		0xA9C98D8CCB009506, 0xD43BF0EFFDC0BA48,	// 1e-328, 1e-327
		0x84A57695FE98746D, 0xA5CED43B7E3E9188,	// 1e-326, 1e-325
		0xCF42894A5DCE35EA, 0x818995CE7AA0E1B2,	// 1e-324, 1e-323
		0xA1EBFB4219491A1F, 0xCA66FA129F9B60A6,	// 1e-322, 1e-321
		0xFD00B897478238D0, 0x9E20735E8CB16382,	// 1e-320, 1e-319
		0xC5A890362FDDBC62, 0xF712B443BBD52B7B,	// 1e-318, 1e-317
		0x9A6BB0AA55653B2D, 0xC1069CD4EABE89F8,	// 1e-316, 1e-315
		0xF148440A256E2C76, 0x96CD2A865764DBCA,	// 1e-314, 1e-313
		0xBC807527ED3E12BC, 0xEBA09271E88D976B,	// 1e-312, 1e-311
		0x93445B8731587EA3, 0xB8157268FDAE9E4C,	// 1e-310, 1e-309
		0xE61ACF033D1A45DF, 0x8FD0C16206306BAB,	// 1e-308, 1e-307
		0xB3C4F1BA87BC8696, 0xE0B62E2929ABA83C,	// 1e-306, 1e-305
		0x8C71DCD9BA0B4925, 0xAF8E5410288E1B6F,	// 1e-304, 1e-303
		0xDB71E91432B1A24A, 0x892731AC9FAF056E,	// 1e-302, 1e-301
		0xAB70FE17C79AC6CA, 0xD64D3D9DB981787D,	// 1e-300, 1e-299
		0x85F0468293F0EB4E, 0xA76C582338ED2621,	// 1e-298, 1e-297
		0xD1476E2C07286FAA, 0x82CCA4DB847945CA,	// 1e-296, 1e-295
		0xA37FCE126597973C, 0xCC5FC196FEFD7D0C,	// 1e-294, 1e-293
		0xFF77B1FCBEBCDC4F, 0x9FAACF3DF73609B1,	// 1e-292, 1e-291
		0xC795830D75038C1D, 0xF97AE3D0D2446F25,	// 1e-290, 1e-289
		0x9BECCE62836AC577, 0xC2E801FB244576D5,	// 1e-288, 1e-287
		0xF3A20279ED56D48A, 0x9845418C345644D6,	// 1e-286, 1e-285
		0xBE5691EF416BD60C, 0xEDEC366B11C6CB8F,	// 1e-284, 1e-283
		0x94B3A202EB1C3F39, 0xB9E08A83A5E34F07,	// 1e-282, 1e-281
		0xE858AD248F5C22C9, 0x91376C36D99995BE,	// 1e-280, 1e-279
		0xB58547448FFFFB2D, 0xE2E69915B3FFF9F9,	// 1e-278, 1e-277
		0x8DD01FAD907FFC3B, 0xB1442798F49FFB4A,	// 1e-276, 1e-275
		0xDD95317F31C7FA1D, 0x8A7D3EEF7F1CFC52,	// 1e-274, 1e-273
		0xAD1C8EAB5EE43B66, 0xD863B256369D4A40,	// 1e-272, 1e-271
		0x873E4F75E2224E68, 0xA90DE3535AAAE202,	// 1e-270, 1e-269
		0xD3515C2831559A83, 0x8412D9991ED58091,	// 1e-268, 1e-267
		0xA5178FFF668AE0B6, 0xCE5D73FF402D98E3,	// 1e-266, 1e-265
		0x80FA687F881C7F8E, 0xA139029F6A239F72,	// 1e-264, 1e-263
		0xC987434744AC874E, 0xFBE9141915D7A922,	// 1e-262, 1e-261
		0x9D71AC8FADA6C9B5, 0xC4CE17B399107C22,	// 1e-260, 1e-259
		0xF6019DA07F549B2B, 0x99C102844F94E0FB,	// 1e-258, 1e-257
		0xC0314325637A1939, 0xF03D93EEBC589F88,	// 1e-256, 1e-255
		0x96267C7535B763B5, 0xBBB01B9283253CA2,	// 1e-254, 1e-253
		0xEA9C227723EE8BCB, 0x92A1958A7675175F,	// 1e-252, 1e-251
		0xB749FAED14125D36, 0xE51C79A85916F484,	// 1e-250, 1e-249
		0x8F31CC0937AE58D2, 0xB2FE3F0B8599EF07,	// 1e-248, 1e-247
		0xDFBDCECE67006AC9, 0x8BD6A141006042BD,	// 1e-246, 1e-245
		0xAECC49914078536D, 0xDA7F5BF590966848,	// 1e-244, 1e-243
		0x888F99797A5E012D, 0xAAB37FD7D8F58178,	// 1e-242, 1e-241
		0xD5605FCDCF32E1D6, 0x855C3BE0A17FCD26,	// 1e-240, 1e-239
		0xA6B34AD8C9DFC06F, 0xD0601D8EFC57B08B,	// 1e-238, 1e-237
		0x823C12795DB6CE57, 0xA2CB1717B52481ED,	// 1e-236, 1e-235
		0xCB7DDCDDA26DA268, 0xFE5D54150B090B02,	// 1e-234, 1e-233
		0x9EFA548D26E5A6E1, 0xC6B8E9B0709F109A,	// 1e-232, 1e-231
		0xF867241C8CC6D4C0, 0x9B407691D7FC44F8,	// 1e-230, 1e-229
		0xC21094364DFB5636, 0xF294B943E17A2BC4,	// 1e-228, 1e-227
		0x979CF3CA6CEC5B5A, 0xBD8430BD08277231,	// 1e-226, 1e-225
		0xECE53CEC4A314EBD, 0x940F4613AE5ED136,	// 1e-224, 1e-223
		0xB913179899F68584, 0xE757DD7EC07426E5,	// 1e-222, 1e-221
		0x9096EA6F3848984F, 0xB4BCA50B065ABE63,	// 1e-220, 1e-219
		0xE1EBCE4DC7F16DFB, 0x8D3360F09CF6E4BD,	// 1e-218, 1e-217
		0xB080392CC4349DEC, 0xDCA04777F541C567,	// 1e-216, 1e-215
		0x89E42CAAF9491B60, 0xAC5D37D5B79B6239,	// 1e-214, 1e-213
		0xD77485CB25823AC7, 0x86A8D39EF77164BC,	// 1e-212, 1e-211
		0xA8530886B54DBDEB, 0xD267CAA862A12D66,	// 1e-210, 1e-209
		0x8380DEA93DA4BC60, 0xA46116538D0DEB78,	// 1e-208, 1e-207
		0xCD795BE870516656, 0x806BD9714632DFF6,	// 1e-206, 1e-205
		0xA086CFCD97BF97F3, 0xC8A883C0FDAF7DF0,	// 1e-204, 1e-203
		0xFAD2A4B13D1B5D6C, 0x9CC3A6EEC6311A63,	// 1e-202, 1e-201
		0xC3F490AA77BD60FC, 0xF4F1B4D515ACB93B,	// 1e-200, 1e-199
		0x991711052D8BF3C5, 0xBF5CD54678EEF0B6,	// 1e-198, 1e-197
		0xEF340A98172AACE4, 0x9580869F0E7AAC0E,	// 1e-196, 1e-195
		0xBAE0A846D2195712, 0xE998D258869FACD7,	// 1e-194, 1e-193
		0x91FF83775423CC06, 0xB67F6455292CBF08,	// 1e-192, 1e-191
		0xE41F3D6A7377EECA, 0x8E938662882AF53E,	// 1e-190, 1e-189
		0xB23867FB2A35B28D, 0xDEC681F9F4C31F31,	// 1e-188, 1e-187
		0x8B3C113C38F9F37E, 0xAE0B158B4738705E,	// 1e-186, 1e-185
		0xD98DDAEE19068C76, 0x87F8A8D4CFA417C9,	// 1e-184, 1e-183
		0xA9F6D30A038D1DBC, 0xD47487CC8470652B,	// 1e-182, 1e-181
		0x84C8D4DFD2C63F3B, 0xA5FB0A17C777CF09,	// 1e-180, 1e-179
		0xCF79CC9DB955C2CC, 0x81AC1FE293D599BF,	// 1e-178, 1e-177
		0xA21727DB38CB002F, 0xCA9CF1D206FDC03B,	// 1e-176, 1e-175
		0xFD442E4688BD304A, 0x9E4A9CEC15763E2E,	// 1e-174, 1e-173
		0xC5DD44271AD3CDBA, 0xF7549530E188C128,	// 1e-172, 1e-171
		0x9A94DD3E8CF578B9, 0xC13A148E3032D6E7,	// 1e-170, 1e-169
		0xF18899B1BC3F8CA1, 0x96F5600F15A7B7E5,	// 1e-168, 1e-167
		0xBCB2B812DB11A5DE, 0xEBDF661791D60F56,	// 1e-166, 1e-165
		0x936B9FCEBB25C995, 0xB84687C269EF3BFB,	// 1e-164, 1e-163
		0xE65829B3046B0AFA, 0x8FF71A0FE2C2E6DC,	// 1e-162, 1e-161
		0xB3F4E093DB73A093, 0xE0F218B8D25088B8,	// 1e-160, 1e-159
		0x8C974F7383725573, 0xAFBD2350644EEACF,	// 1e-158, 1e-157
		0xDBAC6C247D62A583, 0x894BC396CE5DA772,	// 1e-156, 1e-155
		0xAB9EB47C81F5114F, 0xD686619BA27255A2,	// 1e-154, 1e-153
		0x8613FD0145877585, 0xA798FC4196E952E7,	// 1e-152, 1e-151
		0xD17F3B51FCA3A7A0, 0x82EF85133DE648C4,	// 1e-150, 1e-149
		0xA3AB66580D5FDAF5, 0xCC963FEE10B7D1B3,	// 1e-148, 1e-147
		0xFFBBCFE994E5C61F, 0x9FD561F1FD0F9BD3,	// 1e-146, 1e-145
		0xC7CABA6E7C5382C8, 0xF9BD690A1B68637B,	// 1e-144, 1e-143
		0x9C1661A651213E2D, 0xC31BFA0FE5698DB8,	// 1e-142, 1e-141
		0xF3E2F893DEC3F126, 0x986DDB5C6B3A76B7,	// 1e-140, 1e-139
		0xBE89523386091465, 0xEE2BA6C0678B597F,	// 1e-138, 1e-137
		0x94DB483840B717EF, 0xBA121A4650E4DDEB,	// 1e-136, 1e-135
		0xE896A0D7E51E1566, 0x915E2486EF32CD60,	// 1e-134, 1e-133
		0xB5B5ADA8AAFF80B8, 0xE3231912D5BF60E6,	// 1e-132, 1e-131
		0x8DF5EFABC5979C8F, 0xB1736B96B6FD83B3,	// 1e-130, 1e-129
		0xDDD0467C64BCE4A0, 0x8AA22C0DBEF60EE4,	// 1e-128, 1e-127
		0xAD4AB7112EB3929D, 0xD89D64D57A607744,	// 1e-126, 1e-125
		0x87625F056C7C4A8B, 0xA93AF6C6C79B5D2D,	// 1e-124, 1e-123
		0xD389B47879823479, 0x843610CB4BF160CB,	// 1e-122, 1e-121
		0xA54394FE1EEDB8FE, 0xCE947A3DA6A9273E,	// 1e-120, 1e-119
		0x811CCC668829B887, 0xA163FF802A3426A8,	// 1e-118, 1e-117
		0xC9BCFF6034C13052, 0xFC2C3F3841F17C67,	// 1e-116, 1e-115
		0x9D9BA7832936EDC0, 0xC5029163F384A931,	// 1e-114, 1e-113
		0xF64335BCF065D37D, 0x99EA0196163FA42E,	// 1e-112, 1e-111
		0xC06481FB9BCF8D39, 0xF07DA27A82C37088,	// 1e-110, 1e-109
		0x964E858C91BA2655, 0xBBE226EFB628AFEA,	// 1e-108, 1e-107
		0xEADAB0ABA3B2DBE5, 0x92C8AE6B464FC96F,	// 1e-106, 1e-105
		0xB77ADA0617E3BBCB, 0xE55990879DDCAABD,	// 1e-104, 1e-103
		0x8F57FA54C2A9EAB6, 0xB32DF8E9F3546564,	// 1e-102, 1e-101
		0xDFF9772470297EBD, 0x8BFBEA76C619EF36,	// 1e-100, 1e-99
		0xAEFAE51477A06B03, 0xDAB99E59958885C4,	// 1e-98, 1e-97
		0x88B402F7FD75539B, 0xAAE103B5FCD2A881,	// 1e-96, 1e-95
		0xD59944A37C0752A2, 0x857FCAE62D8493A5,	// 1e-94, 1e-93
		0xA6DFBD9FB8E5B88E, 0xD097AD07A71F26B2,	// 1e-92, 1e-91
		0x825ECC24C873782F, 0xA2F67F2DFA90563B,	// 1e-90, 1e-89
		0xCBB41EF979346BCA, 0xFEA126B7D78186BC,	// 1e-88, 1e-87
		0x9F24B832E6B0F436, 0xC6EDE63FA05D3143,	// 1e-86, 1e-85
		0xF8A95FCF88747D94, 0x9B69DBE1B548CE7C,	// 1e-84, 1e-83
		0xC24452DA229B021B, 0xF2D56790AB41C2A2,	// 1e-82, 1e-81
		0x97C560BA6B0919A5, 0xBDB6B8E905CB600F,	// 1e-80, 1e-79
		0xED246723473E3813, 0x9436C0760C86E30B,	// 1e-78, 1e-77
		0xB94470938FA89BCE, 0xE7958CB87392C2C2,	// 1e-76, 1e-75
		0x90BD77F3483BB9B9, 0xB4ECD5F01A4AA828,	// 1e-74, 1e-73
		0xE2280B6C20DD5232, 0x8D590723948A535F,	// 1e-72, 1e-71
		0xB0AF48EC79ACE837, 0xDCDB1B2798182244,	// 1e-70, 1e-69
		0x8A08F0F8BF0F156B, 0xAC8B2D36EED2DAC5,	// 1e-68, 1e-67
		0xD7ADF884AA879177, 0x86CCBB52EA94BAEA,	// 1e-66, 1e-65
		0xA87FEA27A539E9A5, 0xD29FE4B18E88640E,	// 1e-64, 1e-63
		0x83A3EEEEF9153E89, 0xA48CEAAAB75A8E2B,	// 1e-62, 1e-61
		0xCDB02555653131B6, 0x808E17555F3EBF11,	// 1e-60, 1e-59
		0xA0B19D2AB70E6ED6, 0xC8DE047564D20A8B,	// 1e-58, 1e-57
		0xFB158592BE068D2E, 0x9CED737BB6C4183D,	// 1e-56, 1e-55
		0xC428D05AA4751E4C, 0xF53304714D9265DF,	// 1e-54, 1e-53
		0x993FE2C6D07B7FAB, 0xBF8FDB78849A5F96,	// 1e-52, 1e-51
		0xEF73D256A5C0F77C, 0x95A8637627989AAD,	// 1e-50, 1e-49
		0xBB127C53B17EC159, 0xE9D71B689DDE71AF,	// 1e-48, 1e-47
		0x9226712162AB070D, 0xB6B00D69BB55C8D1,	// 1e-46, 1e-45
		0xE45C10C42A2B3B05, 0x8EB98A7A9A5B04E3,	// 1e-44, 1e-43
		0xB267ED1940F1C61C, 0xDF01E85F912E37A3,	// 1e-42, 1e-41
		0x8B61313BBABCE2C6, 0xAE397D8AA96C1B77,	// 1e-40, 1e-39
		0xD9C7DCED53C72255, 0x881CEA14545C7575,	// 1e-38, 1e-37
		0xAA242499697392D2, 0xD4AD2DBFC3D07787,	// 1e-36, 1e-35
		0x84EC3C97DA624AB4, 0xA6274BBDD0FADD61,	// 1e-34, 1e-33
		0xCFB11EAD453994BA, 0x81CEB32C4B43FCF4,	// 1e-32, 1e-31
		0xA2425FF75E14FC31, 0xCAD2F7F5359A3B3E,	// 1e-30, 1e-29
		0xFD87B5F28300CA0D, 0x9E74D1B791E07E48,	// 1e-28, 1e-27
		0xC612062576589DDA, 0xF79687AED3EEC551,	// 1e-26, 1e-25
		0x9ABE14CD44753B52, 0xC16D9A0095928A27,	// 1e-24, 1e-23
		0xF1C90080BAF72CB1, 0x971DA05074DA7BEE,	// 1e-22, 1e-21
		0xBCE5086492111AEA, 0xEC1E4A7DB69561A5,	// 1e-20, 1e-19
		0x9392EE8E921D5D07, 0xB877AA3236A4B449,	// 1e-18, 1e-17
		0xE69594BEC44DE15B, 0x901D7CF73AB0ACD9,	// 1e-16, 1e-15
		0xB424DC35095CD80F, 0xE12E13424BB40E13,	// 1e-14, 1e-13
		0x8CBCCC096F5088CB, 0xAFEBFF0BCB24AAFE,	// 1e-12, 1e-11
		0xDBE6FECEBDEDD5BE, 0x89705F4136B4A597,	// 1e-10, 1e-9
		0xABCC77118461CEFC, 0xD6BF94D5E57A42BC,	// 1e-8, 1e-7
		0x8637BD05AF6C69B5, 0xA7C5AC471B478423,	// 1e-6, 1e-5
		0xD1B71758E219652B, 0x83126E978D4FDF3B,	// 1e-4, 1e-3
		0xA3D70A3D70A3D70A, 0xCCCCCCCCCCCCCCCC,	// 1e-2, 1e-1
		0x8000000000000000, 0xA000000000000000,	// 1e0, 1e1
		0xC800000000000000, 0xFA00000000000000,	// 1e2, 1e3
		0x9C40000000000000, 0xC350000000000000,	// 1e4, 1e5
		0xF424000000000000, 0x9896800000000000,	// 1e6, 1e7
		0xBEBC200000000000, 0xEE6B280000000000,	// 1e8, 1e9
		0x9502F90000000000, 0xBA43B74000000000,	// 1e10, 1e11
		0xE8D4A51000000000, 0x9184E72A00000000,	// 1e12, 1e13
		0xB5E620F480000000, 0xE35FA931A0000000,	// 1e14, 1e15
		0x8E1BC9BF04000000, 0xB1A2BC2EC5000000,	// 1e16, 1e17
		0xDE0B6B3A76400000, 0x8AC7230489E80000,	// 1e18, 1e19
		0xAD78EBC5AC620000, 0xD8D726B7177A8000,	// 1e20, 1e21
		0x878678326EAC9000, 0xA968163F0A57B400,	// 1e22, 1e23
		0xD3C21BCECCEDA100, 0x84595161401484A0,	// 1e24, 1e25
		0xA56FA5B99019A5C8, 0xCECB8F27F4200F3A,	// 1e26, 1e27
		0x813F3978F8940984, 0xA18F07D736B90BE5,	// 1e28, 1e29
		0xC9F2C9CD04674EDE, 0xFC6F7C4045812296,	// 1e30, 1e31
		0x9DC5ADA82B70B59D, 0xC5371912364CE305,	// 1e32, 1e33
		0xF684DF56C3E01BC6, 0x9A130B963A6C115C,	// 1e34, 1e35
		0xC097CE7BC90715B3, 0xF0BDC21ABB48DB20,	// 1e36, 1e37
		0x96769950B50D88F4, 0xBC143FA4E250EB31,	// 1e38, 1e39
		0xEB194F8E1AE525FD, 0x92EFD1B8D0CF37BE,	// 1e40, 1e41
		0xB7ABC627050305AD, 0xE596B7B0C643C719,	// 1e42, 1e43
		0x8F7E32CE7BEA5C6F, 0xB35DBF821AE4F38B,	// 1e44, 1e45
		0xE0352F62A19E306E, 0x8C213D9DA502DE45,	// 1e46, 1e47
		0xAF298D050E4395D6, 0xDAF3F04651D47B4C,	// 1e48, 1e49
		0x88D8762BF324CD0F, 0xAB0E93B6EFEE0053,	// 1e50, 1e51
		0xD5D238A4ABE98068, 0x85A36366EB71F041,	// 1e52, 1e53
		0xA70C3C40A64E6C51, 0xD0CF4B50CFE20765,	// 1e54, 1e55
		0x82818F1281ED449F, 0xA321F2D7226895C7,	// 1e56, 1e57
		0xCBEA6F8CEB02BB39, 0xFEE50B7025C36A08,	// 1e58, 1e59
		0x9F4F2726179A2245, 0xC722F0EF9D80AAD6,	// 1e60, 1e61
		0xF8EBAD2B84E0D58B, 0x9B934C3B330C8577,	// 1e62, 1e63
		0xC2781F49FFCFA6D5, 0xF316271C7FC3908A,	// 1e64, 1e65
		0x97EDD871CFDA3A56, 0xBDE94E8E43D0C8EC,	// 1e66, 1e67
		0xED63A231D4C4FB27, 0x945E455F24FB1CF8,	// 1e68, 1e69
		0xB975D6B6EE39E436, 0xE7D34C64A9C85D44,	// 1e70, 1e71
		0x90E40FBEEA1D3A4A, 0xB51D13AEA4A488DD,	// 1e72, 1e73
		0xE264589A4DCDAB14, 0x8D7EB76070A08AEC,	// 1e74, 1e75
		0xB0DE65388CC8ADA8, 0xDD15FE86AFFAD912,	// 1e76, 1e77
		0x8A2DBF142DFCC7AB, 0xACB92ED9397BF996,	// 1e78, 1e79
		0xD7E77A8F87DAF7FB, 0x86F0AC99B4E8DAFD,	// 1e80, 1e81
		0xA8ACD7C0222311BC, 0xD2D80DB02AABD62B,	// 1e82, 1e83
		0x83C7088E1AAB65DB, 0xA4B8CAB1A1563F52,	// 1e84, 1e85
		0xCDE6FD5E09ABCF26, 0x80B05E5AC60B6178,	// 1e86, 1e87
		0xA0DC75F1778E39D6, 0xC913936DD571C84C,	// 1e88, 1e89
		0xFB5878494ACE3A5F, 0x9D174B2DCEC0E47B,	// 1e90, 1e91
		0xC45D1DF942711D9A, 0xF5746577930D6500,	// 1e92, 1e93
		0x9968BF6ABBE85F20, 0xBFC2EF456AE276E8,	// 1e94, 1e95
		0xEFB3AB16C59B14A2, 0x95D04AEE3B80ECE5,	// 1e96, 1e97
		0xBB445DA9CA61281F, 0xEA1575143CF97226,	// 1e98, 1e99
		0x924D692CA61BE758, 0xB6E0C377CFA2E12E,	// 1e100, 1e101
		0xE498F455C38B997A, 0x8EDF98B59A373FEC,	// 1e102, 1e103
		0xB2977EE300C50FE7, 0xDF3D5E9BC0F653E1,	// 1e104, 1e105
		0x8B865B215899F46C, 0xAE67F1E9AEC07187,	// 1e106, 1e107
		0xDA01EE641A708DE9, 0x884134FE908658B2,	// 1e108, 1e109
		0xAA51823E34A7EEDE, 0xD4E5E2CDC1D1EA96,	// 1e110, 1e111
		0x850FADC09923329E, 0xA6539930BF6BFF45,	// 1e112, 1e113
		0xCFE87F7CEF46FF16, 0x81F14FAE158C5F6E,	// 1e114, 1e115
		0xA26DA3999AEF7749, 0xCB090C8001AB551C,	// 1e116, 1e117
		0xFDCB4FA002162A63, 0x9E9F11C4014DDA7E,	// 1e118, 1e119
		0xC646D63501A1511D, 0xF7D88BC24209A565,	// 1e120, 1e121
		0x9AE757596946075F, 0xC1A12D2FC3978937,	// 1e122, 1e123
		0xF209787BB47D6B84, 0x9745EB4D50CE6332,	// 1e124, 1e125
		0xBD176620A501FBFF, 0xEC5D3FA8CE427AFF,	// 1e126, 1e127
		0x93BA47C980E98CDF, 0xB8A8D9BBE123F017,	// 1e128, 1e129
		0xE6D3102AD96CEC1D, 0x9043EA1AC7E41392,	// 1e130, 1e131
		0xB454E4A179DD1877, 0xE16A1DC9D8545E94,	// 1e132, 1e133
		0x8CE2529E2734BB1D, 0xB01AE745B101E9E4,	// 1e134, 1e135
		0xDC21A1171D42645D, 0x899504AE72497EBA,	// 1e136, 1e137
		0xABFA45DA0EDBDE69, 0xD6F8D7509292D603,	// 1e138, 1e139
		0x865B86925B9BC5C2, 0xA7F26836F282B732,	// 1e140, 1e141
		0xD1EF0244AF2364FF, 0x8335616AED761F1F,	// 1e142, 1e143
		0xA402B9C5A8D3A6E7, 0xCD036837130890A1,	// 1e144, 1e145
		0x802221226BE55A64, 0xA02AA96B06DEB0FD,	// 1e146, 1e147
		0xC83553C5C8965D3D, 0xFA42A8B73ABBF48C,	// 1e148, 1e149
		0x9C69A97284B578D7, 0xC38413CF25E2D70D,	// 1e150, 1e151
		0xF46518C2EF5B8CD1, 0x98BF2F79D5993802,	// 1e152, 1e153
		0xBEEEFB584AFF8603, 0xEEAABA2E5DBF6784,	// 1e154, 1e155
		0x952AB45CFA97A0B2, 0xBA756174393D88DF,	// 1e156, 1e157
		0xE912B9D1478CEB17, 0x91ABB422CCB812EE,	// 1e158, 1e159
		0xB616A12B7FE617AA, 0xE39C49765FDF9D94,	// 1e160, 1e161
		0x8E41ADE9FBEBC27D, 0xB1D219647AE6B31C,	// 1e162, 1e163
		0xDE469FBD99A05FE3, 0x8AEC23D680043BEE,	// 1e164, 1e165
		0xADA72CCC20054AE9, 0xD910F7FF28069DA4,	// 1e166, 1e167
		0x87AA9AFF79042286, 0xA99541BF57452B28,	// 1e168, 1e169
		0xD3FA922F2D1675F2, 0x847C9B5D7C2E09B7,	// 1e170, 1e171
		0xA59BC234DB398C25, 0xCF02B2C21207EF2E,	// 1e172, 1e173
		0x8161AFB94B44F57D, 0xA1BA1BA79E1632DC,	// 1e174, 1e175
		0xCA28A291859BBF93, 0xFCB2CB35E702AF78,	// 1e176, 1e177
		0x9DEFBF01B061ADAB, 0xC56BAEC21C7A1916,	// 1e178, 1e179
		0xF6C69A72A3989F5B, 0x9A3C2087A63F6399,	// 1e180, 1e181
		0xC0CB28A98FCF3C7F, 0xF0FDF2D3F3C30B9F,	// 1e182, 1e183
		0x969EB7C47859E743, 0xBC4665B596706114,	// 1e184, 1e185
		0xEB57FF22FC0C7959, 0x9316FF75DD87CBD8,	// 1e186, 1e187
		0xB7DCBF5354E9BECE, 0xE5D3EF282A242E81,	// 1e188, 1e189
		0x8FA475791A569D10, 0xB38D92D760EC4455,	// 1e190, 1e191
		0xE070F78D3927556A, 0x8C469AB843B89562,	// 1e192, 1e193
		0xAF58416654A6BABB, 0xDB2E51BFE9D0696A,	// 1e194, 1e195
		0x88FCF317F22241E2, 0xAB3C2FDDEEAAD25A,	// 1e196, 1e197
		0xD60B3BD56A5586F1, 0x85C7056562757456,	// 1e198, 1e199
		0xA738C6BEBB12D16C, 0xD106F86E69D785C7,	// 1e200, 1e201
		0x82A45B450226B39C, 0xA34D721642B06084,	// 1e202, 1e203
		0xCC20CE9BD35C78A5, 0xFF290242C83396CE,	// 1e204, 1e205
		0x9F79A169BD203E41, 0xC75809C42C684DD1,	// 1e206, 1e207
		0xF92E0C3537826145, 0x9BBCC7A142B17CCB,	// 1e208, 1e209
		0xC2ABF989935DDBFE, 0xF356F7EBF83552FE,	// 1e210, 1e211
		0x98165AF37B2153DE, 0xBE1BF1B059E9A8D6,	// 1e212, 1e213
		0xEDA2EE1C7064130C, 0x9485D4D1C63E8BE7,	// 1e214, 1e215
		0xB9A74A0637CE2EE1, 0xE8111C87C5C1BA99,	// 1e216, 1e217
		0x910AB1D4DB9914A0, 0xB54D5E4A127F59C8,	// 1e218, 1e219
		0xE2A0B5DC971F303A, 0x8DA471A9DE737E24,	// 1e220, 1e221
		0xB10D8E1456105DAD, 0xDD50F1996B947518,	// 1e222, 1e223
		0x8A5296FFE33CC92F, 0xACE73CBFDC0BFB7B,	// 1e224, 1e225
		0xD8210BEFD30EFA5A, 0x8714A775E3E95C78,	// 1e226, 1e227
		0xA8D9D1535CE3B396, 0xD31045A8341CA07C,	// 1e228, 1e229
		0x83EA2B892091E44D, 0xA4E4B66B68B65D60,	// 1e230, 1e231
		0xCE1DE40642E3F4B9, 0x80D2AE83E9CE78F3,	// 1e232, 1e233
		0xA1075A24E4421730, 0xC94930AE1D529CFC,	// 1e234, 1e235
		0xFB9B7CD9A4A7443C, 0x9D412E0806E88AA5,	// 1e236, 1e237
		0xC491798A08A2AD4E, 0xF5B5D7EC8ACB58A2,	// 1e238, 1e239
		0x9991A6F3D6BF1765, 0xBFF610B0CC6EDD3F,	// 1e240, 1e241
		0xEFF394DCFF8A948E, 0x95F83D0A1FB69CD9,	// 1e242, 1e243
		0xBB764C4CA7A4440F, 0xEA53DF5FD18D5513,	// 1e244, 1e245
		0x92746B9BE2F8552C, 0xB7118682DBB66A77,	// 1e246, 1e247
		0xE4D5E82392A40515, 0x8F05B1163BA6832D,	// 1e248, 1e249
		0xB2C71D5BCA9023F8, 0xDF78E4B2BD342CF6,	// 1e250, 1e251
		0x8BAB8EEFB6409C1A, 0xAE9672ABA3D0C320,	// 1e252, 1e253
		0xDA3C0F568CC4F3E8, 0x8865899617FB1871,	// 1e254, 1e255
		0xAA7EEBFB9DF9DE8D, 0xD51EA6FA85785631,	// 1e256, 1e257
		0x8533285C936B35DE, 0xA67FF273B8460356,	// 1e258, 1e259
		0xD01FEF10A657842C, 0x8213F56A67F6B29B,	// 1e260, 1e261
		0xA298F2C501F45F42, 0xCB3F2F7642717713,	// 1e262, 1e263
		0xFE0EFB53D30DD4D7, 0x9EC95D1463E8A506,	// 1e264, 1e265
		0xC67BB4597CE2CE48, 0xF81AA16FDC1B81DA,	// 1e266, 1e267
		0x9B10A4E5E9913128, 0xC1D4CE1F63F57D72,	// 1e268, 1e269
		0xF24A01A73CF2DCCF, 0x976E41088617CA01,	// 1e270, 1e271
		0xBD49D14AA79DBC82, 0xEC9C459D51852BA2,	// 1e272, 1e273
		0x93E1AB8252F33B45, 0xB8DA1662E7B00A17,	// 1e274, 1e275
		0xE7109BFBA19C0C9D, 0x906A617D450187E2,	// 1e276, 1e277
		0xB484F9DC9641E9DA, 0xE1A63853BBD26451,	// 1e278, 1e279
		0x8D07E33455637EB2, 0xB049DC016ABC5E5F,	// 1e280, 1e281
		0xDC5C5301C56B75F7, 0x89B9B3E11B6329BA,	// 1e282, 1e283
		0xAC2820D9623BF429, 0xD732290FBACAF133,	// 1e284, 1e285
		0x867F59A9D4BED6C0, 0xA81F301449EE8C70,	// 1e286, 1e287
		0xD226FC195C6A2F8C, 0x83585D8FD9C25DB7,	// 1e288, 1e289
		0xA42E74F3D032F525, 0xCD3A1230C43FB26F,	// 1e290, 1e291
		0x80444B5E7AA7CF85, 0xA0555E361951C366,	// 1e292, 1e293
		0xC86AB5C39FA63440, 0xFA856334878FC150,	// 1e294, 1e295
		0x9C935E00D4B9D8D2, 0xC3B8358109E84F07,	// 1e296, 1e297
		0xF4A642E14C6262C8, 0x98E7E9CCCFBD7DBD,	// 1e298, 1e299
		0xBF21E44003ACDD2C, 0xEEEA5D5004981478,	// 1e300, 1e301
		0x95527A5202DF0CCB, 0xBAA718E68396CFFD,	// 1e302, 1e303
		0xE950DF20247C83FD, 0x91D28B7416CDD27E,	// 1e304, 1e305
		0xB6472E511C81471D, 0xE3D8F9E563A198E5,	// 1e306, 1e307
		0x8E679C2F5E44FF8F, 0xB201833B35D63F73,	// 1e308, 1e309
	};

	// Eq. is 2^(e2) = 10^(e10), solve it urself, you'll get linear exp.
	return (flt_t){ .mant = mant[e10 - MANT_COARSE_MIN],
			.e = (e10 * 217706) >> 16 };
#endif // EKJSON_SPACE_EFFICENT
}

// Parses exponent and adds it to the int pointed by exp.
// src should point right after the 'e' character
static void addexp(const char *src, int32_t *exp) {
	uint64_t e; // Where to store the parsed exponent
	
	// Get the sign and check for either +/-
	const bool esign = *src == '-';
	src += esign | (*src == '+');

	// Parse the exponent. Check early for obvious overflow, so we
	// dont actually overflow the flt.e i32
	const bool bad = parsedigits8(src, &e) > 3 || e > 324;
	*exp += esign ? -(int32_t)e : (int32_t)e;
	if (bad) *exp = (int32_t)((uint32_t)INT32_MAX + esign);
}

// Slow path for parsing floats. If even THIS overflows we just give up and
// return NAN. I doub't anybody is passing in numbers over 200 sig-figs long,
// besides that can't even be represented in double precision floats
static EKJSON_NO_INLINE double slowflt(const char *src,
				const uint64_t int_part, const bool sign) {
	// Create a place to store and exact significand and exponent
	static bigint_t sig;	// Integer siginifcand
	int32_t e = 0;		// Exponent

	// Get significand (and parse fractional component)
	bigint_set64(&sig, int_part); // Int part has first 19 or less digits
	if (*src == '.') goto frac;	// Skip to fraction if we can

	int n;			// Number of digits parsed in 1 run
	do {
		uint64_t run;
		src += n = parsedigits8(src, &run); // Parse run of digits

		// Add these digits to the end
		if (bigint_pow10(&sig, n)
			|| bigint_add32(&sig, run)) return FLTNAN;
	} while (n == 8); // Continue if we parsed max run

	// Do fractional part if we have one to parse
	if (*src == '.') {
frac:
		src++;
		do {
			uint64_t run;
			src += n = parsedigits8(src, &run);
			e -= n; // Keep sig*10^e representative of actual num

			// Add these digits to the end
			if (bigint_pow10(&sig, n)
				|| bigint_add32(&sig, run)) return FLTNAN;
		} while (n == 8); // Continue if we parsed max run
	}

	// Conditionally parse an exponential
	if ((*src & 0x4F) == 'E') addexp(src + 1, &e);

	// Check for infinity, zero, denormals (not implemented yet), etc
	if (bigint_iszero(&sig) || e < -308) return sign ? -0.0 : 0.0;
	if (e > 308) return sign ? -FLTINF : FLTINF;

	// Generate guess using normal flt_mul
	flt_t flt;

	// Gets top 64 bits (rounded) and sets flt.e to how many times right it
	// had to shift to get the top 64 bits
	flt.mant = bigint_ms64(&sig, &flt.e);
	flt.e += 63;	// Bias the exponent to make it normalized (1.63 fixed)
	
	// These are how many units of error in the ULP we are off from
	// the closest approximation of the floating point number we
	// are trying to find. If e is out of range of exactly representable
	// mantissas, then we are going to have a lot more error, and if flt.e
	// is greater than 0, then we have a mantissa not exactly representable
	// in 64 bits.
	const int ulperr = (flt.e > 63) + (e < 0 || e >= MANT_FINE_RANGE) * 3;

	// Generate guess
	flt = flt_mul(flt, ten2e(e));

	// Get conversion result. If its not on the 1/2 barrier then we're good
	bitdbl_t dbl;
	if (flt_dbl(flt, ulperr, sign, &dbl)) return dbl.d;

	// Well, we have to do some iteration to find the closest value. We
	// know that because we already had more precsion than we needed for
	// 53 bit floats, we are only 1 above or 1 under the float that we
	// should get.
	static bigint_t m;
	bigint_set64(&m, bitdbl_sig(dbl) << 1 | 1);

	// Bias the double back to normal to make mantissa an integer.
	// Also take into account the 1 we shifted up there
	const int e2 = (int)dbl.u.e - 1023 - 52 - 1;

	// Make sig and m both proportionally exact integers for what we
	// should exactly get (sig*10^e) and what we have (m*2^dbl.e)
	// Return FLTNAN if the numbers overflow
	if (e >= 0 && bigint_pow10(&sig, e)
		|| e < 0 && bigint_pow10(&m, -e)) return FLTNAN;
	if (e2 >= 0 && bigint_shl(&m, e2)
		|| e2 < 0 && bigint_shl(&sig, -e2)) return FLTNAN;
	
	const int cmp = bigint_cmp(&sig, &m);

	// Round up or tie to even
	if (cmp == 0 && (dbl.u.m & 1) || cmp > 0) bitdbl_next(&dbl);
	return dbl.d;
}

// Returns if it could use the fast path
static bool fastflt(const char *src, const uint64_t int_part,
			const bool sign, double *result) {
	// Precalculated powers of 10 to shift the integer part of the
	// significand by when parsing the fractional component
	static const uint64_t shiftpows[] = {
		1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull,
		10000000ull, 100000000ull, 1000000000ull, 10000000000ull,
		100000000000ull, 1000000000000ull, 10000000000000ull,
		100000000000000ull, 1000000000000000ull, 10000000000000000ull,
		100000000000000000ull, 1000000000000000000ull,
		10000000000000000000ull,
	};

	// These are the powers of ten that can be exactly representable by
	// a double precision floating point number
	static const double exact[23] = {
		1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,
		1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19,
		1e20, 1e21, 1e22
	};

	// First part the significand and the exponent. For now, these will be
	// stored in a flt_t even though the 'mantissa' is not normalized.
	// Another different thing here is that we also (temporarily) store the
	// base 10 exponent here in the float (which should be base 2)
	flt_t flt = { .mant = int_part, .e = 0 };

	// Conditionally parse the fractional component
	if (*src == '.') {
		uint64_t frac;	// Where to store fractional part

		// Shift significand and add fractional part on, while making
		// sure not to overflow (abort to slow path)
		int n; // Number of digits parsed by parsebase10
		if (!(n = parsebase10(++src, &frac))
			|| mul_overflow(flt.mant, shiftpows[n], &flt.mant)
			|| add_overflow(flt.mant, frac, &flt.mant)) {
			return false;
		}

		// Advance src, and subtract the exponent because we are
		// storing the significand (no fractional part)
		src += n, flt.e -= n;
	}

	// Conditionally parse an exponential
	if ((*src & 0x4F) == 'E') addexp(src + 1, &flt.e);

	// Check for infinity, zero, denormals (pass to slow route), etc
	if (flt.mant == 0 || flt.e < -308) {
		*result = sign ? -0.0 : 0.0;
		return flt.e >= -308; // Slow path does denormals
	} else if (flt.e > 308) {
		*result = sign ? -FLTINF : FLTINF;
		return true;
	}

	// We within range of exactly representable powers of 10 for doubles?
	const bool inrange = flt.e > -(int32_t)ARRLEN(exact)
		&& flt.e < (int32_t)ARRLEN(exact);

	// Count leading zeros so we can normalize the significand and check if
	// the mantissa can fit in the 52 bits of a normal double
	const int lz = clz(flt.mant);

	// Maybe fast paths. Since the mantissa is either greater than the
	// maximum, or the exponent is out of range we need higher precision
	if (lz < 12 || !inrange) {
		// These are how many units of error in the ULP we are off from
		// the closest approximation of the floating point number we
		// are trying to find. If e is out of the range of exactly
		// representable 64 bit mantissas, then 
		const int ulperr = (flt.e < 0 || flt.e >= MANT_FINE_RANGE) * 3;
		
		// Now convert flt from significand * 10^e to a normalized
		// binary floating point representation
		flt = flt_mul((flt_t){
			.mant = flt.mant << lz, // Normalize significand
			.e = 63 - lz,		// Adjust binary exponent
		}, ten2e(flt.e));		// Get decimal exponent

		// Now just convert the high precision double we calculated to
		// a lower precision (double precision). Returns NAN on ties
		return flt_dbl(flt, ulperr, sign, (bitdbl_t *)result);
	} else {
		// Really fast path. Here we can divide or multiply by exact
		// powers of ten and guarentee exact results (if we have a
		// normal cpu that is)

		// Get the 'magnitude'
		if (flt.e >= 0) *result = (double)flt.mant * exact[flt.e];
		else *result = (double)flt.mant / exact[-flt.e];

		// Apply the sign
		*result *= ((double)sign * -2.0 + 1.0);
		return true;
	}
}

// Returns the number token as a float. If the number is out of the range that
// can be represented, it will return either +/-inf. This function will never
// return nan. This function will also handle +/-0.0
double ejflt(const char *src) {
	// Get the sign and skip it
	const bool sign = *src == '-';
	src += sign;

	// Sometimes just doing things byte by byte is faster, and that is the
	// cast here so we just do it byte by byte and check for overflow. If
	// we do overflow, then goto slow path but keep first x digits.
	uint64_t i = 0, n = 0;
	for (;*src >= '0' && *src <= '9'; i = i * 10 + *src++ - '0') {
		if (EKJSON_EXPECT(++n > 19, 0)) goto slowpath;
	}

	// Try fast paths first and if they won't work use biguint and do slow
	double result;
	if (fastflt(src, i, sign, &result)) return result;
slowpath:
	return slowflt(src, i, sign);
}

// Returns whether the boolean is true or false
bool ejbool(const char *tok_start) {
	// Since tokens are already validated, this is all that is needed
	return *tok_start == 't';
}

