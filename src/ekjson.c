#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ekjson.h"

struct state {
	const char *start, *src;
	struct jsontok *toks;
	unsigned ntoks, tok;
};

// Returns 0xFFFFFFFF for anything that is not a digit
static uint32_t hextou32(const uint8_t h) {
	const static uint8_t t[256] = {
		['0'] = 0x01, ['1'] = 0x02, ['2'] = 0x03, ['3'] = 0x04,
		['4'] = 0x05, ['5'] = 0x06, ['6'] = 0x07, ['7'] = 0x08,
		['8'] = 0x09, ['9'] = 0x0A, ['A'] = 0x0B, ['B'] = 0x0C,
		['C'] = 0x0D, ['D'] = 0x0E, ['E'] = 0x0F, ['F'] = 0x10,
		['a'] = 0x0B, ['b'] = 0x0C, ['c'] = 0x0D, ['d'] = 0x0E,
		['e'] = 0x0F, ['f'] = 0x10,
	};
	return (uint32_t)t[h] - 1;
}

static double _pow10(int n) {
	double x = 1.0;

	if (n > 0) {
		const static double ex[] = {
			1.0e0,  1.0e1,  1.0e2,  1.0e3,
			1.0e4,  1.0e5,  1.0e6,  1.0e7,
			1.0e8,  1.0e9,  1.0e10, 1.0e11,
			1.0e12, 1.0e13, 1.0e14, 1.0e15,
		};

		for (; n >= 15; n -= 15) x *= ex[15];
		x *= ex[n];
	} else if (n < 0) {
		const static double ex[] = {
			1.0e-0,  1.0e-1,  1.0e-2,  1.0e-3,
			1.0e-4,  1.0e-5,  1.0e-6,  1.0e-7,
			1.0e-8,  1.0e-9,  1.0e-10, 1.0e-11,
			1.0e-12, 1.0e-13, 1.0e-14, 1.0e-15,
		};

		for (; n <= -15; n += 15) x *= ex[15];
		x *= ex[-n];
	}

	return x;
}

static void whitespace(const char **src) {
	for (; **src == ' ' || **src == '\n'
		|| **src == '\r' || **src == '\t'; ++*src);
}

// Default implementation;
struct escape {
	char buf[4];
	int len;		// 0 on error
};

// Call with jstr pointing to '\\' character
static struct escape escape(const char **const jstr) {
	struct escape e = { .len = 1 };

	switch (*(*jstr)++) {
	case '"': e.buf[0] = '"'; return e;
	case '\\': e.buf[0] = '\\'; return e;
	case '/': e.buf[0] = '/'; return e;
	case 'b': e.buf[0] = '\b'; return e;
	case 'f': e.buf[0] = '\f'; return e;
	case 'n': e.buf[0] = '\n'; return e;
	case 'r': e.buf[0] = '\r'; return e;
	case 't': e.buf[0] = '\t'; return e;
	case 'u': break;
	default: e.len = 0; return e;
	}

	const uint32_t codept = hextou32((*jstr)[0]) << 12
		| hextou32((*jstr)[1]) << 8
		| hextou32((*jstr)[2]) << 4
		| hextou32((*jstr)[3]);

	if (codept < 0x80) {
		e.buf[0] = codept;
		e.len = 1;
	} else if (codept < 0x800) {
		e.buf[0] = 0xC0 | codept >> 6;
		e.buf[1] = 0x80 | (codept & 0x3F);
		e.len = 2;
	} else if (codept < 0xD800 || codept > 0xDFFF && codept < 0x10000) {
		e.buf[0] = 0xE0 | codept >> 12;
		e.buf[1] = 0x80 | ((codept >> 6) & 0x3F);
		e.buf[2] = 0x80 | (codept & 0x3F);
		e.len = 3;
	} else if (codept < 0x110000) {
		e.buf[0] = 0xE0 | codept >> 18;
		e.buf[1] = 0x80 | ((codept >> 12) & 0x3F);
		e.buf[2] = 0x80 | ((codept >> 6) & 0x3F);
		e.buf[3] = 0x80 | (codept & 0x3F);
		e.len = 4;
	} else {
		e.len = 0;
	}

