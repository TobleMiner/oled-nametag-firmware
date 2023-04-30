#pragma once

#include <stdint.h>

#define STR(s) #s

#define EMBEDDED_FILE_PTR(name_) \
	binary_ ## name_ ## _start

#define EMBEDDED_FILE_PTR_END(name_) \
	binary_ ## name_ ## _end

#define EMBEDDED_FILE_PTRS(name_) \
	EMBEDDED_FILE_PTR(name_), EMBEDDED_FILE_PTR_END(name_)

#define DECLARE_EMBEDDED_FILE(name_) \
	extern const uint8_t binary_ ## name_ ## _start[] asm("_binary_"STR(name_)"_start"); \
	extern const uint8_t binary_ ## name_ ## _end[] asm("_binary_"STR(name_)"_end")

DECLARE_EMBEDDED_FILE(animation_js);
DECLARE_EMBEDDED_FILE(animation_thtml);
DECLARE_EMBEDDED_FILE(bootstrap_bundle_min_js);
DECLARE_EMBEDDED_FILE(bootstrap_min_css);
DECLARE_EMBEDDED_FILE(datatables_min_css);
DECLARE_EMBEDDED_FILE(favicon_ico);
DECLARE_EMBEDDED_FILE(jquery_3_3_1_min_js);
DECLARE_EMBEDDED_FILE(navbar_thtml);
DECLARE_EMBEDDED_FILE(ota_js);
DECLARE_EMBEDDED_FILE(ota_thtml);
DECLARE_EMBEDDED_FILE(resources_html);

DECLARE_EMBEDDED_FILE(applications_119x22_raw);
DECLARE_EMBEDDED_FILE(battery_21x10_raw);
DECLARE_EMBEDDED_FILE(gif_player_119x22_raw);
DECLARE_EMBEDDED_FILE(power_off_119x18_raw);
DECLARE_EMBEDDED_FILE(settings_119x22_raw);
DECLARE_EMBEDDED_FILE(wlan_ap_15x12_raw);
DECLARE_EMBEDDED_FILE(wlan_settings_119x22_raw);
DECLARE_EMBEDDED_FILE(wlan_station_20x12_raw);
