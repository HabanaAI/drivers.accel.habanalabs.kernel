// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2019-2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "grecoP.h"
#include "../include/greco/asic_reg/greco_regs.h"

/*
 * Range registers
 * Protecting internal address space is done using range registers.
 * The RR operate as a gateway between the SW and the HW by blocking the
 * outgoing transaction targeting specific regions and making it impossible for
 * a non-trusted SW to reprogram the device for some unintended use case.
 * The RR specifies a range of addresses for which access is being block for a
 * non-secure/non-privilege entity. By doing so, it creates isolated memory and
 * configuration regions that are protected by from access by non-secure agents.
 * In shared regions, the set RR_SHRD should be configured while in private
 * regions the RR_PRVT set should be used.
 * The RR implementation differs between HBW and LBW interconnect.
 * The HBW implements:
 * - 6 x 16bit partial address region.
 *     Fixed MSB value and 4KB granularity- leaves 16bits: 0x7F_F<16bits>000
 * - 4 x 38bit full address regions.
 *     Full address regions with 4KB granularity leaves 38bits: 0x<38bits>000
 *     2 last ranges are only for write
 *  A region is a set of MIN address and MAX address registers defining a range.
 *  - A hit bitmap register for read.
 *  - A hit bitmap register for write.
 *  A hit register determines for each range if an address within a range should
 *  be considered as good (and allow access) or bad (and prevent the access).
 *
 * The LBW implements:
 * - 14 x 14bit partial address regions.
 *    Fixed MSB value and 4KB granularity leaves 14bits: 0x7F_F<11b><14bits>000
 *  - 4 x 26bit full address regions
 *     Full address regions - leaves 26bits: from address 0x7F_FC00_0000 to
 *     address 0x7F_FFFF_FFFF
 *  - A region is a set of MIN address and MAX address registers defining a
 *    range.
 *  - A hit bitmap register for read.
 *  - A hit bitmap register for write.
 *  A hit register determines for each range if an address within a range should
 *  be considered as good (and allow access) or bad (and prevent the access).
 *
 * Range registers pseudo code:
 *	bool secure_rr(access) {
 *		if (access.is_secure())
 *			return false;
 *		bool bad_access = false;
 *		for (int r = 0; r < RANGE_NUM; r++) {
 *			range_hit = ((access.address < cfg_end[r]) &&
 *				(access.address >= cfg_start[r]));
 *			bad_access = bad_access ||
 *				(range_hit_is_bad[r] ? range_hit : ~range_hit);
 *		}
 *		return bad_access;
 *	}
 */

/* HBW Range Registers Definitions */

#define RR_SHRD_HBW_SEC_RANGE_OFFSET 0
#define RR_PRVT_HBW_SEC_RANGE_OFFSET (mmPSOC_MSTR_IF_RR_PRVT_HBW_BASE - \
					 mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* 6 regs from offset 0x0 */
