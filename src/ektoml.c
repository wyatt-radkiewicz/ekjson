#include <alloca.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "ektoml.h"

#define SET_ERR(_parser, _msg) ((_parser)->res = (toml_res_t){ \
		.ok = false, \
		.line = (_parser)->line, .pos = (_parser)->pos, \
		.msg = (_msg) \
	})

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

static toml_table_info_t *keyid_add_table_info(parser_t *parser,
						toml_table_info_t *table,
						int keyid) {
	const toml_t *key = table->start + keyid;

	if (key->val.type != TOML_TABLE) {
		SET_ERR(parser, "expected type to be table");
		return NULL;
	}

	const toml_table_info_t tableinf = key->val.load_table(table->data);
	if (!tableinf.start) return NULL;

	toml_table_info_t *new_table = add_table_info(parser, tableinf);
	if (!new_table) return NULL;
	table->_kv[keyid] = new_table;
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

		// Get table from table if we used . operator
		if (!(*table = keyid_add_table_info(parser, *table, keyid))) return -1;
	}
}

static void *parse_value(parser_t *parser, const toml_val_t *val) {
	parse_whitespace(parser, false);

	return NULL;
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

	void *val;
	if (!(val = parse_value(parser, &table->start[keyid].val))) return false;
	table->_kv[keyid] = val;

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

