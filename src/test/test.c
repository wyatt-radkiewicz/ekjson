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
TEST_SETUP(array_empty, "[]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 0, 1)
TEST_END
TEST_SETUP(array_bool, "[true]", 64)
	CHECK_SIMPLE(JSON_ARRAY, 0, 2)
	CHECK_SIMPLE(JSON_ARRAY, 1, 1)
TEST_END

static const test_t tests[] = {
	TEST_ADD(test_nothing)
	TEST_ADD(test_array_array_array_empty)
	TEST_ADD(test_array_array_empty)
	TEST_ADD(test_array_empty)
};

int main(int argc, char **argv) {
	return tests_run_foreach(NULL, tests, arrlen(tests), stdout) ? 0 : -1;
}

