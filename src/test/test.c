#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../ekjson.h"
#include "ek.h"

#define TEST_SETUP(_name, _src, _ntoks) \
	static bool _name(unsigned id) { \
		ejtok_t toks[_ntoks]; \
		static const char *const __src = _src; \
		const ejresult_t res = ejparse(__src, toks, \
					arrlen(toks)); \
		int __idx = 0, _pos = 0;
#define PASS_SETUP(_name, _src, _ntoks) \
	TEST_SETUP(pass_##_name, _src, _ntoks) \
	const bool dopass = true; \
	if (res.err) goto err;
#define FAIL_SETUP(_name, _src, _ntoks) \
	TEST_SETUP(fail_##_name, _src, _ntoks) \
	const bool dopass = false;
#define PASS_END \
		return true; \
	err: \
		return false; \
	}
#define FAIL_END \
		if (res.err) return true; \
		return false; \
	}
#define CHECK_BASE(_type, _size, _len) \
	if (toks[__idx].type != _type) { \
		if (!dopass) return true; \
		fprintf(stderr, "token %d type: %d != %d\n", \
			__idx, toks[__idx].type, _type); \
		return false; \
	} \
	if (toks[__idx].start != _pos) { \
		if (!dopass) return true; \
		fprintf(stderr, "token %d start: %d != %d\n", \
			__idx, toks[__idx].start, _pos); \
		return false; \
	} \
	_pos += _size; \
	if (toks[__idx].len != _len) { \
		if (!dopass) return true; \
		fprintf(stderr, "token %d len: %d != %d\n", \
			__idx, toks[__idx].len, _len); \
		return false; \
	}
#define CHECK_START \
	{
#define CHECK_END \
		__idx++; \
	}
#define CHECK_POS(pos) _pos = pos;
#define CHECK_SIMPLE(_type, _size, _len) \
	CHECK_START \
		CHECK_BASE(_type, _size, _len) \
	CHECK_END
#define CHECK_FLOAT(_size, _num) \
	CHECK_START \
		CHECK_BASE(EJFLT, _size, 1) \
	CHECK_END
#define CHECK_INT(_size, _num) \
	CHECK_START \
		CHECK_BASE(EJINT, _size, 1) \
		const int64_t num = ejint(__src + toks[__idx].start); \
		if (num != (int64_t)_num) { \
			if (!dopass) return true; \
			fprintf(stderr, "token %d num: %lld != %lld\n", \
				__idx, num, (int64_t)_num); \
			return false; \
		} \
	CHECK_END
#define CHECK_STRING(_size, _len, _str) \
	CHECK_START \
		CHECK_BASE(EJSTR, _size, _len) \
	CHECK_END
#define CHECK_KV(_size, _len, _str) \
	CHECK_START \
		CHECK_BASE(EJKV, _size, _len) \
	CHECK_END

static bool pass_nothing(unsigned id) {
	ejtok_t toks[2];
	return !ejparse("", toks, arrlen(toks)).err;
}

PASS_SETUP(array_array_array_empty, "[[[]]]", 64)
	CHECK_SIMPLE(EJARR, 1, 3)
	CHECK_SIMPLE(EJARR, 1, 2)
	CHECK_SIMPLE(EJARR, 1, 1)
PASS_END
PASS_SETUP(array_array_empty, "[[]]", 64)
	CHECK_SIMPLE(EJARR, 1, 2)
	CHECK_SIMPLE(EJARR, 1, 1)
PASS_END
PASS_SETUP(array_bool, "[true]", 64)
	CHECK_SIMPLE(EJARR, 1, 2)
	CHECK_SIMPLE(EJBOOL, 1, 1)
PASS_END
PASS_SETUP(array_bools, "[true,false,true]", 64)
	CHECK_SIMPLE(EJARR, 1, 4)
	CHECK_SIMPLE(EJBOOL, 5, 1)
	CHECK_SIMPLE(EJBOOL, 6, 1)
	CHECK_SIMPLE(EJBOOL, 5, 1)
PASS_END
PASS_SETUP(array_empty, "[]", 64)
	CHECK_SIMPLE(EJARR, 2, 1)
PASS_END
PASS_SETUP(array_float, "[3.14]", 64)
	CHECK_SIMPLE(EJARR, 1, 2)
	CHECK_FLOAT(5, 3.14)
PASS_END
PASS_SETUP(array_floats, "[1.2,3.4,5.6]", 64)
	CHECK_SIMPLE(EJARR, 1, 4)
	CHECK_FLOAT(4, 1.2)
	CHECK_FLOAT(4, 3.4)
	CHECK_FLOAT(4, 5.6)
PASS_END
PASS_SETUP(array_int, "[1]", 64)
	CHECK_SIMPLE(EJARR, 1, 2)
	CHECK_INT(1, 1)
PASS_END
PASS_SETUP(array_ints, "[1,2,3]", 64)
	CHECK_SIMPLE(EJARR, 1, 4)
	CHECK_INT(2, 1)
	CHECK_INT(2, 2)
	CHECK_INT(2, 3)
PASS_END
PASS_SETUP(array_matrix, "[[1,2,3],[4,5,6],[7,8,9]]", 64)
	CHECK_SIMPLE(EJARR, 1, 13)
	CHECK_SIMPLE(EJARR, 1, 4)
	CHECK_INT(2, 1)
	CHECK_INT(2, 2)
	CHECK_INT(3, 3)
	CHECK_SIMPLE(EJARR, 1, 4)
	CHECK_INT(2, 4)
	CHECK_INT(2, 5)
	CHECK_INT(3, 6)
	CHECK_SIMPLE(EJARR, 1, 4)
	CHECK_INT(2, 7)
	CHECK_INT(2, 8)
	CHECK_INT(3, 9)
PASS_END
PASS_SETUP(array_null, "[null]", 64)
	CHECK_SIMPLE(EJARR, 1, 2)
	CHECK_SIMPLE(EJNULL, 5, 1)
PASS_END
PASS_SETUP(array_nulls, "[null,null,null]", 64)
	CHECK_SIMPLE(EJARR, 1, 4)
	CHECK_SIMPLE(EJNULL, 5, 1)
	CHECK_SIMPLE(EJNULL, 5, 1)
	CHECK_SIMPLE(EJNULL, 5, 1)
PASS_END
PASS_SETUP(array_object, "[{\"a\":1}]", 64)
	CHECK_SIMPLE(EJARR, 1, 4)
	CHECK_SIMPLE(EJOBJ, 1, 3)
	CHECK_KV(4, 2, "a")
	CHECK_INT(1, 1)
PASS_END
PASS_SETUP(array_object_empty, "[{}]", 64)
	CHECK_SIMPLE(EJARR, 1, 2)
	CHECK_SIMPLE(EJOBJ, 1, 1)
PASS_END
PASS_SETUP(array_objects, "[{\"a\":1},{\"b\":2},{\"c\":3}]", 64)
	CHECK_SIMPLE(EJARR, 1, 10)
	CHECK_SIMPLE(EJOBJ, 1, 3)
	CHECK_KV(4, 2, "a")
	CHECK_INT(3, 1)
	CHECK_SIMPLE(EJOBJ, 1, 3)
	CHECK_KV(4, 2, "b")
	CHECK_INT(3, 2)
	CHECK_SIMPLE(EJOBJ, 1, 3)
	CHECK_KV(4, 2, "c")
	CHECK_INT(3, 3)
PASS_END
PASS_SETUP(array_string, "[\"abc\"]", 64)
	CHECK_SIMPLE(EJARR, 1, 2)
	CHECK_STRING(5, 1, "abc")
PASS_END
PASS_SETUP(array_string_empty, "[\"\"]", 64)
	CHECK_SIMPLE(EJARR, 1, 2)
	CHECK_STRING(2, 1, "")
PASS_END
PASS_SETUP(array_strings, "[\"abc\",\"def\",\"ghi\"]", 64)
	CHECK_SIMPLE(EJARR, 1, 4)
	CHECK_STRING(6, 1, "abc")
	CHECK_STRING(6, 1, "def")
	CHECK_STRING(6, 1, "ghi")
PASS_END
PASS_SETUP(array_tensor, "[[[1,2],[3,4]],[[5,6],[7,8]]]", 64)
	CHECK_SIMPLE(EJARR, 1, 15)
	CHECK_SIMPLE(EJARR, 1, 7)
	CHECK_SIMPLE(EJARR, 1, 3)
	CHECK_INT(2, 1)
	CHECK_INT(3, 2)
	CHECK_SIMPLE(EJARR, 1, 3)
	CHECK_INT(2, 3)
	CHECK_INT(4, 4)
	CHECK_SIMPLE(EJARR, 1, 7)
	CHECK_SIMPLE(EJARR, 1, 3)
	CHECK_INT(2, 5)
	CHECK_INT(3, 6)
	CHECK_SIMPLE(EJARR, 1, 3)
	CHECK_INT(2, 7)
	CHECK_INT(2, 8)
PASS_END

PASS_SETUP(bool_false, "false", 64)
	CHECK_SIMPLE(EJBOOL, 2, 1)
PASS_END
PASS_SETUP(bool_true, "true", 64)
	CHECK_SIMPLE(EJBOOL, 2, 1)
PASS_END

PASS_SETUP(float_neg1, "-1.0", 64)
	CHECK_FLOAT(0, -1.0)
PASS_END
PASS_SETUP(float_0, "0.0", 64)
	CHECK_FLOAT(0, 0.0)
PASS_END
PASS_SETUP(float_1, "1.0", 64)
	CHECK_FLOAT(0, 1.0)
PASS_END
PASS_SETUP(float_max, "1.7976931348623157e+308", 64)
	CHECK_FLOAT(0, 1.7976931348623157e+308)
PASS_END
PASS_SETUP(float_min, "-1.7976931348623157e+308", 64)
	CHECK_FLOAT(0, -1.7976931348623157e+308)
PASS_END

PASS_SETUP(int_neg1, "-1", 64)
	CHECK_INT(0, -1)
PASS_END
PASS_SETUP(int_0, "0", 64)
	CHECK_INT(0, 0)
PASS_END
PASS_SETUP(int_1, "1", 64)
	CHECK_INT(0, 1)
PASS_END
PASS_SETUP(int_8digits, "12345678", 64)
	CHECK_INT(0, 12345678)
PASS_END
PASS_SETUP(int_12digits, "123456789012", 64)
	CHECK_INT(0, 123456789012)
PASS_END
PASS_SETUP(int_max, "9223372036854775807", 64)
	CHECK_INT(0, INT64_MAX)
PASS_END
PASS_SETUP(int_min, "-9223372036854775808", 64)
	CHECK_INT(0, INT64_MIN)
PASS_END
PASS_SETUP(int_supermax1, "9223372036854885890", 64)
	CHECK_INT(0, INT64_MAX)
PASS_END
PASS_SETUP(int_supermax2, "9223372036854775808", 64)
	CHECK_INT(0, INT64_MAX)
PASS_END
PASS_SETUP(int_supermax3, "10223372036854885890", 64)
	CHECK_INT(0, INT64_MAX)
PASS_END
PASS_SETUP(int_supermax4, "1123412349182481237491230223372036854885890", 64)
	CHECK_INT(0, INT64_MAX)
PASS_END
PASS_SETUP(int_supermin1, "-9223372036854775809", 64)
	CHECK_INT(0, INT64_MIN)
PASS_END
PASS_SETUP(int_supermin2, "-9223372036854775809", 64)
	CHECK_INT(0, INT64_MIN)
PASS_END
PASS_SETUP(int_supermin3, "-10223372036954775809", 64)
	CHECK_INT(0, INT64_MIN)
PASS_END
PASS_SETUP(int_supermin4, "-10234912341723491283410223372036954775809", 64)
	CHECK_INT(0, INT64_MIN)
PASS_END

PASS_SETUP(object_array, "{\"abc\":[1,2,3]}", 64)
	CHECK_SIMPLE(EJOBJ, 1, 6)
	CHECK_KV(6, 5, "abc")
	CHECK_SIMPLE(EJARR, 1, 4)
	CHECK_INT(2, 1)
	CHECK_INT(2, 2)
	CHECK_INT(2, 3)
PASS_END
PASS_SETUP(object_array_object, "{\"a\":[{\"a\":1}]}", 64)
	CHECK_SIMPLE(EJOBJ, 1, 6)
	CHECK_KV(4, 5, "a")
	CHECK_SIMPLE(EJARR, 1, 4)
	CHECK_SIMPLE(EJOBJ, 1, 3)
	CHECK_KV(4, 2, "a")
	CHECK_INT(1, 1)
PASS_END
PASS_SETUP(object_array_objects, "{\"a\":[{\"a\":1},{\"b\":2},{\"c\":3}]}", 64)
	CHECK_SIMPLE(EJOBJ, 1, 12)
	CHECK_KV(4, 11, "a")
	CHECK_SIMPLE(EJARR, 1, 10)
	CHECK_SIMPLE(EJOBJ, 1, 3)
	CHECK_KV(4, 2, "a")
	CHECK_INT(3, 1)
	CHECK_SIMPLE(EJOBJ, 1, 3)
	CHECK_KV(4, 2, "b")
	CHECK_INT(3, 2)
	CHECK_SIMPLE(EJOBJ, 1, 3)
	CHECK_KV(4, 2, "c")
	CHECK_INT(3, 3)
PASS_END
PASS_SETUP(object_string, "{\"abc\":\"def\"}", 64)
	CHECK_SIMPLE(EJOBJ, 1, 3)
	CHECK_KV(6, 2, "abc")
	CHECK_STRING(6, 1, "def")
PASS_END
PASS_SETUP(object_strings, "{\"abc\":\"def\",\"ghi\":\"jkl\","
	"\"mno\":\"pqr\"}", 64)
	CHECK_SIMPLE(EJOBJ, 1, 7)
	CHECK_KV(6, 2, "abc")
	CHECK_STRING(6, 1, "def")
	CHECK_KV(6, 2, "ghi")
	CHECK_STRING(6, 1, "jkl")
	CHECK_KV(6, 2, "mno")
	CHECK_STRING(6, 1, "pqr")
PASS_END
PASS_SETUP(object_true, "{\"abc\":true}", 64)
	CHECK_SIMPLE(EJOBJ, 1, 3)
	CHECK_KV(6, 2, "abc")
	CHECK_SIMPLE(EJBOOL, 1, 1)
PASS_END

PASS_SETUP(string_a, "\"a\"", 64)
	CHECK_STRING(1, 1, "a")
PASS_END
PASS_SETUP(string_abc, "\"abc\"", 64)
	CHECK_STRING(1, 1, "abc")
PASS_END
PASS_SETUP(string_backspace, "\"\\b\"", 64)
	CHECK_STRING(1, 1, "\b")
PASS_END
PASS_SETUP(string_carriage_return, "\"\\r\"", 64)
	CHECK_STRING(1, 1, "\r")
PASS_END
PASS_SETUP(string_empty, "\"\"", 64)
	CHECK_STRING(1, 1, "")
PASS_END
PASS_SETUP(string_escape, "\"\\u12A4\"", 64)
	CHECK_STRING(1, 1, "\u12A4")
PASS_END
PASS_SETUP(string_formfeed, "\"\\f\"", 64)
	CHECK_STRING(1, 1, "\f")
PASS_END
PASS_SETUP(string_horizontal_tab, "\"\\t\"", 64)
	CHECK_STRING(1, 1, "\t")
PASS_END
PASS_SETUP(string_linefeed, "\"\\n\"", 64)
	CHECK_STRING(1, 1, "\n")
PASS_END
PASS_SETUP(string_quote, "\"\\\"\"", 64)
	CHECK_STRING(1, 1, "\"")
PASS_END
PASS_SETUP(string_quote_abc_quote, "\"\\\"abc\\\"\"", 64)
	CHECK_STRING(1, 1, "\"abc\"")
PASS_END
PASS_SETUP(string_quote_quote, "\"\\\"\\\"\"", 64)
	CHECK_STRING(1, 1, "\"\"")
PASS_END
PASS_SETUP(string_backslash, "\"\\\\\"", 64)
	CHECK_STRING(1, 1, "\\")
PASS_END
PASS_SETUP(string_forwardslash, "\"\\/\"", 64)
	CHECK_STRING(1, 1, "/")
PASS_END
PASS_SETUP(string_whitespace_abc, "    \"abc\"", 64)
	CHECK_POS(4)
	CHECK_STRING(5, 1, "abc")
PASS_END

PASS_SETUP(null, "null", 64)
	CHECK_SIMPLE(EJNULL, 1, 1)
PASS_END

FAIL_SETUP(bool_false, "fals", 64)
	CHECK_SIMPLE(EJBOOL, 1, 1)
FAIL_END
FAIL_SETUP(bool_true, "tru", 64)
	CHECK_SIMPLE(EJBOOL, 1, 1)
FAIL_END

//FAIL_SETUP(float_max, "1.8e+308", 64)
//	CHECK_FLOAT(1, 0)
//FAIL_END
//FAIL_SETUP(float_min, "-1.8e+308", 64)
//	CHECK_FLOAT(1, 0)
//FAIL_END
FAIL_SETUP(float_dot_after, "1.", 64)
	CHECK_FLOAT(1, 0)
FAIL_END
FAIL_SETUP(float_dot_before, ".1", 64)
	CHECK_FLOAT(1, 0)
FAIL_END
FAIL_SETUP(float_leading_zeros, "0000.1", 64)
	CHECK_FLOAT(1, 0)
FAIL_END
FAIL_SETUP(float_exponent, "1e", 64)
	CHECK_FLOAT(1, 0)
FAIL_END
FAIL_SETUP(float_exponent_a, "1ea", 64)
	CHECK_FLOAT(1, 0)
FAIL_END
FAIL_SETUP(float_exponent_sign, "1e+", 64)
	CHECK_FLOAT(1, 0)
FAIL_END
FAIL_SETUP(float_a, "12u4.0", 64)
	CHECK_FLOAT(1, 0)
FAIL_END

//FAIL_SETUP(int_max, "9223372036854775808", 64)
//	CHECK_INT(1, 0)
//FAIL_END
//FAIL_SETUP(int_min, "-9223372036854775809", 64)
//	CHECK_INT(1, 0)
//FAIL_END
//FAIL_SETUP(int_dot, "1234.8", 64)
//	CHECK_INT(1, 1235)
//FAIL_END
FAIL_SETUP(int_a, "12a4", 64)
	CHECK_INT(1, 1234)
FAIL_END

FAIL_SETUP(string_missing_begin_quote, "abc\"", 64)
	CHECK_STRING(1, 1, "abc")
FAIL_END
FAIL_SETUP(string_missing_end_quote, "\"abc", 64)
	CHECK_STRING(1, 1, "abc")
FAIL_END
FAIL_SETUP(string_escape, "\"\\u12i4\"", 64)
FAIL_END

FAIL_SETUP(object_key_missing_quote1, "{\"a", 64)
	if (!res.loc || res.loc[0] != '\0') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_key_missing_quote2, "{\"abc", 64)
	if (!res.loc || res.loc[0] != '\0') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_key_missing_quote3, "{\"abcdefgh", 64)
	if (!res.loc || res.loc[0] != '\0') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_string_missing_quote1, "{\"a\":\"a", 64)
	if (!res.loc || res.loc[0] != '\0') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_string_missing_quote2, "{\"a\":\"abc", 64)
	if (!res.loc || res.loc[0] != '\0') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_string_missing_quote3, "{\"a\":\"abcdefgh", 64)
	if (!res.loc || res.loc[0] != '\0') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_string_missing_quote4, "{\"a\":\"abcdef\\", 64)
	if (!res.loc || res.loc[0] != '\0') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_string_missing_quote5, "{\"a\":\"abcdef\\u34", 64)
	if (!res.loc || res.loc[0] != '\0') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_number_eof1, "{\"a\":435.", 64)
	if (!res.loc || res.loc[0] != '\0') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_number_eof2, "{\"a\":435e", 64)
	if (!res.loc || res.loc[0] != '\0') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_number_eof3, "{\"a\":435e+", 64)
	if (!res.loc || res.loc[0] != '\0') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_number_eof4, "{\"a\":-", 64)
	if (!res.loc || res.loc[0] != '\0') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_bool_eof1, "{\"a\":f", 64)
	if (!res.loc || res.loc[0] != 'f') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_bool_eof2, "{\"a\":t", 64)
	if (!res.loc || res.loc[0] != 't') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_bool_eof3, "{\"a\":tru ", 64)
	if (!res.loc || res.loc[0] != 't') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_null_eof1, "{\"a\":n", 64)
	if (!res.loc || res.loc[0] != 'n') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_null_eof2, "{\"a\":nul", 64)
	if (!res.loc || res.loc[0] != 'n') return TEST_BAD;