#define RR_HBW_SEC_RANGE_MIN_SHORT_0_OFFSET \
		(mmPSOC_MSTR_IF_RR_SHRD_HBW_SEC_RANGE_MIN_SHORT_0 - \
				mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* 6 regs from offset 0x18 */
#define RR_HBW_SEC_RANGE_MAX_SHORT_0_OFFSET \
		(mmPSOC_MSTR_IF_RR_SHRD_HBW_SEC_RANGE_MAX_SHORT_0 - \
				mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* Enable access towards PCIe, offset 0xE0 */
#define RR_HBW_SEC_PCIE_EN \
		(mmPSOC_MSTR_IF_RR_SHRD_HBW_SEC_PCIE_EN - \
				mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* Bit mask at offset 0xE8, default value 0x3FF */
#define RR_HBW_SEC_HIT_AW  \
		(mmPSOC_MSTR_IF_RR_SHRD_HBW_SEC_HIT_AW - \
				mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* Bit mask at offset 0xF0, default value 0xFF */
#define RR_HBW_SEC_HIT_AR \
		(mmPSOC_MSTR_IF_RR_SHRD_HBW_SEC_HIT_AR - \
				mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* LBW Range Registers Definitions */

#define RR_SHRD_LBW_SEC_RANGE_OFFSET (mmPSOC_MSTR_IF_RR_SHRD_LBW_BASE - \
					mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)
#define RR_PRVT_LBW_SEC_RANGE_OFFSET (mmPSOC_MSTR_IF_RR_PRVT_LBW_BASE - \
					mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* 14 x 14bit regs at offset 0x0 */
#define RR_LBW_SEC_RANGE_MIN_SHORT_0_OFFSET \
		(mmPSOC_MSTR_IF_RR_SHRD_LBW_SEC_RANGE_MIN_SHORT_0 - \
			mmPSOC_MSTR_IF_RR_SHRD_LBW_BASE)

/* 14 x 14bit regs at offset 0x38 */
#define RR_LBW_SEC_RANGE_MAX_SHORT_0_OFFSET \
		(mmPSOC_MSTR_IF_RR_SHRD_LBW_SEC_RANGE_MAX_SHORT_0 - \
			mmPSOC_MSTR_IF_RR_SHRD_LBW_BASE)

/* 4 x 26bit regs at offset 0xE0 */
#define RR_LBW_SEC_RANGE_MIN_0_OFFSET \
		(mmPSOC_MSTR_IF_RR_SHRD_LBW_SEC_RANGE_MIN_0 - \
			mmPSOC_MSTR_IF_RR_SHRD_LBW_BASE)

/* 4 x 26bit regs at offset 0xF0 */
#define RR_LBW_SEC_RANGE_MAX_0_OFFSET \
		(mmPSOC_MSTR_IF_RR_SHRD_LBW_SEC_RANGE_MAX_0 - \
			mmPSOC_MSTR_IF_RR_SHRD_LBW_BASE) /* 4 x 26bit regs */

/* Bit map at offset 0x120, default value 0x3FFFF */
#define RR_LBW_SEC_HIT_AW \
		(mmPSOC_MSTR_IF_RR_SHRD_LBW_SEC_HIT_AW - \
				mmPSOC_MSTR_IF_RR_SHRD_LBW_BASE)

/* Bit map at offset 0x128, default value 0x3FFFF */
#define RR_LBW_SEC_HIT_AR \
		(mmPSOC_MSTR_IF_RR_SHRD_LBW_SEC_HIT_AR - \
				mmPSOC_MSTR_IF_RR_SHRD_LBW_BASE)

/*
 * MMU Range registers for protecting regions behind the MMU
 */
/* 8 sets of the following registers */
#define MMU_RR_SEC_MIN_49_32_0_OFFSET \
	(mmDCORE0_HMMU0_MMU_MMU_RR_SEC_MIN_49_32_0 - mmDCORE0_HMMU0_MMU_BASE)
#define MMU_RR_SEC_MIN_31_0_0_OFFSET \
	(mmDCORE0_HMMU0_MMU_MMU_RR_SEC_MIN_31_0_0 - mmDCORE0_HMMU0_MMU_BASE)

#define MMU_RR_SEC_MAX_49_32_0_OFFSET \
	(mmDCORE0_HMMU0_MMU_MMU_RR_SEC_MAX_49_32_0 - mmDCORE0_HMMU0_MMU_BASE)
#define MMU_RR_SEC_MAX_31_0_0_OFFSET \
	(mmDCORE0_HMMU0_MMU_MMU_RR_SEC_MAX_31_0_0 - mmDCORE0_HMMU0_MMU_BASE)

#define MMU_DDR_RANGE_REG_ENABLE_OFFSET \
	(mmDCORE0_HMMU0_MMU_DDR_RANGE_REG_ENABLE - mmDCORE0_HMMU0_MMU_BASE)

/*
 * Clarification:
 *  Due to mismatch between the documentation and the generated definitions,
 *  the following conversion is done in the code:
 * +---------------+----------------------+
 * | Documentation | Definition/SocOnline |
 * +===============+======================+
 * |    DCON0      |       DCON0          |
 * +---------------+----------------------+
 * |    RCON0      |       DCON1          |
 * +---------------+----------------------+
 * |    DCON1      |       DCON2          |
 * +---------------+----------------------+
 * |    RCON1      |       DCON3          |
 * +---------------+----------------------+
 */

/*
 * +------+--------+------------------------+------------------------+---------+
 * |Engine|Physical|          HBW           |          LBW           | P/S Type|
 * |      |Location|                        |                        |         |
 * +------+--------+------------------------+------------------------+---------+
 * | TPC  |  RTR   |DCORE0/1_TPCIF_RTR<0..7>|DCORE0/1_TPCIF_RTR<0..7>| Private |
 * |      |        | _MSTR_IF               | _MSTR_IF               |         |
 * +------+--------+------------------------+------------------------+---------+
 */
static u32 greco_rr_blocks_hbw_tpc[] = {
	mmDCORE0_TPCIF_RTR0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCORE0_TPCIF_RTR1_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCORE0_TPCIF_RTR2_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCORE0_TPCIF_RTR3_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCORE1_TPCIF_RTR0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCORE1_TPCIF_RTR1_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCORE1_TPCIF_RTR2_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCORE1_TPCIF_RTR3_MSTR_IF_RR_PRVT_HBW_BASE,
};

static u32 greco_rr_blocks_lbw_tpc[] = {
	mmDCORE0_TPCIF_RTR0_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCORE0_TPCIF_RTR1_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCORE0_TPCIF_RTR2_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCORE0_TPCIF_RTR3_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCORE1_TPCIF_RTR0_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCORE1_TPCIF_RTR1_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCORE1_TPCIF_RTR2_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCORE1_TPCIF_RTR3_MSTR_IF_RR_PRVT_LBW_BASE,
};

/*
 * +------+--------+------------------------+------------------------+---------+
 * |Engine|Physical|          HBW           |          LBW           |PRVT/SHRD|
 * |      |Location|                        |                        |         |
 * +------+--------+------------------------+------------------------+---------+
 * | MME  |  RTR   |DCORE0/1_MMEIF_RTR<0..7>|DCORE0/1_MMEIF_RTR<0..7>| Private |
 * |      |        | _MSTR_IF               | _MSTR_IF               |         |
 * +------+--------+------------------------+------------------------+---------+
 */
static u32 greco_rr_blocks_priv_hbw_mme[] = {
	mmDCORE0_MMEIF_RTR0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCORE0_MMEIF_RTR1_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCORE0_MMEIF_RTR2_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCORE0_MMEIF_RTR3_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCORE1_MMEIF_RTR0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCORE1_MMEIF_RTR1_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCORE1_MMEIF_RTR2_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCORE1_MMEIF_RTR3_MSTR_IF_RR_PRVT_HBW_BASE,
};

static u32 greco_rr_blocks_priv_lbw_mme[] = {
	mmDCORE0_MMEIF_RTR0_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCORE0_MMEIF_RTR1_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCORE0_MMEIF_RTR2_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCORE0_MMEIF_RTR3_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCORE1_MMEIF_RTR0_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCORE1_MMEIF_RTR1_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCORE1_MMEIF_RTR2_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCORE1_MMEIF_RTR3_MSTR_IF_RR_PRVT_LBW_BASE,
};

/*
 * +------+--------+-------------------------+------------------------+--------+
 * |Engine|Physical|          HBW            |          LBW           | P/S    |
 * |      |Location|                         |                        |        |
 * +------+--------+-------------------------+------------------------+--------+
 * | DEC  |  DCON  |DCON0/1_HBW_RTR_IF<0..1> |DCON0/1_LBW_RTR_IF<0..1>|Private |
 * |      |        | _MSTR_IF                | _MSTR_IF               |        |
 * +------+--------+-------------------------+------------------------+--------+
 */
static u32 greco_rr_blocks_hbw_dec[] = {
	mmDCON0_HBW_RTR_IF0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON0_HBW_RTR_IF1_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON2_HBW_RTR_IF0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON2_HBW_RTR_IF1_MSTR_IF_RR_PRVT_HBW_BASE,
};

static u32 greco_rr_blocks_lbw_dec[] = {
	mmDCON0_LBW_RTR_IF_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCON2_LBW_RTR_IF_MSTR_IF_RR_PRVT_LBW_BASE,
};

/*
 * +------+--------+-------------------------+------------------------+--------+
 * |Engine|Physical|          HBW            |          LBW           | P/S    |
 * |      |Location|                         |                        |        |
 * +------+--------+-------------------------+------------------------+--------+
 * | ENC  |  DCON  |DCON1_HBW_RTR_IF<0..1>_  |DCON1_LBW_RTR_IF<0..1>_ | Shared |
 * |      |        | _MSTR_IF                | _MSTR_IF               |        |
 * +------+--------+-------------------------+------------------------+--------+
 */
static u32 greco_rr_blocks_hbw_enc[] = {
	mmDCON2_HBW_RTR_IF0_MSTR_IF_RR_SHRD_HBW_BASE,
	mmDCON2_HBW_RTR_IF1_MSTR_IF_RR_SHRD_HBW_BASE,
};

static u32 greco_rr_blocks_lbw_enc[] = {
	mmDCON2_LBW_RTR_IF_MSTR_IF_RR_SHRD_LBW_BASE,
};

/*
 * +------+--------+----------------------------------+---------------+--------+
 * |Engine|Physical|          HBW                     |     LBW       | P/S    |
 * |      |Location|                                  |               |        |
 * +------+--------+----------------------------------+---------------+--------+
 * | DMMU |  DCON  |DCON<0/1>_HBW_RTR_IF<0..1>_MSTR_IF|     N/A       |Private |
 * |      |  RCON  |RCON<0/1>_HBW_RTR_IF<0..1>_MSTR_IF|     N/A       |Private |
 * |      |        |RCON0 == DCON2; RCON1 == DCON3;   |               |        |
 * +------+--------+-------------------------+--------+---------------+--------+
 */
static u32 greco_rr_blocks_hbw_dmmu[] = {
	mmDCON0_HBW_RTR_IF0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON0_HBW_RTR_IF1_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON1_HBW_RTR_IF0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON1_HBW_RTR_IF1_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON2_HBW_RTR_IF0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON2_HBW_RTR_IF1_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON3_HBW_RTR_IF0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON3_HBW_RTR_IF1_MSTR_IF_RR_PRVT_HBW_BASE,
};

/*
 * +------+--------+-----------------------+--------------------------+--------+
 * |Engine|Physical|          HBW          |           LBW            | P/S    |
 * |      |Location|                       |                          |        |
 * +------+--------+-----------------------+--------------------------+--------+
 * | CPU  |  DCON  |DCON1_HBW_RTR_IF<0..1>_|DCON1_LBW_RTR_IF_MSTR_IF  |Shared  |
 * |      |        |_MSTR_IF               |                          |        |
 * +------+--------+-----------------------+--------------------------+--------+
 */
static u32 greco_rr_blocks_hbw_cpu[] = {
	mmDCON2_HBW_RTR_IF0_MSTR_IF_RR_SHRD_HBW_BASE,
	mmDCON2_HBW_RTR_IF1_MSTR_IF_RR_SHRD_HBW_BASE,
};

static u32 greco_rr_blocks_lbw_cpu[] = {
	mmDCON2_LBW_RTR_IF_MSTR_IF_RR_SHRD_LBW_BASE,
};

/*
 * +------+--------+------------------------+-------------------------+--------+
 * |Engine|Physical|          HBW           |           LBW           | P/S    |
 * |      |Location|                        |                         |        |
 * +------+--------+------------------------+-------------------------+--------+
 * | DDMA |  DCON  |DCON0/1_HBW_RTR_IF<0..1>|DCON0/1_LBW_RTR_IF<0..1> |Private |
 * |      |        |_MSTR_IF                |_MSTR_IF                 |        |
 * +------+--------+------------------------+-------------------------+--------+
 */
static u32 greco_rr_blocks_hbw_ddma[] = {
	mmDCON0_HBW_RTR_IF0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON0_HBW_RTR_IF1_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON2_HBW_RTR_IF0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON2_HBW_RTR_IF1_MSTR_IF_RR_PRVT_HBW_BASE,
};

static u32 greco_rr_blocks_lbw_ddma[] = {
	mmDCON0_LBW_RTR_IF_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCON2_LBW_RTR_IF_MSTR_IF_RR_PRVT_LBW_BASE,
};

/*
 * +------+--------+------------------------+-------------------------+--------+
 * |Engine|Physical|          HBW           |           LBW           | P/S    |
 * |      |Location|                        |                         |        |
 * +------+--------+------------------------+-------------------------+--------+
 * | PSOC |  DCON  |DCON1_HBW_RTR_IF<0..1>_ |DCON1_LBW_RTR_IF<0..1>_  | Shared |
 * | ARC  |        | _MSTR_IF               | _MSTR_IF                |        |
 * +------+--------+------------------------+-------------------------+--------+
 */
static u32 greco_rr_blocks_hbw_psoc_arc[] = {
	mmDCON2_HBW_RTR_IF0_MSTR_IF_RR_SHRD_HBW_BASE,
	mmDCON2_HBW_RTR_IF1_MSTR_IF_RR_SHRD_HBW_BASE,
};

static u32 greco_rr_blocks_lbw_psoc_arc[] = {
	mmDCON2_LBW_RTR_IF_MSTR_IF_RR_SHRD_LBW_BASE,
};

/*
 * +------+--------+------------------------+-------------------------+--------+
 * |Engine|Physical|          HBW           |           LBW           | P/S    |
 * |      |Location|                        |                         |        |
 * +------+--------+------------------------+-------------------------+--------+
 * | ROT  |  RCON  |RCON0/1_HBW_RTR_IF<0..1>|RCON0/1_LBW_RTR_IF<0..1> |Private |
 * |      |        |_MSTR_IF                |_MSTR_IF                 |        |
 * +------+--------+------------------------+-------------------------+--------+
 */
static u32 greco_rr_blocks_hbw_rot[] = {
	mmDCON1_HBW_RTR_IF0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON1_HBW_RTR_IF1_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON3_HBW_RTR_IF0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON3_HBW_RTR_IF1_MSTR_IF_RR_PRVT_HBW_BASE,
};

static u32 greco_rr_blocks_lbw_rot[] = {
	mmDCON1_LBW_RTR_IF_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCON3_LBW_RTR_IF_MSTR_IF_RR_PRVT_LBW_BASE,
};

/*
 * +------+--------+------------------------+-------------------------+--------+
 * |Engine|Physical|          HBW           |           LBW           | P/S    |
 * |      |Location|                        |                         |        |
 * +------+--------+------------------------+-------------------------+--------+
 * | PCIe |  DCON  |DCON0_HBW_RTR_IF<0..1>_ |DCON0_LBW_RTR_IF<0..1>_  |Shared  |
 * |      |        |_MSTR_IF                |_MSTR_IF                 |        |
 * +------+--------+------------------------+-------------------------+--------+
 */
static u32 greco_rr_blocks_hbw_pcie[] = {
	mmDCON0_HBW_RTR_IF0_MSTR_IF_RR_SHRD_HBW_BASE,
	mmDCON0_HBW_RTR_IF1_MSTR_IF_RR_SHRD_HBW_BASE,
};

static u32 greco_rr_blocks_lbw_pcie[] = {
	mmDCON0_LBW_RTR_IF_MSTR_IF_RR_SHRD_LBW_BASE,
};

/*
 * +------+--------+------------------------+-------------------------+--------+
 * |Engine|Physical|          HBW           |           LBW           | P/S    |
 * |      |Location|                        |                         |        |
 * +------+--------+------------------------+-------------------------+--------+
 * | ELBI |  PCIe  |          N/A           |    PCIE_ELBI_RR_MSTR_IF |Shared  |
 * +------+--------+------------------------+-------------------------+--------+
 */
static u32 greco_rr_blocks_lbw_elbi[] = {
	mmPCIE_ELBI_RR_MSTR_IF_RR_SHRD_LBW_BASE,
};

/*
 * +------+--------+------------------------+-------------------------+--------+
 * |Engine|Physical|          HBW           |           LBW           | P/S    |
 * |      |Location|                        |                         |        |
 * +------+--------+------------------------+-------------------------+--------+
 * | PMMU |  DCON  |DCON0_HBW_RTR_IF<0..1>_ |DCON0_LBW_RTR_IF<0..1>_  |Shared  |
 * |      |        |_MSTR_IF                |_MSTR_IF                 |        |
 * +------+--------+------------------------+-------------------------+--------+
 */
static u32 greco_rr_blocks_hbw_pmmu[] = {
	mmDCON0_HBW_RTR_IF0_MSTR_IF_RR_SHRD_HBW_BASE,
	mmDCON0_HBW_RTR_IF1_MSTR_IF_RR_SHRD_HBW_BASE,
};

static u32 greco_rr_blocks_lbw_pmmu[] = {
	mmDCON0_LBW_RTR_IF_MSTR_IF_RR_SHRD_LBW_BASE,
};

/*
 * +------+--------+------------------------+-------------------------+--------+
 * |Engine|Physical|          HBW           |           LBW           | P/S    |
 * |      |Location|                        |                         |        |
 * +------+--------+------------------------+-------------------------+--------+
 * | KDMA |  RCON  |RCON0/1_HBW_RTR_IF<0..1>|RCON0/1_LBW_RTR_IF<0..1>_|Private |
 * | PDMA |        |_MSTR_IF                |_MSTR_IF                 |        |
 * | SM   |        |                        |                         |        |
 * +------+--------+------------------------+-------------------------+--------+
 */
static u32 greco_rr_blocks_hbw_kdma_pdma_sm[] = {
	mmDCON1_HBW_RTR_IF0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON1_HBW_RTR_IF1_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON3_HBW_RTR_IF0_MSTR_IF_RR_PRVT_HBW_BASE,
	mmDCON3_HBW_RTR_IF1_MSTR_IF_RR_PRVT_HBW_BASE,
};

static u32 greco_rr_blocks_lbw_kdma_pdma_sm[] = {
	mmDCON1_LBW_RTR_IF_MSTR_IF_RR_PRVT_LBW_BASE,
	mmDCON3_LBW_RTR_IF_MSTR_IF_RR_PRVT_LBW_BASE,
};

/* MMU has a different set of RR */
static u32 greco_rr_blocks_mmu[] = {
	mmDCORE0_HMMU0_MMU_BASE,
	mmDCORE0_HMMU1_MMU_BASE,
	mmDCORE1_HMMU0_MMU_BASE,
	mmDCORE1_HMMU1_MMU_BASE,
};

/*
 * In each 4K block of registers, the last 128 bytes are protection
 * bits - total of 1024 bits, one for each register. Each bit is related
 * to a specific register, by the order of the registers.
 * So in order to calculate the bit that is related to a given register,
 * we need to calculate its word offset and then the exact bit inside
 * the word (which is 4 bytes).
 *
 * Register address:
 *
 * 31                 12 11           7   6             2  1      0
 * -----------------------------------------------------------------
 * |      Don't         |    word       |  bit location  |    0    |
 * |      care          |   offset      |  inside word   |         |
 * -----------------------------------------------------------------
 *
 * Bits 7-11 represents the word offset inside the 128 bytes.
 * Bits 2-6 represents the bit location inside the word.
 *
 * When a bit is cleared, it means the register it represents can only
 * be accessed by a secured entity. When the bit is set, any entity can
 * access the register.
 *
 * The last 4 bytes in the block of the PBs control the security of
 * the PBs themselves, so they always need to be configured to be
 * secured
 */

/* Most of the greco config blocks are 4K in size and have 32 32bit protection
 * registers located at offset 0xF80
 */

#define SPECIAL_GLBL_ERR_CAUSE_APB_PRIV_RD \
		DCORE0_DDMA_MSTR_IF_SPECIAL_GLBL_ERR_CAUSE_APB_PRIV_RD_MASK
#define SPECIAL_GLBL_ERR_CAUSE_APB_SEC_RD \
		DCORE0_DDMA_MSTR_IF_SPECIAL_GLBL_ERR_CAUSE_APB_SEC_RD_MASK
#define SPECIAL_GLBL_ERR_CAUSE_APB_UNMAPPED_RD \
		DCORE0_DDMA_MSTR_IF_SPECIAL_GLBL_ERR_CAUSE_APB_UNMAPPED_RD_MASK
#define SPECIAL_GLBL_ERR_CAUSE_APB_PRIV_WR \
		DCORE0_DDMA_MSTR_IF_SPECIAL_GLBL_ERR_CAUSE_APB_PRIV_WR_MASK
#define SPECIAL_GLBL_ERR_CAUSE_APB_SEC_WR \
		DCORE0_DDMA_MSTR_IF_SPECIAL_GLBL_ERR_CAUSE_APB_SEC_WR_MASK
#define SPECIAL_GLBL_ERR_CAUSE_APB_UNMAPPED_WR \
		DCORE0_DDMA_MSTR_IF_SPECIAL_GLBL_ERR_CAUSE_APB_UNMAPPED_WR_MASK
#define SPECIAL_GLBL_ERR_CAUSE_EXT_SEC_WR \
		DCORE0_DDMA_MSTR_IF_SPECIAL_GLBL_ERR_CAUSE_EXT_SEC_WR_MASK
#define SPECIAL_GLBL_ERR_CAUSE_EXT_UNMAPPED_WR \
		DCORE0_DDMA_MSTR_IF_SPECIAL_GLBL_ERR_CAUSE_EXT_UNMAPPED_WR_MASK

#define GRECO_ATYPICAL_BLOCK_GLBL_SEC_MAX_LEN  144

struct greco_atypical_bp_blocks {
	u32 mm_block_base_addr;
	u32 block_size;
	u32 glbl_sec_offset;
	u32 glbl_sec_length;
};

static const struct greco_atypical_bp_blocks greco_pb_dcr0_sm_objs = {
	mmDCORE0_SYNC_MNGR_OBJS_BASE, 24 * 1024, SM_OBJS_PROT_BITS_OFFS, 144,
};

static const u32 greco_pb_cpu[] = {
	mmCPU_CA53_CFG_BASE,
	mmCPU_IF_BASE,
	mmCPU_MSTR_IF_RR_SHRD_HBW_BASE,
};

static const u32 greco_pb_dcon0_base[] = {
	mmDCON0_BASE,
};

static const u32 greco_pb_dcon0_hbw_if0[] = {
	mmDCON0_HBW_RTR_IF0_MSTR_IF_RR_SHRD_HBW_BASE,
	mmDCON0_HBW_RTR_IF0_RTR_CTRL_BASE,
	mmDCON0_HBW_RTR_IF0_RTR_H3_BASE,
};

static const u32 greco_pb_dcon0_lbw[] = {
	mmDCON0_LBW_RTR_IF_MSTR_IF_RR_SHRD_HBW_BASE,
	mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE,
	mmDCON0_LBW_RTR_IF_RTR_H3_BASE,
};

static const u32 greco_pb_dcr0_ddma[] = {
	mmDCORE0_DDMA_CORE_BASE,
	mmDCORE0_DDMA_MSTR_IF_RR_SHRD_HBW_BASE,
	mmDCORE0_DDMA_QM_BASE,
};

static const u32 greco_pb_dcr0_ddma_unsecured_regs[] = {
	mmDCORE0_DDMA_CORE_CTX_WR_COMP_ADDR_HI,
	mmDCORE0_DDMA_CORE_CTX_WR_COMP_ADDR_LO,
	mmDCORE0_DDMA_CORE_CTX_WR_COMP_WDATA,
	mmDCORE0_DDMA_CORE_CTX_SRC_BASE_LO,
	mmDCORE0_DDMA_CORE_CTX_SRC_BASE_HI,
	mmDCORE0_DDMA_CORE_CTX_DST_BASE_LO,
	mmDCORE0_DDMA_CORE_CTX_DST_BASE_HI,
	mmDCORE0_DDMA_CORE_CTX_SRC_TSIZE_0,
	mmDCORE0_DDMA_CORE_CTX_SRC_TSIZE_1,
	mmDCORE0_DDMA_CORE_CTX_SRC_TSIZE_2,
	mmDCORE0_DDMA_CORE_CTX_SRC_TSIZE_3,
	mmDCORE0_DDMA_CORE_CTX_SRC_TSIZE_4,
	mmDCORE0_DDMA_CORE_CTX_SRC_STRIDE_1,
	mmDCORE0_DDMA_CORE_CTX_SRC_STRIDE_2,
	mmDCORE0_DDMA_CORE_CTX_SRC_STRIDE_3,
	mmDCORE0_DDMA_CORE_CTX_SRC_STRIDE_4,
	mmDCORE0_DDMA_CORE_CTX_DST_TSIZE_0,
	mmDCORE0_DDMA_CORE_CTX_DST_TSIZE_1,
	mmDCORE0_DDMA_CORE_CTX_DST_TSIZE_2,
	mmDCORE0_DDMA_CORE_CTX_DST_TSIZE_3,
	mmDCORE0_DDMA_CORE_CTX_DST_TSIZE_4,
	mmDCORE0_DDMA_CORE_CTX_DST_STRIDE_1,
	mmDCORE0_DDMA_CORE_CTX_DST_STRIDE_2,
	mmDCORE0_DDMA_CORE_CTX_DST_STRIDE_3,
	mmDCORE0_DDMA_CORE_CTX_DST_STRIDE_4,
	mmDCORE0_DDMA_CORE_CTX_COMMIT,
	mmDCORE0_DDMA_CORE_CTX_CTRL,
	mmDCORE0_DDMA_CORE_CTX_TE_NUMROWS,
	mmDCORE0_DDMA_CORE_CTX_AXUSER_HB_REDUCTION,
	mmDCORE0_DDMA_QM_CP_FENCE0_RDATA_0,
	mmDCORE0_DDMA_QM_CP_FENCE0_RDATA_1,
	mmDCORE0_DDMA_QM_CP_FENCE0_RDATA_2,
	mmDCORE0_DDMA_QM_CP_FENCE0_RDATA_3,
	mmDCORE0_DDMA_QM_CP_FENCE0_RDATA_4,
	mmDCORE0_DDMA_QM_CP_FENCE1_RDATA_0,
	mmDCORE0_DDMA_QM_CP_FENCE1_RDATA_1,
	mmDCORE0_DDMA_QM_CP_FENCE1_RDATA_2,
	mmDCORE0_DDMA_QM_CP_FENCE1_RDATA_3,
	mmDCORE0_DDMA_QM_CP_FENCE1_RDATA_4,
	mmDCORE0_DDMA_QM_CP_FENCE2_RDATA_0,
	mmDCORE0_DDMA_QM_CP_FENCE2_RDATA_1,
	mmDCORE0_DDMA_QM_CP_FENCE2_RDATA_2,
	mmDCORE0_DDMA_QM_CP_FENCE2_RDATA_3,
	mmDCORE0_DDMA_QM_CP_FENCE2_RDATA_4,
	mmDCORE0_DDMA_QM_CP_FENCE3_RDATA_0,
	mmDCORE0_DDMA_QM_CP_FENCE3_RDATA_1,
	mmDCORE0_DDMA_QM_CP_FENCE3_RDATA_2,
	mmDCORE0_DDMA_QM_CP_FENCE3_RDATA_3,
	mmDCORE0_DDMA_QM_CP_FENCE3_RDATA_4,
	mmDCORE0_DDMA_QM_CP_PRED_0,
	mmDCORE0_DDMA_QM_CP_PRED_1,
	mmDCORE0_DDMA_QM_CP_PRED_2,
	mmDCORE0_DDMA_QM_CP_PRED_3,
	mmDCORE0_DDMA_QM_CP_PRED_4,
	mmDCORE0_DDMA_QM_CP_PRED_UPEN_0,
	mmDCORE0_DDMA_QM_CP_PRED_UPEN_1,
	mmDCORE0_DDMA_QM_CQ_CFG0_0,
	mmDCORE0_DDMA_QM_CQ_CFG0_1,
	mmDCORE0_DDMA_QM_CQ_CFG0_2,
	mmDCORE0_DDMA_QM_CQ_CFG0_3,
	mmDCORE0_DDMA_QM_CQ_CFG0_4,
	mmDCORE0_DDMA_QM_CQ_PTR_LO_0,
	mmDCORE0_DDMA_QM_CQ_PTR_HI_0,
	mmDCORE0_DDMA_QM_CQ_TSIZE_0,
	mmDCORE0_DDMA_QM_CQ_CTL_0,
	mmDCORE0_DDMA_QM_CQ_PTR_LO_1,
	mmDCORE0_DDMA_QM_CQ_PTR_HI_1,
	mmDCORE0_DDMA_QM_CQ_TSIZE_1,
	mmDCORE0_DDMA_QM_CQ_CTL_1,
	mmDCORE0_DDMA_QM_CQ_PTR_LO_2,
	mmDCORE0_DDMA_QM_CQ_PTR_HI_2,
	mmDCORE0_DDMA_QM_CQ_TSIZE_2,
	mmDCORE0_DDMA_QM_CQ_CTL_2,
	mmDCORE0_DDMA_QM_CQ_PTR_LO_3,
	mmDCORE0_DDMA_QM_CQ_PTR_HI_3,
	mmDCORE0_DDMA_QM_CQ_TSIZE_3,
	mmDCORE0_DDMA_QM_CQ_CTL_3,
	mmDCORE0_DDMA_QM_CQ_PTR_LO_4,
	mmDCORE0_DDMA_QM_CQ_PTR_HI_4,
	mmDCORE0_DDMA_QM_CQ_TSIZE_4,
	mmDCORE0_DDMA_QM_CQ_CTL_4,
	mmDCORE0_DDMA_QM_ARB_CFG_0,
	mmDCORE0_DDMA_QM_ARB_MST_QUIET_PER,
	mmDCORE0_DDMA_QM_ARB_CHOICE_Q_PUSH,
	mmDCORE0_DDMA_QM_ARB_WRR_WEIGHT_0,
	mmDCORE0_DDMA_QM_ARB_WRR_WEIGHT_1,
	mmDCORE0_DDMA_QM_ARB_WRR_WEIGHT_2,
	mmDCORE0_DDMA_QM_ARB_WRR_WEIGHT_3,
	mmDCORE0_DDMA_QM_ARB_BASE_LO,
	mmDCORE0_DDMA_QM_ARB_BASE_HI,
	mmDCORE0_DDMA_QM_ARB_MST_SLAVE_EN,
	mmDCORE0_DDMA_QM_ARB_MST_CRED_INC,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_0,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_1,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_2,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_3,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_4,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_5,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_6,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_7,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_8,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_9,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_10,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_11,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_12,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_13,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_14,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_15,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_16,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_17,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_18,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_19,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_20,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_21,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_22,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_23,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_24,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_25,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_26,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_27,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_28,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_29,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_30,
	mmDCORE0_DDMA_QM_ARB_MST_CHOICE_PUSH_OFST_31,
	mmDCORE0_DDMA_QM_ARB_SLV_ID,
	mmDCORE0_DDMA_QM_ARB_SLV_MASTER_INC_CRED_OFST,
};

static const u32 greco_pb_dcr0_ddr0[] = {
	mmDCORE0_DDR0_MISC_BASE,
};

static const u32 greco_pb_dcr0_hif0[] = {
	mmDCORE0_HIF0_BASE,
};

static const u32 greco_pb_dcr0_hmmu0[] = {
	mmDCORE0_HMMU0_HPC_BASE,
	mmDCORE0_HMMU0_MMU_BASE,
	mmDCORE0_HMMU0_MSTR_IF_RR_SHRD_HBW_BASE,
	mmDCORE0_HMMU0_SCRAMB_OUT_BASE,
	mmDCORE0_HMMU0_STLB_BASE,
};

static const u32 greco_pb_dcr0_hmmu0_axi[] = {
	mmDCORE0_HMMU0_AXI_MBIST0_BASE,
	mmDCORE0_HMMU0_AXI_MBIST1_BASE,
	mmDCORE0_HMMU0_AXI_MBIST2_BASE,
	mmDCORE0_HMMU0_AXI_MBIST3_BASE,
};

static const u32 greco_pb_dcr0_kdma[] = {
	mmDCORE0_KDMA_CORE_BASE,
	mmDCORE0_KDMA_MSTR_IF_RR_SHRD_HBW_BASE,
};

static const u32 greco_pb_dcr0_mme[] = {
	mmDCORE0_MME_ACC_BASE,
	mmDCORE0_MME_CTRL_HI_BASE,
	mmDCORE0_MME_CTRL_LO_BASE,
	mmDCORE0_MME_MSTR_IF_RR_SHRD_HBW_BASE,
	mmDCORE0_MME_SRAM_L0_BASE,
};

static const u32 greco_pb_dcr0_mme_unsecured_regs[] = {
	mmDCORE0_MME_ACC_AP_LFSR_POLY,
	mmDCORE0_MME_ACC_AP_LFSR_SEED_WDATA,
	mmDCORE0_MME_ACC_AP_LFSR_SEED_SEL,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_ACT_PIPE_DW0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_ACT_PIPE_DW1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_ACT_PIPE_DW2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_ACT_PIPE_SCALE_GLP_CIN,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_ACT_PIPE_SCALE_GLP_COUT,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_ACT_PIPE_SCALE_PWL_NEG,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_ACT_PIPE_SCALE_PWL_POS,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_A_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_A_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_A_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_A_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_A_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_A_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_A_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_A_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_A_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_A_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_B_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_B_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_B_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_B_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_B_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_B_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_B_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_B_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_B_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_B_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_CIN_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_CIN_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_CIN_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_CIN_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_CIN_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_CIN_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_CIN_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_CIN_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_CIN_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_CIN_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_COUT_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_COUT_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_COUT_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_COUT_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_COUT_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_COUT_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_COUT_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_COUT_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_COUT_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_COUT_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_A_SS,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_BASE_ADDR_ACT_MD_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_BASE_ADDR_ACT_MD_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_BASE_ADDR_A_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_BASE_ADDR_A_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_BASE_ADDR_B_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_BASE_ADDR_B_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_BASE_ADDR_CIN_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_BASE_ADDR_CIN_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_BASE_ADDR_COUT0_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_BASE_ADDR_COUT0_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_BASE_ADDR_COUT1_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_BASE_ADDR_COUT1_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_BASE_ADDR_LUT_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_BASE_ADDR_LUT_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_B_MASTER_SS,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_B_SLAVE_SS,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_CIN_SS,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_CONV_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_CONV_KERNEL_SIZE_MINUS_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_CONV_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_COUT_SS,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_LOCAL_MASTER_SS,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_LOCAL_SLAVE_SS,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_MD_TYPE_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_MD_TYPE_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_PADDING_VALUE_A,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_PADDING_VALUE_B,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_PADDING_VALUE_C,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_PCU,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_PERF_EVT_IN,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_PERF_EVT_OUT,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_POWER_LOOP,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_RATE_LIMITER,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_SB_REPEAT,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_SPARE0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_SPARE1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_SPARE2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_USER_DATA,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_END_WKL,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_START_BRAINS_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_START_BRAINS_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_START_CTRL_EUS,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_START_HEADER_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NON_TENSOR_START_HEADER_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_NUM_ITERATIONS_MINUS_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_OUTER_LOOP,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_STATUS,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_SYNC_OBJ_ADDR0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_SYNC_OBJ_ADDR1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_SYNC_OBJ_DW0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_SYNC_OBJ_VAL0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_SYNC_OBJ_VAL1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_START_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_START_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_START_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_START_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_A_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_START_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_START_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_START_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_START_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_B_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_START_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_START_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_START_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_START_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_CIN_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_START_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_START_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_START_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_START_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_2_TENSOR_COUT_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_ACT_PIPE_DW0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_ACT_PIPE_DW1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_ACT_PIPE_DW2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_ACT_PIPE_SCALE_GLP_CIN,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_ACT_PIPE_SCALE_GLP_COUT,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_ACT_PIPE_SCALE_PWL_NEG,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_ACT_PIPE_SCALE_PWL_POS,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_A_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_A_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_A_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_A_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_A_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_A_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_A_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_A_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_A_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_A_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_B_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_B_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_B_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_B_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_B_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_B_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_B_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_B_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_B_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_B_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_CIN_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_CIN_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_CIN_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_CIN_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_CIN_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_CIN_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_CIN_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_CIN_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_CIN_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_CIN_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_COUT_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_COUT_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_COUT_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_COUT_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_COUT_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_COUT_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_COUT_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_COUT_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_COUT_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_COUT_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_A_SS,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_BASE_ADDR_ACT_MD_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_BASE_ADDR_ACT_MD_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_BASE_ADDR_A_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_BASE_ADDR_A_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_BASE_ADDR_B_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_BASE_ADDR_B_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_BASE_ADDR_CIN_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_BASE_ADDR_CIN_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_BASE_ADDR_COUT0_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_BASE_ADDR_COUT0_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_BASE_ADDR_COUT1_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_BASE_ADDR_COUT1_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_BASE_ADDR_LUT_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_BASE_ADDR_LUT_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_B_MASTER_SS,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_B_SLAVE_SS,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_CIN_SS,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_CONV_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_CONV_KERNEL_SIZE_MINUS_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_CONV_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_COUT_SS,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_LOCAL_MASTER_SS,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_LOCAL_SLAVE_SS,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_MD_TYPE_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_MD_TYPE_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_PADDING_VALUE_A,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_PADDING_VALUE_B,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_PADDING_VALUE_C,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_PCU,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_PERF_EVT_IN,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_PERF_EVT_OUT,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_POWER_LOOP,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_RATE_LIMITER,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_SB_REPEAT,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_SPARE0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_SPARE1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_SPARE2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_USER_DATA,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_END_WKL,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_START_BRAINS_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_START_BRAINS_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_START_CTRL_EUS,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_START_HEADER_HIGH,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NON_TENSOR_START_HEADER_LOW,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_NUM_ITERATIONS_MINUS_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_OUTER_LOOP,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_STATUS,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_SYNC_OBJ_ADDR0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_SYNC_OBJ_ADDR1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_SYNC_OBJ_DW0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_SYNC_OBJ_VAL0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_SYNC_OBJ_VAL1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_START_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_START_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_START_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_START_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_A_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_START_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_START_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_START_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_START_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_B_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_START_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_START_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_START_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_START_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_CIN_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_START_OFFSET_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_START_OFFSET_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_START_OFFSET_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_START_OFFSET_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_HI_SHADOW_3_TENSOR_COUT_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_LO_AGU,
	mmDCORE0_MME_CTRL_LO_ARCH_ACT_PIPE_DW0,
	mmDCORE0_MME_CTRL_LO_ARCH_ACT_PIPE_DW1,
	mmDCORE0_MME_CTRL_LO_ARCH_ACT_PIPE_DW2,
	mmDCORE0_MME_CTRL_LO_ARCH_ACT_PIPE_SCALE_GLP_CIN,
	mmDCORE0_MME_CTRL_LO_ARCH_ACT_PIPE_SCALE_GLP_COUT,
	mmDCORE0_MME_CTRL_LO_ARCH_ACT_PIPE_SCALE_PWL_NEG,
	mmDCORE0_MME_CTRL_LO_ARCH_ACT_PIPE_SCALE_PWL_POS,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_A_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_A_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_A_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_A_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_A_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_A_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_A_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_A_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_A_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_A_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_B_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_B_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_B_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_B_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_B_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_B_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_B_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_B_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_B_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_B_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_CIN_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_CIN_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_CIN_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_CIN_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_CIN_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_CIN_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_CIN_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_CIN_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_CIN_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_CIN_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_COUT_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_COUT_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_COUT_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_COUT_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_COUT_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_COUT_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_COUT_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_COUT_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_COUT_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_COUT_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_ARCH_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_ARCH_A_SS,
	mmDCORE0_MME_CTRL_LO_ARCH_BASE_ADDR_ACT_MD_HIGH,
	mmDCORE0_MME_CTRL_LO_ARCH_BASE_ADDR_ACT_MD_LOW,
	mmDCORE0_MME_CTRL_LO_ARCH_BASE_ADDR_A_HIGH,
	mmDCORE0_MME_CTRL_LO_ARCH_BASE_ADDR_A_LOW,
	mmDCORE0_MME_CTRL_LO_ARCH_BASE_ADDR_B_HIGH,
	mmDCORE0_MME_CTRL_LO_ARCH_BASE_ADDR_B_LOW,
	mmDCORE0_MME_CTRL_LO_ARCH_BASE_ADDR_CIN_HIGH,
	mmDCORE0_MME_CTRL_LO_ARCH_BASE_ADDR_CIN_LOW,
	mmDCORE0_MME_CTRL_LO_ARCH_BASE_ADDR_COUT0_HIGH,
	mmDCORE0_MME_CTRL_LO_ARCH_BASE_ADDR_COUT0_LOW,
	mmDCORE0_MME_CTRL_LO_ARCH_BASE_ADDR_COUT1_HIGH,
	mmDCORE0_MME_CTRL_LO_ARCH_BASE_ADDR_COUT1_LOW,
	mmDCORE0_MME_CTRL_LO_ARCH_BASE_ADDR_LUT_HIGH,
	mmDCORE0_MME_CTRL_LO_ARCH_BASE_ADDR_LUT_LOW,
	mmDCORE0_MME_CTRL_LO_ARCH_B_MASTER_SS,
	mmDCORE0_MME_CTRL_LO_ARCH_B_SLAVE_SS,
	mmDCORE0_MME_CTRL_LO_ARCH_CIN_SS,
	mmDCORE0_MME_CTRL_LO_ARCH_CONV_HIGH,
	mmDCORE0_MME_CTRL_LO_ARCH_CONV_KERNEL_SIZE_MINUS_1,
	mmDCORE0_MME_CTRL_LO_ARCH_CONV_LOW,
	mmDCORE0_MME_CTRL_LO_ARCH_COUT_SS,
	mmDCORE0_MME_CTRL_LO_ARCH_LOCAL_MASTER_SS,
	mmDCORE0_MME_CTRL_LO_ARCH_LOCAL_SLAVE_SS,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_MD_TYPE_HIGH,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_MD_TYPE_LOW,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_PADDING_VALUE_A,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_PADDING_VALUE_B,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_PADDING_VALUE_C,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_PCU,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_PERF_EVT_IN,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_PERF_EVT_OUT,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_POWER_LOOP,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_RATE_LIMITER,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_SB_REPEAT,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_SPARE0,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_SPARE1,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_SPARE2,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_USER_DATA,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_END_WKL,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_START_BRAINS_HIGH,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_START_BRAINS_LOW,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_START_CTRL_EUS,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_START_HEADER_HIGH,
	mmDCORE0_MME_CTRL_LO_ARCH_NON_TENSOR_START_HEADER_LOW,
	mmDCORE0_MME_CTRL_LO_ARCH_NUM_ITERATIONS_MINUS_1,
	mmDCORE0_MME_CTRL_LO_ARCH_OUTER_LOOP,
	mmDCORE0_MME_CTRL_LO_ARCH_STATUS,
	mmDCORE0_MME_CTRL_LO_ARCH_SYNC_OBJ_ADDR0,
	mmDCORE0_MME_CTRL_LO_ARCH_SYNC_OBJ_ADDR1,
	mmDCORE0_MME_CTRL_LO_ARCH_SYNC_OBJ_DW0,
	mmDCORE0_MME_CTRL_LO_ARCH_SYNC_OBJ_VAL0,
	mmDCORE0_MME_CTRL_LO_ARCH_SYNC_OBJ_VAL1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_START_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_START_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_START_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_START_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_A_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_START_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_START_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_START_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_START_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_B_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_START_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_START_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_START_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_START_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_CIN_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_START_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_START_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_START_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_START_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_LO_ARCH_TENSOR_COUT_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_LO_TWO_MASTERS_MODE_NON_SEC,
	mmDCORE0_MME_CTRL_LO_CMD,
	mmDCORE0_MME_CTRL_LO_DUMMY0,
	mmDCORE0_MME_CTRL_LO_DUMMY1,
	mmDCORE0_MME_CTRL_LO_EUS_ROLLUP_DLY,
	mmDCORE0_MME_CTRL_LO_FENCE_DUP_COLOR0_ADD_HI,
	mmDCORE0_MME_CTRL_LO_FENCE_DUP_COLOR0_ADD_LO,
	mmDCORE0_MME_CTRL_LO_FENCE_DUP_COLOR1_ADD_HI,
	mmDCORE0_MME_CTRL_LO_FENCE_DUP_COLOR1_ADD_LO,
	mmDCORE0_MME_CTRL_LO_QM_STALL,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_ACT_PIPE_DW0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_ACT_PIPE_DW1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_ACT_PIPE_DW2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_ACT_PIPE_SCALE_GLP_CIN,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_ACT_PIPE_SCALE_GLP_COUT,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_ACT_PIPE_SCALE_PWL_NEG,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_ACT_PIPE_SCALE_PWL_POS,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_A_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_A_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_A_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_A_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_A_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_A_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_A_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_A_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_A_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_A_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_B_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_B_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_B_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_B_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_B_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_B_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_B_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_B_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_B_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_B_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_CIN_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_CIN_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_CIN_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_CIN_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_CIN_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_CIN_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_CIN_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_CIN_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_CIN_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_CIN_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_COUT_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_COUT_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_COUT_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_COUT_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_COUT_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_COUT_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_COUT_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_COUT_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_COUT_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_COUT_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_A_SS,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_BASE_ADDR_ACT_MD_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_BASE_ADDR_ACT_MD_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_BASE_ADDR_A_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_BASE_ADDR_A_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_BASE_ADDR_B_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_BASE_ADDR_B_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_BASE_ADDR_CIN_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_BASE_ADDR_CIN_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_BASE_ADDR_COUT0_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_BASE_ADDR_COUT0_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_BASE_ADDR_COUT1_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_BASE_ADDR_COUT1_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_BASE_ADDR_LUT_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_BASE_ADDR_LUT_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_B_MASTER_SS,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_B_SLAVE_SS,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_CIN_SS,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_CONV_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_CONV_KERNEL_SIZE_MINUS_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_CONV_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_COUT_SS,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_LOCAL_MASTER_SS,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_LOCAL_SLAVE_SS,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_MD_TYPE_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_MD_TYPE_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_PADDING_VALUE_A,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_PADDING_VALUE_B,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_PADDING_VALUE_C,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_PCU,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_PERF_EVT_IN,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_PERF_EVT_OUT,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_POWER_LOOP,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_RATE_LIMITER,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_SB_REPEAT,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_SPARE0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_SPARE1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_SPARE2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_USER_DATA,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_END_WKL,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_START_BRAINS_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_START_BRAINS_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_START_CTRL_EUS,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_START_HEADER_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NON_TENSOR_START_HEADER_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_NUM_ITERATIONS_MINUS_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_OUTER_LOOP,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_STATUS,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_SYNC_OBJ_ADDR0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_SYNC_OBJ_ADDR1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_SYNC_OBJ_DW0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_SYNC_OBJ_VAL0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_SYNC_OBJ_VAL1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_START_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_START_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_START_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_START_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_A_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_START_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_START_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_START_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_START_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_B_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_START_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_START_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_START_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_START_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_CIN_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_START_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_START_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_START_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_START_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_0_TENSOR_COUT_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_ACT_PIPE_DW0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_ACT_PIPE_DW1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_ACT_PIPE_DW2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_ACT_PIPE_SCALE_GLP_CIN,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_ACT_PIPE_SCALE_GLP_COUT,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_ACT_PIPE_SCALE_PWL_NEG,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_ACT_PIPE_SCALE_PWL_POS,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_A_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_A_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_A_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_A_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_A_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_A_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_A_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_A_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_A_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_A_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_B_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_B_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_B_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_B_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_B_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_B_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_B_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_B_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_B_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_B_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_CIN_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_CIN_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_CIN_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_CIN_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_CIN_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_CIN_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_CIN_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_CIN_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_CIN_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_CIN_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_COUT_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_COUT_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_COUT_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_COUT_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_COUT_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_COUT_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_COUT_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_COUT_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_COUT_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_COUT_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_LOCAL_MASTER_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_AGU_LOCAL_SLAVE_ROI_BASE_OFFSET_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_A_SS,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_BASE_ADDR_ACT_MD_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_BASE_ADDR_ACT_MD_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_BASE_ADDR_A_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_BASE_ADDR_A_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_BASE_ADDR_B_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_BASE_ADDR_B_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_BASE_ADDR_CIN_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_BASE_ADDR_CIN_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_BASE_ADDR_COUT0_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_BASE_ADDR_COUT0_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_BASE_ADDR_COUT1_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_BASE_ADDR_COUT1_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_BASE_ADDR_LUT_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_BASE_ADDR_LUT_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_B_MASTER_SS,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_B_SLAVE_SS,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_CIN_SS,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_CONV_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_CONV_KERNEL_SIZE_MINUS_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_CONV_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_COUT_SS,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_LOCAL_MASTER_SS,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_LOCAL_SLAVE_SS,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_MD_TYPE_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_MD_TYPE_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_PADDING_VALUE_A,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_PADDING_VALUE_B,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_PADDING_VALUE_C,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_PCU,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_PERF_EVT_IN,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_PERF_EVT_OUT,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_POWER_LOOP,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_RATE_LIMITER,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_SB_REPEAT,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_SPARE0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_SPARE1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_SPARE2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_USER_DATA,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_END_WKL,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_START_BRAINS_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_START_BRAINS_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_START_CTRL_EUS,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_START_HEADER_HIGH,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NON_TENSOR_START_HEADER_LOW,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_NUM_ITERATIONS_MINUS_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_OUTER_LOOP,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_STATUS,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_SYNC_OBJ_ADDR0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_SYNC_OBJ_ADDR1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_SYNC_OBJ_DW0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_SYNC_OBJ_VAL0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_SYNC_OBJ_VAL1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_START_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_START_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_START_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_START_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_A_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_START_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_START_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_START_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_START_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_B_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_START_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_START_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_START_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_START_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_CIN_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_LOOP_STRIDE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_LOOP_STRIDE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_LOOP_STRIDE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_LOOP_STRIDE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_LOOP_STRIDE_4,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_ROI_SIZE_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_ROI_SIZE_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_ROI_SIZE_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_ROI_SIZE_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_SPATIAL_STRIDES_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_SPATIAL_STRIDES_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_SPATIAL_STRIDES_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_SPATIAL_STRIDES_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_START_OFFSET_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_START_OFFSET_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_START_OFFSET_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_START_OFFSET_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_VALID_ELEMENTS_0,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_VALID_ELEMENTS_1,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_VALID_ELEMENTS_2,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_VALID_ELEMENTS_3,
	mmDCORE0_MME_CTRL_LO_SHADOW_1_TENSOR_COUT_VALID_ELEMENTS_4,
	mmDCORE0_MME_CTRL_LO_STATUS1,
};

static const u32 greco_pb_dcr0_mme_qm[] = {
	mmDCORE0_MME_QM_BASE,
};

static const u32 greco_pb_dcr0_mme_qm_unsecured_regs[] = {
	mmDCORE0_MME_QM_CP_FENCE0_RDATA_0,
	mmDCORE0_MME_QM_CP_FENCE0_RDATA_1,
	mmDCORE0_MME_QM_CP_FENCE0_RDATA_2,
	mmDCORE0_MME_QM_CP_FENCE0_RDATA_3,
	mmDCORE0_MME_QM_CP_FENCE0_RDATA_4,
	mmDCORE0_MME_QM_CP_FENCE1_RDATA_0,
	mmDCORE0_MME_QM_CP_FENCE1_RDATA_1,
	mmDCORE0_MME_QM_CP_FENCE1_RDATA_2,
	mmDCORE0_MME_QM_CP_FENCE1_RDATA_3,
	mmDCORE0_MME_QM_CP_FENCE1_RDATA_4,
	mmDCORE0_MME_QM_CP_FENCE2_RDATA_0,
	mmDCORE0_MME_QM_CP_FENCE2_RDATA_1,
	mmDCORE0_MME_QM_CP_FENCE2_RDATA_2,
	mmDCORE0_MME_QM_CP_FENCE2_RDATA_3,
	mmDCORE0_MME_QM_CP_FENCE2_RDATA_4,
	mmDCORE0_MME_QM_CP_FENCE3_RDATA_0,
	mmDCORE0_MME_QM_CP_FENCE3_RDATA_1,
	mmDCORE0_MME_QM_CP_FENCE3_RDATA_2,
	mmDCORE0_MME_QM_CP_FENCE3_RDATA_3,
	mmDCORE0_MME_QM_CP_FENCE3_RDATA_4,
	mmDCORE0_MME_QM_CP_PRED_0,
	mmDCORE0_MME_QM_CP_PRED_1,
	mmDCORE0_MME_QM_CP_PRED_2,
	mmDCORE0_MME_QM_CP_PRED_3,
	mmDCORE0_MME_QM_CP_PRED_4,
	mmDCORE0_MME_QM_CP_PRED_UPEN_0,
	mmDCORE0_MME_QM_CP_PRED_UPEN_1,
	mmDCORE0_MME_QM_CQ_CFG0_0,
	mmDCORE0_MME_QM_CQ_CFG0_1,
	mmDCORE0_MME_QM_CQ_CFG0_2,
	mmDCORE0_MME_QM_CQ_CFG0_3,
	mmDCORE0_MME_QM_CQ_CFG0_4,
	mmDCORE0_MME_QM_CQ_PTR_LO_0,
	mmDCORE0_MME_QM_CQ_PTR_HI_0,
	mmDCORE0_MME_QM_CQ_TSIZE_0,
	mmDCORE0_MME_QM_CQ_CTL_0,
	mmDCORE0_MME_QM_CQ_PTR_LO_1,
	mmDCORE0_MME_QM_CQ_PTR_HI_1,
	mmDCORE0_MME_QM_CQ_TSIZE_1,
	mmDCORE0_MME_QM_CQ_CTL_1,
	mmDCORE0_MME_QM_CQ_PTR_LO_2,
	mmDCORE0_MME_QM_CQ_PTR_HI_2,
	mmDCORE0_MME_QM_CQ_TSIZE_2,
	mmDCORE0_MME_QM_CQ_CTL_2,
	mmDCORE0_MME_QM_CQ_PTR_LO_3,
	mmDCORE0_MME_QM_CQ_PTR_HI_3,
	mmDCORE0_MME_QM_CQ_TSIZE_3,
	mmDCORE0_MME_QM_CQ_CTL_3,
	mmDCORE0_MME_QM_CQ_PTR_LO_4,
	mmDCORE0_MME_QM_CQ_PTR_HI_4,
	mmDCORE0_MME_QM_CQ_TSIZE_4,
	mmDCORE0_MME_QM_CQ_CTL_4,
	mmDCORE0_MME_QM_ARB_CFG_0,
	mmDCORE0_MME_QM_ARB_MST_QUIET_PER,
	mmDCORE0_MME_QM_ARB_CHOICE_Q_PUSH,
	mmDCORE0_MME_QM_ARB_WRR_WEIGHT_0,
	mmDCORE0_MME_QM_ARB_WRR_WEIGHT_1,
	mmDCORE0_MME_QM_ARB_WRR_WEIGHT_2,
	mmDCORE0_MME_QM_ARB_WRR_WEIGHT_3,
	mmDCORE0_MME_QM_ARB_BASE_LO,
	mmDCORE0_MME_QM_ARB_BASE_HI,
	mmDCORE0_MME_QM_ARB_MST_SLAVE_EN,
	mmDCORE0_MME_QM_ARB_MST_CRED_INC,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_0,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_1,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_2,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_3,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_4,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_5,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_6,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_7,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_8,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_9,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_10,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_11,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_12,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_13,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_14,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_15,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_16,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_17,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_18,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_19,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_20,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_21,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_22,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_23,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_24,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_25,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_26,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_27,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_28,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_29,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_30,
	mmDCORE0_MME_QM_ARB_MST_CHOICE_PUSH_OFST_31,
	mmDCORE0_MME_QM_ARB_SLV_ID,
	mmDCORE0_MME_QM_ARB_SLV_MASTER_INC_CRED_OFST,
};

static const u32 greco_pb_dcr0_mme_sbte[] = {
	mmDCORE0_MME_SBTEA_BASE,
	mmDCORE0_MME_SBTEA_MSTR_IF_RR_SHRD_HBW_BASE,
	mmDCORE0_MME_SBTEB_BASE,
	mmDCORE0_MME_SBTEB_MSTR_IF_RR_SHRD_HBW_BASE,
	mmDCORE0_MME_SBTEL_BASE,
	mmDCORE0_MME_SBTEL_MSTR_IF_RR_SHRD_HBW_BASE,
};

static const u32 greco_pb_dcr0_mmeif_rtr0[] = {
	mmDCORE0_MMEIF_RTR0_BASE,
	mmDCORE0_MMEIF_RTR0_CTRL_BASE,
	mmDCORE0_MMEIF_RTR0_H3_BASE,
	mmDCORE0_MMEIF_RTR0_MSTR_IF_RR_SHRD_HBW_BASE,
};

static const u32 greco_pb_dcr0_pdma0[] = {
	mmDCORE0_PDMA0_CORE_BASE,
	mmDCORE0_PDMA0_MSTR_IF_RR_SHRD_HBW_BASE,
	mmDCORE0_PDMA0_QM_BASE,
};

static const u32 greco_pb_dcr0_pdma0_unsecured_regs[] = {
	mmDCORE0_PDMA0_CORE_CTX_WR_COMP_ADDR_HI,
	mmDCORE0_PDMA0_CORE_CTX_WR_COMP_ADDR_LO,
	mmDCORE0_PDMA0_CORE_CTX_WR_COMP_WDATA,
	mmDCORE0_PDMA0_CORE_CTX_SRC_BASE_LO,
	mmDCORE0_PDMA0_CORE_CTX_SRC_BASE_HI,
	mmDCORE0_PDMA0_CORE_CTX_DST_BASE_LO,
	mmDCORE0_PDMA0_CORE_CTX_DST_BASE_HI,
	mmDCORE0_PDMA0_CORE_CTX_SRC_TSIZE_0,
	mmDCORE0_PDMA0_CORE_CTX_SRC_TSIZE_1,
	mmDCORE0_PDMA0_CORE_CTX_SRC_TSIZE_2,
	mmDCORE0_PDMA0_CORE_CTX_SRC_TSIZE_3,
	mmDCORE0_PDMA0_CORE_CTX_SRC_TSIZE_4,
	mmDCORE0_PDMA0_CORE_CTX_SRC_STRIDE_1,
	mmDCORE0_PDMA0_CORE_CTX_SRC_STRIDE_2,
	mmDCORE0_PDMA0_CORE_CTX_SRC_STRIDE_3,
	mmDCORE0_PDMA0_CORE_CTX_SRC_STRIDE_4,
	mmDCORE0_PDMA0_CORE_CTX_DST_TSIZE_0,
	mmDCORE0_PDMA0_CORE_CTX_DST_TSIZE_1,
	mmDCORE0_PDMA0_CORE_CTX_DST_TSIZE_2,
	mmDCORE0_PDMA0_CORE_CTX_DST_TSIZE_3,
	mmDCORE0_PDMA0_CORE_CTX_DST_TSIZE_4,
	mmDCORE0_PDMA0_CORE_CTX_DST_STRIDE_1,
	mmDCORE0_PDMA0_CORE_CTX_DST_STRIDE_2,
	mmDCORE0_PDMA0_CORE_CTX_DST_STRIDE_3,
	mmDCORE0_PDMA0_CORE_CTX_DST_STRIDE_4,
	mmDCORE0_PDMA0_CORE_CTX_COMMIT,
	mmDCORE0_PDMA0_QM_CP_FENCE0_RDATA_0,
	mmDCORE0_PDMA0_QM_CP_FENCE0_RDATA_1,
	mmDCORE0_PDMA0_QM_CP_FENCE0_RDATA_2,
	mmDCORE0_PDMA0_QM_CP_FENCE0_RDATA_3,
	mmDCORE0_PDMA0_QM_CP_FENCE0_RDATA_4,
	mmDCORE0_PDMA0_QM_CP_FENCE1_RDATA_0,
	mmDCORE0_PDMA0_QM_CP_FENCE1_RDATA_1,
	mmDCORE0_PDMA0_QM_CP_FENCE1_RDATA_2,
	mmDCORE0_PDMA0_QM_CP_FENCE1_RDATA_3,
	mmDCORE0_PDMA0_QM_CP_FENCE1_RDATA_4,
	mmDCORE0_PDMA0_QM_CP_FENCE2_RDATA_0,
	mmDCORE0_PDMA0_QM_CP_FENCE2_RDATA_1,
	mmDCORE0_PDMA0_QM_CP_FENCE2_RDATA_2,
	mmDCORE0_PDMA0_QM_CP_FENCE2_RDATA_3,
	mmDCORE0_PDMA0_QM_CP_FENCE2_RDATA_4,
	mmDCORE0_PDMA0_QM_CP_FENCE3_RDATA_0,
	mmDCORE0_PDMA0_QM_CP_FENCE3_RDATA_1,
	mmDCORE0_PDMA0_QM_CP_FENCE3_RDATA_2,
	mmDCORE0_PDMA0_QM_CP_FENCE3_RDATA_3,
	mmDCORE0_PDMA0_QM_CP_FENCE3_RDATA_4,
	mmDCORE0_PDMA0_QM_CP_PRED_0,
	mmDCORE0_PDMA0_QM_CP_PRED_1,
	mmDCORE0_PDMA0_QM_CP_PRED_2,
	mmDCORE0_PDMA0_QM_CP_PRED_3,
	mmDCORE0_PDMA0_QM_CP_PRED_4,
	mmDCORE0_PDMA0_QM_CP_PRED_UPEN_0,
	mmDCORE0_PDMA0_QM_CP_PRED_UPEN_1,
	mmDCORE0_PDMA0_QM_CQ_CFG0_0,
	mmDCORE0_PDMA0_QM_CQ_CFG0_1,
	mmDCORE0_PDMA0_QM_CQ_CFG0_2,
	mmDCORE0_PDMA0_QM_CQ_CFG0_3,
	mmDCORE0_PDMA0_QM_CQ_CFG0_4,
	mmDCORE0_PDMA0_QM_CQ_PTR_LO_0,
	mmDCORE0_PDMA0_QM_CQ_PTR_HI_0,
	mmDCORE0_PDMA0_QM_CQ_TSIZE_0,
	mmDCORE0_PDMA0_QM_CQ_CTL_0,
	mmDCORE0_PDMA0_QM_CQ_PTR_LO_1,
	mmDCORE0_PDMA0_QM_CQ_PTR_HI_1,
	mmDCORE0_PDMA0_QM_CQ_TSIZE_1,
	mmDCORE0_PDMA0_QM_CQ_CTL_1,
	mmDCORE0_PDMA0_QM_CQ_PTR_LO_2,
	mmDCORE0_PDMA0_QM_CQ_PTR_HI_2,
	mmDCORE0_PDMA0_QM_CQ_TSIZE_2,
	mmDCORE0_PDMA0_QM_CQ_CTL_2,
	mmDCORE0_PDMA0_QM_CQ_PTR_LO_3,
	mmDCORE0_PDMA0_QM_CQ_PTR_HI_3,
	mmDCORE0_PDMA0_QM_CQ_TSIZE_3,
	mmDCORE0_PDMA0_QM_CQ_CTL_3,
	mmDCORE0_PDMA0_QM_CQ_PTR_LO_4,
	mmDCORE0_PDMA0_QM_CQ_PTR_HI_4,
	mmDCORE0_PDMA0_QM_CQ_TSIZE_4,
	mmDCORE0_PDMA0_QM_CQ_CTL_4,
	mmDCORE0_PDMA0_QM_ARB_CFG_0,
	mmDCORE0_PDMA0_QM_ARB_MST_QUIET_PER,
	mmDCORE0_PDMA0_QM_ARB_CHOICE_Q_PUSH,
	mmDCORE0_PDMA0_QM_ARB_WRR_WEIGHT_0,
	mmDCORE0_PDMA0_QM_ARB_WRR_WEIGHT_1,
	mmDCORE0_PDMA0_QM_ARB_WRR_WEIGHT_2,
	mmDCORE0_PDMA0_QM_ARB_WRR_WEIGHT_3,
	mmDCORE0_PDMA0_QM_ARB_BASE_LO,
	mmDCORE0_PDMA0_QM_ARB_BASE_HI,
	mmDCORE0_PDMA0_QM_ARB_MST_SLAVE_EN,
	mmDCORE0_PDMA0_QM_ARB_MST_CRED_INC,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_0,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_1,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_2,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_3,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_4,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_5,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_6,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_7,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_8,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_9,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_10,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_11,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_12,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_13,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_14,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_15,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_16,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_17,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_18,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_19,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_20,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_21,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_22,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_23,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_24,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_25,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_26,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_27,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_28,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_29,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_30,
	mmDCORE0_PDMA0_QM_ARB_MST_CHOICE_PUSH_OFST_31,
	mmDCORE0_PDMA0_QM_ARB_SLV_ID,
	mmDCORE0_PDMA0_QM_ARB_SLV_MASTER_INC_CRED_OFST,
};

static const u32 greco_pb_dcr0_rot[] = {
	mmDCORE0_ROT_BASE,
	mmDCORE0_ROT_MSTR_IF_RR_SHRD_HBW_BASE,
	mmDCORE0_ROT_QM_BASE,
};

static const u32 greco_pb_dcr0_rot_unsecured_regs[] = {
	mmDCORE0_ROT_QM_CP_FENCE0_RDATA_0,
	mmDCORE0_ROT_QM_CP_FENCE0_RDATA_1,
	mmDCORE0_ROT_QM_CP_FENCE0_RDATA_2,
	mmDCORE0_ROT_QM_CP_FENCE0_RDATA_3,
	mmDCORE0_ROT_QM_CP_FENCE0_RDATA_4,
	mmDCORE0_ROT_QM_CP_FENCE1_RDATA_0,
	mmDCORE0_ROT_QM_CP_FENCE1_RDATA_1,
	mmDCORE0_ROT_QM_CP_FENCE1_RDATA_2,
	mmDCORE0_ROT_QM_CP_FENCE1_RDATA_3,
	mmDCORE0_ROT_QM_CP_FENCE1_RDATA_4,
	mmDCORE0_ROT_QM_CP_FENCE2_RDATA_0,
	mmDCORE0_ROT_QM_CP_FENCE2_RDATA_1,
	mmDCORE0_ROT_QM_CP_FENCE2_RDATA_2,
	mmDCORE0_ROT_QM_CP_FENCE2_RDATA_3,
	mmDCORE0_ROT_QM_CP_FENCE2_RDATA_4,
	mmDCORE0_ROT_QM_CP_FENCE3_RDATA_0,
	mmDCORE0_ROT_QM_CP_FENCE3_RDATA_1,
	mmDCORE0_ROT_QM_CP_FENCE3_RDATA_2,
	mmDCORE0_ROT_QM_CP_FENCE3_RDATA_3,
	mmDCORE0_ROT_QM_CP_FENCE3_RDATA_4,
	mmDCORE0_ROT_QM_CP_PRED_0,
	mmDCORE0_ROT_QM_CP_PRED_1,
	mmDCORE0_ROT_QM_CP_PRED_2,
	mmDCORE0_ROT_QM_CP_PRED_3,
	mmDCORE0_ROT_QM_CP_PRED_4,
	mmDCORE0_ROT_QM_CP_PRED_UPEN_0,
	mmDCORE0_ROT_QM_CP_PRED_UPEN_1,
	mmDCORE0_ROT_QM_CQ_CFG0_0,
	mmDCORE0_ROT_QM_CQ_CFG0_1,
	mmDCORE0_ROT_QM_CQ_CFG0_2,
	mmDCORE0_ROT_QM_CQ_CFG0_3,
	mmDCORE0_ROT_QM_CQ_CFG0_4,
	mmDCORE0_ROT_QM_CQ_PTR_LO_0,
	mmDCORE0_ROT_QM_CQ_PTR_HI_0,
	mmDCORE0_ROT_QM_CQ_TSIZE_0,
	mmDCORE0_ROT_QM_CQ_CTL_0,
	mmDCORE0_ROT_QM_CQ_PTR_LO_1,
	mmDCORE0_ROT_QM_CQ_PTR_HI_1,
	mmDCORE0_ROT_QM_CQ_TSIZE_1,
	mmDCORE0_ROT_QM_CQ_CTL_1,
	mmDCORE0_ROT_QM_CQ_PTR_LO_2,
	mmDCORE0_ROT_QM_CQ_PTR_HI_2,
	mmDCORE0_ROT_QM_CQ_TSIZE_2,
	mmDCORE0_ROT_QM_CQ_CTL_2,
	mmDCORE0_ROT_QM_CQ_PTR_LO_3,
	mmDCORE0_ROT_QM_CQ_PTR_HI_3,
	mmDCORE0_ROT_QM_CQ_TSIZE_3,
	mmDCORE0_ROT_QM_CQ_CTL_3,
	mmDCORE0_ROT_QM_CQ_PTR_LO_4,
	mmDCORE0_ROT_QM_CQ_PTR_HI_4,
	mmDCORE0_ROT_QM_CQ_TSIZE_4,
	mmDCORE0_ROT_QM_CQ_CTL_4,
	mmDCORE0_ROT_QM_ARB_CFG_0,
	mmDCORE0_ROT_QM_ARB_MST_QUIET_PER,
	mmDCORE0_ROT_QM_ARB_CHOICE_Q_PUSH,
	mmDCORE0_ROT_QM_ARB_WRR_WEIGHT_0,
	mmDCORE0_ROT_QM_ARB_WRR_WEIGHT_1,
	mmDCORE0_ROT_QM_ARB_WRR_WEIGHT_2,
	mmDCORE0_ROT_QM_ARB_WRR_WEIGHT_3,
	mmDCORE0_ROT_QM_ARB_BASE_LO,
	mmDCORE0_ROT_QM_ARB_BASE_HI,
	mmDCORE0_ROT_QM_ARB_MST_SLAVE_EN,
	mmDCORE0_ROT_QM_ARB_MST_CRED_INC,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_0,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_1,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_2,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_3,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_4,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_5,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_6,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_7,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_8,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_9,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_10,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_11,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_12,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_13,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_14,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_15,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_16,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_17,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_18,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_19,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_20,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_21,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_22,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_23,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_24,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_25,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_26,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_27,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_28,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_29,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_30,
	mmDCORE0_ROT_QM_ARB_MST_CHOICE_PUSH_OFST_31,
	mmDCORE0_ROT_QM_ARB_SLV_ID,
	mmDCORE0_ROT_QM_ARB_SLV_MASTER_INC_CRED_OFST,
	mmDCORE0_ROT_DESC_CONTEXT_ID,
	mmDCORE0_ROT_DESC_IN_IMG_START_ADDR_L,
	mmDCORE0_ROT_DESC_IN_IMG_START_ADDR_H,
	mmDCORE0_ROT_DESC_OUT_IMG_START_ADDR_L,
	mmDCORE0_ROT_DESC_OUT_IMG_START_ADDR_H,
	mmDCORE0_ROT_DESC_CFG,
	mmDCORE0_ROT_DESC_TAN_D,
	mmDCORE0_ROT_DESC_SIN_D_0,
	mmDCORE0_ROT_DESC_SIN_D_1,
	mmDCORE0_ROT_DESC_SIN_D_2,
	mmDCORE0_ROT_DESC_SIN_D_3,
	mmDCORE0_ROT_DESC_SIN_D_4,
	mmDCORE0_ROT_DESC_SIN_D_5,
	mmDCORE0_ROT_DESC_SIN_D_6,
	mmDCORE0_ROT_DESC_COS_D_0,
	mmDCORE0_ROT_DESC_COS_D_1,
	mmDCORE0_ROT_DESC_COS_D_2,
	mmDCORE0_ROT_DESC_COS_D_3,
	mmDCORE0_ROT_DESC_COS_D_4,
	mmDCORE0_ROT_DESC_COS_D_5,
	mmDCORE0_ROT_DESC_COS_D_6,
	mmDCORE0_ROT_DESC_IN_IMG,
	mmDCORE0_ROT_DESC_IN_STRIDE,
	mmDCORE0_ROT_DESC_IN_STRIPE,
	mmDCORE0_ROT_DESC_IN_CENTER,
	mmDCORE0_ROT_DESC_OUT_IMG,
	mmDCORE0_ROT_DESC_OUT_STRIDE,
	mmDCORE0_ROT_DESC_OUT_STRIPE,
	mmDCORE0_ROT_DESC_OUT_CENTER,
	mmDCORE0_ROT_DESC_BACKGROUND,
	mmDCORE0_ROT_DESC_CPL_MSG_EN,
	mmDCORE0_ROT_DESC_IDLE_STATE,
	mmDCORE0_ROT_DESC_CPL_MSG_ADDR,
	mmDCORE0_ROT_DESC_CPL_MSG_DATA,
	mmDCORE0_ROT_DESC_X_I_START_OFFSET,
	mmDCORE0_ROT_DESC_X_I_START_OFFSET_FLIP,
	mmDCORE0_ROT_DESC_X_I_FIRST,
	mmDCORE0_ROT_DESC_Y_I_FIRST,
	mmDCORE0_ROT_DESC_Y_I_END,
	mmDCORE0_ROT_DESC_IN_IMG_LAST_ADDR_L,
	mmDCORE0_ROT_DESC_IN_IMG_LAST_ADDR_H,
	mmDCORE0_ROT_DESC_OUT_STRIPE_SIZE,
	mmDCORE0_ROT_DESC_RSB_CFG_0,
	mmDCORE0_ROT_DESC_RSB_PAD_VAL,
	mmDCORE0_ROT_DESC_OWM_CFG,
	mmDCORE0_ROT_DESC_PUSH_DESC,
};

static const u32 greco_pb_dcr0_sram0[] = {
	mmDCORE0_SRAM_BANK_0_BASE,
	mmDCORE0_SRAM_DBG_CNT_0_N_HBW_DBG_CNT_BASE,
	mmDCORE0_SRAM_RTR_0_BASE,
};

static const u32 greco_pb_dcr0_sm[] = {
	mmDCORE0_SYNC_MNGR_GLBL_BASE,
	mmDCORE0_SYNC_MNGR_MSTR_IF_RR_SHRD_HBW_BASE,
};

static const u32 greco_pb_dcr0_tpc0[] = {
	mmDCORE0_TPC0_QM_BASE,
	mmDCORE0_TPC0_CFG_BASE,
	mmDCORE0_TPC0_MSTR_IF_RR_SHRD_HBW_BASE,
};

static const u32 greco_pb_dcr0_tpc0_unsecured_regs[] = {
	mmDCORE0_TPC0_QM_CP_FENCE0_RDATA_0,
	mmDCORE0_TPC0_QM_CP_FENCE0_RDATA_1,
	mmDCORE0_TPC0_QM_CP_FENCE0_RDATA_2,
	mmDCORE0_TPC0_QM_CP_FENCE0_RDATA_3,
	mmDCORE0_TPC0_QM_CP_FENCE0_RDATA_4,
	mmDCORE0_TPC0_QM_CP_FENCE1_RDATA_0,
	mmDCORE0_TPC0_QM_CP_FENCE1_RDATA_1,
	mmDCORE0_TPC0_QM_CP_FENCE1_RDATA_2,
	mmDCORE0_TPC0_QM_CP_FENCE1_RDATA_3,
	mmDCORE0_TPC0_QM_CP_FENCE1_RDATA_4,
	mmDCORE0_TPC0_QM_CP_FENCE2_RDATA_0,
	mmDCORE0_TPC0_QM_CP_FENCE2_RDATA_1,
	mmDCORE0_TPC0_QM_CP_FENCE2_RDATA_2,
	mmDCORE0_TPC0_QM_CP_FENCE2_RDATA_3,
	mmDCORE0_TPC0_QM_CP_FENCE2_RDATA_4,
	mmDCORE0_TPC0_QM_CP_FENCE3_RDATA_0,
	mmDCORE0_TPC0_QM_CP_FENCE3_RDATA_1,
	mmDCORE0_TPC0_QM_CP_FENCE3_RDATA_2,
	mmDCORE0_TPC0_QM_CP_FENCE3_RDATA_3,
	mmDCORE0_TPC0_QM_CP_FENCE3_RDATA_4,
	mmDCORE0_TPC0_QM_CP_PRED_0,
	mmDCORE0_TPC0_QM_CP_PRED_1,
	mmDCORE0_TPC0_QM_CP_PRED_2,
	mmDCORE0_TPC0_QM_CP_PRED_3,
	mmDCORE0_TPC0_QM_CP_PRED_4,
	mmDCORE0_TPC0_QM_CP_PRED_UPEN_0,
	mmDCORE0_TPC0_QM_CP_PRED_UPEN_1,
	mmDCORE0_TPC0_QM_CQ_CFG0_0,
	mmDCORE0_TPC0_QM_CQ_CFG0_1,
	mmDCORE0_TPC0_QM_CQ_CFG0_2,
	mmDCORE0_TPC0_QM_CQ_CFG0_3,
	mmDCORE0_TPC0_QM_CQ_CFG0_4,
	mmDCORE0_TPC0_QM_CQ_PTR_LO_0,
	mmDCORE0_TPC0_QM_CQ_PTR_HI_0,
	mmDCORE0_TPC0_QM_CQ_TSIZE_0,
	mmDCORE0_TPC0_QM_CQ_CTL_0,
	mmDCORE0_TPC0_QM_CQ_PTR_LO_1,
	mmDCORE0_TPC0_QM_CQ_PTR_HI_1,
	mmDCORE0_TPC0_QM_CQ_TSIZE_1,
	mmDCORE0_TPC0_QM_CQ_CTL_1,
	mmDCORE0_TPC0_QM_CQ_PTR_LO_2,
	mmDCORE0_TPC0_QM_CQ_PTR_HI_2,
	mmDCORE0_TPC0_QM_CQ_TSIZE_2,
	mmDCORE0_TPC0_QM_CQ_CTL_2,
	mmDCORE0_TPC0_QM_CQ_PTR_LO_3,
	mmDCORE0_TPC0_QM_CQ_PTR_HI_3,
	mmDCORE0_TPC0_QM_CQ_TSIZE_3,
	mmDCORE0_TPC0_QM_CQ_CTL_3,
	mmDCORE0_TPC0_QM_CQ_PTR_LO_4,
	mmDCORE0_TPC0_QM_CQ_PTR_HI_4,
	mmDCORE0_TPC0_QM_CQ_TSIZE_4,
	mmDCORE0_TPC0_QM_CQ_CTL_4,
	mmDCORE0_TPC0_CFG_QM_SYNC_OBJECT_MESSAGE,
	mmDCORE0_TPC0_CFG_QM_SYNC_OBJECT_ADDR,
	mmDCORE0_TPC0_CFG_QM_KERNEL_BASE_ADDRESS_LOW,
	mmDCORE0_TPC0_CFG_QM_KERNEL_BASE_ADDRESS_HIGH,
	mmDCORE0_TPC0_CFG_QM_TID_BASE_DIM_0,
	mmDCORE0_TPC0_CFG_QM_TID_SIZE_DIM_0,
	mmDCORE0_TPC0_CFG_QM_TID_BASE_DIM_1,
	mmDCORE0_TPC0_CFG_QM_TID_SIZE_DIM_1,
	mmDCORE0_TPC0_CFG_QM_TID_BASE_DIM_2,
	mmDCORE0_TPC0_CFG_QM_TID_SIZE_DIM_2,
	mmDCORE0_TPC0_CFG_QM_TID_BASE_DIM_3,
	mmDCORE0_TPC0_CFG_QM_TID_SIZE_DIM_3,
	mmDCORE0_TPC0_CFG_QM_TID_BASE_DIM_4,
	mmDCORE0_TPC0_CFG_QM_TID_SIZE_DIM_4,
	mmDCORE0_TPC0_CFG_QM_KERNEL_CONFIG,
	mmDCORE0_TPC0_CFG_QM_KERNEL_ID,
	mmDCORE0_TPC0_CFG_QM_POWER_LOOP,
	mmDCORE0_TPC0_CFG_LUT_FUNC32_BASE2_ADDR_LO,
	mmDCORE0_TPC0_CFG_LUT_FUNC32_BASE2_ADDR_HI,
	mmDCORE0_TPC0_CFG_LUT_FUNC64_BASE2_ADDR_LO,
	mmDCORE0_TPC0_CFG_LUT_FUNC64_BASE2_ADDR_HI,
	mmDCORE0_TPC0_CFG_LUT_FUNC128_BASE2_ADDR_LO,
	mmDCORE0_TPC0_CFG_LUT_FUNC128_BASE2_ADDR_HI,
	mmDCORE0_TPC0_CFG_LUT_FUNC256_BASE2_ADDR_LO,
	mmDCORE0_TPC0_CFG_LUT_FUNC256_BASE2_ADDR_HI,
	mmDCORE0_TPC0_CFG_ROUND_CSR,
	mmDCORE0_TPC0_CFG_CONV_ROUND_CSR,
	mmDCORE0_TPC0_CFG_SEMAPHORE,
	mmDCORE0_TPC0_CFG_LFSR_POLYNOM,
	mmDCORE0_TPC0_CFG_STATUS,
	mmDCORE0_TPC0_CFG_SM_BASE_ADDRESS_HIGH,
	mmDCORE0_TPC0_CFG_TPC_CMD,
	mmDCORE0_TPC0_CFG_TPC_EXECUTE,
	mmDCORE0_TPC0_CFG_TPC_DCACHE_L0CD,
	mmDCORE0_TPC0_CFG_ICACHE_BASE_ADDERESS_LOW,
	mmDCORE0_TPC0_CFG_ICACHE_BASE_ADDERESS_HIGH,
	mmDCORE0_TPC0_CFG_RD_RATE_LIMIT,
	mmDCORE0_TPC0_CFG_WR_RATE_LIMIT,
	mmDCORE0_TPC0_CFG_TPC_INTR_CAUSE,
	mmDCORE0_TPC0_CFG_TPC_INTR_MASK,
	mmDCORE0_TPC0_CFG_LUT_FUNC32_BASE_ADDR_LO,
	mmDCORE0_TPC0_CFG_LUT_FUNC32_BASE_ADDR_HI,
	mmDCORE0_TPC0_CFG_LUT_FUNC64_BASE_ADDR_LO,
	mmDCORE0_TPC0_CFG_LUT_FUNC64_BASE_ADDR_HI,
	mmDCORE0_TPC0_CFG_LUT_FUNC128_BASE_ADDR_LO,
	mmDCORE0_TPC0_CFG_LUT_FUNC128_BASE_ADDR_HI,
	mmDCORE0_TPC0_CFG_LUT_FUNC256_BASE_ADDR_LO,
	mmDCORE0_TPC0_CFG_LUT_FUNC256_BASE_ADDR_HI,
	mmDCORE0_TPC0_CFG_KERNEL_KERNEL_CONFIG,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_0,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_1,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_2,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_3,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_4,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_5,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_6,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_7,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_8,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_9,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_10,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_11,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_12,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_13,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_14,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_15,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_16,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_17,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_18,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_19,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_20,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_21,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_22,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_23,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_24,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_25,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_26,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_27,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_28,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_29,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_30,
	mmDCORE0_TPC0_CFG_KERNEL_SRF_31,
	mmDCORE0_TPC0_CFG_TPC_SB_L0CD,
	mmDCORE0_TPC0_QM_ARB_CFG_0,
	mmDCORE0_TPC0_QM_ARB_MST_QUIET_PER,
	mmDCORE0_TPC0_QM_ARB_CHOICE_Q_PUSH,
	mmDCORE0_TPC0_QM_ARB_WRR_WEIGHT_0,
	mmDCORE0_TPC0_QM_ARB_WRR_WEIGHT_1,
	mmDCORE0_TPC0_QM_ARB_WRR_WEIGHT_2,
	mmDCORE0_TPC0_QM_ARB_WRR_WEIGHT_3,
	mmDCORE0_TPC0_QM_ARB_BASE_LO,
	mmDCORE0_TPC0_QM_ARB_BASE_HI,
	mmDCORE0_TPC0_QM_ARB_MST_SLAVE_EN,
	mmDCORE0_TPC0_QM_ARB_MST_CRED_INC,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_0,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_1,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_2,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_3,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_4,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_5,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_6,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_7,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_8,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_9,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_10,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_11,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_12,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_13,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_14,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_15,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_16,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_17,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_18,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_19,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_20,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_21,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_22,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_23,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_24,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_25,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_26,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_27,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_28,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_29,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_30,
	mmDCORE0_TPC0_QM_ARB_MST_CHOICE_PUSH_OFST_31,
	mmDCORE0_TPC0_QM_ARB_SLV_ID,
	mmDCORE0_TPC0_QM_ARB_SLV_MASTER_INC_CRED_OFST,
};

static const u32 greco_pb_dcr0_tpc0_ktensor_unsecured_regs[] = {
	mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_BASE_ADDR_LOW,
	mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_BASE_ADDR_HIGH,
	mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_PADDING_VALUE,
	mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_TENSOR_CONFIG,
	mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_DIM_0_SIZE,
	mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_DIM_0_STRIDE,
	mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_DIM_1_SIZE,
	mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_DIM_1_STRIDE,
	mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_DIM_2_SIZE,
	mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_DIM_2_STRIDE,
	mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_DIM_3_SIZE,
	mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_DIM_3_STRIDE,
	mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_DIM_4_SIZE,
	mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_DIM_4_STRIDE,
};

static const u32 greco_pb_dcr0_tpc0_qtensor_unsecured_regs[] = {
	mmDCORE0_TPC0_CFG_QM_TENSOR_0_BASE_ADDR_LOW,
	mmDCORE0_TPC0_CFG_QM_TENSOR_0_BASE_ADDR_HIGH,
	mmDCORE0_TPC0_CFG_QM_TENSOR_0_PADDING_VALUE,
	mmDCORE0_TPC0_CFG_QM_TENSOR_0_TENSOR_CONFIG,
	mmDCORE0_TPC0_CFG_QM_TENSOR_0_DIM_0_SIZE,
	mmDCORE0_TPC0_CFG_QM_TENSOR_0_DIM_0_STRIDE,
	mmDCORE0_TPC0_CFG_QM_TENSOR_0_DIM_1_SIZE,
	mmDCORE0_TPC0_CFG_QM_TENSOR_0_DIM_1_STRIDE,
	mmDCORE0_TPC0_CFG_QM_TENSOR_0_DIM_2_SIZE,
	mmDCORE0_TPC0_CFG_QM_TENSOR_0_DIM_2_STRIDE,
	mmDCORE0_TPC0_CFG_QM_TENSOR_0_DIM_3_SIZE,
	mmDCORE0_TPC0_CFG_QM_TENSOR_0_DIM_3_STRIDE,
	mmDCORE0_TPC0_CFG_QM_TENSOR_0_DIM_4_SIZE,
	mmDCORE0_TPC0_CFG_QM_TENSOR_0_DIM_4_STRIDE,
};

static const u32 greco_pb_dcr0_tpcif_rtr0[] = {
	mmDCORE0_TPCIF_RTR0_BASE,
	mmDCORE0_TPCIF_RTR0_CTRL_BASE,
	mmDCORE0_TPCIF_RTR0_H3_BASE,
	mmDCORE0_TPCIF_RTR0_MSTR_IF_RR_SHRD_HBW_BASE,
};

static const u32 greco_pb_dcr0_vdec0[] = {
	mmDCORE0_VDEC0_BRDG_CTRL_BASE,
	mmDCORE0_VDEC0_CTRL_BASE,
};

static const u32 greco_pb_dcr0_vsi_wrap[] = {
	mmDCORE0_VSI_WRAP_BASE,
	mmDCORE0_VSI_WRAP_MSTR_IF_RR_SHRD_HBW_BASE,
};

static const u32 greco_pb_mstr_if[] = {
	mmJT_MSTR_IF_RR_SHRD_HBW_BASE,
	mmSMI_MSTR_IF_RR_SHRD_HBW_BASE,
	mmI2C_S_MSTR_IF_RR_SHRD_HBW_BASE,
};

static const u32 greco_pb_pcie[] = {
	mmPCIE_AUX_BASE,
	mmPCIE_CORE_BASE,
	mmPCIE_ELBI_RR_MSTR_IF_RR_SHRD_HBW_BASE,
	mmPCIE_LBW_RR_MSTR_IF_RR_SHRD_HBW_BASE,
	mmPCIE_MSTR_RR_MSTR_IF_RR_SHRD_HBW_BASE,
	mmPCIE_PHY_BASE,
	mmPCIE_WRAP_BASE,
};

static const u32 greco_pb_pmmu[] = {
	mmPMMU_HBW_MMU_BASE,
	mmPMMU_HBW_MSTR_IF_RR_SHRD_HBW_BASE,
	mmPMMU_HBW_STLB_BASE,
	mmPMMU_PIF_BASE,
	mmPMMU_DDR_PLL_CTRL_BASE,
};

static const u32 greco_pb_psoc_arc[] = {
	mmPSOC_ARC0_AUX_BASE,
	mmPSOC_ARC0_CFG_BASE,
	mmPSOC_ARC0_MSTR_IF_RR_SHRD_HBW_BASE,
};

static const u32 greco_pb_psoc_plls[] = {
	mmPSOC_BANK_PLL_CTRL_BASE,
	mmPSOC_MESH_PLL_CTRL_BASE,
	mmPSOC_DDR_PLL_CTRL_BASE,
	mmPSOC_MMU_PLL_CTRL_BASE,
	mmPSOC_PCI_PLL_CTRL_BASE,
	mmPSOC_SIF_PLL_CTRL_BASE,
};

/* Arbitrary separation from psoc_plls due to compiler frame-size warning */
static const u32 greco_pb_psoc_engines_pll[] = {
	mmPSOC_MME_PLL_CTRL_BASE,
	mmPSOC_TPC_PLL_CTRL_BASE,
	mmPSOC_VIDEO_PLL_CTRL_BASE,
};

static const u32 greco_psoc_avs[] = {
	mmPSOC_AVS_BASE,
};

static const u32 greco_psoc_btl[] = {
	mmPSOC_BTL_BASE,
};

static const u32 greco_psoc_cs_trace[] = {
	mmPSOC_CS_TRACE_BASE,
};

static const u32 greco_psoc_efuse[] = {
	mmPSOC_EFUSE_BASE,
	mmPSOC_DFT_EFUSE_BASE,
};

static const u32 greco_psoc_glbl_conf[] = {
	mmPSOC_GLOBAL_CONF_BASE,
};

static const u32 greco_psoc_mstr_if_rr[] = {
	mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE,
};

static const u32 greco_psoc_pid[] = {
	mmPSOC_PID_BASE,
};

static const u32 greco_psoc_pwm0[] = {
	mmPSOC_PWM0_BASE,
};

static const u32 greco_psoc_rpm0[] = {
	mmPSOC_RPM_0_BASE,
};

static const u32 greco_pb_venc[] = {
	mmVENC_CTRL0_BASE,
	mmVENC_CTRL1_BASE,
	mmVENC_VL2C_CTRL_BASE,
};

static const u32 greco_pb_xif[] = {
	mmXIF_BASE,
};

static void greco_init_pb_cpu(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_cpu);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_cpu)];

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev, greco_pb_cpu, glbl_sec, 0, array_size);
}

