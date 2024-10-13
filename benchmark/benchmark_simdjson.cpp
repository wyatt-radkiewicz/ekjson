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

	simdjson::ondemand::parser parser;
	simdjson::ondemand::document doc = parser.iterate(gs);
	simdjson::ondemand::array arr = doc.get_array();
	for (auto i : arr) {
		simdjson::ondemand::object obj = i.get_object();
		n += obj.count_fields();
	}
	return 0;
}

void cleanup_simdjson(void) {

}

