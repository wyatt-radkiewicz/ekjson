#ifndef _ektoml_h_
#define _ektoml_h_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

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
typedef void *(toml_ctor_fn)(const toml_t *initializing, void *user);

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

	// Length of the pattern for the array type
	uint16_t pattern_len;

	// Type of the value
	uint8_t type;

	// NOTE: Only used in kv pair inside of table
	// Specifies if this value is optional
	bool optional;
};

// Key/value type in a toml file
struct toml {
	// Name of this key
	const char *name;

	// What function gets run before this type is parsed?
	toml_ctor_fn *ctor;

	// Value of the key-value pair
	toml_val_t val;
};

// Result of an toml parsing operation
typedef struct toml_result {
	// Did everything parse okay?
	uint64_t okay : 1;

	// If not, here is the line it failed on
	uint64_t line : 63;

	// Parsing failed here in this part of the schema
	const toml_t *in;
} toml_result_t;

// Parses a toml file using schema and calls the parsing functions in the
// schema with the user_data parameter.
toml_result_t toml_parse(const uint8_t *src, size_t root_table_len,
			const toml_t schema_root_table[static root_table_len],
			void *user_data);

#endif

