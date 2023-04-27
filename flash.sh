#!/bin/bash

set -e -o pipefail

idf.py build

dd if=build/oled_nametag.bin of=build/oled_nametag1.bin bs=4096 count=100 &> /dev/null
dd if=build/oled_nametag.bin of=build/oled_nametag2.bin bs=4096 skip=100 &> /dev/null

esptool.py -p /dev/ttyACM* -b 460800 --before default_reset --after hard_reset --chip esp32s3  write_flash --flash_mode dio --flash_size detect --flash_freq 80m \
	0x0 build/bootloader/bootloader.bin \
	0x10000 build/partition_table/partition-table.bin \
	0x1e000 build/ota_data_initial.bin \
	0x20000 build/oled_nametag1.bin \
	0x84000 build/oled_nametag2.bin

case $1 in
	*monitor*) idf.py -p /dev/ttyACM* monitor;;
esac