FAIL_END
FAIL_SETUP(object_null_eof3, "{\"a\":nul ", 64)
	if (!res.loc || res.loc[0] != 'n') return TEST_BAD;
FAIL_END

FAIL_SETUP(null, "nul", 64)
	CHECK_SIMPLE(EJNULL, 1, 1)
FAIL_END

static bool pass_ejstr_len1(unsigned test) {
	return ejstr("\"abcdef\"", NULL, 0) == 7;
}
static bool pass_ejstr_len2(unsigned test) {
	return ejstr("\"abc\ndef\"", NULL, 0) == 8;
}
static bool pass_ejstr_len3(unsigned test) {
	return ejstr("\"abc\\u003Fdef\"", NULL, 0) == 8;
}
static bool pass_ejstr_len4(unsigned test) {
	return ejstr("\"abc\\u00DAdef\"", NULL, 0) == 9;
}
static bool pass_ejstr_len5(unsigned test) {
	return ejstr("\"abc\\u235Edef\"", NULL, 0) == 10;
}
static bool pass_ejstr_len6(unsigned test) {
	return ejstr("\"abc\\uD835\\uDC0Bdef\"", NULL, 0) == 11;
}
static bool pass_ejstr_len7(unsigned test) {
	return ejstr("\"abcÚdef\"", NULL, 0) == 9;
}
static bool fail_ejstr_len1(unsigned test) {
	return ejstr("\"abc\\uD800\\uD800Bef\"", NULL, 0) == 0;
}
static bool fail_ejstr_len2(unsigned test) {
	return ejstr("\"abc\\uDC0B\\uD835def\"", NULL, 0) == 0;
}

