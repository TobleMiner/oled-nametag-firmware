#include "cbjson.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define STRBUF_INITIAL_SIZE	32
#define STACK_INITIAL_SIZE	 8

typedef enum cbjson_stack_type {
	CBJSON_STACK_ARRAY,
	CBJSON_STACK_OBJECT,
} cbjson_stack_type_t;

struct cbjson_stack {
	cbjson_stack_type_t type;
	struct list_head non_matching_paths;
	union {
		struct {
			unsigned int index;
		} array;
	};
};

typedef enum cbjson_path_type_t {
	CBJSON_PATH_TYPE_DESCEND,
	CBJSON_PATH_TYPE_OBJECT_KEY,
	CBJSON_PATH_TYPE_ARRAY_ENTRY,
} cbjson_path_type_t;

typedef struct cbjson_path_component {
	cbjson_path_type_t type;
	union {
		struct {
			bool any_key;
			const char *name;
			unsigned int name_len;
		} object_key;
		struct {
			bool any_index;
			unsigned int index;
		} array_entry;
	};
} cbjson_path_component_t;

#define PATH_PARSE_NO_COMPONENT			1
#define PATH_PARSE_ARRAY_IDX_NOT_TERMINATED	2
#define PATH_PARSE_ARRAY_IDX_INVALID		3
#define PATH_PARSE_OBJECT_KEY_INVALID		4

static int path_parse_descend(const char **rpath, cbjson_path_component_t *component) {
	component->type = CBJSON_PATH_TYPE_DESCEND;
	*rpath = *rpath + 1;

	return 0;
}

static int path_parse_array_index(const char **rpath, cbjson_path_component_t *component) {
	const char *path = *rpath + 1;
	const char *idx_start = path;
	const char *idx_end;
	unsigned long idx;

	idx_end = strchr(idx_start, ']');
	if (!idx_end) {
		return PATH_PARSE_ARRAY_IDX_NOT_TERMINATED;
	}

	if (idx_end == idx_start || *idx_start == '*') {
		component->type = CBJSON_PATH_TYPE_ARRAY_ENTRY;
		component->array_entry.any_index = true;
		component->array_entry.index = 0;
	} else {
		errno = 0;
		idx = strtoul(idx_start, NULL, 10);
		if (idx != ULONG_MAX && !errno) {
			component->type = CBJSON_PATH_TYPE_ARRAY_ENTRY;
			component->array_entry.any_index = false;
			component->array_entry.index = idx;
		} else {
			return PATH_PARSE_ARRAY_IDX_INVALID;
		}
	}

	*rpath = idx_end + 1;
	return 0;
}

static int path_parse_object_key(const char **rpath, cbjson_path_component_t *component) {
	const char *key_start = *rpath;
	const char *key_end;
	unsigned int key_len;

	key_end = strchr(key_start, '.');
	if (!key_end) {
		key_end = key_start + strlen(key_start);
	}

	key_len = key_end - key_start;
	if (!key_len) {
		return PATH_PARSE_OBJECT_KEY_INVALID;
	}

	if (key_len == 1 && *key_start == '*') {
		component->type = CBJSON_PATH_TYPE_OBJECT_KEY;
		component->object_key.any_key = true;
		component->object_key.name = NULL;
		component->object_key.name_len = 0;
	} else {
		component->type = CBJSON_PATH_TYPE_OBJECT_KEY;
		component->object_key.any_key = false;
		component->object_key.name = key_start;
		component->object_key.name_len = key_len;
	}

	*rpath = key_end;
	return 0;
}

static int parse_next_path_component(const char **rpath, cbjson_path_component_t *component) {
	const char *path = *rpath;

	if (!*path) {
		return PATH_PARSE_NO_COMPONENT;
	}

	switch (*path) {
	case ('.'):
		return path_parse_descend(rpath, component);
	case('['):
		return path_parse_array_index(rpath, component);
	default:
		return path_parse_object_key(rpath, component);
	}
}

