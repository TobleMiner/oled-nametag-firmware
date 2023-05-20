#include "iputil.h"

#include <stdint.h>

#include "util.h"

static inline uint16_t ipv6_addr_block(const esp_ip6_addr_t *addr, unsigned int block) {
	unsigned int idx = block / 2;
	unsigned int shift = ((block + 1) % 2) * 16;

	return (uint16_t)((esp_netif_htonl(addr->addr[idx]) >> shift) & 0xffff);
}

typedef struct ipv6_block_range {
	unsigned int start:8;
	unsigned int end:8;
} ipv6_block_range_t;

static ipv6_block_range_t find_longest_zero_sequence(const esp_ip6_addr_t *addr) {
	ipv6_block_range_t range = { 0, 0 };
	unsigned int current_sequence_start = 0;
	unsigned int i;

	for (i = 0; i < 8; i++) {
		uint16_t block = ipv6_addr_block(addr, i);

		if (block) {
			unsigned int sequence_length = i - current_sequence_start;

			if (sequence_length > range.end - range.start) {
				range.start = current_sequence_start;
				range.end = i;
			}
			current_sequence_start = i + 1;
		}
	}

	if (i - current_sequence_start > range.end - range.start) {
		range.start = current_sequence_start;
		range.end = i;
	}

	return range;
}

static char *iputil_ipv6_block_to_str(uint16_t block, char *str) {
	unsigned int i;

	for (i = 0; i < 3 && !(block & 0xf000); i++, block <<= 4);

	for (; i < 4; i++, block <<= 4) {
		*str++ = nibble_to_hex((block & 0xf000) >> 12);
	}

	return str;
}

char *iputil_ipv6_addr_to_str(const esp_ip6_addr_t *addr, char *str) {
	unsigned int i;
	ipv6_block_range_t range_ellipsis = find_longest_zero_sequence(addr);

	// RFC5952 Section 4.2.2, single zero block must not be ellipsized
	if (range_ellipsis.end - range_ellipsis.start == 1) {
		range_ellipsis.start = 0;
		range_ellipsis.end = 0;
	}

	for (i = 0; i < 8; i++) {
		if (i == range_ellipsis.start && i < range_ellipsis.end) {
			// Start of ellipsized section, put down double colon
			*str++ = ':';
			*str++ = ':';
		} else if (i < range_ellipsis.start || i >= range_ellipsis.end ||
			   range_ellipsis.start == range_ellipsis.end) {
			// Every block except the first one or one following an
			// ellipsized section is preceeded by a double colon
			if (i && (i != range_ellipsis.end || range_ellipsis.start == range_ellipsis.end)) {
				*str++ = ':';
			}
			// We are either outside the ellipsized section or there is none in this address
			str = iputil_ipv6_block_to_str(ipv6_addr_block(addr, i), str);
		}
	}

	// Ensure null-termination
	*str = 0;

	return str;
}
