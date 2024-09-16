#include "ektoml.h"

toml_result_t toml_parse(const uint8_t *src, size_t root_table_len,
			const toml_t schema_root_table[static root_table_len],
			void *user_data);

