/**
 * \file ekjson.h
 * \author eklipsed
 *
 * Who is ekjson for?
 * ==================
 *
 * Well, there are a lot of reasons to include a JSON parser in a project. I
 * wanted to create a small one that was fast to compile and even faster at
 * runtime and ended up with this. My main goal for the project is to create
 * something I can just use to parse JSON objects into C structs a bit easier.
 * It isn't meant for any crazy JSON DOM editing and you can't even do that
 * with this parser. For me, that isn't a comprimise and actually is great
 * because that's all I need. Because of that you will see design decisions
 * tailored around that way of using JSON in a project. I would recommend that
 * if you need more complex functionaly that you include a more featureful and
 * complex JSON parser, this is meant to be a solid bare bones implementation.
 *
 * How to use the library:
 * =======================
 * Ekjson is meant to have a very small footprint on lines of code in your
 * project, especially when it comes to the API that ekjson exposes. Ekjson
 * exposes 3 main types of functions:
 *  - A function to parse documents into a buffer (ejparse)
 *  - Functions to compare and copy JSON strings (ejstr/ejcmp)
 *  - Functions to read lightweight tokens (ejflt/ejint/ejbool)
 *
 * DOM Structure:
 * ==============
 * A ekjson document is a collection of tokens representing the document (this
 * is inspired by the jsmn c parser).
 * 
 * These tokens are only *fully* valid if you run ej* functions on them to
 * ensure validation. If its a object, key/value, or array token, then it is
 * already fully validated (but not the contents of each value). 'Tokens' are
 * analogus to a DOM model's nodes (values, objects, etc), but are different in
 * that they are very lightweight by only holding where they are in the source
 * string and how to get to the next token.
 * 
 * For example this string:
 * ```
 *	"{\n"
 * 	     \"numbers\": [1, 2, 3],\n"
 * 	     \"name\": "hello\",\n"
 * 	     \"float\": 3.14,\n"
 * 	}"
 * ```
 *
 * Would have the following values:
 *
 * ```
 *	{ <----------------------------+
 * 	     "numbers": [1, 2, 3], <---|-+
 * 	     "name": "hello",  <-------|-|---+
 * 	     "float": 3.14,  <---------|-|---|-+
 * 	}                              | |   | |
 * 	                               | |   | |
 * 	type: EJOBJ, start: 0, len: 19<+ |   | |
 * 	                                 |   | |
 * 	type: EJKV, start: 2, len: 5  <--|   | |   // "numbers"
 * 	type: EJARR, start: 13, len: 4 <-+   | |
 * 	type: EJINT, start: 14, len: 1       | |   // 1
 * 	type: EJINT, start: 17, len: 1       | |   // 2
 * 	type: EJINT, start: 20, len: 1       | |   // 3
 * 	                                     | |
 * 	type: EJKV, start: 24, len: 2 <------| |   // "name"
 * 	type: EKSTR, start: 32, len: 1 <-----+ |   // "hello"
 * 	                                       |
 * 	type: EJKV, start: 42, len: 2 <--------|   // "float"
 * 	type: EJFLT, start: 9, len: 1 <--------+   // 3.14
 * ```
 *
 * Navigating the DOM:
 * -------------------
 * The DOM is parsed and stored in a depth first manner, so when traversing the
 * DOM, to access the inner or child token of an object, array, or key, just go
 * to the next token after it. Keys point to the next key in the object and to
 * access their value, you do the same by going to the token after them. To go
 * to the next token in the above parent object or array, skip \ref ejtok.len
 * entries beyond the current one. This length value also includes the token
 * itsself. So if you have something like a number token with no children, it
 * sill has a length of 1. If you are in a value from a key value pair, this
 * will skip to the next key name in the object. To know if you are beyond the
 * end of the DOM, just check the current index you are at against the number
 * of tokens returned by ejparse (\ref ejresult.ntoks) or the length of the
 * root object (\ref ejtok.len) in the DOM.
 *
 * Code Structure:
 * ===============
 * ejparse parses a json document and *partially* validates the file. By
 * partially, I mean that you can read the file without expecting any structure
 * breaking errors. You may still have invalid UTF8 codepoints, or float or
 * integer literals outside of normal range, but they should still be
 * structured correctly according to spec and you should be able to put those
 * strings through any normal parsing function without them crashing.
 * 
 * The other types of functions will take the begining of a JSON token, which
 * you can find by adding the start parameter in a JSON token to the start of
 * the JSON document's string, and it will either give you some sort of error
 * if its not a valid string, number, etc, or the value if it succeeded.
 *
 * Remarks:
 * ========
 * Due note that ekjson is still in development but has been put on hold so
 * that I can focus on new projects that I want to build. I'm open to feature
 * requests just due note the core tenats of the project:
 *
 *  - Low line of code count
 *  - Completely freestanding from the standard library (but not headers)
 *  - Fast code, but not at the comprimise of the other tenats
 *  - Simple code
 *
 * TODO List:
 * ----------
 * Features that I already have in mind but will put off until I need them are
 * as follows:
 *  1. Better error handling
 *  2. Find way to add array length parameter to EJARR tokens.
 *  3. More optmizations
 *  4. Cut down on code complexity
 *  5. SIMD implementations?
 *  6. Very basic JSON writer*
 * 
 * > *I feel a JSON writer is beyond the scope of this project. This is due to
 * >  the fact that atleast for me, the library will mainly be used to
 * >  parse json objects into c structs, which by that point serializing them
 * >  even to JSON is a trivial task and doesn't exactly require a big library.
 *
 */
