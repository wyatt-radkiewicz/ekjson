#ifndef _ektoml_h_
#define _ektoml_h_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef enum toml_type {
	TOML_STRING,
	TOML_INT,
	TOML_FLOAT,
	TOML_BOOL,
	TOML_DATETIME_UNIX,
	TOML_DATETIME,
	TOML_DATE,
	TOML_TIME,
	TOML_ARRAY,
	TOML_TABLE,
	TOML_ANY,
} toml_type_t;

typedef struct toml toml_t;
typedef struct toml_val toml_val_t;
typedef struct toml_res toml_res_t;
typedef struct toml_table_info toml_table_info_t;
typedef struct toml_array_info toml_array_info_t;

struct toml_table_info {
	// Custom data
	void *data;

	// Leave NULL to signify error
	// This points to key-value pairs of the table ordered
	// alphebetically by key name
	const toml_t *start;

	// Length of table array
	// If this is 0, then this table can have a variable amount of kv-pairs
	size_t len;

	// Used internally, no need to allocate room for it
	void *_kv[];
};
struct toml_array_info {
	// Custom data
	void *data;

	// Leave NULL to signify error
	const toml_val_t *type;

	// Maximum length of the array (0 for flexible sized arrays)
	uint32_t cap;

	// Used internally, no need to allocate room for it
	uint32_t _len;
};

typedef bool toml_load_string_fn(void *parent_data, const char *val);
typedef bool toml_load_int_fn(void *parent_data, int64_t val);
typedef bool toml_load_float_fn(void *parent_data, double val);
typedef bool toml_load_bool_fn(void *parent_data, bool val);
typedef bool toml_load_datetime_unix_fn(void *parent_data, struct timespec val);
typedef bool toml_load_datetime_fn(void *parent_data, struct tm datetime, uint32_t nano);
typedef bool toml_load_date_fn(void *parent_data, struct tm date);
typedef bool toml_load_time_fn(void *parent_data, uint32_t hour, uint32_t min,
				struct timespec sec);
typedef void toml_load_any_fn(void *parent_data, toml_type_t type, void *val);
typedef toml_table_info_t toml_load_table_fn(void *parent_data);
typedef toml_array_info_t toml_load_array_fn(void *parent_data);

struct toml_val {
	toml_type_t type;
	bool optional;
	union {
		toml_load_string_fn *load_string;
		toml_load_int_fn *load_int;
		toml_load_float_fn *load_float;
		toml_load_bool_fn *load_bool;
		toml_load_datetime_unix_fn *load_datetime_unix;
		toml_load_datetime_fn *load_datetime;
		toml_load_date_fn *load_date;
		toml_load_time_fn *load_time;
		toml_load_any_fn *load_any;
		toml_load_table_fn *load_table;
		toml_load_array_fn *load_array;
	};
};

struct toml {
	const char *name;
	const toml_val_t val;
};

struct toml_res {
	// Did an error occur?
	bool ok;

	// Where in the source code the error occurred
	uint32_t line, pos;

	// Diagnosis info for the error
	const char *msg;
};

// toml_parse expects an already valid utf-8 string
toml_res_t toml_parse(const char *src,
			const toml_table_info_t schema_root,
			void *arena, // If NULL, arena_len bytes are alloca'd
			size_t arena_len); // If 0, default is 4kb

#endif

