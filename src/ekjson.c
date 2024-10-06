#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "ekjson.h"

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

		for (; n >= 16; n -= 16) x *= ex[15];
		x *= ex[n];
	} else if (n < 0) {
		const static double ex[] = {
			1.0e-0,  1.0e-1,  1.0e-2,  1.0e-3,
			1.0e-4,  1.0e-5,  1.0e-6,  1.0e-7,
			1.0e-8,  1.0e-9,  1.0e-10, 1.0e-11,
			1.0e-12, 1.0e-13, 1.0e-14, 1.0e-15,
		};

		for (; n <= -16; n += 16) x *= ex[15];
		x *= ex[-n];
	}

	return x;
}

static unsigned whitespace(const char **src) {
	unsigned len = 0;
	for (; **src == ' ' || **src == '\n'
		|| **src == '\r' || **src == '\t'; ++*src, ++len);
	return len;
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

static unsigned value(const char **src, struct jsontok *toks,
		unsigned ntoks, unsigned tok);

const char *json_parse(const char *src, struct jsontok *toks,
			unsigned ntoks) {
	toks[0] = (struct jsontok){ .type = JSON_NULL };
	if (!value(&src, toks, ntoks, 0)) return src;
	return NULL;
}

static unsigned basic_value_end(const char **src, struct jsontok *tok) {
	for (; **src != '\0' && **src != '}' && **src != ':'
		&& **src != '[' && **src != ','; ++*src, ++tok->next);
	return 1;
}

static unsigned array(const char **src, struct jsontok *toks,
			unsigned ntoks, unsigned tok) {
	const char *const start = *src;
	struct jsontok *arr = toks + ++tok;
	arr->type = JSON_ARRAY;
	arr->len = 0;
	
	// Parse key value pairs until you find a ] instead of ,
	whitespace(src);
	while (**src != ']') {
		// Parse value
		if (!(arr->len += value(src, toks, ntoks, tok))) return 0;
		whitespace(src);
		if (**src == ',') ++*src;
	}
	
	arr->next = *src - start;
	return 1 + arr->len;
}

static unsigned string(const char **src, struct jsontok *toks, unsigned tok) {
	const char *const start = *src;
	struct jsontok *str = toks + ++tok;
	str->type = JSON_STRING;
	
	// Continue until , } \0 or ] again, but NOT in string
	while (true) {
		for (; **src != '"' && **src != '\\'; ++*src);
		if (**src == '"') break;

		const struct escape e = escape(src);
		if (e.len == 0) return 0;
	}

	str->next = str->len = *src - start;
	return basic_value_end(src, toks + tok);
}

static unsigned object(const char **src, struct jsontok *toks,
			unsigned ntoks, unsigned tok) {
	const char *const start = *src;
	struct jsontok *obj = toks + ++tok;
	obj->type = JSON_OBJECT;
	obj->len = 0;
	
	// Parse key value pairs until you find a } instead of ,
	whitespace(src);
	while (**src != '}') {
		// Parse string first
		if (*(*src)++ != '"' || ++tok == ntoks) return 0;
		if (!string(src, toks, tok)) return 0;
		if (*(*src)++ != ':') return 0;
		obj->len++;

		// Parse value
		if (!(obj->len += value(src, toks, ntoks, tok))) return 0;
		whitespace(src);
		if (**src == ',') ++*src;
	}
	
	obj->next = *src - start;
	return 1 + obj->len;
}

// Returns the number of tokens this value took up
static unsigned value(const char **src, struct jsontok *toks,
			unsigned ntoks, unsigned tok) {
	toks[tok++].next += whitespace(src);
	if (tok + 1 > ntoks) return 0;

	switch (*(*src)++) {
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		toks[++tok].type = JSON_NUMBER; break;
	case 't': toks[++tok].type = JSON_TRUE; break;
	case 'f': toks[++tok].type = JSON_FALSE; break;
	case 'n': toks[++tok].type = JSON_NULL; break;

	// Long paths
	case '"': toks[tok++].next++; return string(src, toks, tok);
	case '[': toks[tok++].next++; return array(src, toks, ntoks, tok);
	case '{': toks[tok++].next++; return object(src, toks, ntoks, tok);
	default: return 0;
	}
	
	toks[tok].next = 1;
	return basic_value_end(src, toks + tok);
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
		for (; *jstr != '"' && *jstr != '\\' && buf < end; jstr++, buf++) {
			if (*jstr != *buf) return false;
		}
		if (*jstr == '"') return true;
		
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

unsigned json_len(const char *jstr) {
	for (unsigned len = 0;;) {
		for (; *jstr != '"' && *jstr != '\\'; jstr++, len++);
		if (*jstr == '"') return len;
		const struct escape e = escape(&jstr);
		if (!(len += e.len)) return 0;
	}
}

// Default implementation (slow, but works)
double json_num(const char *jnum) {
	double sign = *jnum == '-' ? -1.0 : 1.0;

	jnum += *jnum == '-';
	if (*jnum == '0') {
		switch (*++jnum) {
		case '.': goto fractional;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9': return NAN;
		default: return 0.0;
		}
	}

	double n = 0.0;
	while (*jnum >= '0' && *jnum <= '9') {
		n *= 10.0;
		n += (double)(*jnum++ - '0');
	}

	if (*jnum++ != '.') return n * sign;

fractional:
	for (double p = 0.1; *jnum >= '0' && *jnum <= '9'; p *= 0.1) {
		n += (double)(*jnum++ - '0') * p;
	}

	if (*jnum != 'E' && *jnum != 'e') return n * sign;

	int esign;
exponent:
	esign = *jnum == '-' ? -1 : 1;
	jnum += *jnum == '-' || *jnum == '+';

	int en = 0.0;
	while (*jnum >= '0' && *jnum <= '9') {
		en *= 10.0;
		en += *jnum++ - '0';
	}

	return n * sign * _pow10(en * esign);
}

// Just say shure for now
bool json_validate_value(const char *jval, int type) {
	return true;
}

// Just says everythings valid (for now)
const char *json_validate_utf8(const char *str) {
	return NULL;
}

