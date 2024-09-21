#include <alloca.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ektoml.h"

#define SET_ERR(_parser, _msg) ((_parser)->res = (toml_res_t){ \
		.ok = false, \
		.line = (_parser)->line, .pos = (_parser)->pos, \
		.msg = (_msg) \
	})
#define GET_PTR(_ptr_with_flag) ((_ptr_with_flag) & ~0x1ull)
#define GET_FLAG(_ptr_with_flag) ((_ptr_with_flag) & 0x1ull)
#define PTR_WITH_FLAG(_ptr, _set) ((typeof(_ptr))((uintptr_t)(_ptr) \
					| (uintptr_t)(_set)))

typedef struct parser parser_t;

struct parser {
	const char *src;
	uint32_t line, pos;

	toml_res_t res;

	toml_table_info_t *root, *table;

	uint8_t *arena_ptr, *arena_end;
};

static void set_err_oom(parser_t *parser) {
	SET_ERR(parser, "out of arena memory");
}

static void set_err_type(parser_t *parser, toml_type_t type) {
	switch (type) {
	case TOML_STRING: SET_ERR(parser, "expected type of string"); break;
	case TOML_INT: SET_ERR(parser, "expected type of int"); break;
	case TOML_UINT: SET_ERR(parser, "expected type of uint"); break;
	case TOML_FLOAT: SET_ERR(parser, "expected type of float"); break;
	case TOML_BOOL: SET_ERR(parser, "expected type of bool"); break;
	case TOML_DATETIME_UNIX: SET_ERR(parser, "expected type of datetime"); break;
	case TOML_DATETIME: SET_ERR(parser, "expected type of datetime"); break;
	case TOML_DATE: SET_ERR(parser, "expected type of date"); break;
	case TOML_TIME: SET_ERR(parser, "expected type of time"); break;
	case TOML_ARRAY: SET_ERR(parser, "expected type of array"); break;
	case TOML_TABLE: SET_ERR(parser, "expected type of table"); break;
	case TOML_ANY: SET_ERR(parser, "expected type of any"); break;
	}
}

// Increment newline
static inline void parser_newline(parser_t *parser) {
	parser->line++, parser->pos = 1;
}

static void parse_whitespace(parser_t *parser, bool skip_newline) {
	while (*parser->src == ' ' || *parser->src == '\t'
		|| skip_newline && *parser->src == '\n') {
		parser->pos++;
		if (*parser->src++ == '\n') parser_newline(parser);
	}
}

static bool parse_escape(parser_t *parser) {
	uint32_t req_digits;

	parser->pos++;
	switch (*parser->src++) {
	case 'b': *parser->arena_ptr++ = '\b'; return true;
	case 't': *parser->arena_ptr++ = '\t'; return true;
	case 'n': *parser->arena_ptr++ = '\n'; return true;
	case 'f': *parser->arena_ptr++ = '\f'; return true;
	case 'r': *parser->arena_ptr++ = '\r'; return true;
	case '"': *parser->arena_ptr++ = '\"'; return true;
	case '\\': *parser->arena_ptr++ = '\\'; return true;
	default:
		SET_ERR(parser, "unknown escape sequence");
		return false;
	case 'u': req_digits = 4; break;
	case 'U': req_digits = 8; break;
	}

	uint32_t codepoint;
	const char *start = parser->src, *end = NULL;

	codepoint = strtoul(start, (char **)&end, 16);
	if (!end || end - start != req_digits) {
		SET_ERR(parser, "malformed unicode escape sequence");
		return false;
	}
	parser->pos += end - start, parser->src = end;

	if (codepoint >= 0xD800 && codepoint <= 0xDFFF || codepoint > 0x10FFFF) {
		SET_ERR(parser, "illegal unicode codepoint");
		return false;
	}

	// Write the codepoint
	if (codepoint < 0x80) {
		if (parser->arena_ptr == parser->arena_end) {
			set_err_oom(parser);
			return false;
		}
		*parser->arena_ptr++ = codepoint;
	} else if (codepoint < 0x800) {
		if (parser->arena_ptr + 2 > parser->arena_end) {
			set_err_oom(parser);
			return false;
		}
		*parser->arena_ptr++ = 0xC0 | codepoint >> 6 & 0x1F;
		*parser->arena_ptr++ = 0x80 | codepoint >> 0 & 0x3F;
	} else if (codepoint < 0x10000) {
		if (parser->arena_ptr + 3 > parser->arena_end) {
			set_err_oom(parser);
			return false;
		}
		*parser->arena_ptr++ = 0xE0 | codepoint >> 12 & 0x0F;
		*parser->arena_ptr++ = 0x80 | codepoint >> 6 & 0x3F;
		*parser->arena_ptr++ = 0x80 | codepoint >> 0 & 0x3F;
	} else {
		if (parser->arena_ptr + 4 > parser->arena_end) {
			set_err_oom(parser);
			return false;
		}
		*parser->arena_ptr++ = 0xF0 | codepoint >> 18 & 0x07;
		*parser->arena_ptr++ = 0x80 | codepoint >> 12 & 0x3F;
		*parser->arena_ptr++ = 0x80 | codepoint >> 6 & 0x3F;
		*parser->arena_ptr++ = 0x80 | codepoint >> 0 & 0x3F;
	}

	return true;
}

