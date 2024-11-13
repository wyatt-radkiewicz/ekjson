#include <stdio.h>

#include "ekjson.h"

int main(int argc, char **argv) {
	ejtok_t tokens[2];

	{ // Parsing strings
		const char *const src = "\"Hello World!\"";
		ejparse(src, tokens, 2);

		char buf[13];
		ejstr(src + tokens[0].start, buf, sizeof(buf));
		printf("String token is: %s\n", buf);
	}

	{ // Parsing integers
		const char *const src = "2632010";
		ejparse(src, tokens, 2);
		printf("Integer token is: %lld\n",
			ejint(src + tokens[0].start));
	}

	{ // Parsing floats
		const char *const src = "3.14159";
		ejparse(src, tokens, 2);
		printf("Float token is: %lf\n",
			ejflt(src + tokens[0].start));
	}

	{ // Parsing boolean
		const char *const src = "true";
		ejparse(src, tokens, 2);
		printf("Boolean token is: %s\n",
			ejbool(src + tokens[0].start) ? "true" : "false");
	}

	{ // Parsing null
		const char *const src = "null";
		ejparse(src, tokens, 2);

		printf("Null token is: %s\n",
			tokens[0].type == EJNULL ? "valid" : "invalid");
	}

	return 0;
}