static int get_path_depth(const char *path) {
	cbjson_path_component_t component;
	int err;
	unsigned int depth = 0;

	while ((err = parse_next_path_component(&path, &component)) == 0) {
		if (component.type == CBJSON_PATH_TYPE_DESCEND) {
			depth++;
		}
	}

	if (err == PATH_PARSE_NO_COMPONENT) {
		return depth;
	}

	return -err;
}

static int path_seek_to_depth(cbjson_path_t *json_path, unsigned int target_depth) {
	cbjson_path_component_t component;
	int err;
	unsigned int depth = 0;
	const char *path = json_path->path;

	if (target_depth == 0) {
		json_path->path_match = path;
		return CBJSON_OK;
	}

	while ((err = parse_next_path_component(&path, &component)) == 0) {
		if (component.type == CBJSON_PATH_TYPE_DESCEND) {
			depth++;
		}

		if (depth == target_depth) {
			json_path->path_match = path;
			return CBJSON_OK;
		}
	}

	return err;
}

static int path_descend(cbjson_path_t *json_path) {
	cbjson_path_component_t component;
	int err;
	const char *path = json_path->path_match;

	while ((err = parse_next_path_component(&path, &component)) == 0) {
		if (component.type == CBJSON_PATH_TYPE_DESCEND) {
			json_path->path_match = path;
			return CBJSON_OK;
		}
	}

	return err;
}

void cbjson_init(cbjson_t *cbj) {
	INIT_LIST_HEAD(cbj->paths);
	INIT_LIST_HEAD(cbj->non_matching_paths);
	cbj->stack = NULL;
	cbj->stack_size = 0;
	cbj->stack_depth = 0;
	cbj->strbuf = NULL;
	cbj->strbuf_size = 0;
	cbj->in_literal = false;
	cbj->escaped = false;
	cbj->str_is_key = false;
	cbj->in_array = false;
}

void cbjson_free(cbjson_t *cbj) {
	if (cbj->stack) {
		free(cbj->stack);
	}
	if (cbj->strbuf) {
		free(cbj->strbuf);
	}
}

int cbjson_path_init(cbjson_path_t *path, const char *path_, cbjson_path_cb_f cb, void *priv) {
	int err;

	INIT_LIST_HEAD(path->list);
	path->path = path_;
	path->path_match = path_;

	err = get_path_depth(path_);
	if (err < 0) {
		return -err;
	}
	path->path_depth = err;
	path->cb = cb;
	path->priv = priv;

	return CBJSON_OK;
}

static int push_stack_entry(cbjson_t *cbj, cbjson_stack_t **stack_entry) {
	cbjson_stack_t *entry;

	if (!cbj->stack) {
		cbj->stack = calloc(STACK_INITIAL_SIZE, sizeof(cbjson_stack_t));
		if (!cbj->stack) {
			return CBJSON_ERR_NO_MEM;
		}
		cbj->stack_size = STACK_INITIAL_SIZE;
		cbj->stack_depth = 0;
	}

	if (cbj->stack_depth >= cbj->stack_size) {
		cbjson_stack_t *stack = reallocarray(cbj->stack, sizeof(cbjson_stack_t), cbj->stack_size * 2);

		if (!stack) {
			return CBJSON_ERR_NO_MEM;
		}
		cbj->stack = stack;
		cbj->stack_size *= 2;
	}

	entry = &cbj->stack[cbj->stack_depth++];
	INIT_LIST_HEAD(entry->non_matching_paths);
	if (cbj->in_array) {
		entry->type = CBJSON_STACK_ARRAY;
		entry->array.index = cbj->array_index;
	} else {
		entry->type = CBJSON_STACK_OBJECT;
	}
	*stack_entry = entry;
	return CBJSON_OK;
}

