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

#include "stm32_vfs_dev_flash.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "stm32_sdk_hal.h"

#include "common/cs_dbg.h"

#include "frozen.h"

#include "mgos_hal.h"
#include "mgos_vfs.h"
#include "mgos_vfs_dev.h"

#include "stm32_flash.h"

/* Note: FLASH_BASE_ADDR and FLASH_SIZE are defined externally. */
#if FLASH_BASE_ADDR != FLASH_BASE
#error "FLASH_BASE used by compiler and linker do not match"
#endif

struct dev_data {
  size_t addr;
  size_t size;
};

static bool check_bounds(const struct dev_data *dd, size_t offset, size_t len) {
  return (dd->addr <= FLASH_SIZE && dd->size <= FLASH_SIZE &&
          dd->addr + dd->size <= FLASH_SIZE && offset <= FLASH_SIZE &&
          len <= FLASH_SIZE && dd->addr + offset + len <= FLASH_SIZE);
}

static enum mgos_vfs_dev_err stm32_vfs_dev_flash_open(struct mgos_vfs_dev *dev,
                                                      const char *opts) {
  enum mgos_vfs_dev_err res = MGOS_VFS_DEV_ERR_INVAL;
  struct dev_data *dd = (struct dev_data *) calloc(1, sizeof(*dd));
  if (opts != NULL) {
    json_scanf(opts, strlen(opts), "{addr: %u, size: %u}", &dd->addr,
               &dd->size);
  }
  if (dd->addr == 0 || dd->size == 0) {
    LOG(LL_INFO, ("addr and size are required"));
    goto out;
  }
  if (!check_bounds(dd, 0, 0)) {
    LOG(LL_INFO, ("invalid settings: %u %u (flash size: %u)", dd->addr,
                  dd->size, FLASH_SIZE));
    goto out;
  }
  dev->dev_data = dd;
  res = MGOS_VFS_DEV_ERR_NONE;

out:
  if (res != 0) free(dd);
  return res;
}

static enum mgos_vfs_dev_err stm32_vfs_dev_flash_read(struct mgos_vfs_dev *dev,
                                                      size_t offset, size_t len,
                                                      void *dst) {
  enum mgos_vfs_dev_err res = MGOS_VFS_DEV_ERR_INVAL;
  const struct dev_data *dd = (struct dev_data *) dev->dev_data;
  if (check_bounds(dd, offset, len)) {
    memcpy(dst, (uint8_t *) (FLASH_BASE + dd->addr + offset), len);
    res = MGOS_VFS_DEV_ERR_NONE;
  }
  LOG((res == 0 ? LL_VERBOSE_DEBUG : LL_ERROR),
      ("%p: %s %u @ %d = %d", dev, "read", len, offset, res));
  return res;
}

static enum mgos_vfs_dev_err stm32_vfs_dev_flash_write(struct mgos_vfs_dev *dev,
                                                       size_t offset,
                                                       size_t len,
                                                       const void *src) {
  enum mgos_vfs_dev_err res = MGOS_VFS_DEV_ERR_INVAL;
  const struct dev_data *dd = (struct dev_data *) dev->dev_data;
  if (!check_bounds(dd, offset, len)) goto out;
  if (!stm32_flash_write_region(dd->addr + offset, len, src)) {
    res = MGOS_VFS_DEV_ERR_IO;
    goto out;
  }
  res = MGOS_VFS_DEV_ERR_NONE;
out:
  LOG((res == 0 ? LL_VERBOSE_DEBUG : LL_ERROR),
      ("%p: %s %u @ %d = %d", dev, "write", len, offset, res));
  return res;
}

static enum mgos_vfs_dev_err stm32_vfs_dev_flash_erase(struct mgos_vfs_dev *dev,
                                                       size_t offset,
                                                       size_t len) {
  enum mgos_vfs_dev_err res = MGOS_VFS_DEV_ERR_INVAL;
  uint8_t *tmp = NULL;
  const struct dev_data *dd = (struct dev_data *) dev->dev_data;
  if (!check_bounds(dd, offset, len)) goto out;
  int abs_offset = dd->addr + offset;
  int sector = stm32_flash_get_sector(abs_offset);
  if (sector < 0) goto out;
  int sector_offset = stm32_flash_get_sector_offset(sector);
  int sector_size = stm32_flash_get_sector_size(sector);
  if (abs_offset == sector_offset && len == sector_size) {
    if (stm32_flash_sector_is_erased(sector)) goto out_ok;
    if (!stm32_flash_erase_sector(sector)) {
      res = MGOS_VFS_DEV_ERR_IO;
      goto out;
    }
  } else if (abs_offset >= sector_offset &&
             abs_offset + len <= sector_offset + sector_size) {
    if (stm32_flash_region_is_erased(abs_offset, len)) goto out_ok;
    LOG(LL_WARN, ("Unsafe flash erase: %u @ 0x%x", len, abs_offset));
    tmp = malloc(sector_size);
    if (tmp == NULL) {
      res = MGOS_VFS_DEV_ERR_NOMEM;
      goto out;
    }
    int before_len = abs_offset - sector_offset;
    int after_offset = before_len + len;
    int after_len = sector_size - after_offset;
    memcpy(tmp, (const uint8_t *) (FLASH_BASE + sector_offset), sector_size);
    res = MGOS_VFS_DEV_ERR_IO;
    if (!stm32_flash_erase_sector(sector)) {
      goto out;
    }
    if (before_len > 0) {
      if (!stm32_flash_write_region(sector_offset, before_len, tmp)) {
        goto out;
      }
    }
    if (after_len > 0) {
      if (!stm32_flash_write_region(sector_offset + after_offset, after_len,
                                    tmp + after_offset)) {
        goto out;
      }
    }
    free(tmp);
  } else {
    /* Cross-sector operations are not supported. */
    res = MGOS_VFS_DEV_ERR_INVAL;
    goto out;
  }
out_ok:
  res = MGOS_VFS_DEV_ERR_NONE;
out:
  LOG((res == 0 ? LL_VERBOSE_DEBUG : LL_ERROR),
      ("%p: %s %u @ %d = %d", dev, "erase", len, offset, res));
  return res;
}

static size_t stm32_vfs_dev_flash_get_size(struct mgos_vfs_dev *dev) {
  struct dev_data *dd = (struct dev_data *) dev->dev_data;
  return dd->size;
}

static enum mgos_vfs_dev_err stm32_vfs_dev_flash_close(
    struct mgos_vfs_dev *dev) {
  free(dev->dev_data);
  return MGOS_VFS_DEV_ERR_NONE;
}

static const struct mgos_vfs_dev_ops stm32_vfs_dev_flash_ops = {
    .open = stm32_vfs_dev_flash_open,
    .read = stm32_vfs_dev_flash_read,
    .write = stm32_vfs_dev_flash_write,
    .erase = stm32_vfs_dev_flash_erase,
    .get_size = stm32_vfs_dev_flash_get_size,
    .close = stm32_vfs_dev_flash_close,
};

bool stm32_vfs_dev_flash_register_type(void) {
  return mgos_vfs_dev_register_type(MGOS_VFS_DEV_TYPE_STM32_FLASH,
                                    &stm32_vfs_dev_flash_ops);
}
