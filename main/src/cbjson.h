#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "list.h"

typedef enum cbjson_type {
	CBJSON_TYPE_STRING,
	CBJSON_TYPE_INTEGER,
	CBJSON_TYPE_BOOLEAN,
	CBJSON_TYPE_FLOAT,
	CBJSON_TYPE_NULL
} cbjson_type_t;

typedef struct cbjson_value {
	cbjson_type_t type;
	union {
		const char *string;
		long long integer;
		bool boolean;
	};
} cbjson_value_t;

typedef int (*cbjson_path_cb_f)(const cbjson_value_t *value, void *priv);

typedef struct cbjson_path {
	struct list_head list;
	const char *path;
	unsigned int path_depth;
	const char *path_match;
	cbjson_path_cb_f cb;
	void *priv;
} cbjson_path_t;

typedef struct cbjson_stack cbjson_stack_t;

typedef struct cbjson {
	struct list_head paths;
	struct list_head non_matching_paths;
	cbjson_stack_t *stack;
	unsigned int stack_size;
	unsigned int stack_depth;
	char *strbuf;
	unsigned int strbuf_size;
	unsigned int strbuf_depth;
	bool in_literal;
	cbjson_type_t literal_type;
	bool escaped;
	bool str_is_key;
	bool in_array;
	unsigned int array_index;
} cbjson_t;

typedef enum cbjson_err {
	CBJSON_OK = 0,
	CBJSON_ERR_UNKNOWN_ESCAPE_CHAR,
	CBJSON_ERR_NO_MEM,
	CBJSON_ERR_NOT_NESTED,
	CBJSON_ERR_NO_KEY,
	CBJSON_ERR_KEY_IN_ARRAY,
	CBJSON_ERR_INVALID_LITERAL,
	CBJSON_ERR_INT_OUT_OF_RANGE,
} cbjson_err_t;

void cbjson_init(cbjson_t *cbj);
void cbjson_free(cbjson_t *cbj);
int cbjson_path_init(cbjson_path_t *path, const char *path_, cbjson_path_cb_f cb, void *priv);
void cbjson_add_path(cbjson_t *cbj, cbjson_path_t *path);
int cbjson_process(cbjson_t *cbj, const char *str, size_t len);
