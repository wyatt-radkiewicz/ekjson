#include "json/json.h"

static json_value *v;

int benchmark_jjson(const char *src) {
	v = NULL;
	if (!json_value_parse((char *)src, NULL, &v)) return 1;
	return 0;
}

void cleanup_jjson(void) {
	if (v) json_value_free(v);
}

