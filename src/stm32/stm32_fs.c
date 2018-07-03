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

#include <stdio.h>

#include "common/cs_dbg.h"
#include "common/str_util.h"

#include "miniz.h"

#include "mgos_hal.h"
#include "mgos_vfs.h"
#include "mgos_vfs_internal.h"

#include "stm32_flash.h"
#include "stm32_vfs_dev_flash.h"

#include "stm32_sdk_hal.h"

extern const unsigned char fs_zip[];
extern const char _fs_bin_start, _fs_bin_end;

static bool stm32_fs_extract(void) {
  bool res = false;
  FILE *fp = NULL;
  void *data = NULL;
  mz_zip_archive zip = {0};
  uintptr_t fs_size = &_fs_bin_end - &_fs_bin_start;
  mz_bool zs = mz_zip_reader_init_mem(&zip, &fs_zip[0], fs_size, 0);
  if (!zs) return false;
  int num_files = (int) mz_zip_reader_get_num_files(&zip);
  for (int i = 0; i < num_files; i++) {
    mz_zip_archive_file_stat zfst;
    if (!mz_zip_reader_file_stat(&zip, i, &zfst)) goto out;
    LOG(LL_INFO, ("%s, size: %d, csize: %d", zfst.m_filename,
                  (int) zfst.m_uncomp_size, (int) zfst.m_comp_size));
    // We have plenty of heap at this point, keep it simple.
    size_t uncomp_size = 0;
    data = mz_zip_reader_extract_file_to_heap(&zip, zfst.m_filename,
                                              &uncomp_size, 0);
    if (data == NULL) goto out;
    fp = fopen(zfst.m_filename, "w");
    if (fp == NULL) {
      LOG(LL_ERROR, ("open failed"));
      goto out;
    }
    if (fwrite(data, uncomp_size, 1, fp) != 1) {
      LOG(LL_ERROR, ("write failed"));
      goto out;
    }
    fclose(fp);
    fp = NULL;
    free(data);
    data = NULL;
  }
  res = true;
out:
  mz_zip_reader_end(&zip);
  free(data);
  if (fp != NULL) fclose(fp);
  return res;
}

bool mgos_core_fs_init(void) {
  const char *fsdt = CS_STRINGIFY_MACRO(MGOS_FS_DEV_TYPE);
  const char *fsdo = CS_STRINGIFY_MACRO(MGOS_FS_DEV_OPTS);
  const char *fst = CS_STRINGIFY_MACRO(MGOS_FS_TYPE);
  const char *fso = CS_STRINGIFY_MACRO(MGOS_FS_OPTS);
  bool res = mgos_vfs_mount("/", fsdt, fsdo, fst, fso);
  if (!res) {
    LOG(LL_INFO, ("Creating FS..."));
    res = mgos_vfs_mkfs(fsdt, fsdo, fst, fso);
    if (!res) goto out;
    res = mgos_vfs_mount("/", fsdt, fsdo, fst, fso);
    LOG(LL_INFO, ("Extracting FS..."));
    res = stm32_fs_extract();
    mgos_vfs_print_fs_info("/");
  }
out:
  return res;
}

bool mgos_vfs_common_init(void) {
  return stm32_vfs_dev_flash_register_type();
}
