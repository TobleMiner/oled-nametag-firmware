#pragma once

#include <stdbool.h>

void battery_gauge_init(void);
void battery_gauge_stop(void);
void battery_gauge_start(void);

unsigned int battery_gauge_get_voltage_mv(void);
int battery_gauge_get_current_ma(void);
unsigned int battery_gauge_get_soc_percent(void);
unsigned int battery_gauge_get_soh_percent(void);
unsigned int battery_gauge_get_time_to_empty_min(void);
int battery_gauge_get_temperature_0_1degc(void);
bool battery_gauge_is_healthy(void);
unsigned int battery_gauge_get_full_capacity_mah(void);
unsigned int battery_gauge_get_remaining_capacity_mah(void);