static inline bool parse_char(parser_t *parser) {
	if (parser->arena_ptr == parser->arena_end) {
		set_err_oom(parser);
		return false;
	}

	const char c = *parser->src++;
	parser->pos++;

	if (c == '\\') return parse_escape(parser);
	if (c == '\n') parser_newline(parser);

	*parser->arena_ptr++ = c;
	return true;
}

static char *parse_string(parser_t *parser) {
	const char start = *parser->src;
	const bool is_multiline = parser->src[0] == parser->src[1]
				&& parser->src[0] == parser->src[2];
	char *const str = (char *)parser->arena_ptr;

	parser->src += is_multiline ? 3 : 1;
	parser->pos += is_multiline ? 3 : 1;

	while (*parser->src != start) {
		if (!parse_char(parser)) return NULL;
	}
	if (parser->arena_ptr == parser->arena_end) {
		set_err_oom(parser);
		return NULL;
	}
	*parser->arena_ptr++ = '\0';

	if (is_multiline) {
		if (parser->src[1] != start || parser->src[2] != start) {
			SET_ERR(parser, "string end sequence does not match start");
			return NULL;
		}
		parser->src += 3, parser->pos += 3;
	} else {
		parser->src += 1, parser->pos += 1;
	}

	return str;
}

static inline bool istoml_keychar(const char c) {
	return isalnum(c) || c == '_' || c == '-';
}

static char *parse_word(parser_t *parser) {
	char *const str = (char *)parser->arena_ptr;

	if (!istoml_keychar(*parser->src)) {
		SET_ERR(parser, "expected key");
		return NULL;
	}

	while (istoml_keychar(*parser->src)) {
		if (parser->arena_ptr == parser->arena_end) {
			set_err_oom(parser);
			return NULL;
		}
		parser->src++, parser->pos++;
	}
	if (parser->arena_ptr == parser->arena_end) {
		set_err_oom(parser);
		return NULL;
	}
	*parser->arena_ptr++ = '\0';

	return str;
}

// Add and init table info
static toml_table_info_t *add_table_info(parser_t *parser,
					const toml_table_info_t info) {
	toml_table_info_t *table = (toml_table_info_t *)parser->arena_ptr;
	parser->arena_ptr += sizeof(*table) + info.len * sizeof(void *);
	if (parser->arena_ptr >= parser->arena_end) {
		set_err_oom(parser);
		return NULL;
	}
	*table = info;
	memset(table->_kv, 0, sizeof(*table->_kv) * table->len);
	return table;
}

// Skip byte-order mark (BOM) for src since its UTF-8 and doesn't need a BOM
static void skip_bom(parser_t *parser) {
	if (strncmp(parser->src, (char []){'\xEF', '\xBB', '\xBF'}, 3) == 0) {
		parser->src += 3;
	}
}

