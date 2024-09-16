#include "ektoml.h"

// Helper defines to return an error or okay value
#define TOML_OK ((toml_result_t){ .okay = true })
#define TOML_ERR(_state, _in, _msg) ((toml_result_t){ \
	.okay = false, \
	.line = (_state)->line, \
	.in = (_in), \
	.msg = (_msg), \
})

// NOTE: This check macro depends on depth, state, and an optional table value
// It also requries that the function returns 
#define TOML_CHECK_DEPTH(_table) \
	if (depth >= TOML_MAX_RECURSION_DEPTH) { \
		return TOML_ERR(state, (_table), "hit recursion depth limit"); \
	}

// Monolithic struct for parsing
typedef struct parser_state {
	// Where we are in the source bytes wise
	const uint8_t *src;

	// What line we're on
	uint32_t line, col;

	// The root node
	const toml_t *root;

	// The user variable passed in (passed to setter funcs)
	void *user;

	// What 'has been parsed flag' generation we are on
	uint16_t gen;

	// Temporary storage for key names
	uint8_t key_buf[TOML_MAX_KEY_SIZE];
} parser_state_t;

static toml_result_t toml_parse_key(parser_state_t *const state) {
	return TOML_OK;
}

static toml_result_t toml_dotable(parser_state_t *const state,
					const toml_t *table,
					const uint32_t depth) {
	TOML_CHECK_DEPTH(table)


	return TOML_OK;
}

toml_result_t toml_parse(const toml_args_t args) {
	const toml_t root_node = {
		.val = {
			.type = TOML_TABLE,
			.isoptional = false,
			.data.table_start = args.root_table,
			.len = args.root_len,
		},
	};
	parser_state_t state = {
		.src = args.src,
		.line = 1,
		.user = args.user,
		.root = &root_node,
		.gen = *args.gen,
	};

	if (args.root_len >= 1ull << (sizeof(root_node.val.len) * 8)) {
		return TOML_ERR(&state, NULL, "root node length too big");
	}

	return toml_dotable(&state, &root_node, 1);
}

