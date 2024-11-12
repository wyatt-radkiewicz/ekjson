#include "fast_double_parser/include/fast_double_parser.h"

extern "C" {
double parse_fast_double_parser(const char *src);
}

double parse_fast_double_parser(const char *src) {
	double d;
	(void)fast_double_parser::parse_number(src, &d);
	return d;
}