static void greco_init_pb_dcon_base(struct hl_device *hdev)
{
	u32 offset = mmDCON1_BASE - mmDCON0_BASE;
	int i, array_size = ARRAY_SIZE(greco_pb_dcon0_base);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcon0_base)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < 4 ; i++)
		hl_config_glbl_sec(hdev, greco_pb_dcon0_base,
				glbl_sec, i * offset, array_size);
}

static void greco_init_pb_dcon_hbw(struct hl_device *hdev)
{
	u32 dcon_offset = mmDCON1_BASE - mmDCON0_BASE;
	u32 rtr_offset = mmDCON0_HBW_RTR_IF1_RTR_H3_BASE -
			 mmDCON0_HBW_RTR_IF0_RTR_H3_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcon0_hbw_if0);
	struct hl_block_glbl_sec
			glbl_sec[ARRAY_SIZE(greco_pb_dcon0_hbw_if0)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < 4 ; i++)
		for (j = 0 ; j < 2 ; j++)
			hl_config_glbl_sec(hdev, greco_pb_dcon0_hbw_if0,
					glbl_sec, i * dcon_offset +
					j * rtr_offset, array_size);
}

static void greco_init_pb_dcon_lbw(struct hl_device *hdev)
{
	u32 offset = mmDCON1_BASE - mmDCON0_BASE;
	int i, array_size = ARRAY_SIZE(greco_pb_dcon0_lbw);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcon0_lbw)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < 4 ; i++)
		hl_config_glbl_sec(hdev, greco_pb_dcon0_lbw,
				glbl_sec, i * offset, array_size);
}