static int search_key(parser_t *parser, toml_table_info_t *table,
				const char *key) {
	if (table->len < 8) {
		for (int i = 0; i < table->len; i++) {
			if (strcmp(table->start[i].name, key) != 0) continue;
			return i;
		}
	} else {
		int last = 0;
		for (int i = table->len / 2; last != i;) {
			last = i;
			int order = strcmp(key, table->start[i].name);
			if (order < 0) {
				i /= 2;
			} else if (order > 0) {
				i += (table->len - i) / 2;
			} else {
				return i;
			}
		}
	}

	SET_ERR(parser, "key name not found");
	return -1;
}

static char *parse_single_key(parser_t *parser) {
	if (*parser->src == '"' || *parser->src == '\'') return parse_string(parser);
	else return parse_word(parser);
}

// Add table info and add its reference to the key in the table
static toml_table_info_t *keyid_add_table_info(parser_t *parser,
						toml_table_info_t *table,
						int keyid) {
	const toml_t *key = table->start + keyid;

	if (key->val.type != TOML_TABLE) {
		set_err_type(parser, TOML_TABLE);
		return NULL;
	}

	const toml_table_info_t tableinf = key->val.load_table(table->data);
	if (!tableinf.start) return NULL;

	toml_table_info_t *new_table = add_table_info(parser, tableinf);
	if (!new_table) return NULL;
	table->_kv[keyid] = PTR_WITH_FLAG(new_table, true);
	return new_table;
}

// Returns keyid found in table
static int parse_key(parser_t *parser, toml_table_info_t **table) {
	char *name;

	while (true) {
		// Get the name of the key
		parse_whitespace(parser, false);
		if (!(name = parse_single_key(parser))) return -1;
		parse_whitespace(parser, false);

		// Search for it in the current table
		int keyid = search_key(parser, *table, name);
		if (keyid != -1) return -1;
		parser->arena_ptr = (uint8_t *)name;

		// End
		if (*parser->src != '.') return keyid;
		parser->src++, parser->pos++;

		// Get table from table if we used . operator
		if (!(*table = keyid_add_table_info(parser, *table, keyid))) return -1;
	}
}

static bool parse_table(parser_t *parser, void *restrict parent_data,
			const toml_val_t *val, void **data_out) {
	return false;
}

static bool parse_array(parser_t *parser, void *restrict parent_data,
			const toml_val_t *val, void **data_out) {
	return false;
}

typedef struct parsed_number {
	bool okay : 1;

	bool used_sign : 1;
	bool used_padding : 1;
	bool is_float : 1;

	int32_t leading_zeros;
	int32_t base;
	int32_t ndigits;

	union {
		int64_t int_literal;
		uint64_t uint_literal;
		double float_literal;
	};
} parsed_number_t;

typedef struct parsed_number_metadata {
	bool okay;
	bool is_neg;
	int32_t decimal_pt;
	uint8_t *digits;
} parsed_number_metadata_t;

