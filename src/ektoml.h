#ifndef _ektoml_h_
#define _ektoml_h_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// How many deep recursive functions in ektoml can go
// This is NOT the same as call stack depth, non recursive
// functions can make the call stack deeper than this limit
#define TOML_MAX_RECURSION_DEPTH 64

// See structure definition below for details
typedef struct toml toml_t;

// All the different types in a toml file
// TY(enum name, code name, type)
#define TOML_TYPES \
	/* NOTE: String is CONST qualified in code */ \
	TY(TOML_STRING, string, char *) \
	TY(TOML_INT, int, int64_t) \
	TY(TOML_FLOAT, float, double) \
	TY(TOML_BOOL, bool, bool) \
	TY(TOML_DATETIME_UNIX, datetime_unix, struct timespec) \
	TY(TOML_DATETIME_LOCAL, datetime_local, toml_datetime_local_t) \
	TY(TOML_DATE_LOCAL, date_local, struct tm) \
	TY(TOML_TIME_LOCAL, time_local, toml_time_local_t) \
	TY_SPECIAL(TOML_ARRAY) \
	TY_SPECIAL(TOML_TABLE)

// An eunmeration of different types in a toml file
typedef enum toml_type {
#define TY(ename, cname, ty) ename,
#define TY_SPECIAL(ename) ename,
	TOML_TYPES
#undef TY
#undef TY_SPECIAL
} toml_type_t;

// Represents a local time with no offset from an absolute time (like UTC)
// Actual time is interpreted by user or system
typedef struct toml_time_local {
	// Hour and minute of unspecified day
	uint8_t hour, min;

	// Seconds into the minute plus nanoseconds
	struct timespec sec;
} toml_time_local_t;

// Represents a date and time with no offset from an absolute time (like UTC)
// Actual date and time are interpreted by user or system
typedef struct toml_datetime_local {
	// Normal C date struct with time included
	struct tm date;

	// Nano seconds into the current second
	uint32_t nano;
} toml_datetime_local_t;

// Constructs the memory (struct, array, etc) representing the toml structure, 
// which could be any of the types above.
// Should return a non-null pointer to that data or something else, or NULL
// upon failure
typedef void *(toml_ctor_fn)(const toml_t *parent, void *user);

// toml value constructors
// These return true if they pass, false if they don't
#define TY(ename, cname, ty) \
	typedef bool (toml_##cname##_fn)(void *data, const toml_t *in, \
					void *user, const ty val);
#define TY_SPECIAL(ename)
	TOML_TYPES
#undef TY
#undef TY_SPECIAL

typedef struct toml_val toml_val_t;

// A union of constructors for different toml types
typedef union toml_val_data {
	// These functions simply set the data for the current table/array
	toml_string_fn *set_string;
	toml_int_fn *set_int;
	toml_float_fn *set_float;
	toml_bool_fn *set_bool;
	toml_datetime_unix_fn *set_datetime_unix;
	toml_datetime_local_fn *set_datetime_local;
	toml_date_local_fn *set_date_local;
	toml_time_local_fn *set_time_local;

	// Refrence to the inner array types
	const toml_val_t *array_start;

	// Refrence to a table
	// The kv pairs in here must be ordered alphebetically by key name
	const toml_t *table_start;
} toml_val_data_t;

struct toml_val {
	// Setter / data associated with this type
	toml_val_data_t data;

	// Length of array / number of kv pairs in table
	uint32_t len;

	// Leave this to 0, this is a generational boolean for if this value
	// has been visited or not yet
	uint16_t _parsed_gen;

	// Length of the pattern for the array type
	uint16_t pattern_len : 11;

	// Type of the value
	uint16_t type : 4;

	// NOTE: Only used in kv pair inside of table
	// Specifies if this value is optional
	uint16_t isoptional : 1;
};

// Maximum number of bytes that a toml key can be (including null-terminator)
#define TOML_MAX_KEY_SIZE 32

// Key/value type in a toml file
struct toml {
	// Name of this key
	const char *name;

	// What function gets run before this type is parsed?
	toml_ctor_fn *ctor;

	// Leave this to 0
	uint32_t _num_parsed;

	// Set this to the number of values that must be parsed
	// (number of values that are not optional in this table)
	uint32_t num_needed;

	// Value of the key-value pair
	toml_val_t val;
};

// Result of an toml parsing operation
typedef struct toml_result {
	// Did everything parse okay?
	uint64_t okay : 1;

	uint64_t _completely_parsed : 1;

	// If not, here is the line it failed on
	uint64_t line : 62;

	// Parsing failed here in this part of the schema
	const toml_t *in;

	// Optional message explaining what happened
	const char *msg;
} toml_result_t;

// Structure to be passed into toml_parse
typedef struct toml_args {
	// The source code
	const uint8_t *src;
	
	// Root toml table
	size_t root_len;
	toml_t *root_table;

	// Optional user data
	void *user;

	// Since the parser uses a generation count to check if a value has
	// been parsed, there needs to be a pointer to the current generation,
	// this is also useful because then if the generation overflows, the
	// parser will reset all generation counts (at a cost of time).
	uint16_t *gen;
} toml_args_t;

// Parses a toml file using schema and calls the parsing functions in the
// schema with the user_data parameter.
// NOTE: the schema 
toml_result_t toml_parse(const toml_args_t args);

#endif