static void greco_init_pb_ddma(struct hl_device *hdev)
{
	int i, block_array_size = ARRAY_SIZE(greco_pb_dcr0_ddma);
	int regs_array_size = ARRAY_SIZE(greco_pb_dcr0_ddma_unsecured_regs);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_ddma)];

	hl_secure_block(hdev, glbl_sec, block_array_size);
	hl_unsecure_registers(hdev, greco_pb_dcr0_ddma_unsecured_regs,
			regs_array_size, 0, greco_pb_dcr0_ddma, glbl_sec,
			block_array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++)
		hl_config_glbl_sec(hdev, greco_pb_dcr0_ddma,
				glbl_sec, i * DCORE_OFFSET, block_array_size);
}

static void greco_init_pb_ddr(struct hl_device *hdev)
{
	u32 ddr_offset = mmDCORE0_DDR1_MISC_BASE - mmDCORE0_DDR0_MISC_BASE;
	/* ddr_dcore_offset is not the standard DCORE offset */
	u32 ddr_dcore_offset = mmDCORE1_DDR0_MISC_BASE -
						mmDCORE0_DDR0_MISC_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_ddr0);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_ddr0)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 4 ; j++)
			hl_config_glbl_sec(hdev, greco_pb_dcr0_ddr0, glbl_sec,
					i * ddr_dcore_offset + j * ddr_offset,
					array_size);
	}
}

