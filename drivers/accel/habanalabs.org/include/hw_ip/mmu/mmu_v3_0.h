/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef INCLUDE_MMU_V3_0_H_
#define INCLUDE_MMU_V3_0_H_

#define MMU_V3_0_HOP0_MASK		0x0000FFC000000000ull
#define MMU_V3_0_HOP1_MASK		0x0000003FF0000000ull
#define MMU_V3_0_HOP2_MASK		0x000000000FF00000ull

#define MMU_V3_0_HOP0_SHIFT		38
#define MMU_V3_0_HOP1_SHIFT		28
#define MMU_V3_0_HOP2_SHIFT		20

/* V3 PTE flags */
#define TLB_PAGE_SIZE_MASK		0x00000000000F0ull

#define HOP1_TLB_PAGE_SIZE_256M		0x8
#define HOP1_TLB_PAGE_SIZE_512M		0x9
#define HOP1_TLB_PAGE_SIZE_1G		0xA
#define HOP1_TLB_PAGE_SIZE_2G		0xB

/* when HOP1 is not final this value must be set in TLB_PAGE_SIZE field */
#define HOP1_TLB_PAGE_SIZE_NOT_FINAL	HOP1_TLB_PAGE_SIZE_256M

#define HOP2_TLB_PAGE_SIZE_1M		0x0
#define HOP2_TLB_PAGE_SIZE_2M		0x1
#define HOP2_TLB_PAGE_SIZE_4M		0x2
#define HOP2_TLB_PAGE_SIZE_8M		0x3
#define HOP2_TLB_PAGE_SIZE_16M		0x4
#define HOP2_TLB_PAGE_SIZE_32M		0x5
#define HOP2_TLB_PAGE_SIZE_64M		0x6

#define TLB_PAGE_SIZE_INVALID	0x7

#endif /* INCLUDE_MMU_V3_0_H_ */