	return e;
}

static unsigned value(struct state *state);

const char *json_parse(const char *src, struct jsontok *toks,
			unsigned ntoks) {
	struct state state = {
		.start = src,
		.src = src,
		.toks = toks,
		.ntoks = ntoks,
		.tok = 0,
	};

	// Get root value
	return value(&state) ? NULL : src;
}

static unsigned array(struct state *state, struct jsontok *arr) {
	arr->type = JSON_ARRAY;

	// Parse key value pairs until you find a ] instead of ,
	whitespace(&state->src);
	while (*state->src != ']') {
		// Parse value
		unsigned tmp = value(state);
		if (!tmp) return 0;
		arr->len += tmp;
		whitespace(&state->src);
		if (*state->src == ',') ++state->src;
	}
	state->src++;
	
	return arr->len;
}

static unsigned string(struct state *state, struct jsontok *str) {
	str->start++;
	str->type = JSON_STRING;
	
	// Continue until , } \0 or ] again, but NOT in string
	while (true) {
		for (; *state->src != '"' && *state->src != '\\'
			&& *state->src != '\0'; ++state->src);
		switch (*state->src) {
		case '"':
			state->src++;
			return str->len;
		case '\\':
			state->src++;
			break;
		case '\0':
			return 0;
		}
	}
}

static unsigned object(struct state *state, struct jsontok *obj) {
	obj->type = JSON_OBJECT;
	
	// Parse key value pairs until you find a } instead of ,
	whitespace(&state->src);
	while (*state->src != '}') {
		// Parse string first
		if (state->tok == state->ntoks
			|| *state->src != '"') return 0;
		struct jsontok *str = state->toks + state->tok++;
		str->start = state->src++ - state->start;
		str->len = 1;
		if (!string(state, str)) return 0;
		if (*state->src++ != ':') return 0;
		obj->len++;

		// Parse value
		unsigned tmp = value(state);
		if (!tmp) return 0;
		obj->len += tmp;
		str->len += tmp;

		whitespace(&state->src);
		if (*state->src == ',') ++state->src;
	}
	state->src++;

	return obj->len;
}

// Returns the number of tokens this value took up
static unsigned value(struct state *state) {
	whitespace(&state->src);
	if (state->tok == state->ntoks) return 0;
	struct jsontok *tok = state->toks + state->tok++;
	tok->start = state->src - state->start;
	tok->len = 1;

	switch (*state->src++) {
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9': case '-':
		tok->type = JSON_NUMBER; break;
	case 't': tok->type = JSON_TRUE; break;
	case 'f': tok->type = JSON_FALSE; break;
	case 'n': tok->type = JSON_NULL; break;

	// Long paths
	case '"': if (!string(state, tok)) return 0; else break;
	case '[': if (!array(state, tok)) return 0; else break;
	case '{': if (!object(state, tok)) return 0; else break;
	case '\0': return 0;
	default: return 1;
	}
	
	for (; *state->src != '\0' && *state->src != '}' && *state->src != ':'
		&& *state->src != ']' && *state->src != ','; ++state->src);
	return tok->len;
}

// Default niave impelmentation
bool json_streq(const char *jstr, const char *str) {
	while (true) {
		for (; *jstr != '"' && *jstr != '\\'; jstr++, str++) {
			if (*jstr != *str) return false;
		}
		if (*jstr == '"') return true;
		
		// Test escape character
		const struct escape e = escape(&jstr);
		switch (e.len) {
		case 0: return false;
		case 4: if (e.buf[3] != str[3]) return false;
		case 3: if (e.buf[2] != str[2]) return false;
		case 2: if (e.buf[1] != str[1]) return false;
		case 1: if (e.buf[0] != str[0]) return false;
		}

		str += e.len;
	}
}

