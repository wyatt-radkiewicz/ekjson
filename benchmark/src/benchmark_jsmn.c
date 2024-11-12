#include <string.h>

#include "jsmn/jsmn.h"

#define N 1024*1024

int benchmark_jsmn(const char *src) {
	static jsmn_parser p;
	static jsmntok_t t[N];

	jsmn_init(&p);
	if (jsmn_parse(&p, src, strlen(src), t, N) < 0) return 1;
	return 0;
}

void cleanup_jsmn(void) {

}

