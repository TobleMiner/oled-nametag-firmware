#pragma once

#include <stdbool.h>

#include <esp_netif.h>

void wlan_station_init(void);

// Threadsafe
bool wlan_station_is_enabled(void);
bool wlan_station_is_connected(void);
void wlan_station_enable(void);
void wlan_station_disable(void);
void wlan_station_lock(void);
void wlan_station_unlock(void);
void wlan_station_set_ssid(const char *ssid);
void wlan_station_set_psk(const char *psk);
esp_err_t wlan_station_get_ipv4_address(esp_netif_ip_info_t *ip_info);
int wlan_station_get_ipv6_addresses(esp_ip6_addr_t *addresses, unsigned int num_addresses);

// Non-threadsafe, use only with lock held
void wlan_station_enable_(void);
void wlan_station_disable_(void);
const char *wlan_station_get_ssid_(void);
