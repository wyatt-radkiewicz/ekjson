#include "../ekjson.h"
#include "ek.h"

#define TEST_SETUP(_name, _src, _ntoks) \
	static bool test_##_name(void) { \
		struct jsontok toks[_ntoks]; \
		static const char *const __src = _src; \
		const char *errloc = json_parse(__src, toks, arrlen(toks)); \
		const static int __base = __COUNTER__; \
		int _pos = 0;
#define TEST_END \
		return true; \
	}
#define CHECK_BASE(_type, _size, _len) \
	if (toks[__idx].type != _type) { \
		fprintf(stderr, "token %d type: %d != %d\n", \
			__idx, toks[__idx].type, _type); \
		return false; \
	} \
	if (toks[__idx].start != _pos) { \
		fprintf(stderr, "token %d start: %d != %d\n", \
			__idx, toks[__idx].start, _pos); \
		return false; \
	} \
	_pos += _size; \
	if (toks[__idx].len != _len) { \
		fprintf(stderr, "token %d len: %d != %d\n", \
			__idx, toks[__idx].len, _len); \
		return false; \
	}
#define CHECK_START \
	{ \
		const static int __idx = __COUNTER__ - __base - 1;
#define CHECK_END \
	}
#define CHECK_SIMPLE(_type, _size, _len) \
	CHECK_START \
		CHECK_BASE(_type, _size, _len) \
	CHECK_END
#define CHECK_NUMBER(_start, _num) \
	CHECK_START \
		CHECK_BASE(JSON_NUMBER, _start, 1) \
		const double num = json_num(__src + toks[__idx].start); \
		if (num != (double)_num) { \
			fprintf(stderr, "token %d num: %f != %f\n", \
				__idx, num, (double)_num); \
			return false; \
		} \
	CHECK_END