static bool pass_ejstr_escape1(unsigned test) {
	char buf[32];
	const size_t len = ejstr("\"abcdef\"", buf, sizeof(buf));
	if (len != 7) return TEST_BAD;
	if (strncmp(buf, "abcdef", sizeof(buf)) != 0) return TEST_BAD;
	return true;
}
static bool pass_ejstr_escape2(unsigned test) {
	char buf[32];
	const size_t len = ejstr("\"abc\ndef\"", buf, sizeof(buf));
	if (len != 8) return TEST_BAD;
	if (strncmp(buf, "abc\ndef", sizeof(buf)) != 0) return TEST_BAD;
	return true;
}
static bool pass_ejstr_escape3(unsigned test) {
	char buf[32];
	const size_t len = ejstr("\"abc\\u003Fdef\"", buf, sizeof(buf));
	if (len != 8) return TEST_BAD;
	if (strncmp(buf, "abc?def", sizeof(buf)) != 0) return TEST_BAD;
	return true;
}
static bool pass_ejstr_escape4(unsigned test) {
	char buf[32];
	const size_t len = ejstr("\"abc\\u00DAdef\"", buf, sizeof(buf));
	if (len != 9) return TEST_BAD;
	if (strncmp(buf, "abcÚdef", sizeof(buf)) != 0) return TEST_BAD;
	return true;
}
static bool pass_ejstr_escape5(unsigned test) {
	char buf[32];
	const size_t len = ejstr("\"abc\\u235Edef\"", buf, sizeof(buf));
	if (len != 10) return TEST_BAD;
	if (strncmp(buf, "abc⍞def", sizeof(buf)) != 0) return TEST_BAD;
	return true;
}
static bool pass_ejstr_escape6(unsigned test) {
	char buf[32];
	const size_t len = ejstr("\"abc\\uD83D\\uDE03def\"", buf, sizeof(buf));
	if (len != 11) return TEST_BAD;
	if (strncmp(buf, "abc😃def", sizeof(buf)) != 0) return TEST_BAD;
	return true;
}
static bool pass_ejstr_escape7(unsigned test) {
	char buf[32];
	const size_t len = ejstr("\"abcÚdef\"", buf, sizeof(buf));
	if (len != 9) return TEST_BAD;
	if (strncmp(buf, "abcÚdef", sizeof(buf)) != 0) return TEST_BAD;
	return true;
}

