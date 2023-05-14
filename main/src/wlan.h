#pragma once

#include <stdbool.h>

void wlan_init(void);

void wlan_stop(void);
void wlan_reconfigure(void);
void wlan_start(void);
void wlan_restart(void);

bool wlan_is_started(void);
