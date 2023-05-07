#pragma once

#include <stdbool.h>

void charger_init(void);

bool charger_is_charging(void);
bool charger_has_charging_finished(void);