static int pop_stack_entry(cbjson_t *cbj, cbjson_stack_t **stack_entry) {
	cbjson_stack_t *entry;
	struct list_head *next;
	cbjson_path_t *cursor;

	if (!cbj->stack_depth) {
		return CBJSON_ERR_NOT_NESTED;
	}

	entry = &cbj->stack[--cbj->stack_depth];
	LIST_FOR_EACH_ENTRY(cursor, &cbj->paths, list) {
		int err = path_seek_to_depth(cursor, cbj->stack_depth);
		assert(!err);
	}

	cbj->in_array = (entry->type == CBJSON_STACK_ARRAY);
	if (cbj->in_array) {
		cbj->array_index = entry->array.index;
	}
	LIST_FOR_EACH_ENTRY_SAFE(cursor, next, &entry->non_matching_paths, list) {
		LIST_DELETE(&cursor->list);
		LIST_APPEND(&cursor->list, &cbj->paths);
	}

	if (stack_entry) {
		*stack_entry = entry;
	}

	return CBJSON_OK;
}

static void restore_non_matching_paths(cbjson_t *cbj) {
	struct list_head *next;
	cbjson_path_t *cursor;

	LIST_FOR_EACH_ENTRY_SAFE(cursor, next, &cbj->non_matching_paths, list) {
		LIST_DELETE(&cursor->list);
		LIST_APPEND(&cursor->list, &cbj->paths);
	}
}

static int cbjson_add_char(cbjson_t *cbj, char c) {
	if (!cbj->strbuf) {
		cbj->strbuf = calloc(1, STRBUF_INITIAL_SIZE);
		if (!cbj->strbuf) {
			return CBJSON_ERR_NO_MEM;
		}
		cbj->strbuf_size = STRBUF_INITIAL_SIZE;
		cbj->strbuf_depth = 0;
	}

	if (cbj->strbuf_depth >= cbj->strbuf_size) {
		char *strbuf = realloc(cbj->strbuf, cbj->strbuf_size * 2);

		if (!strbuf) {
			return CBJSON_ERR_NO_MEM;
		}
		cbj->strbuf = strbuf;
		cbj->strbuf_size *= 2;
	}
	cbj->strbuf[cbj->strbuf_depth++] = c;

	return CBJSON_OK;
}

static int process_char_escaped(cbjson_t *cbj, char c) {
	assert(cbj->in_literal && cbj->literal_type == CBJSON_TYPE_STRING);

	switch (c) {
		case '"': case '/': case '\\':
			cbj->escaped = false;
			return cbjson_add_char(cbj, c);
		case 'b':
			cbj->escaped = false;
			return cbjson_add_char(cbj, 0x08);
		case 'f':
			cbj->escaped = false;
			return cbjson_add_char(cbj, 0x0c);
		case 'n':
			cbj->escaped = false;
			return cbjson_add_char(cbj, '\n');
		case 'r':
			cbj->escaped = false;
			return cbjson_add_char(cbj, '\r');
		case 't':
			cbj->escaped = false;
			return cbjson_add_char(cbj, '\t');
		default:
			return CBJSON_ERR_UNKNOWN_ESCAPE_CHAR;
	}
}

static int process_escape_char(cbjson_t *cbj) {
	cbj->escaped = true;
	return CBJSON_OK;
}

static bool match_path(cbjson_t *cbj, cbjson_path_t *path) {
	if (path->path_depth != cbj->stack_depth) {
		return false;
	}

	if (cbj->in_array) {
		int err;
		const char *path_match = path->path_match;
		cbjson_path_component_t component;

		err = parse_next_path_component(&path_match, &component);
		if (err || component.type != CBJSON_PATH_TYPE_ARRAY_ENTRY) {
			return false;
		}
		if (component.array_entry.index != cbj->array_index && !component.array_entry.any_index) {
			return false;
		}
	}

	return true;
}