static parsed_number_metadata_t get_digits(parser_t *parser, parsed_number_t *num) {
	parsed_number_metadata_t md = {
		.decimal_pt = -1,
		.digits = parser->arena_ptr,
	};

	if (!isdigit(*parser->src)) {
		SET_ERR(parser, "expected number");
		goto ret;
	}

	if (*parser->src == '+' || *parser->src == '-') {
		md.is_neg = *parser->src++ == '-';
		parser->pos++;
		num->used_sign = true;
	}

	if (parser->src[0] == '0') {
		if (parser->src[1] == 'x') num->base = 16;
		else if (parser->src[2] == 'b') num->base = 1;
		else if (parser->src[3] == 'o') num->base = 8;
		if (num->base != 10) parser->src += 2, parser->pos += 2;
	}

	if (num->base != 10 && num->used_sign) {
		SET_ERR(parser, "sign used with non base-10 integer!");
		goto ret;
	}

	int last_padding = -1;
	while (true) {
		if (parser->arena_ptr == parser->arena_end) {
			set_err_oom(parser);
			goto ret;
		}

		switch (*parser->src) {
		case '2': case '3': case '4': case '5': case '6': case '7':
			if (num->base < 8) {
				SET_ERR(parser, "unexpected digits in number");
				goto ret;
			}
		case '8': case '9':
			if (num->base < 10) {
				SET_ERR(parser, "unexpected digits in number");
				goto ret;
			}
		case '0': case '1':
			if (*parser->src != '0') num->leading_zeros = num->ndigits;
			*parser->arena_ptr++ = *parser->src++ - '0';
			parser->pos++;
			break;
		case '_':
			if (num->ndigits - 1 == md.decimal_pt) {
				SET_ERR(parser, "expected digits before padding");
				goto ret;
			}
			last_padding = num->ndigits;
			parser->src++, parser->pos++;
			continue;
		case '.':
			if (num->ndigits - 1 == last_padding) {
				SET_ERR(parser, "need digits around decimal");
				goto ret;
			}
			if (num->base != 10) {
				SET_ERR(parser, "unexpected character in int");
				goto ret;
			}
			if (md.decimal_pt != -1) {
				SET_ERR(parser, "multiple decimal points in num");
				goto ret;
			}
			md.decimal_pt = num->ndigits;
			parser->src++, parser->pos++;
			continue;
		case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
			if (num->base < 16) {
				SET_ERR(parser, "unexpected digits in number");
				goto ret;
			}
			*parser->arena_ptr++ = *parser->src++ - 'a';
			parser->pos++;
			break;
		case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
			if (num->base < 16) {
				SET_ERR(parser, "unexpected digits in number");
				goto ret;
			}
			*parser->arena_ptr++ = *parser->src++ - 'A';
			parser->pos++;
			break;
		}

		num->ndigits++;
	}
	num->used_padding = last_padding != -1;

	if (last_padding + 1 == num->ndigits) {
		SET_ERR(parser, "expected digits after padding");
		goto ret;
	}
	if (md.decimal_pt + 1 == num->ndigits) {
		SET_ERR(parser, "expected digits after decimal point");
		goto ret;
	}

	md.okay = true;
ret:
	return md;
}

// Only works with non-negative numbers
static bool do_uint(parser_t *parser, parsed_number_t *num,
		parsed_number_metadata_t *md) {
	num->uint_literal = 0;

	if (num->ndigits > 20) {
		SET_ERR(parser, "integer over/underflow");
		return false;
	}

	uint64_t pow = 1;
	for (int i = num->ndigits - 1; i > -1; i--, pow *= num->base) {
		if (UINT64_MAX - pow > num->uint_literal) {
			SET_ERR(parser, "integer over/underflow");
			return false;
		}

		num->uint_literal += pow * (uint64_t)md->digits[i];
	}

	return true;
}

// Only works with base 10
static bool do_int(parser_t *parser, parsed_number_t *num,
		parsed_number_metadata_t *md) {
	num->int_literal = 0;

	if (num->ndigits > 20) {
		SET_ERR(parser, "integer over/underflow");
		return false;
	}

	int64_t pow = md->is_neg ? -1 : 1;
	for (int i = num->ndigits - 1; i > -1; i--, pow *= 10) {
		if (!md->is_neg && INT64_MAX - pow > num->int_literal
			|| md->is_neg && INT64_MIN + pow < num->int_literal) {
			SET_ERR(parser, "integer over/underflow");
			return false;
		}

		num->int_literal += pow * (int64_t)md->digits[i];
	}

	return true;
}

static bool do_float(parser_t *parser, parsed_number_t *num,
		parsed_number_metadata_t *md) {
	num->float_literal = 0.0;

	double power = 1.0;
	for (int i = md->decimal_pt - 1; i > -1; i--, power *= 10) {
		num->float_literal += power * (double)md->digits[i];
	}

	power = 0.1;
	for (int i = md->decimal_pt; i < num->ndigits; i++, power *= 0.1) {
		num->float_literal += power * (double)md->digits[i];
	}
	num->float_literal *= md->is_neg ? -1.0 : 1.0;
	if (tolower(*parser->src) != 'e') return true;
	parser->src++, parser->pos++;
	
	parsed_number_t e_num = { .base = 10 };
	parsed_number_metadata_t e_md;
	if (!(e_md = get_digits(parser, &e_num)).okay) goto ret;
	if (e_md.decimal_pt != -1) {
		SET_ERR(parser, "expected integer number for exponent!");
		goto ret;
	}
	if (!do_int(parser, &e_num, &e_md)) goto ret;
	if (e_num.base != 10) {
		SET_ERR(parser, "expected base 10 for exponent!");
		goto ret;
	}
	
	num->float_literal *= pow(10.0, (double)e_num.int_literal);
ret:
	parser->arena_ptr = e_md.digits;
	return true;
}

