/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2023 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HABANALABS_COMPAT_ACCEL_H_
#define HABANALABS_COMPAT_ACCEL_H_

#include <linux/device.h>

#if !IS_ENABLED(CONFIG_DRM_ACCEL)
extern struct idr hl_devs_idr;
extern struct mutex hl_devs_idr_lock;
#endif

int hl_accel_init(void);
void hl_accel_exit(void);
int hl_accel_get_major(void);
#if !IS_ENABLED(CONFIG_DRM_ACCEL)
struct class *hl_accel_get_class(void);
struct dentry *hl_accel_get_debugfs_root(void);
int hl_accel_device_open(struct inode *inode, struct file *filp);
int hl_accel_device_release(struct inode *inode, struct file *filp);
int hl_accel_info_ioctl(struct hl_fpriv *hpriv, void *data);
int hl_accel_cb_ioctl(struct hl_fpriv *hpriv, void *data);
int hl_accel_cs_ioctl(struct hl_fpriv *hpriv, void *data);
int hl_accel_wait_ioctl(struct hl_fpriv *hpriv, void *data);
int hl_accel_mem_ioctl(struct hl_fpriv *hpriv, void *data);
int hl_accel_debug_ioctl(struct hl_fpriv *hpriv, void *data);
int hl_accel_nic_ioctl(struct hl_fpriv *hpriv, void *data);
int hl_accel_device_add_groups(struct device *dev, const struct attribute_group **groups);
void hl_accel_device_remove_groups(struct device *dev, const struct attribute_group **groups);
#endif /* !IS_ENABLED(CONFIG_DRM_ACCEL) */

#endif /* HABANALABS_COMPAT_ACCEL_H_ */
