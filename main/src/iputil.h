#pragma once

#include <esp_netif.h>

char *iputil_ipv6_addr_to_str(const esp_ip6_addr_t *addr, char *str);