static parsed_number_t parse_number(parser_t *parser) {
	parsed_number_t num = { .base = 10 };
	parsed_number_metadata_t md;

	if (!(md = get_digits(parser, &num)).okay) goto ret;
	if (md.decimal_pt != -1) {
		if (!do_float(parser, &num, &md)) goto ret;
	} else if (num.base != 10) {
		if (!do_uint(parser, &num, &md)) goto ret;
	} else {
		if (!do_int(parser, &num, &md)) goto ret;
	}
	

	num.okay = true;
ret:
	parser->arena_ptr = md.digits;
	return num;
}

// Returns -1 in error conditions
static int parse_dtnum(parser_t *parser, bool yearmode) {
	int num = 0;
	
	for (int pow = yearmode ? 1000 : 10; pow; parser->src++, parser->pos++) {
		num += (*parser->src - '0') * pow;
		if (!isdigit(*parser->src)) {
			SET_ERR(parser, "expected digits for date/time numbers");
			return -1;
		}
		pow /= 10;
	}

	return num;
}

typedef struct parsed_datetime {
	bool has_date, has_time, utc_base;

	union {
		struct {
			struct tm tm;
			uint32_t nano;
		};
		struct timespec unix;
	};
} parsed_datetime_t;

static bool parse_time(parser_t *parser, parsed_datetime_t *dt) {
	dt->has_time = true;

	if ((dt->tm.tm_hour = parse_dtnum(parser, false)) == -1) return false;
	if (*parser->src++ != ':') {
		SET_ERR(parser, "expected ':' after hour");
		return false;
	}
	parser->pos++;
	if (dt->tm.tm_hour > 23) {
		SET_ERR(parser, "expected hour to be in range 0-23");
		return false;
	}
	if ((dt->tm.tm_min = parse_dtnum(parser, false)) == -1) return false;
	if (*parser->src++ != ':') {
		SET_ERR(parser, "expected ':' after minute");
		return false;
	}
	parser->pos++;
	if (dt->tm.tm_min > 59) {
		SET_ERR(parser, "expected hour to be in range 0-59");
		return false;
	}
	if ((dt->tm.tm_sec = parse_dtnum(parser, false)) == -1) return false;
	if (dt->tm.tm_sec > 60) {
		SET_ERR(parser, "expected hour to be in range 0-60");
		return false;
	}

	if (*parser->src != '.') return false;
	parser->src++, parser->pos++;

	parsed_number_t num;
	if (!(num = parse_number(parser)).okay) return false;
	if (num.is_float || num.used_padding || num.base != 10 || num.used_sign) {
		SET_ERR(parser, "malformed seconds number in date/time");
		return false;
	}
	for (; num.ndigits > 9; num.ndigits--, num.int_literal /= 10);
	for (; num.ndigits < 9; num.ndigits++, num.int_literal *= 10);
	dt->nano = num.int_literal;
	return true;
}

static bool parse_date(parser_t *parser, parsed_datetime_t *dt) {
	dt->has_date = true;

	if ((dt->tm.tm_year = parse_dtnum(parser, true)) == -1) return false;
	if (*parser->src++ != '-') {
		SET_ERR(parser, "expected '-' after year");
		return false;
	}
	parser->pos++;
	if ((dt->tm.tm_mon = parse_dtnum(parser, false)) == -1) return false;
	if (*parser->src++ != '-') {
		SET_ERR(parser, "expected '-' after month");
		return false;
	}
	if (dt->tm.tm_mon == 0 || dt->tm.tm_mon > 12) {
		SET_ERR(parser, "expected month to be in range 1-12");
		return false;
	}
	parser->pos++;
	if ((dt->tm.tm_mday = parse_dtnum(parser, true)) == -1) return false;
	switch (dt->tm.tm_mon) {
	case 2:
		if (dt->tm.tm_year % 100 == 0
			? dt->tm.tm_year % 400 == 0
			: dt->tm.tm_year % 4 == 0) {
			if (dt->tm.tm_mday > 0 && dt->tm.tm_mday <= 29) break;
			SET_ERR(parser, "expected day to be in range 1-29");
			return false;
		} else {
			if (dt->tm.tm_mday > 0 && dt->tm.tm_mday <= 28) break;
			SET_ERR(parser, "expected day to be in range 1-28");
			return false;
		}
	case 4: case 6: case 9: case 11:
		if (dt->tm.tm_mday > 0 && dt->tm.tm_mday <= 31) break;
		SET_ERR(parser, "expected day to be in range 1-31");
		return false;
	default:
		if (dt->tm.tm_mday > 0 && dt->tm.tm_mday <= 31) break;
		SET_ERR(parser, "expected day to be in range 1-31");
		return false;
	}

	return true;
}

