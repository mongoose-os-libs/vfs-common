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

#include "stm32_flash.h"

#include "common/cs_dbg.h"
#include "common/str_util.h"

#include "mongoose.h"
#include "mgos_system.h"

#include "stm32_sdk_hal.h"
#include "stm32_system.h"

#ifdef FLASH_FLAG_PGSERR
#define FLASH_ERR_FLAGS                                       \
  (FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | \
   FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR)
#endif
#ifdef FLASH_FLAG_ERSERR
#define FLASH_ERR_FLAGS                                       \
  (FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | \
   FLASH_FLAG_PGPERR | FLASH_FLAG_ERSERR)
#endif

const int s_stm32_flash_layout[FLASH_SECTOR_TOTAL] = {
#if defined(STM32F4)
#if FLASH_SIZE == 524288
    16384,  16384,  16384, 16384, 65536,
    131072, 131072, 131072
#elif FLASH_SIZE == 1048576
    16384,  16384,  16384,  16384,  65536, 131072, 131072,
    131072, 131072, 131072, 131072, 131072
#elif FLASH_SIZE == 1572864
    16384,  16384,  16384,  16384,  65536,  131072, 131072, 131072, 131072,
    131072, 131072, 131072, 131072, 131072, 131072, 131072
#elif FLASH_SIZE == 2097152 /* dual-bank */
    16384,  16384,  16384,  16384,  65536,  131072, 131072, 131072, 131072,
    131072, 131072, 131072, 16384,  16384,  16384,  16384,  65536,  131072,
    131072, 131072, 131072, 131072, 131072, 131072
#else
#error Unsupported flash size
#endif
#elif defined(STM32F7)
#if FLASH_SIZE == 1048576
    32768,  32768,  32768, 32768, 131072,
    262144, 262144, 262144
#else
#error Unsupported flash size
#endif
#else
#error Unknown defice family
#endif
};

int stm32_flash_get_sector(int offset) {
  int sector = -1, sector_end = 0;
  do {
    sector++;
    if (s_stm32_flash_layout[sector] == 0) return -1;
    sector_end += s_stm32_flash_layout[sector];
  } while (sector_end <= offset);
  return sector;
}

int stm32_flash_get_sector_offset(int sector) {
  int sector_offset = 0;
  while (sector > 0) {
    sector--;
    sector_offset += s_stm32_flash_layout[sector];
  }
  return sector_offset;
}

int stm32_flash_get_sector_size(int sector) {
  return s_stm32_flash_layout[sector];
}

bool stm32_flash_write_region(int offset, int len, const void *src) {
  bool res = false;
  if (offset < 0 || len < 0 || offset + len > FLASH_SIZE) goto out;
  HAL_FLASH_Unlock();
  uint8_t *dst = (uint8_t *) (FLASH_BASE + offset), *p = dst;
  const uint8_t *q = (const uint8_t *) src;
  __HAL_FLASH_CLEAR_FLAG(FLASH_ERR_FLAGS);
  for (int i = 0; i < len; i++, p++, q++) {
    FLASH->CR = FLASH_PSIZE_BYTE | FLASH_CR_PG;
    __DSB();
    mgos_ints_disable();
    *p = *q;
    __DSB();
    while (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY) != 0) {
    }
    mgos_ints_enable();
    if (__HAL_FLASH_GET_FLAG(FLASH_ERR_FLAGS) != 0) goto out_unlock;
  }
  stm32_flush_caches();
  res = (memcmp(src, dst, len) == 0);
out_unlock:
  HAL_FLASH_Lock();
out:
  return res;
}

IRAM bool stm32_flash_erase_sector(int sector) {
  bool res = false;
  int offset = stm32_flash_get_sector_offset(sector);
  if (offset < 0) goto out;
  HAL_FLASH_Unlock();
  FLASH->CR = FLASH_PSIZE_BYTE | FLASH_CR_SER | (sector << FLASH_CR_SNB_Pos);
  __HAL_FLASH_CLEAR_FLAG(FLASH_ERR_FLAGS);
  mgos_ints_disable();
  FLASH->CR |= FLASH_CR_STRT;
  __DSB();
  while (__HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY) != 0) {
  }
  mgos_ints_enable();
  HAL_FLASH_Lock();
  stm32_flush_caches();
  res = stm32_flash_sector_is_erased(sector);
out:
  return res;
}

bool stm32_flash_region_is_erased(int offset, int len) {
  bool res = false;
  const uint8_t *p = (const uint8_t *) (FLASH_BASE + offset);
  const uint32_t *q = NULL;
  if (offset < 0 || len < 0 || offset + len > FLASH_SIZE) goto out;
  while (len > 0 && (((uintptr_t) p) & 3) != 0) {
    if (*p != 0xff) goto out;
    len--;
    p++;
  }
  q = (const uint32_t *) p;
  while (len > 4) {
    if (*q != 0xffffffff) goto out;
    len -= 4;
    q++;
  }
  p = (const uint8_t *) q;
  while (len > 0) {
    if (*p != 0xff) goto out;
    len--;
    p++;
  }
  res = true;

out:
  return res;
}

bool stm32_flash_sector_is_erased(int sector) {
  return stm32_flash_region_is_erased(stm32_flash_get_sector_offset(sector),
                                      stm32_flash_get_sector_size(sector));
}
