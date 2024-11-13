#include <stdio.h>
#include <string.h>

#include "common.h"
#include "ekjson.h"

typedef struct human {
	char name[16];
	int age;
	int strength;
	int hp;
	float percentile;
} human_t;

bool load_human(const char *src, ejtok_t *tokens, human_t *human) {
	if (tokens[0].type != EJOBJ) return false;
	
	memset(human, 0, sizeof(*human));
	for (int i = 1; i < tokens[0].len; i += tokens[i].len) {
		const char *key = src + tokens[i].start;
		const char *value = src + tokens[i + 1].start;

		if (ejcmp(key, "name")) {
			ejstr(value, human->name, sizeof(human->name));
		} else if (ejcmp(key, "age")) {
			human->age = ejint(value);
		} else if (ejcmp(key, "strength")) {
			human->strength = ejint(value);
		} else if (ejcmp(key, "hp")) {
			human->hp = ejint(value);
		} else if (ejcmp(key, "percentile")) {
			human->percentile = ejflt(value);
		} else {
			return false;
		}
	}

	return true;
}

void human_print(const human_t *human) {
	printf("human\n");
	printf("\tname: %s\n", human->name);
	printf("\tage: %d\n", human->age);
	printf("\tstrength: %d\n", human->strength);
	printf("\thp: %d\n", human->hp);
	printf("\tpercentile: %f\n", human->percentile);
}

int main(int argc, char **argv) {
	ejtok_t tokens[16];
	char *file = file_load_str("object.json");
	if (!file) return 1;

	ejparse(file, tokens, sizeof(tokens)/sizeof(tokens[0]));

	human_t human;
	if (!load_human(file, tokens, &human)) return 1;
	human_print(&human);

	free(file);
	return 0;
}