static void greco_init_pb_hif(struct hl_device *hdev)
{
	u32 hif_offset = mmDCORE0_HIF1_BASE - mmDCORE0_HIF0_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_hif0);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_hif0)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 2 ; j++)
			hl_config_glbl_sec(hdev, greco_pb_dcr0_hif0, glbl_sec,
					i * DCORE_OFFSET + j * hif_offset,
					array_size);
	}
}

static void greco_init_pb_hmmu(struct hl_device *hdev)
{
	u32 hmmu_offset = mmDCORE0_HMMU1_MMU_BASE - mmDCORE0_HMMU0_MMU_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_hmmu0);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_hmmu0)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 2 ; j++)
			hl_config_glbl_sec(hdev, greco_pb_dcr0_hmmu0, glbl_sec,
					i * DCORE_OFFSET + j * hmmu_offset,
					array_size);
	}
}

static void greco_init_pb_hmmu_axi(struct hl_device *hdev)
{
	u32 hmmu_offset = mmDCORE0_HMMU1_MMU_BASE - mmDCORE0_HMMU0_MMU_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_hmmu0_axi);
	struct hl_block_glbl_sec
		glbl_sec[ARRAY_SIZE(greco_pb_dcr0_hmmu0_axi)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 2 ; j++)
			hl_config_glbl_sec(hdev, greco_pb_dcr0_hmmu0_axi,
					glbl_sec, i * DCORE_OFFSET +
					j * hmmu_offset, array_size);
	}
}

