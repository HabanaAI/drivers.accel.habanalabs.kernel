/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef SIMULATORP_H_
#define SIMULATORP_H_

#define SIM_SHMEM_SIZE		SZ_1G		/* 1GB */
#define SIM_DMA_POOL_BLK_SIZE	SZ_256		/* 256 bytes */
#define SIM_PCI_OUTSTANDING	128

#define SIM_RW_REG_TIMEOUT_US	30000000	/* 30s */

#define SIM_CB_POOL_CB_CNT	64
#define SIM_CB_POOL_CB_SIZE	SZ_4K		/* 4KB */

#define SIM_HALT_WAIT_MSEC	500		/* 500ms */
#define SIM_RESET_WAIT_MSEC	5000		/* 5000ms */

#endif /* SIMULATORP_H_ */
