/*
 * Copyright (c) 2014-2018 Cesanta Software Limited
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "esp32_fs.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_flash_encrypt.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_spi_flash.h"
#include "esp_vfs.h"

#include "common/cs_dbg.h"
#include "common/cs_file.h"

#include "frozen.h"

#include "mgos_hal.h"
#include "mgos_vfs.h"
#include "mgos_vfs_fs_spiffs.h"

#include "esp32_vfs_dev_partition.h"

const esp_partition_t *esp32_find_fs_for_app_slot(int slot) {
  char ota_fs_part_name[5] = {'f', 's', '_', 0, 0};
  const char *fs_part_name = NULL;
  /*
   * If OTA layout is used, use the corresponding FS partition, otherwise use
   * the first data:spiffs partition.
   */
  if (slot >= 0) {
    ota_fs_part_name[3] = slot + (slot < 10 ? '0' : 'a');
    fs_part_name = ota_fs_part_name;
  }
  return esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, fs_part_name);
}

int esp32_get_boot_slot() {
  const esp_partition_t *p = esp_ota_get_boot_partition();
  if (p == NULL) return -1;
  return SUBTYPE_TO_SLOT(p->subtype);
}

bool esp32_fs_mount_part(const char *label, const char *path) {
  bool encrypt = false;
#if CS_SPIFFS_ENABLE_ENCRYPTION
  encrypt = esp_flash_encryption_enabled();
#endif
  char dev_opts[100], fs_opts[100];
  struct json_out out1 = JSON_OUT_BUF(dev_opts, sizeof(dev_opts));
  json_printf(&out1, "{label: %Q, subtype: %d}", label,
              ESP_PARTITION_SUBTYPE_DATA_SPIFFS);
  struct json_out out2 = JSON_OUT_BUF(fs_opts, sizeof(fs_opts));
  json_printf(&out2, "{encr: %B}", encrypt);
  if (!mgos_vfs_mount(path, MGOS_VFS_DEV_TYPE_ESP32_PARTITION, dev_opts,
                      MGOS_VFS_FS_TYPE_SPIFFS, fs_opts)) {
    return false;
  }
  return true;
}

bool mgos_core_fs_init(void) {
#if CS_SPIFFS_ENABLE_ENCRYPTION
  if (esp_flash_encryption_enabled() && !esp32_fs_crypt_init()) {
    LOG(LL_ERROR, ("Failed to initialize FS encryption key"));
    return MGOS_INIT_FS_INIT_FAILED;
  }
#endif
  const esp_partition_t *fs_part =
      esp32_find_fs_for_app_slot(esp32_get_boot_slot());
  if (fs_part == NULL) {
    LOG(LL_ERROR, ("No FS partition"));
    return false;
  }
  return esp32_fs_mount_part(fs_part->label, "/");
}

bool mgos_vfs_common_init(void) {
  esp_vfs_t esp_vfs = {
    .flags = ESP_VFS_FLAG_DEFAULT,
    /* ESP API uses void * as first argument, hence all the ugly casts. */
    .open = mgos_vfs_open,
    .close = mgos_vfs_close,
    .read = mgos_vfs_read,
    .write = mgos_vfs_write,
    .stat = mgos_vfs_stat,
    .fstat = mgos_vfs_fstat,
    .lseek = mgos_vfs_lseek,
    .rename = mgos_vfs_rename,
    .unlink = mgos_vfs_unlink,
#if MG_ENABLE_DIRECTORY_LISTING
    .opendir = mgos_vfs_opendir,
    .readdir = mgos_vfs_readdir,
    .closedir = mgos_vfs_closedir,
#endif
  };
  if (esp_vfs_register("", &esp_vfs, NULL) != ESP_OK) {
    LOG(LL_ERROR, ("ESP VFS registration failed"));
    return false;
  }
  return esp32_vfs_dev_partition_register_type();
}