static bool parse_dtoffs(parser_t *parser, parsed_datetime_t *dt) {
	struct timespec ts = {
		.tv_sec = timegm(&dt->tm),
		.tv_nsec = dt->nano,
	};

	int64_t offs = 0;
	if (*parser->src == 'Z') {
		parser->src++, parser->pos++;
		goto add_offset;
	}

	bool neg = *parser->src++ == '-';
	parser->pos++;

	int64_t num;
	if ((num = parse_dtnum(parser, false)) == -1) return false;
	if (*parser->src++ != ':') {
		SET_ERR(parser, "expected ':' after hour offset");
		return false;
	}
	parser->pos++;
	offs += num * 60;
	if ((num = parse_dtnum(parser, false)) == -1) return false;
	offs += num;

	offs *= neg ? -1 : 1;
add_offset:
	ts.tv_sec += offs;
	dt->utc_base = true;
	dt->unix = ts;
	return true;
}

static bool parse_datetime(parser_t *parser, void *restrict parent_data,
			const toml_val_t *val, bool time_only) {
	parsed_datetime_t dt = {0};
	if (!time_only && !parse_date(parser, &dt)) return false;
	if (*parser->src == ' ' || *parser->src == 'T') {
		parser->src++, parser->pos++;
		if (!parse_time(parser, &dt)) return false;
	}
	if (dt.has_date &&
		(*parser->src == 'Z'
		 || *parser->src == '+'
		 || *parser->src == '-')) {
		if (!parse_dtoffs(parser, &dt)) return false;
	}

	if (dt.utc_base) {
		if (val->type != TOML_DATETIME_UNIX) goto type_err;
		return val->load_datetime_unix(parent_data, dt.unix);
	} else if (dt.has_date && dt.has_time) {
		if (val->type != TOML_DATETIME) goto type_err;
		return val->load_datetime(parent_data, dt.tm, dt.nano);
	} else if (dt.has_date) {
		if (val->type != TOML_DATE) goto type_err;
		return val->load_date(parent_data, dt.tm);
	} else {
		if (val->type != TOML_TIME) goto type_err;
		return val->load_time(parent_data, dt.tm.tm_hour, dt.tm.tm_min,
			(struct timespec){
				.tv_sec = dt.tm.tm_sec,
				.tv_nsec = dt.nano,
			});
	}

type_err:
	set_err_type(parser, val->type);
	return false;
}

static bool parse_digits(parser_t *parser, void *restrict parent_data,
			const toml_val_t *val) {
	// Save this so we can backup if we realize we are actually parsing
	// a date/time stamp
	const char *const src_ptr = parser->src;
	parsed_number_t num = parse_number(parser);
	if (!num.okay) return false;
	
	if (*parser->src == ':') {
		parser->src = src_ptr;
		return parse_datetime(parser, parent_data, val, true);
	} else if (*parser->src == '-') {
		parser->src = src_ptr;
		return parse_datetime(parser, parent_data, val, false);
	} else if (num.is_float) {
		if (val->type != TOML_FLOAT) {
			set_err_type(parser, val->type);
			return false;
		}
		return val->load_float(parent_data, num.float_literal);
	} else if (num.base != 10) {
		if (val->type != TOML_UINT) {
			set_err_type(parser, val->type);
			return false;
		}
		return val->load_uint(parent_data, num.uint_literal);
	} else {
		if (val->type != TOML_INT) {
			set_err_type(parser, val->type);
			return false;
		}
		if (num.leading_zeros) {
			SET_ERR(parser, "leading zeros in base 10 int!");
			return false;
		}
		return val->load_float(parent_data, num.int_literal);
	}
}