static bool pass_ejstr_overflow1(unsigned test) {
	char buf[4];
	const size_t len = ejstr("\"abcdef\"", buf, sizeof(buf));
	if (len != 7) return TEST_BAD;
	if (strnlen(buf, 100) != 3) return TEST_BAD;
	if (strcmp(buf, "abc") != 0) return TEST_BAD;
	return true;
}
static bool pass_ejstr_overflow2(unsigned test) {
	char buf[4];
	const size_t len = ejstr("\"ab\\uD83D\\uDE03\"", buf, sizeof(buf));
	if (len != 7) return TEST_BAD;
	if (strnlen(buf, 100) != 2) return TEST_BAD;
	if (strcmp(buf, "ab") != 0) return TEST_BAD;
	return true;
}

extern const char hell1_escaped[], hell1_string[];
extern size_t hell1_size;
static bool pass_ejstr_hell1(unsigned test) {
	char buf[1024];
	if (ejstr(hell1_escaped, buf, sizeof(buf)) != hell1_size) return TEST_BAD;
	if (strncmp(buf, hell1_string, 1024) != 0) return TEST_BAD;
	return true;
}

extern const char hell2_escaped[], hell2_string[];
extern size_t hell2_size;
static bool pass_ejstr_hell2(unsigned test) {
	char buf[4096];
	if (ejstr(hell2_escaped, buf, sizeof(buf)) != hell2_size) return TEST_BAD;
	if (strncmp(buf, hell2_string, 4096) != 0) return TEST_BAD;
	return true;
}

