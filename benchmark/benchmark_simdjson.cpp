#include "simdjson.h"

extern "C" {
int benchmark_simdjson(const char *src);
void cleanup_simdjson(void);
void cleanup_simdjson_end(void);
}

static simdjson::padded_string gs;
static bool inited;
volatile int n;

int benchmark_simdjson(const char *src) {
	if (!inited) {
		gs = simdjson::padded_string(src, strlen(src));
		inited = true;
		n = 0;
	}

	simdjson::dom::parser parser;
	auto doc = parser.parse(gs);
	return 0;
}

void cleanup_simdjson(void) {

}