static void process_end_of_literal(cbjson_t *cbj, const cbjson_value_t *value) {
	cbjson_path_t *path;

	LIST_FOR_EACH_ENTRY(path, &cbj->paths, list) {
		if (match_path(cbj, path)) {
			path->cb(value, path->priv);
		}
	}

	if (!cbj->in_array) {
		restore_non_matching_paths(cbj);
	}
}

static int process_end_of_string(cbjson_t *cbj) {
	int err;

	cbj->in_literal = false;
	err = cbjson_add_char(cbj, 0);
	if (err) {
		return err;
	}

	if (!cbj->str_is_key) {
		cbjson_value_t str_val = {
			.type = CBJSON_TYPE_STRING,
			.string = cbj->strbuf
		};

		process_end_of_literal(cbj, &str_val);
	}

	return CBJSON_OK;
}

static int process_char_in_string(cbjson_t *cbj, char c) {
	switch (c) {
		case '"':
			return process_end_of_string(cbj);
		case '\\':
			return process_escape_char(cbj);
		default:
			return cbjson_add_char(cbj, c);
	}
}

static int process_char_outside_literal(cbjson_t *cbj, char c);
static int process_char_in_float(cbjson_t *cbj, char c) {
	cbjson_value_t value_float = {
		.type = CBJSON_TYPE_FLOAT
	};

	switch (c) {
	case '0' ... '9': case '.': case 'e': case '-':
		break;
	default:
		cbj->in_literal = false;
#warning Float not supported
		process_end_of_literal(cbj, &value_float);
		return process_char_outside_literal(cbj, c);
	}

	return cbjson_add_char(cbj, c);
}

static int process_char_in_integer(cbjson_t *cbj, char c) {
	long long intval;
	int err;
	cbjson_value_t value_int = {
		.type = CBJSON_TYPE_INTEGER
	};

	switch (c) {
	case '0' ... '9':
		break;
	case '.': case 'e':
		cbj->literal_type = CBJSON_TYPE_FLOAT;
		return process_char_in_float(cbj, c);
	default:
		cbj->in_literal = false;

		err = cbjson_add_char(cbj, 0);
		if (err) {
			return err;
		}
		errno = 0;
		intval = strtoll(cbj->strbuf, NULL, 10);
		if (intval == LLONG_MIN || intval == LLONG_MAX) {
			return CBJSON_ERR_INT_OUT_OF_RANGE;
		}
		if (errno) {
			return CBJSON_ERR_INVALID_LITERAL;
		}
		value_int.integer = intval;
		process_end_of_literal(cbj, &value_int);

		return process_char_outside_literal(cbj, c);
	}

	return cbjson_add_char(cbj, c);
}

static int process_char_in_boolean(cbjson_t *cbj, char c) {
	int err;
	cbjson_value_t value_bool = {
		.type = CBJSON_TYPE_BOOLEAN
	};

	switch (c) {
	case 'a': case 'e': case 'f': case 'l': case 'r': case 's':
	case 't': case 'u':
		break;
	default:
		cbj->in_literal = false;
		err = cbjson_add_char(cbj, 0);
		if (err) {
			return err;
		}

		if (!strcmp(cbj->strbuf, "true")) {
			value_bool.boolean = true;
		} else if (!strcmp(cbj->strbuf, "false")) {
			value_bool.boolean = false;
		} else {
			return CBJSON_ERR_INVALID_LITERAL;
		}
		process_end_of_literal(cbj, &value_bool);

		return process_char_outside_literal(cbj, c);
	}

	return cbjson_add_char(cbj, c);
}

static int process_char_in_null(cbjson_t *cbj, char c) {
	int err;
	cbjson_value_t value_null = {
		.type = CBJSON_TYPE_NULL
	};

	switch (c) {
	case 'l': case 'n': case 'u':
		break;
	default:
		cbj->in_literal = false;

		err = cbjson_add_char(cbj, 0);
		if (err) {
			return err;
		}

		if (strcmp(cbj->strbuf, "null")) {
			return CBJSON_ERR_INVALID_LITERAL;
		}

		process_end_of_literal(cbj, &value_null);

		return process_char_outside_literal(cbj, c);
	}

	return cbjson_add_char(cbj, c);
}