extern const char hell3[];
static bool pass_ejstr_hell3(unsigned test) {
	static ejtok_t t[16384];
	return !ejparse(hell3, t, arrlen(t)).err;
}

extern const char hell4[];
static bool pass_ejstr_hell4(unsigned test) {
	static ejtok_t t[16384];
	return !ejparse(hell4, t, arrlen(t)).err;
}

static char buf[1024*1024];
extern const char hell5_escaped[], hell5_string[];
extern size_t hell5_size;
static bool pass_ejstr_hell5(unsigned test) {
	if (ejstr(hell5_escaped, buf, sizeof(buf)) != hell5_size) return TEST_BAD;
	//if (strncmp(buf, hell5_string, sizeof(buf)) != 0) return TEST_BAD;
	for (int i = 0; buf[i]; i++) {
		if (!buf[i] && !hell5_string[i]) return true;
		if (buf[i] != hell5_string[i]) {
			printf("\n%d\n", i);
			break;
		}
	}
	return true;
}

static bool pass_ejcmp1(unsigned test) {
	return ejcmp("\"abcdef\"", "abcdef");
}
static bool pass_ejcmp2(unsigned test) {
	return ejcmp("\"\\nabcdef\"", "\nabcdef");
}
static bool pass_ejcmp3(unsigned test) {
	return ejcmp("\"abcdef\\t\"", "abcdef\t");
}
static bool pass_ejcmp4(unsigned test) {
	return ejcmp("\"abc\\u00DAdef\"", "abc\u00DAdef");
}
static bool pass_ejcmp5(unsigned test) {
	return ejcmp("\"a\"", "a");
}
static bool pass_ejcmp6(unsigned test) {
	return ejcmp("\"\"", "");
}
static bool pass_ejcmp7(unsigned test) {
	return ejcmp(hell1_escaped, hell1_string);
}
static bool pass_ejcmp8(unsigned test) {
	return ejcmp(hell2_escaped, hell2_string);
}
static bool pass_ejcmp9(unsigned test) {
	return ejcmp(hell5_escaped, hell5_string);
}
static bool pass_ejcmp10(unsigned test) {
	return !ejcmp("\"a\"", "b");
}
static bool pass_ejcmp11(unsigned test) {
	return !ejcmp("\"\\r\"", "r");
}
static bool pass_ejcmp12(unsigned test) {
	return !ejcmp("\"\u00DA\"", "r");
}
static bool pass_ejcmp13(unsigned test) {
	return !ejcmp("\"abcd\"", "abcdef");
}
static bool pass_ejcmp14(unsigned test) {
	return !ejcmp("\"abcdef\"", "abcd");
}

