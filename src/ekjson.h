#ifndef _ekjson_h_
#define _ekjson_h_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//#define EKJSON_SPACE_EFFICIENT 1
//#define EKJSON_NO_BITWISE 1

// A ekjson document is a collection of tokens representing the document
// These tokens are only valid if you run ej* functions on them to ensure
// validation. If its a object, key/value, or array token, then it is already
// validated.
enum { EJOBJ, EJKV, EJARR, EJSTR, EJNUM, EJBOOL, EJNULL };
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
// UTF-8/WTF-8 string. See the documentation for ejtok for more details.
ejresult_t ejparse(const char *src, ejtok_t *t, size_t nt);

// Returns what the length of the string is (not including null-terminator)
size_t ejstr(const char *src, const ejtok_t t, char *out, size_t outlen);

// Compares the string token to a normal c string
bool ejcmp(const char *src, const ejtok_t t, const char *other);

// Returns the number token as a float
double ejflt(const char *src, const ejtok_t t);

// Returns the int token as an int
int64_t ejint(const char *src, const ejtok_t t);

// Returns whether the boolean is true or false
bool ejbool(const char *src, const ejtok_t t);

#endif

