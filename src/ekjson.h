#ifndef _ekjson_h_
#define _ekjson_h_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Removes big tables and stops inlining for functions in the source code
// when set to true
#define EKJSON_SPACE_EFFICENT 0

// When set, turns off 'bitwise' 64bit tricks. This will for sure be better
// for 32 bit systems without fast 64-bit operations and systems without
// branch overhead (due to mis-predicts)
#define EKJSON_NO_BITWISE 0

// Max value depth that the json document can go.
// ekjson uses recurrsion so keep that in mind when setting this number
#define EKJSON_MAX_DEPTH 64

// Maximum number of bits in numbers for slow path of ejflt
// NOTE: This must be atleast a little bit above 1024 since the maximum
// base 2 exponent in double precision floating point numbers is 1023
#define EKJSON_MAX_SIG (1024 + 512)

// A ekjson document is a collection of tokens representing the document
// These tokens are only valid if you run ej* functions on them to ensure
// validation. If its a object, key/value, or array token, then it is already
// validated.
enum { EJOBJ, EJKV, EJARR, EJSTR, EJFLT, EJINT, EJBOOL, EJNULL };
typedef struct ejtok {
	uint32_t start;		// Offset from the start of the source string
	uint32_t type : 3;	// General type of the token (kv is a string)
	uint32_t len : 29;	// Number of child tokens (including this one)
} ejtok_t;

// Result of an ekjson parsing routine
typedef struct ejresult {
	bool err;		// Was this an error, or did we run out of toks
	const char *loc;	// Location of where the error occured
	size_t ntoks;		// Number of tokens parsed
} ejresult_t;

// Parses and partially validates a json file. Expects src to be a valid
// UTF-8/WTF-8 string.
// Writes tokens to the t buffer. Will give an error if there is not enough
// buffer storage for the whole json file.
// See the ejtok_t documentation for more details.
ejresult_t ejparse(const char *src, ejtok_t *t, size_t nt);

// Copies and escapes a json string/kv to a string buffer
// Takes in json source, token, and the out buffer and out length
// If out is non-null and outlen is greater than 0, it will write characters
// until outlen-1 and then output a null-terminator meaning the output buffer
// will always be null-terminated.
// Returns length of what the would be string would be (including null
// terminator so that length is always above 0 when there are no errors)
// If the string contains an invalid utf-8 codepoint or surrogate, it will
// return the length as 0 to signify error
size_t ejstr(const char *tok_start, char *out, size_t outlen);

// Compares the string token to a normal c string, escaping characters as
// needed and returning whether or not they are equal. Passing in null for
// tok_start or cstr is undefined.
bool ejcmp(const char *tok_start, const char *cstr);

// Returns the number token parsed as an int64_t. If there are decimals, it
// just returns the number truncated towards 0. If the number is outside of
// the int64_t range, it will saturate it to the closest limit.
int64_t ejint(const char *tok_start);

// Returns the number token as a float. The number token can both be EJFLT and
// EJINT. If the number is out of the range that can be represented, it will
// return either +/-inf. If the number in tok_start has a significand
// (after multiplying exponent) over EKJSON_MAX_SIG, then the function will
// return nan.
double ejflt(const char *tok_start);

// Returns whether the boolean is true or false
bool ejbool(const char *tok_start);

#endif

