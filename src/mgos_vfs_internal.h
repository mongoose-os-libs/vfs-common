/*
 * Copyright (c) 2014-2017 Cesanta Software Limited
 * All rights reserved
 */

#ifndef CS_FW_SRC_MGOS_VFS_INTERNAL_H_
#define CS_FW_SRC_MGOS_VFS_INTERNAL_H_

#include "mgos_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

void mgos_vfs_mmap_init(void);

void mgos_vfs_print_fs_info(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* CS_FW_SRC_MGOS_VFS_INTERNAL_H_ */