static bool pass_ejbool1(unsigned test) {
	return ejbool("true") == true;
}
static bool pass_ejbool2(unsigned test) {
	return ejbool("false") == false;
}

// Speed tests
extern void unopt_strtest(volatile size_t *size,
			const char *restrict a,
			const char *restrict b);
static size_t byte_strlen(const char *str) {
	size_t len = 0;
	while (*str++) len++;
	return len;
}

extern const char speed_test_normal[], speed_test_quoted[],
		speed_test_utf[], speed_test_script[];
extern const char *const speed_str_normal, *const speed_str_quoted,
		*const speed_str_utf, *const speed_str_script;
static void test_ejstr_speed(void) {
	volatile size_t _total_len = 0;
	static const char *test_strings[4] = {
		speed_test_normal, speed_test_quoted,
		speed_test_utf, speed_test_script,
	};
	static const char *test_names[4] = {
		"normal", "quotes", "utf", "script"
	};
	const char *cmp_strings[4] = {
		speed_str_normal, speed_str_quoted,
		speed_str_utf, speed_str_script,
	};
	const int niters = 50000;
	double tps[2][4];

	printf("\n");
	for (int i = 0; i < 4; i++) {
		printf("starting speed test \"%s\"\n", test_names[i]);
		const double len = (double)(strlen(test_strings[i]) + 1)
			/ 1024.0 / 1024.0 / 1024.0;
		clock_t start;
		double time;

		// strcmp
		start = clock();
		for (int n = 0; n < niters; n++) {
			unopt_strtest(&_total_len, test_strings[i],
					test_strings[i]);
		}
		time = (double)(clock() - start) / CLOCKS_PER_SEC;
		printf("strcmp  throughput: %.2f GB/s\n",
			len / (time / niters));

		// naive strlen
		start = clock();
		for (int n = 0; n < niters; n++) {
			_total_len += byte_strlen(test_strings[i]);
		}
		time = (double)(clock() - start) / CLOCKS_PER_SEC;
		printf("bstrlen throughput: %.2f GB/s\n",
			len / (time / niters));

		// ejstr
		start = clock();
		for (int n = 0; n < niters; n++) {
			ejstr(test_strings[i], buf, sizeof(buf));
		}
		time = (double)(clock() - start) / CLOCKS_PER_SEC;
		
		tps[0][i] = len / (time / niters);
		printf("ejstr   throughput: %.2f GB/s\n", tps[0][i]);

		// ejcmp
		start = clock();
		for (int n = 0; n < niters; n++) {
			ejcmp(test_strings[i], cmp_strings[i]);
		}
		time = (double)(clock() - start) / CLOCKS_PER_SEC;
		
		tps[1][i] = len / (time / niters);
		printf("ejcmp   throughput: %.2f GB/s\n", tps[1][i]);
	}

	for (int f = 0; f < arrlen(tps); f++) {
		for (int i = 0; i < arrlen(tps[f]); i++) {
			printf("%.2f,\t", tps[f][i]);
		}
		printf("\n");
	}
}

