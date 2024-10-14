#include "ekjson/src/ekjson.h"

#define N 1024*1024

static ejtok_t t[N];

int benchmark_ekjson(const char *src) {
	// 16k tokens (16k*8B of data)
	if (ejparse(src, t, N).err) return 1;
	//static struct jsontok t[N];
	//unsigned n;
	//if (json_parse(src, t, N, &n)) return 1;

	// Validate
	//for (unsigned i = 0; i < n; i++) {
	//	if (!json_validate_value(src, t[i])) return 1;
	//}
	return 0;
}

void cleanup_ekjson(void) {

}

