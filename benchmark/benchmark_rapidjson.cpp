#ifdef __APPLE__
#define RAPIDJSON_NEON
#else
#define RAPIDJSON_SSE2
#endif

#include "rapidjson/document.h"

using namespace rapidjson;

extern "C" {
int benchmark_rapidjson(const char *src);
void cleanup_rapidjson(void);
}

int benchmark_rapidjson(const char *src) {
	Document d;
	d.Parse(src);
	return 0;
}
void cleanup_rapidjson(void) {

}