#ifndef _ekjson_h_
#define _ekjson_h_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * \brief Removes big tables
 *
 * When set, removes big tables and stops inline for most functions in the
 * source code. Off by default, will use faster bigger tables.
 */
#ifndef EKJSON_SPACE_EFFICENT
#define EKJSON_SPACE_EFFICENT 0
#endif

/**
 * \brief Turns of 64bit bit 'twiddling' tricks
 *
 * When set, turns off 'bitwise' 64bit tricks. This will for sure be better
 * for 32 bit systems without fast 64-bit operations and systems without
 * branch overhead (due to mis-predicts).
 *
 * Off by default, takes advantage of most processors made within 20 years and
 * runs better on this setting.
 */
#ifndef EKJSON_NO_BITWISE
#define EKJSON_NO_BITWISE 0
#endif

/**
 * \brief Max nesting for json values
 *
 * Max value depth that the json document can go.
 * Ekjson uses recurrsion so keep that in mind when setting this number. This
 * number also isn't representative of the actual limit of the callstack when
 * using ejparse, it's just a limit of recursion in general.
 */
#ifndef EKJSON_MAX_DEPTH
#define EKJSON_MAX_DEPTH 64
#endif

/**
 * \brief Maximum number of bits in numbers for slow path of ejflt
 * 
 * \note This must be atleast a little bit above 1024 since the maximum
 * base 2 exponent in double precision floating point numbers is 1023. It can
 * be lower and ejflt will still work, it just means the slow path of ejflt
 * might return NAN more often if the significand can't fit in the bignum with
 * this many bits.
 */
#ifndef EKJSON_MAX_SIG
#define EKJSON_MAX_SIG (1024 + 512)
#endif

/**
 * Each ekjson token is one of these types. These are here to make checking
 * ekjson types easier and to make traversing the DOM simpler as the types are
 * baked into each token.
 */
enum ejtok_type {
	/**
	 * \brief Corresponds to a JSON object
	 *
	 * To access the children of this token object, go to the next token,
	 * to skip this object increment your token pointer/idx by the len
	 * struct variable.
	 */
	EJOBJ,

	/**
	 * \brief Corresponds to a JSON key
	 *
	 * To access the value of this object key/value pair, go to the next
	 * token, to skip this key/value pair increment your token pointer/idx
	 * by the len struct variable.
	 *
	 * To access the string, use normal string functions like ejcmp and
	 * ejstr.
	 */
	EJKV,

	/**
	 * \brief Corresponds to a JSON array
	 *
	 * To access the value of this object key/value pair, go to the next
	 * token, to skip this key/value pair increment your token pointer/idx
	 * by the len struct variable.
	 *
	 * \note The length parameter DOES not represent how many elements are
	 * in this JSON array, it only represents 1 plus how many tokens this
	 * array contains. If you know the tokens that *should* be in the array
	 * you can get the length by subtracting 1 and dividing by how many
	 * tokens each item is.
	 */
	EJARR,

	/**
	 * \brief Corresponds to a JSON string value
	 *
	 * This object is always a length of 1 due to it being a value type.
	 * The start parameter of these point to the opening quote of the
	 * string.
	 *
	 * To compare the string (in place) against a normal c string, simply
	 * call ejcmp. To copy the string to a c string buffer call ejstr.
	 */
	EJSTR,

	/**
	 * \brief Corresponds to a JSON number value with decimal or exponent.
	 *
	 * This type corresponds to a JSON number value with a decimal or
	 * exponent on the end. These can be parsed with ejflt.
	 */
	EJFLT,

	/**
	 * \brief Corresponds to a JSON number value without a decimal and
	 * without an exponent.
	 *
	 * This type only has integer parts. This can be parsed with ejint. It
	 * can also be parsed with ejflt.
	 */
	EJINT,

	/**
	 * \brief Corresponds to a JSON boolean value (true/false)
	 *
	 * The underlying boolean can be accessed with ejbool.
	 */
	EJBOOL,

	/**
	 * \brief Corresponds to a JSON null value
	 *
	 * There are no accessors for this type since it can only be the
	 * literal 'null'
	 */
	EJNULL
};

/**
 * \brief Basic building block of the JSON DOM
 *
 * # What these are
 * The DOM is parsed and stored in a depth first manner with these tokens
 * representing JSON values, objects, keys, and arrays.
 *
 * # Traversing the DOM with these
 * 1. To go to the next token, skipping children, increment whatever pointer
 *    or index you are using by \ref ejtok.len
 *    \note If you are in a key from a key value pair, this will skip the key
 * 2. To go to children tokens, just go to the next token
 * 3. Optionaly make sure you are not going beyond the bounds of the object
 *    you want to search by keeping in mind the length of the parent object and
 *    make sure you aren't going beyond the bounds of the document by checking
 *    against \ref ejresult.ntoks
 * 
 */
