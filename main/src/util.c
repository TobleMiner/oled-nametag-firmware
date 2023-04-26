#include "util.h"

#include <errno.h>

void strntr(char* str, size_t len, char a, char b) {
	while(len-- > 0) {
		if(*str == a) {
			*str = b;
		}
		str++;
	}
}

ssize_t hex_decode_inplace(uint8_t *ptr, size_t len) {
	uint8_t *dst = ptr;
	size_t i;

	if (len % 2) {
		return -EINVAL;
	}

	for (i = 0; i < len; i++) {
		*dst++ = hex_to_byte(ptr);
		ptr += 2;
	}

	return len / 2;
}

esp_err_t xlate_err(int err) {
	switch(err) {
	case ENOMEM:
		return ESP_ERR_NO_MEM;
	case EBADF:
	case EACCES:
	case ENOENT:
	case ENOTDIR:
	case EIO:
	case ENAMETOOLONG:
	case EOVERFLOW:
		return ESP_ERR_INVALID_ARG;
	case EMFILE:
	case ENFILE:
	case ELOOP:
		return ESP_ERR_INVALID_STATE;
	}
	return ESP_FAIL;
}

