#include "../ekjson.h"
#include "ek.h"

#define TEST_SETUP(_name, _src, _ntoks) \
	static bool test_##_name(void) { \
		struct jsontok toks[_ntoks]; \
		const char *errloc = json_parse(_src, toks, arrlen(toks)); \
		const static int __base = __COUNTER__;
#define CHECK_SIMPLE(_type, _start, _len) \
	{ \
		const static int __idx = __COUNTER__ - __base - 1; \
		if (toks[__idx].type != _type) { \
			fprintf(stderr, "token %d type: %d != %d\n", __idx, \
				toks[__idx].type, _type); \
			return false; \
		} \
		if (toks[__idx].start != _start) { \
			fprintf(stderr, "token %d start: %d != %d\n", __idx, \
				toks[__idx].start, _start); \
			return false; \
		} \
		if (toks[__idx].len != _len) { \
			fprintf(stderr, "token %d len: %d != %d\n", __idx, \
				toks[__idx].len, _len); \
			return false; \
		} \
	}
#define TEST_END \
		return true; \
	}

static bool test_nothing(void) {
	struct jsontok toks[2];
	const char *errloc = json_parse("", toks, arrlen(toks));
	return errloc;
}

TEST_SETUP(array_array_array_empty, "[[[]]]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 0, 3)
	CHECK_SIMPLE(JSON_ARRAY, 1, 2)
	CHECK_SIMPLE(JSON_ARRAY, 2, 1)
TEST_END
TEST_SETUP(array_array_empty, "[[]]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 0, 2)
	CHECK_SIMPLE(JSON_ARRAY, 1, 1)
TEST_END
TEST_SETUP(array_bool, "[true]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 0, 2)
	CHECK_SIMPLE(JSON_TRUE, 1, 1)
TEST_END
TEST_SETUP(array_bools, "[true,false,true]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 0, 4)
	CHECK_SIMPLE(JSON_TRUE, 1, 1)
	CHECK_SIMPLE(JSON_FALSE, 6, 1)
	CHECK_SIMPLE(JSON_TRUE, 12, 1)
TEST_END
TEST_SETUP(array_empty, "[]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 0, 1)
TEST_END
TEST_SETUP(array_float, "[3.14]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 0, 2)
	CHECK_SIMPLE(JSON_NUMBER, 1, 1)
TEST_END
TEST_SETUP(array_floats, "[1.2,3.4,5.6]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 0, 4)
	CHECK_SIMPLE(JSON_NUMBER, 1, 1)
	CHECK_SIMPLE(JSON_NUMBER, 5, 1)
	CHECK_SIMPLE(JSON_NUMBER, 9, 1)
TEST_END
TEST_SETUP(array_int, "[1]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 0, 2)
	CHECK_SIMPLE(JSON_NUMBER, 1, 1)
TEST_END
TEST_SETUP(array_ints, "[1,2,3]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 0, 4)
	CHECK_SIMPLE(JSON_NUMBER, 1, 1)
	CHECK_SIMPLE(JSON_NUMBER, 3, 1)
	CHECK_SIMPLE(JSON_NUMBER, 5, 1)
TEST_END
TEST_SETUP(array_matrix, "[[1,2,3],[4,5,6],[7,8,9]]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 0, 13)
	CHECK_SIMPLE(JSON_ARRAY, 1, 4)
	CHECK_SIMPLE(JSON_NUMBER, 2, 1)
	CHECK_SIMPLE(JSON_NUMBER, 4, 1)
	CHECK_SIMPLE(JSON_NUMBER, 6, 1)
	CHECK_SIMPLE(JSON_ARRAY, 9, 4)
	CHECK_SIMPLE(JSON_NUMBER, 10, 1)
	CHECK_SIMPLE(JSON_NUMBER, 12, 1)
	CHECK_SIMPLE(JSON_NUMBER, 14, 1)
	CHECK_SIMPLE(JSON_ARRAY, 17, 4)
	CHECK_SIMPLE(JSON_NUMBER, 18, 1)
	CHECK_SIMPLE(JSON_NUMBER, 20, 1)
	CHECK_SIMPLE(JSON_NUMBER, 22, 1)
TEST_END

static const test_t tests[] = {
	TEST_ADD(test_nothing)
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
};

int main(int argc, char **argv) {
	return tests_run_foreach(NULL, tests, arrlen(tests), stdout) ? 0 : -1;
}