static int process_char_in_literal(cbjson_t *cbj, char c) {
	switch (cbj->literal_type) {
	case CBJSON_TYPE_STRING:
		return process_char_in_string(cbj, c);
	case CBJSON_TYPE_INTEGER:
		return process_char_in_integer(cbj, c);
	case CBJSON_TYPE_BOOLEAN:
		return process_char_in_boolean(cbj, c);
	case CBJSON_TYPE_FLOAT:
		return process_char_in_float(cbj, c);
	case CBJSON_TYPE_NULL:
		return process_char_in_null(cbj, c);
	}

	return CBJSON_ERR_INVALID_LITERAL;
}


static bool path_match_key(cbjson_path_t *path, unsigned int depth, const char *key) {
	const char *path_match = path->path_match;
	cbjson_path_component_t component;
	int err;

	if (depth > path->path_depth) {
		return false;
	}

	err = parse_next_path_component(&path_match, &component);
	assert(!err);

	if (component.type != CBJSON_PATH_TYPE_OBJECT_KEY) {
		return false;
	}

	if (component.object_key.any_key) {
		return true;
	}

	return !strncmp(key, component.object_key.name, component.object_key.name_len);
}


static int process_key_value_separator(cbjson_t *cbj) {
	struct list_head *next;
	cbjson_path_t *cursor;

	if (cbj->in_array) {
		return CBJSON_ERR_KEY_IN_ARRAY;
	}

	if (!cbj->strbuf || cbj->strbuf_depth == 0) {
		return CBJSON_ERR_NO_KEY;
	}

	cbj->str_is_key = false;

	LIST_FOR_EACH_ENTRY_SAFE(cursor, next, &cbj->paths, list) {
		const char *key = cbj->strbuf;

		if (!path_match_key(cursor, cbj->stack_depth, key)) {
			LIST_DELETE(&cursor->list);
			LIST_APPEND(&cursor->list, &cbj->non_matching_paths);
		}
	}

	return CBJSON_OK;
}

static void store_non_matching_paths_in_stack_entry(cbjson_t *cbj, cbjson_stack_t *stack_entry) {
	struct list_head *next;
	cbjson_path_t *cursor;

	LIST_FOR_EACH_ENTRY_SAFE(cursor, next, &cbj->paths, list) {
		bool path_match = cbj->stack_depth <= cursor->path_depth;

		if (path_match) {
			int err;
			cbjson_path_component_t component;
			const char *path = cursor->path_match;

			err = parse_next_path_component(&path, &component);
			assert(!err);

			if (component.type == CBJSON_PATH_TYPE_OBJECT_KEY) {
				parse_next_path_component(&path, &component);
			} else if (component.type == CBJSON_PATH_TYPE_ARRAY_ENTRY && cbj->in_array) {
				if (component.array_entry.any_index || component.array_entry.index == cbj->array_index) {
					parse_next_path_component(&path, &component);
				} else {
					path_match = false;
				}
			}
			if (component.type != CBJSON_PATH_TYPE_DESCEND) {
				path_match = false;
			}
		}

		if (path_match) {
			assert(!path_descend(cursor));
		} else {
			LIST_DELETE(&cursor->list);
			LIST_APPEND(&cursor->list, &stack_entry->non_matching_paths);
		}
	}

	LIST_FOR_EACH_ENTRY_SAFE(cursor, next, &cbj->non_matching_paths, list) {
		LIST_DELETE(&cursor->list);
		LIST_APPEND(&cursor->list, &stack_entry->non_matching_paths);
	}
}

