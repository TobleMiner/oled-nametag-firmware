#pragma once

#include <stdbool.h>

#include <esp_wifi.h>

void wlan_ap_init(void);

// Threadsafe
void wlan_ap_lock(void);
void wlan_ap_unlock(void);
void wlan_ap_generate_new_psk(void);
void wlan_ap_enable(void);
void wlan_ap_disable(void);
bool wlan_ap_is_active(void);
bool wlan_ap_is_enabled(void);
unsigned int wlan_ap_get_num_connected_stations(void);

// Not threadsafe, call only with lock held, use results only while lock held
const char *wlan_ap_get_ssid_(void);
const char *wlan_ap_get_psk_(void);
void wlan_ap_enable_(void);
void wlan_ap_disable_(void);
