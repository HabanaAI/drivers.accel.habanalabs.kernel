/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2023 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HABANALABS_COMPAT_ACCEL_H_
#define HABANALABS_COMPAT_ACCEL_H_

#include <linux/device.h>

int hl_accel_init(void);
void hl_accel_exit(void);
int hl_accel_get_major(void);
struct class *hl_accel_get_class(void);

#endif /* HABANALABS_COMPAT_ACCEL_H_ */
