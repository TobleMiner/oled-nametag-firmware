#pragma once

void battery_gauge_init(void);

unsigned int battery_gauge_get_voltage_mv(void);
int battery_gauge_get_current_ma(void);
unsigned int battery_gauge_get_soc_percent(void);
