#pragma once

#include <stdbool.h>

void wlan_station_init(void);

// Threadsafe
bool wlan_station_is_enabled(void);
void wlan_station_enable(void);
void wlan_station_disable(void);
void wlan_station_lock(void);
void wlan_station_unlock(void);

// Non-threadsafe, use only with lock held
void wlan_station_enable_(void);
void wlan_station_disable_(void);