static int process_start_of_object(cbjson_t *cbj) {
	int err;
	cbjson_stack_t *stack_entry;

	err = push_stack_entry(cbj, &stack_entry);
	if (err) {
		return err;
	}

	store_non_matching_paths_in_stack_entry(cbj, stack_entry);

	cbj->str_is_key = true;
	cbj->in_array = false;

	return CBJSON_OK;
}

static int process_end_of_object(cbjson_t *cbj) {
	restore_non_matching_paths(cbj);
	return pop_stack_entry(cbj, NULL);
}

static int process_start_of_array(cbjson_t *cbj) {
	int err;
	cbjson_stack_t *stack_entry;

	err = push_stack_entry(cbj, &stack_entry);
	if (err) {
		return err;
	}

	cbj->in_array = true;
	store_non_matching_paths_in_stack_entry(cbj, stack_entry);

	cbj->str_is_key = false;
	cbj->array_index = 0;

	return CBJSON_OK;
}

static int process_end_of_array(cbjson_t *cbj) {
	cbj->in_array = false;
	restore_non_matching_paths(cbj);
	return pop_stack_entry(cbj, NULL);
}

static int process_entry_separator(cbjson_t *cbj) {
	if (cbj->in_array) {
		cbj->array_index++;
	} else {
		cbj->str_is_key = true;
	}

	return CBJSON_OK;
}

static int process_start_of_string(cbjson_t *cbj) {
	cbj->strbuf_depth = 0;
	cbj->in_literal = true;
	cbj->literal_type = CBJSON_TYPE_STRING;
	return CBJSON_OK;
}

static int process_start_of_literal(cbjson_t *cbj, char c, cbjson_type_t type) {
	cbj->strbuf_depth = 0;
	cbj->in_literal = true;
	cbj->literal_type = type;
	return cbjson_add_char(cbj, c);
}

static int process_start_of_null(cbjson_t *cbj, char c) {
	return process_start_of_literal(cbj, c, CBJSON_TYPE_NULL);
}

static int process_start_of_boolean(cbjson_t *cbj, char c) {
	return process_start_of_literal(cbj, c, CBJSON_TYPE_BOOLEAN);
}

static int process_start_of_integer(cbjson_t *cbj, char c) {
	return process_start_of_literal(cbj, c, CBJSON_TYPE_INTEGER);
}

static int process_start_of_float(cbjson_t *cbj, char c) {
	return process_start_of_literal(cbj, c, CBJSON_TYPE_FLOAT);
}

static int process_char_outside_literal(cbjson_t *cbj, char c) {
	switch (c) {
	case ':':
		return process_key_value_separator(cbj);
	case '{':
		return process_start_of_object(cbj);
	case '}':
		return process_end_of_object(cbj);
	case '[':
		return process_start_of_array(cbj);
	case ']':
		return process_end_of_array(cbj);
	case ',':
		return process_entry_separator(cbj);
	case '"':
		return process_start_of_string(cbj);
	case 'n':
		return process_start_of_null(cbj, c);
	case 't': case 'f':
		return process_start_of_boolean(cbj, c);
	case '-': case '0' ... '9':
		return process_start_of_integer(cbj, c);
	case '.':
		return process_start_of_float(cbj, c);
	default:
		return CBJSON_OK;
	}
}

static int process_char_unescaped(cbjson_t *cbj, char c) {
	if (cbj->in_literal) {
		return process_char_in_literal(cbj, c);
	} else {
		return process_char_outside_literal(cbj, c);
	}
}

int cbjson_process(cbjson_t *cbj, const char *str, size_t len) {
	while (len--) {
		char c = *str++;
		int err;

		if (cbj->escaped) {
			err = process_char_escaped(cbj, c);
		} else {
			err = process_char_unescaped(cbj, c);
		}

		if (err) {
			return err;
		}
	}

	return CBJSON_OK;
}

void cbjson_add_path(cbjson_t *cbj, cbjson_path_t *path) {
	LIST_APPEND(&path->list, &cbj->paths);
}