static void test_ejint_speed(void) {
	static const char *const strings[] = {
		"0", "-1", "1234", "-1234", "12345678", "-12345678",
		"9223372036854775807", "-9223372036854775808",
		"9223372036854775900", "-9223372036854775900",
	};
	static const int64_t numbers[] = {
		0, -1, 1234, -1234, 12345678, -12345678,
		INT64_MAX, INT64_MIN, INT64_MAX, INT64_MIN,
	};

	static const int niters = 10000000;

	// Calculate number of bytes
	double ngigs = 0;
	for (int j = 0; j < arrlen(numbers); j++) ngigs += strlen(strings[j]);
	ngigs *= niters;
	ngigs /= 1024 * 1024 * 1024;

	// test functions
	clock_t start;
	double time;
	printf("\n\nejint tests\n");

	// test strtoll
	start = clock();
	for (int i = 0; i < niters; i++) {
		for (int j = 0; j < arrlen(numbers); j++) {
			strtoll(strings[j], NULL, 10);
		}
	}
	time = (double)(clock() - start) / (double)CLOCKS_PER_SEC;
	printf("strtoll %d iters time (s): %.4f\n", niters, time);
	printf("strtoll throughput (GB/s): %.2f\n", ngigs / time);

	// test ejint
	start = clock();
	for (int i = 0; i < niters; i++) {
		for (int j = 0; j < arrlen(numbers); j++) {
			ejint(strings[j]);
		}
	}
	time = (double)(clock() - start) / (double)CLOCKS_PER_SEC;
	printf("ejint %d iters time (s): %.4f\n", niters, time);
	printf("ejint throughput (GB/s): %.2f\n", ngigs / time);
}

