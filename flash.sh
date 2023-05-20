#!/bin/bash

set -e -o pipefail

CHUNK_SIZE=64

idf.py build

fin=$(for i in `seq 0 64`; do
	dd if=build/oled_nametag.bin of=build/oled_nametag$i.bin bs=4096 skip=$((i * CHUNK_SIZE)) count=$CHUNK_SIZE &> /dev/null
	[ $(wc -c build/oled_nametag$i.bin | cut -d' ' -f1) -eq 0 ] && echo $i && break
done)

esptool.py -p /dev/ttyACM* -b 460800 --before default_reset --after no_reset --chip esp32s3  write_flash --flash_mode dio --flash_size detect --flash_freq 80m --no-compress \
	0x0 build/bootloader/bootloader.bin \
	0x10000 build/partition_table/partition-table.bin \
	0xfe000 build/ota_data_initial.bin

for i in `seq 0 $((fin - 1))`; do
	for try in `seq 3`; do
		esptool.py -p /dev/ttyACM* -b 460800 --before default_reset --after $([ $i -eq $((fin - 1)) ] && echo hard_reset || echo no_reset) --chip esp32s3  write_flash --flash_mode dio --flash_size detect --flash_freq 80m --no-compress \
			$((1048576 + 4096 * $CHUNK_SIZE * $i)) build/oled_nametag$i.bin && break || (echo "Upload try $try for chunk $i failed, retrying..." && [ $try -lt 3 ] && sleep 3)
	done
done

case $1 in
	*monitor*) idf.py -p /dev/ttyACM* monitor;;
esac