// Default implementation
bool json_str(const char *jstr, char *buf, unsigned buflen) {
	for (char *const end = buf + buflen;;) {
		for (; *jstr != '"' && *jstr != '\\' && buf < end;
			jstr++, buf++) {
			if (*jstr != *buf) return false;
		}
		if (*jstr == '"') {
			if (buf == end) return false;
			*buf++ = '\0';
			return true;
		}
		
		// Test escape character
		const struct escape e = escape(&jstr);
		if (e.len == 0 || buf + e.len >= end) return false;
		switch (e.len) {
		case 4: if (e.buf[3] != buf[3]) return false;
		case 3: if (e.buf[2] != buf[2]) return false;
		case 2: if (e.buf[1] != buf[1]) return false;
		case 1: if (e.buf[0] != buf[0]) return false;
		}

		buf += e.len;
	}
}

unsigned _json_len(const char **jstr) {
	for (unsigned len = 0;;) {
		for (; **jstr != '"' && **jstr != '\\'; ++*jstr, ++len);
		if (**jstr == '"') return len + 1;
		const struct escape e = escape(jstr);
		if (!(len += e.len)) return 0;
	}
}
unsigned json_len(const char *jstr) {
	return _json_len(&jstr);
}

// Default implementation (slow, but works)
static double _json_num(const char **const jnum) {
	double sign = **jnum == '-' ? -1.0 : 1.0;

	*jnum += **jnum == '-';
	if (**jnum == '0') {
		switch (*++*jnum) {
		case '.': ++*jnum; goto fractional;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9': return NAN;
		default: return 0.0;
		}
	}

	double n = 0.0;
	while (**jnum >= '0' && **jnum <= '9') {
		n *= 10.0;
		n += (double)(*(*jnum)++ - '0');
	}

	if (**jnum != '.') return n * sign;
	++*jnum;

fractional:
	if (**jnum < '0' || **jnum > '9') return NAN;
	for (double p = 0.1; **jnum >= '0' && **jnum <= '9'; p *= 0.1) {
		n += (double)(*(*jnum)++ - '0') * p;
	}

	if (**jnum != 'E' && **jnum != 'e') return n * sign;

	int esign;
exponent:
	++*jnum;
	esign = **jnum == '-' ? -1 : 1;
	*jnum += **jnum == '-' || **jnum == '+';

	int en = 0.0;
	while (**jnum >= '0' && **jnum <= '9') {
		en *= 10.0;
		en += *(*jnum)++ - '0';
	}

	return n * sign * _pow10(en * esign);
}
int64_t json_int(const char *jnum) {
	int64_t sign = *jnum == '-' ? -1.0 : 1.0;

	jnum += *jnum == '-';

	uint64_t n = 0;
	while (*jnum >= '0' && *jnum <= '9') {
		n *= 10;
		n += (uint64_t)(*jnum++ - '0');
	}

	if (n > INT64_MAX) return (int64_t)n;
	else return (int64_t)n * sign;
}
uint64_t json_uint(const char *jnum) {
	uint64_t n = 0;
	while (*jnum >= '0' && *jnum <= '9') {
		n *= 10;
		n += (uint64_t)(*jnum++ - '0');
	}

	return (uint64_t)n;
}
double json_flt(const char *jnum) {
	return _json_num(&jnum);
}

bool json_validate_value(const char *src, const struct jsontok tok, int type) {
	src += tok.start;
	switch (tok.type) {
	case JSON_OBJECT:
	case JSON_ARRAY: return true;
	case JSON_LITERAL_STRING:
	case JSON_STRING:
		_json_len(&src);
		src++;
	case JSON_NUMBER:
		if (isnan(_json_num(&src))) return false;
		break;
	case JSON_TRUE:
		if (memcmp(src, "true", 4) != 0) return false;
		src += 4;
		break;
	case JSON_FALSE:
		if (memcmp(src, "false", 5) != 0) return false;
		src += 5;
		break;
	case JSON_NULL:
		if (memcmp(src, "null", 4) != 0) return false;
		src += 4;
		break;
	default: return false;
	}

	whitespace(&src);
	switch (*src) {
	case '\0': case '}': case ':': case ']': case ',': return true;
	default: return false;
	}
}

// Just says everythings valid (for now)
const char *json_validate_utf8(const char *str) {
	return NULL;
}