static void greco_init_pb_kdma(struct hl_device *hdev)
{
	int i, array_size = ARRAY_SIZE(greco_pb_dcr0_kdma);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_kdma)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++)
		hl_config_glbl_sec(hdev, greco_pb_dcr0_kdma, glbl_sec,
				i * DCORE_OFFSET, array_size);
}

static void greco_init_pb_mme(struct hl_device *hdev)
{
	int i, blocks_array_size = ARRAY_SIZE(greco_pb_dcr0_mme);
	int regs_array_size = ARRAY_SIZE(greco_pb_dcr0_mme_unsecured_regs);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_mme)];

	if (!hdev->mme_mask)
		return;

	hl_secure_block(hdev, glbl_sec, blocks_array_size);
	hl_unsecure_registers(hdev, greco_pb_dcr0_mme_unsecured_regs,
			regs_array_size, 0, greco_pb_dcr0_mme, glbl_sec,
			blocks_array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++)
		hl_config_glbl_sec(hdev, greco_pb_dcr0_mme,
				glbl_sec, i * DCORE_OFFSET, blocks_array_size);
}

static void greco_init_pb_mme_qm(struct hl_device *hdev)
{
	int blocks_array_size = ARRAY_SIZE(greco_pb_dcr0_mme_qm);
	int regs_array_size = ARRAY_SIZE(greco_pb_dcr0_mme_qm_unsecured_regs);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_mme_qm)];
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hw_queue_properties *queue_props;

	if (!hdev->mme_mask)
		return;

	hl_secure_block(hdev, glbl_sec, blocks_array_size);
	hl_unsecure_registers(hdev, greco_pb_dcr0_mme_qm_unsecured_regs, regs_array_size, 0,
				greco_pb_dcr0_mme_qm, glbl_sec, blocks_array_size);

	/* DCORE0_MME_QM */
	hl_config_glbl_sec(hdev, greco_pb_dcr0_mme_qm, glbl_sec, 0x0, blocks_array_size);

	/* DCORE1_MME_QM */
	queue_props = &prop->hw_queues_props[GRECO_QUEUE_ID_DCORE1_MME_0_0];
	if (!queue_props->slave)
		hl_config_glbl_sec(hdev, greco_pb_dcr0_mme_qm, glbl_sec, DCORE_OFFSET,
					blocks_array_size);
}

static void greco_init_pb_mme_sbte(struct hl_device *hdev)
{
	int i, array_size = ARRAY_SIZE(greco_pb_dcr0_mme_sbte);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_mme_sbte)];

	if (!hdev->mme_mask)
		return;

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++)
		hl_config_glbl_sec(hdev, greco_pb_dcr0_mme_sbte,
				glbl_sec, i * DCORE_OFFSET, array_size);
}

static void greco_init_pb_mmeif_rtr(struct hl_device *hdev)
{
	u32 mme_offset = mmDCORE0_MMEIF_RTR1_BASE - mmDCORE0_MMEIF_RTR0_BASE;
	/* mme_dcore_offset is not the standard DCORE offset */
	u32 mme_dcore_offset = mmDCORE1_MMEIF_RTR0_BASE -
						mmDCORE0_MMEIF_RTR0_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_mmeif_rtr0);
	struct hl_block_glbl_sec
		glbl_sec[ARRAY_SIZE(greco_pb_dcr0_mmeif_rtr0)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 4 ; j++)
			hl_config_glbl_sec(hdev, greco_pb_dcr0_mmeif_rtr0,
					glbl_sec, i * mme_dcore_offset +
					j * mme_offset, array_size);
	}
}

static void greco_init_pb_pdma(struct hl_device *hdev)
{
	u32 pdma_offset = mmDCORE0_PDMA1_CORE_BASE - mmDCORE0_PDMA0_CORE_BASE;
	int i, j, block_array_size = ARRAY_SIZE(greco_pb_dcr0_pdma0);
	int regs_array_size = ARRAY_SIZE(greco_pb_dcr0_pdma0_unsecured_regs);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_pdma0)];

	hl_secure_block(hdev, glbl_sec, block_array_size);
	hl_unsecure_registers(hdev, greco_pb_dcr0_pdma0_unsecured_regs,
			regs_array_size, 0, greco_pb_dcr0_pdma0, glbl_sec,
			block_array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 2 ; j++)
			hl_config_glbl_sec(hdev, greco_pb_dcr0_pdma0, glbl_sec,
					i * DCORE_OFFSET + j * pdma_offset,
					block_array_size);
	}
}

static void greco_init_pb_rot(struct hl_device *hdev)
{
	int i, block_array_size = ARRAY_SIZE(greco_pb_dcr0_rot);
	int regs_array_size = ARRAY_SIZE(greco_pb_dcr0_rot_unsecured_regs);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_rot)];

	hl_secure_block(hdev, glbl_sec, block_array_size);
	hl_unsecure_registers(hdev, greco_pb_dcr0_rot_unsecured_regs,
			regs_array_size, 0, greco_pb_dcr0_rot, glbl_sec,
			block_array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		if (hdev->rotator_mask & (1 << i))
			hl_config_glbl_sec(hdev, greco_pb_dcr0_rot,
					glbl_sec, i * DCORE_OFFSET,
					block_array_size);
	}
}

static void greco_init_pb_sram(struct hl_device *hdev)
{
	u32 sram_offset = mmDCORE0_SRAM_BANK_1_BASE - mmDCORE0_SRAM_BANK_0_BASE;
	/* sram_dcore_offset is not the standard DCORE offset */
	u32 sram_dcore_offset = mmDCORE1_SRAM_BANK_0_BASE -
						mmDCORE0_SRAM_BANK_0_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_sram0);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_sram0)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 16 ; j++)
			hl_config_glbl_sec(hdev, greco_pb_dcr0_sram0,
					glbl_sec, i * sram_dcore_offset +
					j * sram_offset, array_size);
	}
}

static void greco_init_pb_sync_mngr(struct hl_device *hdev)
{
	int i, array_size = ARRAY_SIZE(greco_pb_dcr0_sm);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_sm)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++)
		hl_config_glbl_sec(hdev, greco_pb_dcr0_sm,
				glbl_sec, i * DCORE_OFFSET, array_size);
}

static void greco_init_pb_tpc(struct hl_device *hdev)
{
	u32 tpc_offset = mmDCORE0_TPC1_CFG_BASE - mmDCORE0_TPC0_CFG_BASE;
	u32 kernel_tensor_stride = mmDCORE0_TPC0_CFG_KERNEL_TENSOR_1_BASE -
					mmDCORE0_TPC0_CFG_KERNEL_TENSOR_0_BASE;
	u32 qm_tensor_stride = mmDCORE0_TPC0_CFG_QM_TENSOR_1_BASE -
					mmDCORE0_TPC0_CFG_QM_TENSOR_0_BASE;
	u32 stride;
	int i, j, block_array_size = ARRAY_SIZE(greco_pb_dcr0_tpc0);
	int regs_array_size = ARRAY_SIZE(greco_pb_dcr0_tpc0_unsecured_regs);
	int ktensor_regs_array_size =
		ARRAY_SIZE(greco_pb_dcr0_tpc0_ktensor_unsecured_regs);
	int qtensor_regs_array_size =
			ARRAY_SIZE(greco_pb_dcr0_tpc0_qtensor_unsecured_regs);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_tpc0)];

	hl_secure_block(hdev, glbl_sec, block_array_size);
	hl_unsecure_registers(hdev, greco_pb_dcr0_tpc0_unsecured_regs,
			regs_array_size, 0, greco_pb_dcr0_tpc0, glbl_sec,
			block_array_size);

	/* Unsecure all TPC kernel tensors */
	for (i = 0 ; i < TPC_NUM_OF_KERNEL_TENSORS ; i++)
		hl_unsecure_registers(hdev,
				greco_pb_dcr0_tpc0_ktensor_unsecured_regs,
				ktensor_regs_array_size,
				i * kernel_tensor_stride, greco_pb_dcr0_tpc0,
				glbl_sec, block_array_size);

	/* Unsecure all TPC QM tensors */
	for (i = 0 ; i < TPC_NUM_OF_QM_TENSORS ; i++)
		hl_unsecure_registers(hdev,
				greco_pb_dcr0_tpc0_qtensor_unsecured_regs,
				qtensor_regs_array_size, i * qm_tensor_stride,
				greco_pb_dcr0_tpc0, glbl_sec, block_array_size);

	/* unsecure all 32 TPC QM SRF regs */
	stride = mmDCORE0_TPC0_CFG_QM_SRF_1 - mmDCORE0_TPC0_CFG_QM_SRF_0;
	for (i = 0 ; i < 32 ; i++)
		hl_unsecure_register(hdev, mmDCORE0_TPC0_CFG_QM_SRF_0,
				i * stride, greco_pb_dcr0_tpc0, glbl_sec,
				block_array_size);

	/* unsecure the 4 TPC LOCK VALUE regs */
	stride = mmDCORE0_TPC0_CFG_TPC_LOCK_VALUE_1 -
			mmDCORE0_TPC0_CFG_TPC_LOCK_VALUE_0;
	for (i = 0 ; i < 4 ; i++)
		hl_unsecure_register(hdev, mmDCORE0_TPC0_CFG_TPC_LOCK_VALUE_0,
				i * stride, greco_pb_dcr0_tpc0, glbl_sec,
				block_array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < NUM_OF_TPC_PER_DCORE ; j++) {
			if (hdev->asic_prop.tpc_enabled_mask &
					(1 << (i * NUM_OF_TPC_PER_DCORE + j)))
				hl_config_glbl_sec(hdev, greco_pb_dcr0_tpc0,
						glbl_sec, i * DCORE_OFFSET +
						j * tpc_offset,
						block_array_size);
		}
	}
}

static void greco_init_pb_tpcif_rtr(struct hl_device *hdev)
{
	u32 tpc_offset = mmDCORE0_TPCIF_RTR1_BASE - mmDCORE0_TPCIF_RTR0_BASE;
	/* tpcif RTR dcore offset is not the standard DCORE offset */
	u32 tpc_dcore_offset = mmDCORE1_TPCIF_RTR0_BASE -
						mmDCORE0_TPCIF_RTR0_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_tpcif_rtr0);
	struct hl_block_glbl_sec
		glbl_sec[ARRAY_SIZE(greco_pb_dcr0_tpcif_rtr0)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 4 ; j++)
			hl_config_glbl_sec(hdev, greco_pb_dcr0_tpcif_rtr0,
					glbl_sec, i * tpc_dcore_offset +
					j * tpc_offset, array_size);
	}
}

static void greco_init_pb_vdec(struct hl_device *hdev)
{
	struct asic_fixed_properties *asic_prop = &hdev->asic_prop;
	u32 vdec_offset = mmDCORE0_VDEC1_CTRL_BASE - mmDCORE0_VDEC0_CTRL_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_vdec0);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_vdec0)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < NUM_OF_DEC_PER_DCORE ; j++) {
			if (asic_prop->decoder_enabled_mask &
					(1 << (i * NUM_OF_DEC_PER_DCORE + j)))
				hl_config_glbl_sec(hdev, greco_pb_dcr0_vdec0,
						glbl_sec, i * DCORE_OFFSET +
						j * vdec_offset, array_size);
		}
	}
}

static void greco_init_pb_vsi_wrap(struct hl_device *hdev)
{
	int i, array_size = ARRAY_SIZE(greco_pb_dcr0_vsi_wrap);
	bool encoder_enabled = !!(hdev->asic_prop.decoder_enabled_mask & 0x3E0);
	struct hl_block_glbl_sec
			glbl_sec[ARRAY_SIZE(greco_pb_dcr0_vsi_wrap)];

	if (!encoder_enabled)
		return;

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++)
		hl_config_glbl_sec(hdev, greco_pb_dcr0_vsi_wrap,
				glbl_sec, i * DCORE_OFFSET, array_size);
}

static void greco_init_pb_mstr_if(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_mstr_if);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_mstr_if)];

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev, greco_pb_mstr_if, glbl_sec, 0, array_size);
}

static void greco_init_pb_pcie(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_pcie);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_pcie)];

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev, greco_pb_pcie, glbl_sec, 0, array_size);
}

static void greco_init_pb_pmmu(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_pmmu);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_pmmu)];

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev, greco_pb_pmmu, glbl_sec, 0, array_size);
}

static void greco_init_pb_psoc_arc(struct hl_device *hdev)
{
	u32 offset = mmPSOC_ARC1_AUX_BASE - mmPSOC_ARC0_AUX_BASE;
	int i, array_size = ARRAY_SIZE(greco_pb_psoc_arc);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_psoc_arc)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < 2 ; i++)
		hl_config_glbl_sec(hdev, greco_pb_psoc_arc,
				glbl_sec, i * offset, array_size);
}

static void greco_init_pb_psoc_plls(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_psoc_plls);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_psoc_plls)];

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev, greco_pb_psoc_plls, glbl_sec, 0, array_size);
}

static void greco_init_pb_psoc_engines_pll(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_psoc_engines_pll);
	struct hl_block_glbl_sec
			glbl_sec[ARRAY_SIZE(greco_pb_psoc_engines_pll)];

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev, greco_pb_psoc_engines_pll, glbl_sec, 0,
			array_size);
}