#define CHECK_STRING(_start, _len, _str) \
	CHECK_START \
		CHECK_BASE(JSON_STRING, _start, _len) \
		const char *__str = __src + toks[__idx].start; \
		if (!json_streq(__str, _str)) { \
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

static bool test_nothing(void) {
	struct jsontok toks[2];
	const char *errloc = json_parse("", toks, arrlen(toks));
	return errloc;
}

TEST_SETUP(array_array_array_empty, "[[[]]]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 3)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_SIMPLE(JSON_ARRAY, 1, 1)
TEST_END
TEST_SETUP(array_array_empty, "[[]]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_SIMPLE(JSON_ARRAY, 1, 1)
TEST_END
TEST_SETUP(array_bool, "[true]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_SIMPLE(JSON_TRUE, 1, 1)
TEST_END
TEST_SETUP(array_bools, "[true,false,true]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_SIMPLE(JSON_TRUE, 5, 1)
	CHECK_SIMPLE(JSON_FALSE, 6, 1)
	CHECK_SIMPLE(JSON_TRUE, 5, 1)
TEST_END
TEST_SETUP(array_empty, "[]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 2, 1)
TEST_END
TEST_SETUP(array_float, "[3.14]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_NUMBER(5, 3.14)
TEST_END
TEST_SETUP(array_floats, "[1.2,3.4,5.6]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_NUMBER(4, 1.2)
	CHECK_NUMBER(4, 3.4)
	CHECK_NUMBER(4, 5.6)
TEST_END
TEST_SETUP(array_int, "[1]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_NUMBER(1, 1)
TEST_END
TEST_SETUP(array_ints, "[1,2,3]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_NUMBER(2, 1)
	CHECK_NUMBER(2, 2)
	CHECK_NUMBER(2, 3)
TEST_END
TEST_SETUP(array_matrix, "[[1,2,3],[4,5,6],[7,8,9]]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 13)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_NUMBER(2, 1)
	CHECK_NUMBER(2, 2)
	CHECK_NUMBER(3, 3)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_NUMBER(2, 4)
	CHECK_NUMBER(2, 5)
	CHECK_NUMBER(3, 6)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_NUMBER(2, 7)
	CHECK_NUMBER(2, 8)
	CHECK_NUMBER(3, 9)
TEST_END
TEST_SETUP(array_null, "[null]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_SIMPLE(JSON_NULL, 5, 1)
TEST_END
TEST_SETUP(array_nulls, "[null,null,null]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_SIMPLE(JSON_NULL, 5, 1)
	CHECK_SIMPLE(JSON_NULL, 5, 1)
	CHECK_SIMPLE(JSON_NULL, 5, 1)
TEST_END
TEST_SETUP(array_object, "[{\"a\":1}]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_SIMPLE(JSON_OBJECT, 2, 3)
	CHECK_STRING(3, 2, "a")
	CHECK_NUMBER(1, 1)
TEST_END
TEST_SETUP(array_object_empty, "[{}]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_SIMPLE(JSON_OBJECT, 1, 1)
TEST_END
TEST_SETUP(array_objects, "[{\"a\":1},{\"b\":2},{\"c\":3}]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 10)
	CHECK_SIMPLE(JSON_OBJECT, 2, 3)
	CHECK_STRING(3, 2, "a")
	CHECK_NUMBER(3, 1)
	CHECK_SIMPLE(JSON_OBJECT, 2, 3)
	CHECK_STRING(3, 2, "b")
	CHECK_NUMBER(3, 2)
	CHECK_SIMPLE(JSON_OBJECT, 2, 3)
	CHECK_STRING(3, 2, "c")
	CHECK_NUMBER(3, 3)
TEST_END
TEST_SETUP(array_string, "[\"abc\"]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 2, 2)
	CHECK_STRING(4, 1, "abc")
TEST_END
TEST_SETUP(array_string_empty, "[\"\"]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 2, 2)
	CHECK_STRING(2, 1, "")
TEST_END
TEST_SETUP(array_strings, "[\"abc\",\"def\",\"ghi\"]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 2, 4)
	CHECK_STRING(6, 1, "abc")
	CHECK_STRING(6, 1, "def")
	CHECK_STRING(6, 1, "ghi")
TEST_END
TEST_SETUP(array_tensor, "[[[1,2],[3,4]],[[5,6],[7,8]]]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 1, 15)
	CHECK_SIMPLE(JSON_ARRAY, 1, 7)
	CHECK_SIMPLE(JSON_ARRAY, 1, 3)
	CHECK_NUMBER(2, 1)
	CHECK_NUMBER(3, 2)
	CHECK_SIMPLE(JSON_ARRAY, 1, 3)
	CHECK_NUMBER(2, 3)
	CHECK_NUMBER(4, 4)
	CHECK_SIMPLE(JSON_ARRAY, 1, 7)
	CHECK_SIMPLE(JSON_ARRAY, 1, 3)
	CHECK_NUMBER(2, 5)
	CHECK_NUMBER(3, 6)
	CHECK_SIMPLE(JSON_ARRAY, 1, 3)
	CHECK_NUMBER(2, 7)
	CHECK_NUMBER(2, 8)
TEST_END

static const test_t tests[] = {
	TEST_ADD(test_nothing)
	TEST_PAD
	TEST_ADD(test_array_array_array_empty)
	TEST_ADD(test_array_array_empty)
	TEST_ADD(test_array_bool)
	TEST_ADD(test_array_bools)
	TEST_ADD(test_array_empty)
	TEST_ADD(test_array_float)
	TEST_ADD(test_array_floats)
	TEST_ADD(test_array_int)
	TEST_ADD(test_array_ints)
	TEST_ADD(test_array_matrix)
	TEST_ADD(test_array_null)
	TEST_ADD(test_array_nulls)
	TEST_ADD(test_array_object)
	TEST_ADD(test_array_object_empty)
	TEST_ADD(test_array_objects)
	TEST_ADD(test_array_string)
	TEST_ADD(test_array_string_empty)
	TEST_ADD(test_array_strings)
	TEST_ADD(test_array_tensor)
	TEST_PAD
};

int main(int argc, char **argv) {
	return tests_run_foreach(NULL, tests, arrlen(tests), stdout) ? 0 : -1;
}