static bool parse_bool(parser_t *parser, void *restrict parent_data,
			const toml_val_t *val) {
	if (val->type != TOML_BOOL) {
		set_err_type(parser, val->type);
		return false;
	}

	char *keyword = parse_word(parser);
	if (!keyword) return false;
	parser->arena_ptr = (uint8_t *)keyword;

	bool bool_val;
	if (strcmp(keyword, "true") == 0) {
		bool_val = true;
	} else if (strcmp(keyword, "false") == 0) {
		bool_val = false;
	} else {
		SET_ERR(parser, "expected 'true' or 'false'");
		return false;
	}

	return val->load_bool(parent_data, bool_val);
}

static bool parse_value(parser_t *parser, void *restrict parent_data,
			const toml_val_t *val, void **data_out) {
	switch (*parser->src) {
	case '\'': case '"': {
		char *str = parse_string(parser);
		parser->arena_ptr = (uint8_t *)str;
		if (val->type != TOML_STRING) {
			set_err_type(parser, val->type);
			return false;
		} else {
			return val->load_string(parent_data, str);
		}
	} case 't': case 'f':
		return parse_bool(parser, parent_data, val);
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		return parse_digits(parser, parent_data, val);
	case '[':
		return parse_array(parser, parent_data, val, data_out);
	case '{':
		return parse_table(parser, parent_data, val, data_out);
	default:
		SET_ERR(parser, "unknown value type");
		return false;
	}
}

// Look for table headers eg. [table]
static bool parse_table_header(parser_t *parser) {
	parser->src++;
	parser->table = parser->root;

	int keyid;
	if ((keyid = parse_key(parser, &parser->table)) == -1) return false;
	if (!(parser->table = keyid_add_table_info(parser,
						parser->table, keyid))) return false;
	if (*parser->src++ != ']') {
		SET_ERR(parser, "expected ']' after table");
		return false;
	}
	parser->pos++;
	parse_whitespace(parser, false);
	if (*parser->src++ != '\n') {
		SET_ERR(parser, "expected newline after table");
		return false;
	}
	parser->pos++;
	parser_newline(parser);

	return true;
}

static bool parse_keyvalue(parser_t *parser) {
	toml_table_info_t *table = parser->table;
	int keyid;

	if ((keyid = parse_key(parser, &table)) == -1) return false;
	if (*parser->src++ != '=') {
		SET_ERR(parser, "expected '=' after key");
		return false;
	}
	parser->pos++;

	void *val = NULL;
	if (!parse_value(parser, table->data, &table->start[keyid].val, &val)) return false;
	table->_kv[keyid] = PTR_WITH_FLAG(val, true);

	parse_whitespace(parser, false);
	if (*parser->src != '\n' || *parser->src != '\0') {
		SET_ERR(parser, "expected newline after value");
		return false;
	}
	if (*parser->src) parser->src++, parser->line++, parser->pos = 1;

	return true;
}

// Returns whether or not to parse another line
static bool parse_line(parser_t *parser) {
	parse_whitespace(parser, true);
	switch (*parser->src) {
	case '[': return parse_table_header(parser);
	case '\0': return false;
	default: return parse_keyvalue(parser);
	}
}

toml_res_t toml_parse(const char *src, const toml_table_info_t schema_root,
			void *arena, size_t arena_len) {
	if (!arena_len) arena_len = 1024 * 4;
	if (!arena) arena = alloca(arena_len);

	parser_t *const parser = arena;
	*parser = (parser_t){
		.src = src,
		.line = 1, .pos = 1,
	
		.res = { .ok = true },

		.arena_ptr = (uint8_t *)(parser + 1),
		.arena_end = arena + arena_len / sizeof(void *),
	};
	skip_bom(parser);
	if (!(parser->root = add_table_info(parser, schema_root))) goto end;
	parser->table = parser->root;
	while (parse_line(parser));

end:
	return parser->res;
}

