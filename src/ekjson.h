#ifndef _ekjson_h_
#define _ekjson_h_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Removes big tables in the source code when set to true
#define EKJSON_SPACE_EFFICENT 0

// When set, turns off 'bitwise' 64bit tricks
#define EKJSON_NO_BITWISE 0

// Max value depth that the json document can go.
// ekjson uses recurrsion so keep that in mind when setting this number
#define EKJSON_MAX_DEPTH 64

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
// Returns length of what the would be string would be (not including null
// terminator)
size_t ejstr(const char *tok_start, char *out, size_t outlen);

// Compares the string token to a normal c string
bool ejcmp(const char *src, const ejtok_t t, const char *other);

// Returns the number token as a float
double ejflt(const char *src, const ejtok_t t);

// Returns the int token as an int
int64_t ejint(const char *src, const ejtok_t t);

// Returns whether the boolean is true or false
bool ejbool(const char *src, const ejtok_t t);

#endif

