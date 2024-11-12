#include "json-c/json.h"

static json_object *obj;

int benchmark_jsonc(const char *src) {
	obj = json_tokener_parse(src);
	return 0;
}

void cleanup_jsonc(void) {
	json_object_put(obj);
}

