#include <stdbool.h>

#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_system.h"

#include "flash.h"

esp_vfs_fat_mount_config_t flash_fat_mount_cfg = {
  .format_if_mount_failed = true,
  .max_files = 4,
  .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
};

esp_err_t flash_fatfs_mount(const char* label, const char* base_path) {
  wl_handle_t wl_handle;

  return esp_vfs_fat_spiflash_mount_rw_wl(base_path, label, &flash_fat_mount_cfg, &wl_handle);
}
