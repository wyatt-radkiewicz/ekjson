#include <stdint.h>

#include "../ekjson.h"
#include "ek.h"

#define TEST_SETUP(_name, _src, _ntoks) \
	static bool _name(unsigned id) { \
		struct jsontok toks[_ntoks]; \
		static const char *const __src = _src; \
		const char *errloc = json_parse(__src, toks, arrlen(toks)); \
		int __idx = 0, _pos = 0;
#define PASS_SETUP(_name, _src, _ntoks) \
	TEST_SETUP(pass_##_name, _src, _ntoks) \
	const bool dopass = true;
#define FAIL_SETUP(_name, _src, _ntoks) \
	TEST_SETUP(fail_##_name, _src, _ntoks) \
	const bool dopass = false;
#define PASS_END \
		return true; \
	err: \
		return false; \
	}
#define FAIL_END \
		return false; \
	}
#define CHECK_BASE(_type, _size, _len) \
	if (toks[__idx].type != _type) { \
		if (!dopass) return true; \
		fprintf(stderr, "token %d type: %d != %d\n", \
			__idx, toks[__idx].type, _type); \
		return false; \
	} \
	if (_type == JSON_STRING || _type == JSON_LITERAL_STRING) _pos++; \
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
	} \
	if (!json_validate_value(__src, toks[__idx], _type)) { \
		if (!dopass) return true; \
		fprintf(stderr, "token %d didn't pass validation!\n", __idx); \
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
		CHECK_BASE(JSON_NUMBER, _size, 1) \
		const double num = json_flt(__src + toks[__idx].start); \
		if (num != _num) { \
			if (!dopass) return true; \
			fprintf(stderr, "token %d num: %f != %f\n", \
				__idx, num, (double)_num); \
			return false; \
		} \
	CHECK_END
#define CHECK_INT(_size, _num) \
	CHECK_START \
		CHECK_BASE(JSON_NUMBER, _size, 1) \
		const int64_t num = json_int(__src + toks[__idx].start); \
		if (num != _num) { \
			if (!dopass) return true; \
			fprintf(stderr, "token %d int: %lld != %lld\n", \
				__idx, num, (int64_t)_num); \
			return false; \
		} \
	CHECK_END
#define CHECK_STRING(_size, _len, _str) \
	CHECK_START \
		CHECK_BASE(JSON_STRING, _size, _len) \
		const char *__str = __src + toks[__idx].start; \
		if (!json_streq(__str, _str)) { \
			if (!dopass) return true; \
			char strbuf[4096]; \
			if (json_str(__str, strbuf, 4096)) { \
				fprintf(stderr, "token %d str: expected %s", \
					__idx, _str); \
				return false; \
			} \
			fprintf(stderr, "token %d str: %s != %s\n", \
				__idx, __str, _str); \
			return false; \
		} \
	CHECK_END

static bool pass_nothing(unsigned id) {
	struct jsontok toks[2];
	const char *errloc = json_parse("", toks, arrlen(toks));
	return errloc;
}

PASS_SETUP(array_array_array_empty, "[[[]]]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 3)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_SIMPLE(JSON_ARRAY, 1, 1)
PASS_END
PASS_SETUP(array_array_empty, "[[]]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_SIMPLE(JSON_ARRAY, 1, 1)
PASS_END
PASS_SETUP(array_bool, "[true]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_SIMPLE(JSON_TRUE, 1, 1)
PASS_END
PASS_SETUP(array_bools, "[true,false,true]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_SIMPLE(JSON_TRUE, 5, 1)
	CHECK_SIMPLE(JSON_FALSE, 6, 1)
	CHECK_SIMPLE(JSON_TRUE, 5, 1)
PASS_END
PASS_SETUP(array_empty, "[]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 2, 1)
PASS_END
PASS_SETUP(array_float, "[3.14]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_FLOAT(5, 3.14)
PASS_END
PASS_SETUP(array_floats, "[1.2,3.4,5.6]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_FLOAT(4, 1.2)
	CHECK_FLOAT(4, 3.4)
	CHECK_FLOAT(4, 5.6)
PASS_END
PASS_SETUP(array_int, "[1]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_FLOAT(1, 1)
PASS_END
PASS_SETUP(array_ints, "[1,2,3]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_FLOAT(2, 1)
	CHECK_FLOAT(2, 2)
	CHECK_FLOAT(2, 3)
PASS_END
PASS_SETUP(array_matrix, "[[1,2,3],[4,5,6],[7,8,9]]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 13)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_FLOAT(2, 1)
	CHECK_FLOAT(2, 2)
	CHECK_FLOAT(3, 3)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_FLOAT(2, 4)
	CHECK_FLOAT(2, 5)
	CHECK_FLOAT(3, 6)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_FLOAT(2, 7)
	CHECK_FLOAT(2, 8)
	CHECK_FLOAT(3, 9)
PASS_END
PASS_SETUP(array_null, "[null]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_SIMPLE(JSON_NULL, 5, 1)
PASS_END
PASS_SETUP(array_nulls, "[null,null,null]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_SIMPLE(JSON_NULL, 5, 1)
	CHECK_SIMPLE(JSON_NULL, 5, 1)
	CHECK_SIMPLE(JSON_NULL, 5, 1)
PASS_END
PASS_SETUP(array_object, "[{\"a\":1}]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_SIMPLE(JSON_OBJECT, 1, 3)
	CHECK_STRING(3, 2, "a")
	CHECK_FLOAT(1, 1)
PASS_END
PASS_SETUP(array_object_empty, "[{}]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_SIMPLE(JSON_OBJECT, 1, 1)
PASS_END
PASS_SETUP(array_objects, "[{\"a\":1},{\"b\":2},{\"c\":3}]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 10)
	CHECK_SIMPLE(JSON_OBJECT, 1, 3)
	CHECK_STRING(3, 2, "a")
	CHECK_FLOAT(3, 1)
	CHECK_SIMPLE(JSON_OBJECT, 1, 3)
	CHECK_STRING(3, 2, "b")
	CHECK_FLOAT(3, 2)
	CHECK_SIMPLE(JSON_OBJECT, 1, 3)
	CHECK_STRING(3, 2, "c")
	CHECK_FLOAT(3, 3)
PASS_END
PASS_SETUP(array_string, "[\"abc\"]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_STRING(4, 1, "abc")
PASS_END
PASS_SETUP(array_string_empty, "[\"\"]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_STRING(2, 1, "")
PASS_END
PASS_SETUP(array_strings, "[\"abc\",\"def\",\"ghi\"]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_STRING(5, 1, "abc")
	CHECK_STRING(5, 1, "def")
	CHECK_STRING(5, 1, "ghi")
PASS_END
PASS_SETUP(array_tensor, "[[[1,2],[3,4]],[[5,6],[7,8]]]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 15)
	CHECK_SIMPLE(JSON_ARRAY, 1, 7)
	CHECK_SIMPLE(JSON_ARRAY, 1, 3)
	CHECK_FLOAT(2, 1)
	CHECK_FLOAT(3, 2)
	CHECK_SIMPLE(JSON_ARRAY, 1, 3)
	CHECK_FLOAT(2, 3)
	CHECK_FLOAT(4, 4)
	CHECK_SIMPLE(JSON_ARRAY, 1, 7)
	CHECK_SIMPLE(JSON_ARRAY, 1, 3)
	CHECK_FLOAT(2, 5)
	CHECK_FLOAT(3, 6)
	CHECK_SIMPLE(JSON_ARRAY, 1, 3)
	CHECK_FLOAT(2, 7)
	CHECK_FLOAT(2, 8)
PASS_END

PASS_SETUP(bool_false, "false", 64)
	CHECK_SIMPLE(JSON_FALSE, 2, 1)
PASS_END
PASS_SETUP(bool_true, "true", 64)
	CHECK_SIMPLE(JSON_TRUE, 2, 1)
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
PASS_SETUP(int_max, "9223372036854775807", 64)
	CHECK_INT(0, 9223372036854775807ull)
PASS_END
PASS_SETUP(int_min, "-9223372036854775808", 64)
	CHECK_INT(0, -9223372036854775808ull)
PASS_END

PASS_SETUP(object_array, "{\"abc\":[1,2,3]}", 64)
	CHECK_SIMPLE(JSON_OBJECT, 1, 6)
	CHECK_STRING(5, 5, "abc")
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_INT(2, 1)
	CHECK_INT(2, 2)
	CHECK_INT(2, 3)
PASS_END
PASS_SETUP(object_array_object, "{\"a\":[{\"a\":1}]}", 64)
	CHECK_SIMPLE(JSON_OBJECT, 1, 6)
	CHECK_STRING(3, 5, "a")
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_SIMPLE(JSON_OBJECT, 1, 3)
	CHECK_STRING(3, 2, "a")
	CHECK_INT(1, 1)
PASS_END
PASS_SETUP(object_array_objects, "{\"a\":[{\"a\":1},{\"b\":2},{\"c\":3}]}", 64)
	CHECK_SIMPLE(JSON_OBJECT, 1, 12)
	CHECK_STRING(3, 11, "a")
	CHECK_SIMPLE(JSON_ARRAY, 1, 10)
	CHECK_SIMPLE(JSON_OBJECT, 1, 3)
	CHECK_STRING(3, 2, "a")
	CHECK_INT(3, 1)
	CHECK_SIMPLE(JSON_OBJECT, 1, 3)
	CHECK_STRING(3, 2, "b")
	CHECK_INT(3, 2)
	CHECK_SIMPLE(JSON_OBJECT, 1, 3)
	CHECK_STRING(3, 2, "c")
	CHECK_INT(3, 3)
PASS_END
PASS_SETUP(object_string, "{\"abc\":\"def\"}", 64)
	CHECK_SIMPLE(JSON_OBJECT, 1, 3)
	CHECK_STRING(5, 2, "abc")
	CHECK_STRING(5, 1, "def")
PASS_END
PASS_SETUP(object_strings, "{\"abc\":\"def\",\"ghi\":\"jkl\","
	"\"mno\":\"pqr\"}", 64)
	CHECK_SIMPLE(JSON_OBJECT, 1, 7)
	CHECK_STRING(5, 2, "abc")
	CHECK_STRING(5, 1, "def")
	CHECK_STRING(5, 2, "ghi")
	CHECK_STRING(5, 1, "jkl")
	CHECK_STRING(5, 2, "mno")
	CHECK_STRING(5, 1, "pqr")
PASS_END
PASS_SETUP(object_true, "{\"abc\":true}", 64)
	CHECK_SIMPLE(JSON_OBJECT, 1, 3)
	CHECK_STRING(5, 2, "abc")
	CHECK_SIMPLE(JSON_TRUE, 1, 1)
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
PASS_SETUP(string_escape, "\"\\u1234\"", 64)
	CHECK_STRING(1, 1, "\u1234")
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
	CHECK_SIMPLE(JSON_NULL, 1, 1)
PASS_END

FAIL_SETUP(bool_false, "fals", 64)
	CHECK_SIMPLE(JSON_FALSE, 1, 1)
FAIL_END
FAIL_SETUP(bool_true, "tru", 64)
	CHECK_SIMPLE(JSON_TRUE, 1, 1)
FAIL_END

FAIL_SETUP(float_max, "1.8e+308", 64)
	CHECK_FLOAT(1, 0)
FAIL_END
FAIL_SETUP(float_min, "-1.8e+308", 64)
	CHECK_FLOAT(1, 0)
FAIL_END
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

FAIL_SETUP(int_max, "9223372036854775808", 64)
	CHECK_INT(1, 0)
FAIL_END
FAIL_SETUP(int_min, "-9223372036854775809", 64)
	CHECK_INT(1, 0)
FAIL_END
FAIL_SETUP(int_dot, "1234.8", 64)
	CHECK_INT(1, 1235)
FAIL_END
FAIL_SETUP(int_a, "12a4", 64)
	CHECK_INT(1, 1234)
FAIL_END

FAIL_SETUP(string_missing_begin_quote, "abc\"", 64)
	CHECK_STRING(1, 1, "abc")
FAIL_END
FAIL_SETUP(string_missing_end_quote, "\"abc", 64)
	CHECK_STRING(1, 1, "abc")
FAIL_END

FAIL_SETUP(null, "nul", 64)
	CHECK_SIMPLE(JSON_NULL, 1, 1)
FAIL_END

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
	TEST_ADD(pass_int_max)
	TEST_ADD(pass_int_min)
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
	TEST_ADD(fail_float_max)
	TEST_ADD(fail_float_min)
	TEST_ADD(fail_float_dot_after)
	TEST_ADD(fail_float_dot_before)
	TEST_ADD(fail_float_leading_zeros)
	TEST_ADD(fail_float_exponent)
	TEST_ADD(fail_float_exponent_a)
	TEST_ADD(fail_float_exponent_sign)
	TEST_ADD(fail_float_a)
	TEST_PAD
	TEST_ADD(fail_int_max)
	TEST_ADD(fail_int_min)
	TEST_ADD(fail_int_dot)
	TEST_ADD(fail_int_a)
	TEST_PAD
	TEST_ADD(fail_string_missing_begin_quote)
	TEST_ADD(fail_string_missing_end_quote)
	TEST_PAD
	TEST_ADD(fail_null)
};

int main(int argc, char **argv) {
	return tests_run_foreach(NULL, tests, arrlen(tests), stdout) ? 0 : -1;
}