static void greco_init_pb_psoc_pwm(struct hl_device *hdev)
{
	u32 offset = mmPSOC_PWM1_BASE - mmPSOC_PWM0_BASE;
	int i, array_size = ARRAY_SIZE(greco_psoc_pwm0);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_psoc_pwm0)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < 2 ; i++)
		hl_config_glbl_sec(hdev, greco_psoc_pwm0,
				glbl_sec, i * offset, array_size);
}

static void greco_init_pb_psoc_rpm(struct hl_device *hdev)
{
	u32 offset = mmPSOC_RPM_1_BASE - mmPSOC_RPM_0_BASE;
	int i, array_size = ARRAY_SIZE(greco_psoc_rpm0);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_psoc_rpm0)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < 2 ; i++)
		hl_config_glbl_sec(hdev, greco_psoc_rpm0,
				glbl_sec, i * offset, array_size);
}

static void greco_init_pb_psoc_avs(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_psoc_avs);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_psoc_avs)];

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev, greco_psoc_avs, glbl_sec, 0, array_size);
}

static void greco_init_pb_psoc_btl(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_psoc_btl);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_psoc_btl)];

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev, greco_psoc_btl, glbl_sec, 0, array_size);
}

static void greco_init_pb_psoc_cs_trace(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_psoc_cs_trace);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_psoc_cs_trace)];

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev, greco_psoc_cs_trace, glbl_sec, 0, array_size);
}

static void greco_init_pb_psoc_efuse(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_psoc_efuse);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_psoc_efuse)];

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev, greco_psoc_efuse, glbl_sec, 0, array_size);
}

static void greco_init_pb_psoc_glbl_conf(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_psoc_glbl_conf);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_psoc_glbl_conf)];

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev, greco_psoc_glbl_conf, glbl_sec, 0, array_size);
}

static void greco_init_pb_psoc_mstr_if_rr(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_psoc_mstr_if_rr);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_psoc_mstr_if_rr)];

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev,
			greco_psoc_mstr_if_rr, glbl_sec, 0, array_size);
}

static void greco_init_pb_psoc_pid(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_psoc_pid);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_psoc_pid)];

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev, greco_psoc_pid, glbl_sec, 0, array_size);
}

static void greco_init_pb_sm_objs(struct hl_device *hdev)
{
	u32 sec_entry;
	u32 array_base = greco_pb_dcr0_sm_objs.mm_block_base_addr +
			 greco_pb_dcr0_sm_objs.glbl_sec_offset;
	int i, j, glbl_sec_array_len = greco_pb_dcr0_sm_objs.glbl_sec_length;

	u32 sec_array[GRECO_ATYPICAL_BLOCK_GLBL_SEC_MAX_LEN];

	/* Secure the block */
	memset(sec_array, 0, glbl_sec_array_len * sizeof(u32));

	/* Fill the glbl sec registers */

	/* 2048 SOB_OBJs skipping the driver reserved objects */
	for (j = i = GRECO_NUM_RSRVD_SOBS;
			i < DCORE_NUM_OF_SOB ; i++, j++)
		UNSET_GLBL_SEC_BIT(sec_array, j);

	/* 512 MON_PAY ADDR_L skipping first 64 of them */
	for (i = GRECO_NUM_RSRVD_COMPLETION_Q_MONITORS, j += i;
			i < DCORE_NUM_OF_MONITORS ; i++, j++)
		UNSET_GLBL_SEC_BIT(sec_array, j);

	/* 512 MON_PAY ADDR_H skipping first 64 of them */
	for (i = GRECO_NUM_RSRVD_COMPLETION_Q_MONITORS, j += i;
			i < DCORE_NUM_OF_MONITORS ; i++, j++)
		UNSET_GLBL_SEC_BIT(sec_array, j);

	/* 512 MON_PAY DATA skipping first 64 of them */
	for (i = GRECO_NUM_RSRVD_COMPLETION_Q_MONITORS, j += i;
			i < DCORE_NUM_OF_MONITORS ; i++, j++)
		UNSET_GLBL_SEC_BIT(sec_array, j);

	/* 512 MON_ARM skipping first 64 of them */
	for (i = GRECO_NUM_RSRVD_COMPLETION_Q_MONITORS, j += i;
			i < DCORE_NUM_OF_MONITORS ; i++, j++)
		UNSET_GLBL_SEC_BIT(sec_array, j);

	/* 512 MON_STATUS skipping first 64 of them */
	for (i = GRECO_NUM_RSRVD_COMPLETION_Q_MONITORS, j += i;
			i < DCORE_NUM_OF_MONITORS ; i++, j++)
		UNSET_GLBL_SEC_BIT(sec_array, j);

	/* Unsecure selected Dcore0 registers */
	for (i = 0 ; i < glbl_sec_array_len ; i++) {
		sec_entry = array_base + i * sizeof(u32);
		WREG32(sec_entry, sec_array[i]);
	}

	/* Unsecure all Dcore1 registers */
	memset(sec_array, -1, glbl_sec_array_len * sizeof(u32));

	for (i = 0 ; i < glbl_sec_array_len ; i++) {
		sec_entry = DCORE_OFFSET + array_base + i * sizeof(u32);
		WREG32(sec_entry, sec_array[i]);
	}
}

static void greco_init_pb_venc(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_venc);
	bool encoder_enabled = !!(hdev->asic_prop.decoder_enabled_mask & 0x3E0);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_venc)];

	if (!encoder_enabled)
		return;

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev, greco_pb_venc, glbl_sec, 0, array_size);
}

static void greco_init_pb_xif(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_xif);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_xif)];

	hl_secure_block(hdev, glbl_sec, array_size);
	hl_config_glbl_sec(hdev, greco_pb_xif, glbl_sec, 0, array_size);
}


/**
 * greco_init_protection_bits - Initialize protection bits of specific registers
 *
 * @hdev: pointer to hl_device structure
 *
 * All protection bits are 1 by default, means not protected. Need to set to 0
 * each bit that belongs to a protected register.
 *
 */
static void greco_init_protection_bits(struct hl_device *hdev)
{
	dev_dbg(hdev->dev, "Configure protection bits\n");

	greco_init_pb_cpu(hdev);
	greco_init_pb_dcon_base(hdev);
	greco_init_pb_dcon_hbw(hdev);
	greco_init_pb_dcon_lbw(hdev);
	greco_init_pb_ddma(hdev);
	greco_init_pb_ddr(hdev);
	greco_init_pb_hif(hdev);
	greco_init_pb_hmmu(hdev);
	greco_init_pb_hmmu_axi(hdev);
	greco_init_pb_kdma(hdev);
	greco_init_pb_mme(hdev);
	greco_init_pb_mme_qm(hdev);
	greco_init_pb_mme_sbte(hdev);
	greco_init_pb_mmeif_rtr(hdev);
	greco_init_pb_pdma(hdev);
	greco_init_pb_rot(hdev);
	greco_init_pb_sram(hdev);
	greco_init_pb_sync_mngr(hdev);
	greco_init_pb_tpc(hdev);
	greco_init_pb_tpcif_rtr(hdev);
	greco_init_pb_mstr_if(hdev);
	greco_init_pb_pcie(hdev);
	greco_init_pb_pmmu(hdev);
	greco_init_pb_psoc_arc(hdev);
	greco_init_pb_psoc_avs(hdev);
	greco_init_pb_psoc_btl(hdev);
	greco_init_pb_psoc_cs_trace(hdev);
	greco_init_pb_psoc_efuse(hdev);
	greco_init_pb_psoc_glbl_conf(hdev);
	greco_init_pb_psoc_mstr_if_rr(hdev);
	greco_init_pb_psoc_pid(hdev);
	greco_init_pb_psoc_plls(hdev);
	greco_init_pb_psoc_engines_pll(hdev);
	greco_init_pb_psoc_pwm(hdev);
	greco_init_pb_psoc_rpm(hdev);
	greco_init_pb_sm_objs(hdev);
	greco_init_pb_vdec(hdev);
	greco_init_pb_venc(hdev);
	greco_init_pb_vsi_wrap(hdev);
	greco_init_pb_xif(hdev);

	dev_dbg(hdev->dev, "Configure protection bits finished\n");
}

static void greco_init_hbw_range_registers(struct hl_device *hdev, u32 rr_base)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	/* Configure HBW RR:
	 * Short range covers the General area from 0x7F_F800_0000 to
	 *  0x0x7F_FBFF_FFFF and will use the first set of the
	 *  HBW_SEC_RANGE_MIN/MAX_SHORT regs.
	 *  In case of SRAM binning this start address should also cover the
	 *  upper half of the SRAM address which precedes the STM base address.
	 * AR and AW HIT Default values are good for us.
	 * Access towards PCIe is enabled by default.
	 * First 512MB DRAM  and host mem are protected by the MMU
	 */
	u64 hbw_range_min_short[] = {
		prop->sram_binning ?
			SRAM_BASE_ADDR + (SRAM_SIZE >> 1) : STM_FLASH_BASE_ADDR,
	};

	u64 hbw_range_max_short[] = {
		CFG_BASE,
	};

	int i, reg_offset;

	/* General area */
	for (i = 0 ; i < ARRAY_SIZE(hbw_range_min_short) ; i++) {
		reg_offset = i * sizeof(u32);

		WREG32(rr_base +
			RR_HBW_SEC_RANGE_MIN_SHORT_0_OFFSET + reg_offset,
			lower_32_bits(hbw_range_min_short[i] >> 12) & U16_MAX);
		WREG32(rr_base +
			RR_HBW_SEC_RANGE_MAX_SHORT_0_OFFSET + reg_offset,
			lower_32_bits(hbw_range_max_short[i] >> 12) & U16_MAX);
	}
}

static void greco_init_lbw_range_registers(struct hl_device *hdev, u32 rr_base)
{
	/* Up to 4 26bit-address regs */
	u64 lbw_range_min[] = {
		0x7FFE000000ull, /* DCORE0_TPC0_ROM_TABLE_L */
	};

	u64 lbw_range_max[] = {
		0x7FFFFFFFFFull, /* EOF DCORE1_TPC4_EML_CS */
	};

	/* Up to 14 14bit-address regs */
	u64 lbw_range_min_short[] = {
		0x7FFC800000ull, /* GIC */
		0x7FFC440000ull, /* PSOC_I2C_M0 */
	};

	u64 lbw_range_max_short[] = {
		0x7FFCFC0000ull + 64 * 1024, /* EOF DCORE1_DDR3_PHY */
		0x7FFC6F0000ull + 40 * 1024, /* EOF DCORE1_DDR3_MC1 */
	};

	int i, reg_offset;

	/* Configure LBW RR:
	 * Long range covers:
	 *   0x7FFE000000 - 0x7FFFFFFFFF
	 * Short ranges cover:
	 *   0x7FFC800000 - 0x7FFCFD0000
	 *   0x7FFC440000 - 0x7FFE80A000
	 */
	for (i = 0 ; i < ARRAY_SIZE(lbw_range_min) ; i++) {
		reg_offset = i * sizeof(u32);

		WREG32(rr_base + RR_LBW_SEC_RANGE_MIN_0_OFFSET + reg_offset,
			lower_32_bits(lbw_range_min[i]) & 0x3FFFFFF);
		WREG32(rr_base + RR_LBW_SEC_RANGE_MAX_0_OFFSET + reg_offset,
			lower_32_bits(lbw_range_max[i]) & 0x3FFFFFF);
	}

	for (i = 0 ; i < ARRAY_SIZE(lbw_range_min_short) ; i++) {
		reg_offset = i * sizeof(u32);

		WREG32(rr_base +
			RR_LBW_SEC_RANGE_MIN_SHORT_0_OFFSET + reg_offset,
			lower_32_bits(lbw_range_min_short[i] >> 12) & 0x3FFF);
		WREG32(rr_base +
			RR_LBW_SEC_RANGE_MAX_SHORT_0_OFFSET + reg_offset,
			lower_32_bits(lbw_range_max_short[i] >> 12) & 0x3FFF);
	}
}

static void greco_init_hbw_rr_array(struct hl_device *hdev, int array_size_rr,
				u32 *array_rr, u64 array_mask)
{
	int i;

	for (i = 0 ; i < array_size_rr ; i++) {
		if (array_mask & (1ULL << i))
			greco_init_hbw_range_registers(hdev, array_rr[i]);
	}
}

static void greco_init_lbw_rr_array(struct hl_device *hdev, int array_size_rr,
				u32 *array_rr, u64 array_mask)
{
	int i;

	for (i = 0 ; i < array_size_rr ; i++) {
		if (array_mask & (1ULL << i))
			greco_init_lbw_range_registers(hdev, array_rr[i]);
	}
}

static void greco_init_mmu_range_registers(struct hl_device *hdev,
					u32 block_address)
{
	int i, j, idx;
	/* Up to 8 entries, with unused entries having invalid range*/
	u64 protected_region_min[8] = {
		DRAM_PHYS_BASE,
		0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000, 0x1000,
	};
	u64 protcted_region_max[8] = {
		DRAM_BASE_ADDR_USER,
		0, 0, 0, 0, 0, 0, 0,
	};

	for (i = 0 ; i < ARRAY_SIZE(greco_rr_blocks_mmu) ; i++) {
		for (j = 0 ; j < ARRAY_SIZE(protected_region_min) ; j++) {
			idx = j * sizeof(u32);

			WREG32(greco_rr_blocks_mmu[i] +
				MMU_RR_SEC_MIN_49_32_0_OFFSET + idx,
				upper_32_bits(protected_region_min[j]));
			WREG32(greco_rr_blocks_mmu[i] +
				MMU_RR_SEC_MIN_31_0_0_OFFSET + idx,
				lower_32_bits(protected_region_min[j]));

			WREG32(greco_rr_blocks_mmu[i] +
				MMU_RR_SEC_MAX_49_32_0_OFFSET + idx,
				upper_32_bits(protcted_region_max[j]));
			WREG32(greco_rr_blocks_mmu[i] +
				MMU_RR_SEC_MAX_31_0_0_OFFSET + idx,
				lower_32_bits(protcted_region_max[j]));
		}
		WREG32(greco_rr_blocks_mmu[i] + MMU_DDR_RANGE_REG_ENABLE_OFFSET,
			0xFF);
	}
}

static void greco_init_range_registers(struct hl_device *hdev)
{
	int i;

	/* TPC */
	greco_init_hbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_hbw_tpc),
				greco_rr_blocks_hbw_tpc,
				hdev->asic_prop.tpc_enabled_mask);
	greco_init_lbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_lbw_tpc),
				greco_rr_blocks_lbw_tpc,
				hdev->asic_prop.tpc_enabled_mask);

	/* MME */
	greco_init_hbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_priv_hbw_mme),
				greco_rr_blocks_priv_hbw_mme,
				hdev->mme_mask ? U64_MAX : 0);
	greco_init_lbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_priv_lbw_mme),
				greco_rr_blocks_priv_lbw_mme,
				hdev->mme_mask ? U64_MAX : 0);

	/* DEC */
	greco_init_hbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_hbw_dec),
				greco_rr_blocks_hbw_dec, U64_MAX);
	greco_init_lbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_lbw_dec),
				greco_rr_blocks_lbw_dec, U64_MAX);

	/* ENC */
	greco_init_hbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_hbw_enc),
				greco_rr_blocks_hbw_enc, U64_MAX);
	greco_init_lbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_lbw_enc),
				greco_rr_blocks_lbw_enc, U64_MAX);

	/* DMMU */
	greco_init_hbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_hbw_dmmu),
				greco_rr_blocks_hbw_dmmu, U64_MAX);

	/* CPU */
	greco_init_hbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_hbw_cpu),
				greco_rr_blocks_hbw_cpu, U64_MAX);
	greco_init_lbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_lbw_cpu),
				greco_rr_blocks_lbw_cpu, U64_MAX);

	/* DDMA */
	greco_init_hbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_hbw_ddma),
				greco_rr_blocks_hbw_ddma, U64_MAX);
	greco_init_lbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_lbw_ddma),
				greco_rr_blocks_lbw_ddma, U64_MAX);

	/* PCOC and ARC */
	greco_init_hbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_hbw_psoc_arc),
				greco_rr_blocks_hbw_psoc_arc, U64_MAX);
	greco_init_lbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_lbw_psoc_arc),
				greco_rr_blocks_lbw_psoc_arc, U64_MAX);

	/* ROT */
	greco_init_hbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_hbw_rot),
				greco_rr_blocks_hbw_rot, U64_MAX);
	greco_init_lbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_lbw_rot),
				greco_rr_blocks_lbw_rot, U64_MAX);

	/* PCIe */
	greco_init_hbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_hbw_pcie),
				greco_rr_blocks_hbw_pcie, U64_MAX);
	greco_init_lbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_lbw_pcie),
				greco_rr_blocks_lbw_pcie, U64_MAX);

	/* ELBI */
	greco_init_lbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_lbw_elbi),
				greco_rr_blocks_lbw_elbi, U64_MAX);

	/* PMMU */
	greco_init_hbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_hbw_pmmu),
				greco_rr_blocks_hbw_pmmu, U64_MAX);
	greco_init_lbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_lbw_pmmu),
				greco_rr_blocks_lbw_pmmu, U64_MAX);

	/* KDMA, PDMA and SM */
	greco_init_hbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_hbw_kdma_pdma_sm),
				greco_rr_blocks_hbw_kdma_pdma_sm, U64_MAX);
	greco_init_lbw_rr_array(hdev,
				ARRAY_SIZE(greco_rr_blocks_lbw_kdma_pdma_sm),
				greco_rr_blocks_lbw_kdma_pdma_sm, U64_MAX);

	/* MMU */
	for (i = 0 ; i < ARRAY_SIZE(greco_rr_blocks_mmu) ; i++)
		greco_init_mmu_range_registers(hdev, greco_rr_blocks_mmu[i]);
}

/**
 * greco_init_security - Initialize security model
 *
 * @hdev: pointer to hl_device structure
 *
 * Initialize the security model of the device
 * That includes range registers and protection bit per register.
 */
void greco_init_security(struct hl_device *hdev)
{
	if (!hdev->security_enable)
		return;

	greco_init_range_registers(hdev);
	greco_init_protection_bits(hdev);
}

/*
 * Ack PB security errors
 */

