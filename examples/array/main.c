#include <stdio.h>
#include <string.h>

#include "common.h"
#include "ekjson.h"

int main(int argc, char **argv) {
	ejtok_t tokens[48];
	char *file = file_load_str("array.json");
	if (!file) return 1;

	ejparse(file, tokens, sizeof(tokens)/sizeof(tokens[0]));

	if (tokens[0].type != EJARR) return 1;
	char buf[32];
	for (int i = 1; i < tokens[0].len; i += tokens[i].len) {
		for (int j = i + 1; j < i + tokens[i].len; j++) {
			switch (tokens[j].type) {
			case EJSTR:
				ejstr(file + tokens[j].start,
					buf, sizeof(buf));
				printf("%s, ", buf);
				break;
			case EJINT:
				printf("%lld, ",ejint(file + tokens[j].start));
				break;
			case EJFLT:
				printf("%lg, ", ejflt(file + tokens[j].start));
				break;
			default: return 1;
			}
		}
		printf("\n");
	}

	free(file);
	return 0;
}