typedef struct ejtok {
	/**
	 * \brief Offset from the start of the source string.
	 */
	uint32_t start;

	/**
	 * \brief General type of the token (see \ref ejtok_type)
	 *
	 * \note The key type is the same as the string type except that their
	 * \ref ejtok.len parameter also contains the length of the value they
	 * hold.
	 */
	uint32_t type : 3;

	/**
	 * \brief Number of child tokens + 1
	 *
	 * Describes how many \ref ejtok this token takes up (including its
	 * children)
	 *
	 * - If this is a normal value token or a token with otherwise no
	 *   children, the length parameter will still be 1.
	 * - If this is a key token, then the length will also include the
	 *   value it holds.
	 */
	uint32_t len : 29;
} ejtok_t;

/**
 * \brief Result of an ekjson parsing routine
 *
 * Tells user if errors occured in parsing and contains information about where
 * parsing ended and how many tokens were outputed (roughly)
 */
typedef struct ejresult {
	/**
	 * \brief False if parsing succeeded
	 *
	 * Was this an error, or did we run out of toks
	 */
	bool err;

	/**
	 * \brief Rough location of where the error occured
	 *
	 * This is left NULL if no error occurred
	 *
	 * \warning If the error was due to lack of memory, then this won't
	 * be fully accurate
	 */
	const char *loc;

	/**
	 * \brief Number of tokens parsed
	 *
	 * If \ref ejresult returns that there was an error but this is set to
	 * the maximum number of tokens, then we ran out of buffer room.
	 * Reallocate and start from the begining.
	 *
	 * \warning This is only a rough estimate if an error occured in
	 * parsing.
	 */
	size_t ntoks;
} ejresult_t;

/**
 * \brief Parses and partially validates a json file, creating DOM in process.
 *
 * Writes tokens to the buffer provided (\p t).
 *
 * \param src Valid UTF-8/WTF-8 null-terminated string containing JSON
 * \param t Pointer to buffer to put the DOM into
 * \param nt Size of the buffer pointed to by \p t
 *
 * \returns Result containg info on how parsing went (see \ref ejresult)
 */
ejresult_t ejparse(const char *src, ejtok_t *t, size_t nt);

/**
 * \brief Copies JSON key/string to c string buffer unescaping along the way
 *
 * \warning If this function fails, the output buffer has undefined contents!
 *
 * \param tok_start Pointer to start of \ref ejtok_type.EJSTR or
 *	\ref ejtok_type.EJKV token (first quote).
 * \param out Pointer to where the escaped string should be copied to. This
 *	buffer, even if not big enough for the resulting string, will always be
 *	null-terminated. If left NULL, the function will just return the length
 *	of the unescaped string.
 * \param outlen Length of the buffer pointed to by \p out. If this is left 0
 *	the function will only compute the length of the unescaped string and
 *	return it.
 *
 * \returns Length of what the unescaped string would be if the buffer was
 *	large enough, including null terminator.
 *	\note This will be 0 if the string contains an invalid utf-8 codepoint
 *	or mangled utf-8 surrogate pair.
 */
size_t ejstr(const char *tok_start, char *out, size_t outlen);

/**
 * \brief Returns true if unescaped JSON string equals cstr
 *
 * Compares the string token to a normal c string, escaping characters as
 * needed and returning whether or not they are equal.
 *
 * \param tok_start Pointer to start of \ref ejtok_type.EJSTR or
 *	\ref ejtok_type.EJKV token (first quote).
 * \param cstr Non-NULL pointer to null-terminated c string.
 *
 * \returns True if they match
 */
bool ejcmp(const char *tok_start, const char *cstr);

/**
 * \brief Converts int token to int64_t
 *
 * Parses base 10 integers.
 *
 * \param tok_start Pointer to start of \ref ejtok_type.EJINT.
 *
 * \returns String converted to int64_t. If the number is outside of the
 * int64_t range, it will saturate it to the closest limit.
 */
int64_t ejint(const char *tok_start);

/**
 * \brief Converts float token to double
 *
 * Parses base 10 ECMA-404 float literals. If the number in tok_start has a
 * significand (after multiplying exponent) over \ref EKJSON_MAX_SIG, then the
 * function will return nan. To get this with default \ref EKJSON_MAX_SIG value
 * you would have to pass in a value with over ~100 sig figs with the max
 * or minimum exponent values.
 *
 * \note Currently does not handle subnormal doubles. This may change in the
 * future, but I kept it this way due to me wanting to use this in real time
 * applications (where the subnormal overhead would be undesirable).
 *
 * \param tok_start Pointer to start of \ref ejtok_type.EJINT or
 *	\ref ejtok_type.EJFLT.
 *
 * \returns String converted to double. If the number is outside of the
 * double range, it will saturate to +/-INF.
 */
double ejflt(const char *tok_start);

/**
 * \brief Returns \ref ejtok_type.EJBOOL true or false no error handling needed
 */
bool ejbool(const char *tok_start);

#endif

