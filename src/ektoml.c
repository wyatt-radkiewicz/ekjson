#include <alloca.h>
#include <ctype.h>
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

	toml_table_info_t *root;

	uint8_t *arena_ptr, *arena_end;
};

static void set_err_oom(parser_t *parser) {
	SET_ERR(parser, "out of arena memory");
}

static void parse_whitespace(parser_t *parser, bool skip_newline) {
	while (*parser->src == ' ' || *parser->src == '\t'
		|| skip_newline && *parser->src == '\n') {
		parser->pos++;
		if (*parser->src++ == '\n') {
			parser->line++;
			parser->pos = 1;
		}
	}
}

// The length of utf8 character
// Returns 0 if the first character is invalid
static inline uint32_t utf8_header_char_length(const char start) {
	const uint32_t clz = __builtin_clz(~(uint8_t)start << 24);
	const uint32_t leading_len = clz + !clz;
	if (clz == 1 || leading_len > 4) return 0;
	return leading_len;
}

// Should be called on every utf8-character
static inline bool utf8_arbitrary_byte_valid(const char c) {
	switch ((uint8_t)c) {
	case 0xC0: case 0xC1:
	case 0xF5: case 0xF6: case 0xF7: case 0xF8: case 0xF9:
	case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xFE: case 0xFF:
		return false;
	default:
		return true;
	}
}

static inline bool utf8_is_continue_byte(const char c) {
	return (uint8_t)c >= 0x80 && (uint8_t)c <= 0xBF;
}

// Validate UTF-8 characters coming in (per-spec ofc)
// and push them onto the arena
// Returns whether or not it could allocate memory for the character
// If an invalid UTF-8 character is found, it will deal with it and print
// out an appropiate replacement character ('�' or U+FFFD in this instance)
static bool parse_char(parser_t *parser) {
	uint8_t *const mem_start = parser->arena_ptr;
	const char *const src_start = parser->src;
	uint32_t len = utf8_header_char_length(*parser->src);
	uint32_t codepoint = 0;
	if (!len || !utf8_arbitrary_byte_valid(*parser->src)) goto err;

	// Make sure we have enough memory for this character
	if (parser->arena_ptr + len > parser->arena_end) {
		set_err_oom(parser);
		return false;
	}

	// Validate and get first byte
	switch (len--) {
	case 4: codepoint |= (*parser->src & 0x07) << 18; break;
	case 3: codepoint |= (*parser->src & 0x0F) << 12; break;
	case 2: codepoint |= (*parser->src & 0x1F) << 6; break;
	case 1: codepoint = *parser->src; break;
	}
	*parser->arena_ptr++ = *parser->src++;
	if (!len) return true;

	// Validate continuation bytes
	for (; len; len--) {
		if (*parser->src == '\0') goto err;
		if (!utf8_is_continue_byte(*parser->src)) goto err;
		if (!utf8_arbitrary_byte_valid(*parser->src)) goto err;
		codepoint |= (*parser->src & 0x3F) << (len * 6);
		*parser->arena_ptr++ = *parser->src++;
	}

	// Validate codepoint
	if (codepoint > 0x10FFFF || codepoint > 0xD800 && codepoint <= 0xDFFF) goto err;
	return true;

err:
	parser->arena_ptr = mem_start;
	parser->src = src_start + 1;
	if (parser->arena_ptr + 3 > parser->arena_end) {
		set_err_oom(parser);
		return false;
	}
	memcpy(parser->arena_ptr, (char []){'\xEF', '\xBF', '\xBD'}, 3);
	parser->arena_ptr += 3;

	return false;
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
	if (parser->arena_ptr >= parser->arena_end) {
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

end:
	return parser->res;
}

