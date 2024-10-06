#ifndef _ektoml_h_
#define _ektoml_h_

#include <stdbool.h>

// Define these macros to use different versions of the json parser
//
// Uncommenting this makes the library work with big endian processors
#ifndef JSON_LITTLE
#define JSON_LITTLE 1
#endif
//
// Uncommenting this enables the parser to break chunks up into 4 or 8
// bytes per loop iteration.
//#define JSON_BITWISE_SIMD 1
//
// Uncommenting this enables x86_64 SSE extensions for the parser
//#define JSON_SSE 1
//
// Uncommenting this enable Arm NEON extensions for the parser
//#define JSON_NEON 1
//

#define JSON_OBJECT 0			// JSON Object
#define JSON_LITERAL_STRING 1		// JSON String with no escape chars
#define JSON_STRING 2			// JSON String
#define JSON_ARRAY 3			// JSON Array
#define JSON_NUMBER 4			// JSON Number
#define JSON_TRUE 5			// JSON Bool (true)
#define JSON_FALSE 6			// JSON Bool (false)

// jsontok arrays are special in that they always start out with null to
// denote how much whitespace is present at the start of the document
#define JSON_NULL 7			// JSON Null

// Main building block of a JSON document
struct jsontok {
	// Number of chars until next token
	unsigned next;

	// type of the token
	unsigned type : 3;

	// Number of child tokens for this token
	// (Only applicable to arrays and objects)
	unsigned len : sizeof(unsigned) * 8 - 3;
};

// Returns a pointer to where an error occurred if it did.
// If no errors were present, then it returns NULL. Do note though that if
// extensions are being used the source should be aligned to an 4 or 8 byte
// boundary (depending on extension)
const char *json_parse(const char *src, struct jsontok *toks, unsigned ntoks);

// Tests if a json string is equal to a normal c string
bool json_streq(const char *jstr, const char *str);
// Escapes a json string and stores it in a buffer
// Returns if the buffer was big enough for the string
bool json_str(const char *jstr, char *buf, unsigned buflen);
// Gets the length of a json string in bytes when chars are escaped
// Returns 0 if the string is malformed
unsigned json_len(const char *jstr);
// Returns the json value parsed. Returns NAN if there was an error
double json_num(const char *jnum);
// Returns whether or not a json value is valid
bool json_validate_value(const char *jval, int type);
// Returns NULL if the string is a valid utf-8 string
// If it is not, it will return the first invalid character
const char *json_validate_utf8(const char *str);

#endif