void greco_pb_print_security_errors(struct hl_device *hdev, u32 block_addr,
		u32 cause, u32 offended_addr)
{
	int i = 0;
	const char *error_format =
		"Security error at block 0x%x, offending address 0x%x\n"
		"Cause 0x%x: %s %s %s %s %s %s %s %s\n";
	char *mcause[8] = {"Unknown", "", "", "", "", "", "", "" };

	if (!cause)
		return;

	if (cause & SPECIAL_GLBL_ERR_CAUSE_APB_PRIV_RD)
		mcause[i++] = "APB_PRIV_RD";

	if (cause & SPECIAL_GLBL_ERR_CAUSE_APB_SEC_RD)
		mcause[i++] = "APB_SEC_RD";

	if (cause & SPECIAL_GLBL_ERR_CAUSE_APB_UNMAPPED_RD)
		mcause[i++] = "APB_UNMAPPED_RD";

	if (cause & SPECIAL_GLBL_ERR_CAUSE_APB_PRIV_WR)
		mcause[i++] = "APB_PRIV_WR";

	if (cause & SPECIAL_GLBL_ERR_CAUSE_APB_SEC_WR)
		mcause[i++] = "APB_SEC_WR";

	if (cause & SPECIAL_GLBL_ERR_CAUSE_APB_UNMAPPED_WR)
		mcause[i++] = "APB_UNMAPPED_WR";

	if (cause & SPECIAL_GLBL_ERR_CAUSE_EXT_SEC_WR)
		mcause[i++] = "EXT_SEC_WR";

	if (cause & SPECIAL_GLBL_ERR_CAUSE_EXT_UNMAPPED_WR)
		mcause[i++] = "APB_EXT_UNMAPPED_WR";

	dev_err_ratelimited(hdev->dev, error_format, block_addr, offended_addr,
			cause, mcause[0], mcause[1], mcause[2], mcause[3],
			mcause[4], mcause[5], mcause[6], mcause[7]);
}

static void greco_ack_pb_cpu(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_cpu);

	if (!(hdev->fw_components & FW_TYPE_PREBOOT_CPU))
		return;

	hl_ack_pb_security_violations(hdev, greco_pb_cpu, 0, array_size);
}

static void greco_ack_pb_dcon_base(struct hl_device *hdev)
{
	u32 offset = mmDCON1_BASE - mmDCON0_BASE;
	int i, array_size = ARRAY_SIZE(greco_pb_dcon0_base);

	/* check all dcon blocks */
	for (i = 0 ; i < 4 ; i++)
		hl_ack_pb_security_violations(hdev, greco_pb_dcon0_base,
						i * offset, array_size);
}

static void greco_ack_pb_dcon_hbw(struct hl_device *hdev)
{
	u32 dcon_offset = mmDCON1_BASE - mmDCON0_BASE;
	u32 rtr_offset = mmDCON0_HBW_RTR_IF1_RTR_H3_BASE -
			 mmDCON0_HBW_RTR_IF0_RTR_H3_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcon0_hbw_if0);

	/* check all dcon hbw blocks */
	for (i = 0 ; i < 4 ; i++)
		for (j = 0 ; j < 2 ; j++)
			hl_ack_pb_security_violations(hdev,
					greco_pb_dcon0_hbw_if0,
					i * dcon_offset + j * rtr_offset,
					array_size);
}

static void greco_ack_pb_dcon_lbw(struct hl_device *hdev)
{
	u32 offset = mmDCON1_BASE - mmDCON0_BASE;
	int i, array_size = ARRAY_SIZE(greco_pb_dcon0_lbw);

	/* check all dcon lbw blocks */
	for (i = 0 ; i < 4 ; i++)
		hl_ack_pb_security_violations(hdev, greco_pb_dcon0_lbw,
						i * offset, array_size);
}

static void greco_ack_pb_ddma(struct hl_device *hdev)
{
	int i, array_size = ARRAY_SIZE(greco_pb_dcr0_ddma);

	/* check all ddma blocks */
	for (i = 0 ; i < NUM_OF_DCORES ; i++)
		hl_ack_pb_security_violations(hdev, greco_pb_dcr0_ddma,
				i * DCORE_OFFSET, array_size);
}

static void greco_ack_pb_ddr(struct hl_device *hdev)
{
	u32 ddr_offset = mmDCORE0_DDR1_MISC_BASE - mmDCORE0_DDR0_MISC_BASE;
	/* ddr_dcore_offset is not the standard DCORE offset */
	u32 ddr_dcore_offset = mmDCORE1_DDR0_MISC_BASE -
						mmDCORE0_DDR0_MISC_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_ddr0);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 4 ; j++)
			hl_ack_pb_security_violations(hdev,
					greco_pb_dcr0_ddr0,
					i * ddr_dcore_offset + j * ddr_offset,
					array_size);
	}
}

static void greco_ack_pb_hif(struct hl_device *hdev)
{
	u32 hif_offset = mmDCORE0_HIF1_BASE - mmDCORE0_HIF0_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_hif0);

	/* ack all blocks */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 2 ; j++)
			hl_ack_pb_security_violations(hdev, greco_pb_dcr0_hif0,
					i * DCORE_OFFSET + j * hif_offset,
					array_size);
	}
}

static void greco_ack_pb_hmmu(struct hl_device *hdev)
{
	u32 hmmu_offset = mmDCORE0_HMMU1_MMU_BASE - mmDCORE0_HMMU0_MMU_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_hmmu0);

	if (!hdev->mmu_enable)
		return;

	/* ack all blocks */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 2 ; j++)
			hl_ack_pb_security_violations(hdev, greco_pb_dcr0_hmmu0,
					i * DCORE_OFFSET + j * hmmu_offset,
					array_size);
	}
}

static void greco_ack_pb_hmmu_axi(struct hl_device *hdev)
{
	u32 hmmu_offset = mmDCORE0_HMMU1_MMU_BASE - mmDCORE0_HMMU0_MMU_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_hmmu0_axi);

	if (!hdev->mmu_enable)
		return;

	/* Ack all blocks */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 2 ; j++)
			hl_ack_pb_security_violations(hdev,
					greco_pb_dcr0_hmmu0_axi,
					i * DCORE_OFFSET + j * hmmu_offset,
					array_size);
	}
}

static void greco_ack_pb_kdma(struct hl_device *hdev)
{
	int i, array_size = ARRAY_SIZE(greco_pb_dcr0_kdma);

	for (i = 0 ; i < NUM_OF_DCORES ; i++)
		hl_ack_pb_security_violations(hdev, greco_pb_dcr0_kdma,
				i * DCORE_OFFSET, array_size);
}

static void greco_ack_pb_mme(struct hl_device *hdev)
{
	int i, array_size = ARRAY_SIZE(greco_pb_dcr0_mme);

	if (!hdev->mme_mask)
		return;

	for (i = 0 ; i < NUM_OF_DCORES ; i++)
		hl_ack_pb_security_violations(hdev, greco_pb_dcr0_mme,
						i * DCORE_OFFSET, array_size);
}

static void greco_ack_pb_mme_qm(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int array_size = ARRAY_SIZE(greco_pb_dcr0_mme_qm);
	struct hw_queue_properties *queue_props;

	if (!hdev->mme_mask)
		return;

	/* DCORE0_MME_QM */
	hl_ack_pb_security_violations(hdev, greco_pb_dcr0_mme_qm, 0x0, array_size);

	/* DCORE1_MME_QM */
	queue_props = &prop->hw_queues_props[GRECO_QUEUE_ID_DCORE1_MME_0_0];
	if (!queue_props->slave)
		hl_ack_pb_security_violations(hdev, greco_pb_dcr0_mme_qm, DCORE_OFFSET, array_size);
}

static void greco_ack_pb_mme_sbte(struct hl_device *hdev)
{
	int i, array_size = ARRAY_SIZE(greco_pb_dcr0_mme_sbte);

	if (!hdev->mme_mask)
		return;

	for (i = 0 ; i < NUM_OF_DCORES ; i++)
		hl_ack_pb_security_violations(hdev, greco_pb_dcr0_mme_sbte,
						i * DCORE_OFFSET, array_size);
}

static void greco_ack_pb_mmeif_rtr(struct hl_device *hdev)
{
	u32 mme_offset = mmDCORE0_MMEIF_RTR1_BASE - mmDCORE0_MMEIF_RTR0_BASE;
	/* mme_dcore_offset is not the standard DCORE offset */
	u32 mme_dcore_offset = mmDCORE1_MMEIF_RTR0_BASE -
						mmDCORE0_MMEIF_RTR0_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_mmeif_rtr0);

	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 4 ; j++)
			hl_ack_pb_security_violations(hdev,
					greco_pb_dcr0_mmeif_rtr0,
					i * mme_dcore_offset + j * mme_offset,
					array_size);
	}
}

static void greco_ack_pb_pdma(struct hl_device *hdev)
{
	u32 pdma_offset = mmDCORE0_PDMA1_CORE_BASE - mmDCORE0_PDMA0_CORE_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_pdma0);

	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 2 ; j++)
			hl_ack_pb_security_violations(hdev, greco_pb_dcr0_pdma0,
					i * DCORE_OFFSET + j * pdma_offset,
					array_size);
	}
}

static void greco_ack_pb_rot(struct hl_device *hdev)
{
	int i, array_size = ARRAY_SIZE(greco_pb_dcr0_rot);

	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		if (hdev->rotator_mask & (1 << i))
			hl_ack_pb_security_violations(hdev, greco_pb_dcr0_rot,
					i * DCORE_OFFSET, array_size);
	}
}

static void greco_ack_pb_sram(struct hl_device *hdev)
{
	u32 sram_offset = mmDCORE0_SRAM_BANK_1_BASE - mmDCORE0_SRAM_BANK_0_BASE;
	/* sram_dcore_offset is not the standard DCORE offset */
	u32 sram_dcore_offset = mmDCORE1_SRAM_BANK_0_BASE -
						mmDCORE0_SRAM_BANK_0_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_sram0);

	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 16 ; j++)
			hl_ack_pb_security_violations(hdev, greco_pb_dcr0_sram0,
					i * sram_dcore_offset + j * sram_offset,
					array_size);
	}
}

static void greco_ack_pb_sync_mngr(struct hl_device *hdev)
{
	int i, array_size = ARRAY_SIZE(greco_pb_dcr0_sm);


	for (i = 0 ; i < NUM_OF_DCORES ; i++)
		hl_ack_pb_security_violations(hdev, greco_pb_dcr0_sm,
						i * DCORE_OFFSET, array_size);
}

static void greco_ack_pb_tpc(struct hl_device *hdev)
{
	u32 tpc_offset = mmDCORE0_TPC1_CFG_BASE - mmDCORE0_TPC0_CFG_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_tpc0);

	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < NUM_OF_TPC_PER_DCORE ; j++) {
			if (hdev->asic_prop.tpc_enabled_mask &
					(1 << (i * NUM_OF_TPC_PER_DCORE + j)))
				hl_ack_pb_security_violations(hdev,
						greco_pb_dcr0_tpc0,
						i * DCORE_OFFSET +
						j * tpc_offset,
						array_size);
		}
	}
}

static void greco_ack_pb_tpcif_rtr(struct hl_device *hdev)
{
	u32 tpc_offset = mmDCORE0_TPCIF_RTR1_BASE - mmDCORE0_TPCIF_RTR0_BASE;
	/* tpcif RTR dcore offset is not the standard DCORE offset */
	u32 tpc_dcore_offset = mmDCORE1_TPCIF_RTR0_BASE -
						mmDCORE0_TPCIF_RTR0_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_tpcif_rtr0);

	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < 4 ; j++)
			hl_ack_pb_security_violations(hdev,
					greco_pb_dcr0_tpcif_rtr0,
					i * tpc_dcore_offset + j * tpc_offset,
					array_size);
	}
}

static void greco_ack_pb_vdec(struct hl_device *hdev)
{
	struct asic_fixed_properties *asic_prop = &hdev->asic_prop;
	u32 vdec_offset = mmDCORE0_VDEC1_CTRL_BASE - mmDCORE0_VDEC0_CTRL_BASE;
	int i, j, array_size = ARRAY_SIZE(greco_pb_dcr0_vdec0);
	struct hl_block_glbl_sec glbl_sec[ARRAY_SIZE(greco_pb_dcr0_vdec0)];

	hl_secure_block(hdev, glbl_sec, array_size);

	/* Fill all blocks with the same configuration */
	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < NUM_OF_DEC_PER_DCORE ; j++) {
			if (asic_prop->decoder_enabled_mask &
					(1 << (i * NUM_OF_DEC_PER_DCORE + j)))
				hl_config_glbl_sec(hdev, greco_pb_dcr0_vdec0,
						glbl_sec, i * DCORE_OFFSET +
						j * vdec_offset, array_size);
		}
	}
}

static void greco_ack_pb_vsi_wrap(struct hl_device *hdev)
{
	int i, array_size = ARRAY_SIZE(greco_pb_dcr0_vsi_wrap);
	bool encoder_enabled = !!(hdev->asic_prop.decoder_enabled_mask & 0x3E0);

	if (!encoder_enabled)
		return;

	for (i = 0 ; i < NUM_OF_DCORES ; i++)
		hl_ack_pb_security_violations(hdev, greco_pb_dcr0_vsi_wrap,
						i * DCORE_OFFSET, array_size);
}

static void greco_ack_pb_mstr_if(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_mstr_if);

	hl_ack_pb_security_violations(hdev, greco_pb_mstr_if, 0, array_size);
}

static void greco_ack_pb_pcie(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_pcie);

	hl_ack_pb_security_violations(hdev, greco_pb_pcie, 0, array_size);
}

static void greco_ack_pb_pmmu(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_pmmu);

	if (!hdev->mmu_enable)
		return;

	hl_ack_pb_security_violations(hdev, greco_pb_pmmu, 0, array_size);
}

static void greco_ack_pb_psoc_arc(struct hl_device *hdev)
{
	u32 offset = mmPSOC_ARC1_AUX_BASE - mmPSOC_ARC0_AUX_BASE;
	int i, array_size = ARRAY_SIZE(greco_pb_psoc_arc);

	for (i = 0 ; i < 2 ; i++)
		hl_ack_pb_security_violations(hdev, greco_pb_psoc_arc,
						i * offset, array_size);
}

static void greco_ack_pb_psoc_plls(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_psoc_plls);

	hl_ack_pb_security_violations(hdev, greco_pb_psoc_plls, 0, array_size);
}

static void greco_ack_pb_psoc_engines_pll(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_psoc_engines_pll);

	hl_ack_pb_security_violations(hdev, greco_pb_psoc_engines_pll, 0,
			array_size);
}

static void greco_ack_pb_psoc_pwm(struct hl_device *hdev)
{
	u32 offset = mmPSOC_PWM1_BASE - mmPSOC_PWM0_BASE;
	int i, array_size = ARRAY_SIZE(greco_psoc_pwm0);

	for (i = 0 ; i < 2 ; i++)
		hl_ack_pb_security_violations(hdev, greco_psoc_pwm0, i * offset,
				array_size);
}

static void greco_ack_pb_psoc_rpm(struct hl_device *hdev)
{
	u32 offset = mmPSOC_RPM_1_BASE - mmPSOC_RPM_0_BASE;
	int i, array_size = ARRAY_SIZE(greco_psoc_rpm0);

	for (i = 0 ; i < 2 ; i++)
		hl_ack_pb_security_violations(hdev, greco_psoc_rpm0,
						i * offset, array_size);
}

static void greco_ack_pb_psoc_avs(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_psoc_avs);

	hl_ack_pb_security_violations(hdev, greco_psoc_avs, 0, array_size);
}

static void greco_ack_pb_psoc_btl(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_psoc_btl);

	hl_ack_pb_security_violations(hdev, greco_psoc_btl, 0, array_size);
}

static void greco_ack_pb_psoc_cs_trace(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_psoc_cs_trace);

	hl_ack_pb_security_violations(hdev, greco_psoc_cs_trace, 0, array_size);
}

static void greco_ack_pb_psoc_efuse(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_psoc_efuse);

	hl_ack_pb_security_violations(hdev, greco_psoc_efuse, 0, array_size);
}

static void greco_ack_pb_psoc_glbl_conf(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_psoc_glbl_conf);

	hl_ack_pb_security_violations(hdev, greco_psoc_glbl_conf, 0,
			array_size);
}

static void greco_ack_pb_psoc_mstr_if_rr(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_psoc_mstr_if_rr);

	hl_ack_pb_security_violations(hdev, greco_psoc_mstr_if_rr, 0,
			array_size);
}

static void greco_ack_pb_psoc_pid(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_psoc_pid);

	hl_ack_pb_security_violations(hdev, greco_psoc_pid, 0, array_size);
}

static void greco_ack_pb_sm_objs(struct hl_device *hdev)
{
	/* No reports in this module */
}

static void greco_ack_pb_venc(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_venc);
	bool encoder_enabled = !!(hdev->asic_prop.decoder_enabled_mask & 0x3E0);

	if (!encoder_enabled)
		return;

	hl_ack_pb_security_violations(hdev, greco_pb_venc, 0, array_size);
}

static void greco_ack_pb_xif(struct hl_device *hdev)
{
	int array_size = ARRAY_SIZE(greco_pb_xif);

	hl_ack_pb_security_violations(hdev, greco_pb_xif, 0, array_size);
}

/**
 * greco_ack_protection_bits_errors - scan all blocks having protection bits and
 * and for every protection error found, display the appropriate error message
 * and clear the error.
 *
 * @hdev: pointer to hl_device structure
 *
 * All protection bits are 1 by default, means not protected. Need to set to 0
 * each bit that belongs to a protected register.
 *
 */
void greco_ack_protection_bits_errors(struct hl_device *hdev)
{
	greco_ack_pb_cpu(hdev);
	greco_ack_pb_dcon_base(hdev);
	greco_ack_pb_dcon_hbw(hdev);
	greco_ack_pb_dcon_lbw(hdev);
	greco_ack_pb_ddma(hdev);
	greco_ack_pb_ddr(hdev);
	greco_ack_pb_hif(hdev);
	greco_ack_pb_hmmu(hdev);
	greco_ack_pb_hmmu_axi(hdev);
	greco_ack_pb_kdma(hdev);
	greco_ack_pb_mme(hdev);
	greco_ack_pb_mme_qm(hdev);
	greco_ack_pb_mme_sbte(hdev);
	greco_ack_pb_mmeif_rtr(hdev);
	greco_ack_pb_pdma(hdev);
	greco_ack_pb_rot(hdev);
	greco_ack_pb_sram(hdev);
	greco_ack_pb_sync_mngr(hdev);
	greco_ack_pb_tpc(hdev);
	greco_ack_pb_tpcif_rtr(hdev);
	greco_ack_pb_mstr_if(hdev);
	greco_ack_pb_pcie(hdev);
	greco_ack_pb_pmmu(hdev);
	greco_ack_pb_psoc_arc(hdev);
	greco_ack_pb_psoc_avs(hdev);
	greco_ack_pb_psoc_btl(hdev);
	greco_ack_pb_psoc_cs_trace(hdev);
	greco_ack_pb_psoc_efuse(hdev);
	greco_ack_pb_psoc_glbl_conf(hdev);
	greco_ack_pb_psoc_mstr_if_rr(hdev);
	greco_ack_pb_psoc_pid(hdev);
	greco_ack_pb_psoc_plls(hdev);
	greco_ack_pb_psoc_engines_pll(hdev);
	greco_ack_pb_psoc_pwm(hdev);
	greco_ack_pb_psoc_rpm(hdev);
	greco_ack_pb_sm_objs(hdev);
	greco_ack_pb_vdec(hdev);
	greco_ack_pb_venc(hdev);
	greco_ack_pb_vsi_wrap(hdev);
	greco_ack_pb_xif(hdev);
}
