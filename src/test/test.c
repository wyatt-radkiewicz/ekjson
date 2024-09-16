#include "ek.h"

static const test_t tests[] = {
	TEST_PAD
};

int main(int argc, char **argv) {
	return tests_run_foreach(NULL, tests, arrlen(tests), stdout) ? 0 : -1;
}

