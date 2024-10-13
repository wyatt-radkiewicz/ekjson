#include "ekjson/src/ekjson.h"

#define N 1024*1024

int benchmark_ekjson(const char *src) {
	// 16k tokens (16k*8B of data)
	static ejtok_t t[N];
	if (ejparse(src, t, N).err) return 1;

	// Validate
	//for (unsigned i = 0; i < ntoks; i++) {
	//	if (!json_validate_value(src, tokens[i])) return 1;
	//}
	return 0;
}

void cleanup_ekjson(void) {

}