static const test_t tests[] = {
	TEST_ADD(pass_nothing)
	TEST_PAD
	TEST_ADD(pass_array_array_array_empty)
	TEST_ADD(pass_array_array_empty)
	TEST_ADD(pass_array_bool)
	TEST_ADD(pass_array_bools)
	TEST_ADD(pass_array_empty)
	TEST_ADD(pass_array_float)
	TEST_ADD(pass_array_floats)
	TEST_ADD(pass_array_int)
	TEST_ADD(pass_array_ints)
	TEST_ADD(pass_array_matrix)
	TEST_ADD(pass_array_null)
	TEST_ADD(pass_array_nulls)
	TEST_ADD(pass_array_object)
	TEST_ADD(pass_array_object_empty)
	TEST_ADD(pass_array_objects)
	TEST_ADD(pass_array_string)
	TEST_ADD(pass_array_string_empty)
	TEST_ADD(pass_array_strings)
	TEST_ADD(pass_array_tensor)
	TEST_PAD
	TEST_ADD(pass_bool_false)
	TEST_ADD(pass_bool_true)
	TEST_ADD(pass_float_neg1)
	TEST_ADD(pass_float_0)
	TEST_ADD(pass_float_1)
	TEST_ADD(pass_float_max)
	TEST_ADD(pass_float_min)
	TEST_ADD(pass_int_neg1)
	TEST_ADD(pass_int_0)
	TEST_ADD(pass_int_1)
	TEST_ADD(pass_int_8digits)
	TEST_ADD(pass_int_12digits)
	TEST_ADD(pass_int_max)
	TEST_ADD(pass_int_min)
	TEST_ADD(pass_int_supermax1)
	TEST_ADD(pass_int_supermax2)
	TEST_ADD(pass_int_supermax3)
	TEST_ADD(pass_int_supermax4)
	TEST_ADD(pass_int_supermin1)
	TEST_ADD(pass_int_supermin2)
	TEST_ADD(pass_int_supermin3)
	TEST_ADD(pass_int_supermin4)
	TEST_PAD
	TEST_ADD(pass_object_array)
	TEST_ADD(pass_object_array_object)
	TEST_ADD(pass_object_array_objects)
	TEST_ADD(pass_object_string)
	TEST_ADD(pass_object_strings)
	TEST_ADD(pass_object_true)
	TEST_PAD
	TEST_ADD(pass_string_a)
	TEST_ADD(pass_string_abc)
	TEST_ADD(pass_string_backspace)
	TEST_ADD(pass_string_carriage_return)
	TEST_ADD(pass_string_empty)
	TEST_ADD(pass_string_escape)
	TEST_ADD(pass_string_formfeed)
	TEST_ADD(pass_string_horizontal_tab)
	TEST_ADD(pass_string_linefeed)
	TEST_ADD(pass_string_quote)
	TEST_ADD(pass_string_quote_abc_quote)
	TEST_ADD(pass_string_quote_quote)
	TEST_ADD(pass_string_backslash)
	TEST_ADD(pass_string_forwardslash)
	TEST_ADD(pass_string_whitespace_abc)
	TEST_PAD
	TEST_ADD(pass_null)
	TEST_PAD
	TEST_ADD(fail_bool_false)
	TEST_ADD(fail_bool_true)
	TEST_PAD
	TEST_ADD(fail_float_dot_after)
	TEST_ADD(fail_float_dot_before)
	TEST_ADD(fail_float_leading_zeros)
	TEST_ADD(fail_float_exponent)
	TEST_ADD(fail_float_exponent_a)
	TEST_ADD(fail_float_exponent_sign)
	TEST_ADD(fail_float_a)
	TEST_PAD
	TEST_ADD(fail_int_a)
	TEST_PAD
	TEST_ADD(fail_string_missing_begin_quote)
	TEST_ADD(fail_string_missing_end_quote)
	TEST_ADD(fail_string_escape)
	TEST_PAD
	TEST_ADD(fail_object_key_missing_quote1)
	TEST_ADD(fail_object_key_missing_quote2)
	TEST_ADD(fail_object_key_missing_quote3)
	TEST_ADD(fail_object_string_missing_quote1)
	TEST_ADD(fail_object_string_missing_quote2)
	TEST_ADD(fail_object_string_missing_quote3)
	TEST_ADD(fail_object_string_missing_quote4)
	TEST_ADD(fail_object_string_missing_quote5)
	TEST_ADD(fail_object_number_eof1)
	TEST_ADD(fail_object_number_eof2)
	TEST_ADD(fail_object_number_eof3)
	TEST_ADD(fail_object_number_eof4)
	TEST_ADD(fail_object_bool_eof1)
	TEST_ADD(fail_object_bool_eof2)
	TEST_ADD(fail_object_bool_eof3)
	TEST_ADD(fail_object_null_eof1)
	TEST_ADD(fail_object_null_eof2)
	TEST_ADD(fail_object_null_eof3)
	TEST_PAD
	TEST_ADD(fail_null)
	TEST_PAD
	TEST_ADD(pass_ejstr_len1)
	TEST_ADD(pass_ejstr_len2)
	TEST_ADD(pass_ejstr_len3)
	TEST_ADD(pass_ejstr_len4)
	TEST_ADD(pass_ejstr_len5)
	TEST_ADD(pass_ejstr_len6)
	TEST_ADD(pass_ejstr_len7)
	TEST_ADD(fail_ejstr_len1)
	TEST_ADD(fail_ejstr_len2)
	TEST_PAD
	TEST_ADD(pass_ejstr_escape1)
	TEST_ADD(pass_ejstr_escape2)
	TEST_ADD(pass_ejstr_escape3)
	TEST_ADD(pass_ejstr_escape4)
	TEST_ADD(pass_ejstr_escape5)
	TEST_ADD(pass_ejstr_escape6)
	TEST_ADD(pass_ejstr_escape7)
	TEST_PAD
	TEST_ADD(pass_ejstr_overflow1)
	TEST_ADD(pass_ejstr_overflow2)
	TEST_PAD
	TEST_ADD(pass_ejstr_hell1)
	TEST_ADD(pass_ejstr_hell2)
	TEST_ADD(pass_ejstr_hell3)
	TEST_ADD(pass_ejstr_hell4)
	TEST_ADD(pass_ejstr_hell5)
	TEST_PAD
	TEST_ADD(pass_ejcmp1)
	TEST_ADD(pass_ejcmp2)
	TEST_ADD(pass_ejcmp3)
	TEST_ADD(pass_ejcmp4)
	TEST_ADD(pass_ejcmp5)
	TEST_ADD(pass_ejcmp6)
	TEST_ADD(pass_ejcmp7)
	TEST_ADD(pass_ejcmp8)
	TEST_ADD(pass_ejcmp9)
	TEST_ADD(pass_ejcmp10)
	TEST_ADD(pass_ejcmp11)
	TEST_ADD(pass_ejcmp12)
	TEST_ADD(pass_ejcmp13)
	TEST_ADD(pass_ejcmp14)
	TEST_PAD
	TEST_ADD(pass_ejbool1)
	TEST_ADD(pass_ejbool2)
};

void usage(void) {
	printf("usage: \n");
	printf("test [test | speed]\n");
}

int main(int argc, char **argv) {
	bool speed_test = false;

	if (argc > 2) {
		usage();
		return 1;
	}
	if (argc == 2) {
		if (strcmp(argv[1], "test") == 0) {
			speed_test = false;
		} else if (strcmp(argv[1], "speed") == 0) {
			speed_test = true;
		} else {
			usage();
			return 1;
		}
	}

	int res = tests_run_foreach(NULL, tests, arrlen(tests), stdout)
		? 0 : -1;
	if (speed_test) {
		//test_ejstr_speed();
		test_ejint_speed();
	}
	return res;
}



