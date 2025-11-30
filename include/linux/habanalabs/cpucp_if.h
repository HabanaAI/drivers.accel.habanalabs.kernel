/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2020-2023 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef CPUCP_IF_H
#define CPUCP_IF_H

#include <linux/types.h>
#include <linux/if_ether.h>

#include "hl_boot_if.h"

#define NUM_HBM_PSEUDO_CH				2
#define NUM_HBM_CH_PER_DEV				8
#define CPUCP_PKT_HBM_ECC_INFO_WR_PAR_SHIFT		0
#define CPUCP_PKT_HBM_ECC_INFO_WR_PAR_MASK		0x00000001
#define CPUCP_PKT_HBM_ECC_INFO_RD_PAR_SHIFT		1
#define CPUCP_PKT_HBM_ECC_INFO_RD_PAR_MASK		0x00000002
#define CPUCP_PKT_HBM_ECC_INFO_CA_PAR_SHIFT		2
#define CPUCP_PKT_HBM_ECC_INFO_CA_PAR_MASK		0x00000004
#define CPUCP_PKT_HBM_ECC_INFO_DERR_SHIFT		3
#define CPUCP_PKT_HBM_ECC_INFO_DERR_MASK		0x00000008
#define CPUCP_PKT_HBM_ECC_INFO_SERR_SHIFT		4
#define CPUCP_PKT_HBM_ECC_INFO_SERR_MASK		0x00000010
#define CPUCP_PKT_HBM_ECC_INFO_TYPE_SHIFT		5
#define CPUCP_PKT_HBM_ECC_INFO_TYPE_MASK		0x00000020
#define CPUCP_PKT_HBM_ECC_INFO_HBM_CH_SHIFT		6
#define CPUCP_PKT_HBM_ECC_INFO_HBM_CH_MASK		0x000007C0

#define PLL_MAP_MAX_BITS	128
#define PLL_MAP_LEN		(PLL_MAP_MAX_BITS / 8)

#define HL_EQ_LENGTH		64	/* Must be power of 2 */
enum eq_event_id {
	EQ_EVENT_NIC_STS_REQUEST = 0,
	EQ_EVENT_PWR_MODE_0,
	EQ_EVENT_PWR_MODE_1,
	EQ_EVENT_PWR_MODE_2,
	EQ_EVENT_PWR_MODE_3,
	EQ_EVENT_PWR_BRK_ENTRY,
	EQ_EVENT_PWR_BRK_EXIT,
	EQ_EVENT_HEARTBEAT,
	EQ_EVENT_CPLD_RESET_REASON,
	EQ_EVENT_CPLD_SHUTDOWN,
	EQ_EVENT_POWER_EVT_START,
	EQ_EVENT_POWER_EVT_END,
	EQ_EVENT_THERMAL_EVT_START,
	EQ_EVENT_THERMAL_EVT_END,
	EQ_EVENT_PVT_ALARM_EVT,
};

/*
 * info of the pkt queue pointers in the first async occurrence
 */
struct cpucp_pkt_sync_err {
	__le32 pi;
	__le32 ci;
};

struct hl_eq_hbm_ecc_data {
	/* SERR counter */
	__le32 sec_cnt;
	/* DERR counter */
	__le32 dec_cnt;
	/* Supplemental Information according to the mask bits */
	__le32 hbm_ecc_info;
	/* Address in hbm where the ecc happened */
	__le32 first_addr;
	/* SERR continuous address counter */
	__le32 sec_cont_cnt;
	__le32 pad;
};

/*
 * EVENT QUEUE
 */

enum hl_agg_grp_type {
	INT_GRP_TYPE_SERR,
	INT_GRP_TYPE_DERR,
	INT_GRP_TYPE_SEI,
	INT_GRP_TYPE_SPI,
	INT_GRP_TYPE_ECO,
	INT_GRP_TYPE_MAX
};

enum hl_agg_component_type {
	INT_COMP_TYPE_ARC_FARM,
	INT_COMP_TYPE_CPU,
	INT_COMP_TYPE_CS,
	INT_COMP_TYPE_D2D_MAC,
	INT_COMP_TYPE_D2D_PHY,
	INT_COMP_TYPE_DCH0,
	INT_COMP_TYPE_DCH1,
	INT_COMP_TYPE_DEC,
	INT_COMP_TYPE_DRTR0,
	INT_COMP_TYPE_DRTR1,
	INT_COMP_TYPE_ECON,
	INT_COMP_TYPE_EDMA,
	INT_COMP_TYPE_EDUP,
	INT_COMP_TYPE_GRTR1,
	INT_COMP_TYPE_GRTR3,
	INT_COMP_TYPE_HFT,
	INT_COMP_TYPE_HIF,
	INT_COMP_TYPE_MC,
	INT_COMP_TYPE_MME,
	INT_COMP_TYPE_NCH,
	INT_COMP_TYPE_NCHL,
	INT_COMP_TYPE_NIC,
	INT_COMP_TYPE_NRTR0,
	INT_COMP_TYPE_NRTR2,
	INT_COMP_TYPE_PARC,
	INT_COMP_TYPE_PCIE,
	INT_COMP_TYPE_PDMA,
	INT_COMP_TYPE_PLL,
	INT_COMP_TYPE_PMMU,
	INT_COMP_TYPE_PSOC,
	INT_COMP_TYPE_RFT,
	INT_COMP_TYPE_ROT,
	INT_COMP_TYPE_RRTR,
	INT_COMP_TYPE_SOB,
	INT_COMP_TYPE_STLB,
	INT_COMP_TYPE_TPC,
	INT_COMP_TYPE_TS,
	INT_COMP_TYPE_VM,
	INT_COMP_TYPE_MAX
};

enum hl_agg_hdcore_type {
	INT_HDCORE0,
	INT_HDCORE1,
	INT_HDCORE2,
	INT_HDCORE3,
	INT_SHARED,
	INT_PSOC,
	INT_HDCORE_MAX
};

struct hl_eq_header {
	__le16 flags;
	__le16 size; /* actual data size */
	__le32 ctl;
};

struct hl_agg_eq_header {
	__u8 int_grp_type; /* hl_agg_grp_type */
	__u8 int_comp_type; /* hl_agg_component_type */
	__u8 die_id;
	__u8 hdcore_type; /* hl_agg_hdcore_type */
	__u8 comp_instance;
	__u8 pad;
	__le16 event_id; /* Unique event identifier */
};

struct hl_eq_ecc_data {
	__le64 ecc_address;
	__le64 ecc_syndrom;
	__u8 memory_wrapper_idx;
	__u8 is_critical;
	__le16 block_id;
	__u8 pad[4];
};

enum hl_sm_sei_cause {
	SM_SEI_SO_OVERFLOW,
	SM_SEI_LBW_4B_UNALIGNED,
	SM_SEI_AXI_RESPONSE_ERR
};

struct hl_eq_sm_sei_data {
	__le32 sei_log;
	/* enum hl_sm_sei_cause */
	__u8 sei_cause;
	__u8 pad[3];
};

enum hl_fw_alive_severity {
	FW_ALIVE_SEVERITY_MINOR,
	FW_ALIVE_SEVERITY_CRITICAL
};

struct hl_eq_fw_alive {
	__le64 uptime_seconds;
	__le32 process_id;
	__le32 thread_id;
	/* enum hl_fw_alive_severity */
	__u8 severity;
	__u8 pad[7];
};

struct hl_eq_intr_cause {
	__le64 intr_cause_data;
};

struct hl_eq_pcie_drain_ind_data {
	struct hl_eq_intr_cause intr_cause;
	__le64 drain_wr_addr_lbw;
	__le64 drain_rd_addr_lbw;
	__le64 drain_wr_addr_hbw;
	__le64 drain_rd_addr_hbw;
};

enum hl_eq_glbl_err_idx {
	GLBL_ERR_IDX0,
	GLBL_ERR_IDX1,
	GLBL_ERR_MAX
};

/**
 * struct hl_eq_glbl_err_reg_info - Global error register information
 * @block_addr: lower 32 bits of the block address where global error occurred
 * @cause: Global error cause information
 * @addr: Global error address information
 * @pad: padding
 */
struct hl_eq_glbl_err_reg_info {
	__le32 block_addr;
	__le32 cause;
	__le32 addr;
	__u8 pad[4];
};

/**
 * struct hl_eq_glbl_err - Global error information
 * @num_valid_entries: number of valid entries in info array
 * @pad: padding
 * @info: Global error register information array
 *
 * Upon SPI and SEI events, FW will scan special blocks for
 * global error occurred. FW will fill info array and update
 * number of valid entries.
 */
struct hl_eq_glbl_err {
	__u8 num_valid_entries;
	__u8 pad[7];
	struct hl_eq_glbl_err_reg_info info[GLBL_ERR_MAX];
};

/**
 * struct hl_eq_razwi_regs - RAZWI register information
 * @razwi_happened: flag to indicate RAZWI happened
 * @pad: padding
 * @hi_reg: 32 bit MSB register value
 * @lo_reg: 32 bit LSB register value
 * @id: RAZWI captured ID
 */
struct hl_eq_razwi_regs {
	__u8 razwi_happened;
	__u8 pad[3];
	__le32 hi_reg;
	__le32 lo_reg;
	__le32 id;
};

/**
 * struct hl_eq_razwi_block_info - RAZWI block information
 * @rr_aw: RR_AW RAZWI register information
 * @rr_ar: RR_AR RAZWI register information
 * @adec_aw: ADEC_AW RAZWI register information
 * @adec_ar: ADEC_AR RAZWI register information
 */
struct hl_eq_razwi_block_info {
	struct hl_eq_razwi_regs rr_aw;
	struct hl_eq_razwi_regs rr_ar;
	struct hl_eq_razwi_regs adec_aw;
	struct hl_eq_razwi_regs adec_ar;
};

/**
 * struct hl_eq_razwi_mstr_if_reg_data - MSTR IF RAZWI register information
 * @aw: MSTR IF AW RAZWI register information
 * @ar: MSTR IF AR RAZWI register information
 */
struct hl_eq_razwi_mstr_if_reg_data {
	struct hl_eq_razwi_regs aw;
	struct hl_eq_razwi_regs ar;
};

/**
 * struct hl_eq_razwi_mstr_if_block_data - MSTR IF RAZWI block information
 * @lbw: MSTR IF RAZWI LBW information
 * @hbw: MSTR IF RAZWI HBW information
 */
struct hl_eq_razwi_mstr_if_block_data {
	struct hl_eq_razwi_mstr_if_reg_data lbw;
	struct hl_eq_razwi_mstr_if_reg_data hbw;
};

/**
 * struct hl_eq_razwi_xresp_data - MSTR IF XRESP information
 * @lbw: LBW cause information
 * @hbw: HBW cause information
 */
struct hl_eq_razwi_xresp_data {
	struct hl_eq_intr_cause lbw;
	struct hl_eq_intr_cause hbw;
};

/**
 * struct hl_eq_razwi_mstr_if_data - RAZWI MSTR IF information
 * @rr: RR RAZWI information
 * @isec: ISEC RAZWI information
 * @aw_dup_crdt: DUP credit AW RAZWI information
 * @illegal_txn: Illegal TXN RAZWI information
 * @xresp: Error response RAZWI information
 */
struct hl_eq_razwi_mstr_if_data {
	struct hl_eq_razwi_mstr_if_block_data rr;
	struct hl_eq_razwi_mstr_if_reg_data isec;
	struct hl_eq_razwi_regs aw_dup_crdt;
	struct hl_eq_razwi_mstr_if_block_data illegal_txn;
	struct hl_eq_razwi_xresp_data xresp;
};

/**
 * struct hl_eq_razwi_rtr_data - RAZWI RTR information
 * @lbw: RAZWI LBW information
 * @hbw: RAZWI HBW information
 */
struct hl_eq_razwi_rtr_data {
	struct hl_eq_razwi_block_info lbw;
	struct hl_eq_razwi_block_info hbw;
};

/**
 * struct hl_eq_razwi_with_intr_cause_data - RAZWI information with interrupt cause
 * @intr_cause: Interrupt cause information
 * @rtr_data: RTR RAZWI LBW and HBW information
 * @mstr_if_data: MSTR IF RAZWI information
 * @glbl_err_data: Global error information
 *
 * This data structure will be used as part of hl_eq_dynamic_entry
 * data structure.
 */
struct hl_eq_razwi_with_intr_cause_data {
	struct hl_eq_intr_cause intr_cause;
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data;
	struct hl_eq_glbl_err glbl_err_data;
};

struct hl_eq_razwi_lbw_info_regs {
	__le32 rr_aw_razwi_reg;
	__le32 rr_aw_razwi_id_reg;
	__le32 rr_ar_razwi_reg;
	__le32 rr_ar_razwi_id_reg;
};

struct hl_eq_razwi_hbw_info_regs {
	__le32 rr_aw_razwi_hi_reg;
	__le32 rr_aw_razwi_lo_reg;
	__le32 rr_aw_razwi_id_reg;
	__le32 rr_ar_razwi_hi_reg;
	__le32 rr_ar_razwi_lo_reg;
	__le32 rr_ar_razwi_id_reg;
};

/* razwi_happened masks */
#define RAZWI_HAPPENED_HBW	0x1
#define RAZWI_HAPPENED_LBW	0x2
#define RAZWI_HAPPENED_AW	0x4
#define RAZWI_HAPPENED_AR	0x8

struct hl_eq_razwi_info {
	__le32 razwi_happened_mask;
	union {
		struct hl_eq_razwi_lbw_info_regs lbw;
		struct hl_eq_razwi_hbw_info_regs hbw;
	};
	__le32 pad;
};

struct hl_eq_razwi_with_intr_cause {
	struct hl_eq_razwi_info razwi_info;
	struct hl_eq_intr_cause intr_cause;
};

#define HBM_CA_ERR_CMD_LIFO_LEN		8
#define HBM_RD_ERR_DATA_LIFO_LEN	8
#define HBM_WR_PAR_CMD_LIFO_LEN		11
#define NUM_MC_CMN_PER_HBM		4
#define NUM_PC_PER_MC_CMN		4
#define NUM_CH_PER_MC_CMN		2
#define NUM_PC_PER_CH			2
#define NUM_BEAT_PER_PC			4
#define NUM_DW_PER_PC			2
#define MC_RD_ERR_DATA_ECC_M		0xff
#define MC_RD_ERR_DATA_ECC_S		8
#define CPLD_RESET_REASON_MAX_REGS	9

enum hl_hbm_sei_cause {
	/* Command/address parity error event is split into 2 events due to
	 * size limitation: ODD suffix for odd HBM CK_t cycles and EVEN  suffix
	 * for even HBM CK_t cycles
	 */
	HBM_SEI_CMD_PARITY_EVEN,
	HBM_SEI_CMD_PARITY_ODD,
	/* Read errors can be reflected as a combination of SERR/DERR/parity
	 * errors. Therefore, we define one event for all read error types.
	 * LKD will perform further proccessing.
	 */
	HBM_SEI_READ_ERR,
	HBM_SEI_WRITE_DATA_PARITY_ERR,
	HBM_SEI_CATTRIP,
	HBM_SEI_MEM_BIST_FAIL,
	HBM_SEI_DFI,
	HBM_SEI_INV_TEMP_READ_OUT,
	HBM_SEI_BIST_FAIL,
};

/* Masks for parsing hl_hbm_sei_headr fields */
#define HBM_ECC_SERR_CNTR_MASK		0xFF
#define HBM_ECC_DERR_CNTR_MASK		0xFF00
#define HBM_RD_PARITY_CNTR_MASK		0xFF0000

/* HBM index and MC index are known by the event_id */
struct hl_hbm_sei_header {
	union {
		/* relevant only in case of HBM read error */
		struct {
			__u8 ecc_serr_cnt;
			__u8 ecc_derr_cnt;
			__u8 read_par_cnt;
			__u8 reserved;
		};
		/* All other cases */
		__le32 cnt;
	};
	__u8 sei_cause;		/* enum hl_hbm_sei_cause */
	__u8 mc_channel;		/* range: 0-3 */
	__u8 mc_pseudo_channel;	/* range: 0-7 */
	__u8 is_critical;
};

#define HBM_RD_ADDR_SID_SHIFT		0
#define HBM_RD_ADDR_SID_MASK		0x1
#define HBM_RD_ADDR_BG_SHIFT		1
#define HBM_RD_ADDR_BG_MASK		0x6
#define HBM_RD_ADDR_BA_SHIFT		3
#define HBM_RD_ADDR_BA_MASK		0x18
#define HBM_RD_ADDR_COL_SHIFT		5
#define HBM_RD_ADDR_COL_MASK		0x7E0
#define HBM_RD_ADDR_ROW_SHIFT		11
#define HBM_RD_ADDR_ROW_MASK		0x3FFF800

struct hbm_rd_addr {
	union {
		/* bit fields are only for FW use */
		struct {
			u32 dbg_rd_err_addr_sid:1;
			u32 dbg_rd_err_addr_bg:2;
			u32 dbg_rd_err_addr_ba:2;
			u32 dbg_rd_err_addr_col:6;
			u32 dbg_rd_err_addr_row:15;
			u32 reserved:6;
		};
		__le32 rd_addr_val;
	};
};

/**
 * struct hbm_rd_err_addr - HBM MC SEI RD PAR ERR address information.
 * @rd_err_addr_sid: Holds last command hbm stack id number, range: 0-1.
 * @rd_err_addr_bg: Holds last command hbm bank group number, range: 0-3.
 * @rd_err_addr_ba: Holds last command hbm bank number, range: 0-3.
 * @rd_err_addr_col: Holds last command hbm column address.
 * @rd_err_addr_row: Holds last command hbm row address.
 * @pad: Padding to 8B.
 */
struct hbm_rd_err_addr {
	__u8 rd_err_addr_sid;
	__u8 rd_err_addr_bg;
	__u8 rd_err_addr_ba;
	__u8 rd_err_addr_col;
	__le16 rd_err_addr_row;
	__u8 pad[2];
};

/**
 * struct hbm_wr_err_addr - HBM MC SEI WR PAR ERR address information, row addr is not latched.
 * @sid: Holds last command hbm stack id number, range: 0-1.
 * @bg: Holds last command hbm bank group number, range: 0-3.
 * @ba: Holds last command hbm bank number, range: 0-3.
 * @col: Holds last command hbm column address.
 * @derr: Indication for WR PAR error per BEAT(clock edge).
 * @pad: Padding to 8B.
 */
struct hbm_wr_err_addr {
	__u8 sid;
	__u8 bg;
	__u8 ba;
	__u8 col;
	__u8 derr;
	__u8 pad[3];
};

/**
 * struct hbm_rd_err_beat_data - HBM MC SEI RD ERR info per BEAT(1 HBM burst per 1 PC is 4 BEATS).
 * @rd_err_data: Holds last data on the bus related to the interrupt, 2 DW(32b) per 1 PC(64b).
 * @rd_err_par_err: Indicates in which beat(s) there was read parity error(s).
 *                  Note there are 2 bits per beat because there's PAR signal 1b per DW.
 * @rd_err_par_data: Complements DFI_RD_ERR_REP_DATA/DM.
 *                   It provides the value of the 2 PAR signals during the 4 beats of the command
 *                   1b per DW.
 * @rd_err_serr: Indicates in which beat(s) there was single-bit error(s) 1b per PC.
 * @rd_err_derr: Indicates in which beat(s) there was double-bit error(s) 1b per PC.
 * @rd_err_dm: Holds DM for last command information(ECC calculation) 8b per PC.
 * @rd_err_syndrome: Holds SYNDROME(DQ failure code) for last(bit error mapping) 8b per PC.
 */
struct hbm_rd_err_beat_data {
	__le32 rd_err_data[NUM_DW_PER_PC];
	__u8 rd_err_par_err[NUM_DW_PER_PC];
	__u8 rd_err_par_data[NUM_DW_PER_PC];
	__u8 rd_err_serr;
	__u8 rd_err_derr;
	__u8 rd_err_dm;
	__u8 rd_err_syndrome;
};

#define HBM_RD_ERR_BEAT_SHIFT		2
/* dbg_rd_err_misc fields: */
/* Read parity is calculated per DW on every beat */
#define HBM_RD_ERR_PAR_ERR_BEAT0_SHIFT	0
#define HBM_RD_ERR_PAR_ERR_BEAT0_MASK	0x3
#define HBM_RD_ERR_PAR_DATA_BEAT0_SHIFT	8
#define HBM_RD_ERR_PAR_DATA_BEAT0_MASK	0x300
/* ECC is calculated per PC on every beat */
#define HBM_RD_ERR_SERR_BEAT0_SHIFT	16
#define HBM_RD_ERR_SERR_BEAT0_MASK	0x10000
#define HBM_RD_ERR_DERR_BEAT0_SHIFT	24
#define HBM_RD_ERR_DERR_BEAT0_MASK	0x100000

struct hl_eq_hbm_sei_read_err_intr_info {
	/* DFI_RD_ERR_REP_ADDR */
	struct hbm_rd_addr dbg_rd_err_addr;
	/* DFI_RD_ERR_REP_ERR */
	union {
		struct {
			/* bit fields are only for FW use */
			u32 dbg_rd_err_par:8;
			u32 dbg_rd_err_par_data:8;
			u32 dbg_rd_err_serr:4;
			u32 dbg_rd_err_derr:4;
			u32 reserved:8;
		};
		__le32 dbg_rd_err_misc;
	};
	/* DFI_RD_ERR_REP_DM */
	__le32 dbg_rd_err_dm;
	/* DFI_RD_ERR_REP_SYNDROME */
	__le32 dbg_rd_err_syndrome;
	/* DFI_RD_ERR_REP_DATA */
	__le32 dbg_rd_err_data[HBM_RD_ERR_DATA_LIFO_LEN];
};

struct hl_eq_hbm_sei_ca_par_intr_info {
	/* 14 LSBs */
	__le16 dbg_row[HBM_CA_ERR_CMD_LIFO_LEN];
	/* 18 LSBs */
	__le32 dbg_col[HBM_CA_ERR_CMD_LIFO_LEN];
};

#define WR_PAR_LAST_CMD_COL_SHIFT	0
#define WR_PAR_LAST_CMD_COL_MASK	0x3F
#define WR_PAR_LAST_CMD_BG_SHIFT	6
#define WR_PAR_LAST_CMD_BG_MASK		0xC0
#define WR_PAR_LAST_CMD_BA_SHIFT	8
#define WR_PAR_LAST_CMD_BA_MASK		0x300
#define WR_PAR_LAST_CMD_SID_SHIFT	10
#define WR_PAR_LAST_CMD_SID_MASK	0x400

/* Row address isn't latched */
struct hbm_sei_wr_cmd_address {
	/* DFI_DERR_LAST_CMD */
	union {
		struct {
			/* bit fields are only for FW use */
			u32 col:6;
			u32 bg:2;
			u32 ba:2;
			u32 sid:1;
			u32 reserved:21;
		};
		__le32 dbg_wr_cmd_addr;
	};
};

struct hl_eq_hbm_sei_wr_par_intr_info {
	/* entry 0: WR command address from the 1st cycle prior to the error
	 * entry 1: WR command address from the 2nd cycle prior to the error
	 * and so on...
	 */
	struct hbm_sei_wr_cmd_address dbg_last_wr_cmds[HBM_WR_PAR_CMD_LIFO_LEN];
	/* derr[0:1] - 1st HBM cycle DERR output
	 * derr[2:3] - 2nd HBM cycle DERR output
	 */
	__u8 dbg_derr;
	/* extend to reach 8B */
	__u8 pad[3];
};

/*
 * this struct represents the following sei causes:
 * command parity, ECC double error, ECC single error, dfi error, cattrip,
 * temperature read-out, read parity error and write parity error.
 * some only use the header while some have extra data.
 */
struct hl_eq_hbm_sei_data {
	struct hl_hbm_sei_header hdr;
	union {
		struct hl_eq_hbm_sei_ca_par_intr_info ca_parity_even_info;
		struct hl_eq_hbm_sei_ca_par_intr_info ca_parity_odd_info;
		struct hl_eq_hbm_sei_read_err_intr_info read_err_info;
		struct hl_eq_hbm_sei_wr_par_intr_info wr_parity_info;
	};
};

/* Severe interrupts are mapped to SEI0 signal, non severe interrupts are mapped to SEI1 signal */
enum hl_hbm_mc_sei_type {
	HBM_MC_SEI0_SEVERE_ERR = 0x0,
	HBM_MC_SEI1_NON_SEVERE_ERR = 0x1,
	NONE_SEI = 0x2,
};

/**
 * struct hl_eq_hbm_mc_sei0_header- HBM MC SEI0 interrupts error map.
 * @sei0_status: SEI interrupt status for SEI0 signal per 2 mc ch.
 * @is_sei0_set: Indication for SEI0 interrupt detection.
 * @ecc_derr_err: Bit map indication for ECC DERR on all 4 PCs (2 PCs per CH).
 * @ca_par_err: Bit map indication for CA PARITY on all 4 PCs (2 PCs per CH).
 * @wr_par_err: Bit map indication for WR PARITY on all 4 PCs (2 PCs per CH).
 * @rd_par_err: Bit map indication for RD PARITY on all 4 PCs (2 PCs per CH).
 * @cattrip_asserted: Indication for CATTRIP assertion on the HBM.
 * @sei0_false_alarm: Indication for SEI0 signal assertion with empty SEI0 status information.
 * @pad: Padding to 8B.
 */
struct hl_eq_hbm_mc_sei0_header {
	__le32 sei0_status;
	__u8 is_sei0_set;
	__u8 ecc_derr_err;
	__u8 ca_par_err;
	__u8 wr_par_err;
	__u8 rd_par_err;
	__u8 cattrip_asserted;
	__u8 sei0_false_alarm;
	__u8 pad[5];
};

/**
 * struct hl_eq_hbm_mc_sei1_header- HBM MC SEI1 interrupts error map.
 * @sei1_status: SEI interrupt status for SEI1 signal per 2 mc ch.
 * @is_sei1_set: Indication for SEI1 interrupt detection.
 * @ecc_serr_err: Bit map indication for ECC SERR on all 4 PCs (2 PCs per CH).
 * @dfi_err: Bit map indication for DFI ERR on all 2 CHs.
 *           DFI error per PC, indication on some interface issue PHY-HBM.
 * @inv_temp_rdout: Indication for invalid temperature read out assertion on the HBM.
 * @bist_fail: Bit map indication for BIST FAIL on all 4 PCs (2 PCs per CH).
 *             MCBIST failure per PC, further information is printed during MCBIST run on boot.
 *             above interrupts have no additional information and they only act as flags.
 * @sei1_false_alarm: Indication for SEI1 signal assertion with empty SEI1 status information.
 * @pad: Padding to 8B.
 */
struct hl_eq_hbm_mc_sei1_header {
	__le32 sei1_status;
	__u8 is_sei1_set;
	__u8 ecc_serr_err;
	__u8 dfi_err;
	__u8 inv_temp_rdout;
	__u8 bist_fail;
	__u8 sei1_false_alarm;
	__u8 pad[6];
};

/**
 * struct hl_eq_hbm_mc_sei_header- HBM MC SEI interrupts general information.
 * @hbm_num: HBM device number, range: 0-7.
 * @mc_cmn_num: MC CMN (2 CH MC) device number, range: 0-3.
 * @sei0_header: SEI0 interrupt general information.
 * @sei1_header: SEI1 interrupt general information.
 * @sei_type: SEI type - SEI0=severe interrupt / SEI1=non severe interrupt, enum hl_hbm_mc_sei_type.
 *            In case of both SEI0 and SEI1 will set SEI0 due to severity.
 * @mc_ch: Holds HBM channel index that interrupt occurred on, range: 0-7.
 * @mc_pc: Holds HBM pseudo channel index that interrupt occurred on, range: 0-1.
 * @pad: Padding to 8B.
 */
struct hl_eq_hbm_mc_sei_header {
	__le32 hbm_num;
	__le32 mc_cmn_num;
	struct hl_eq_hbm_mc_sei0_header sei0_header;
	struct hl_eq_hbm_mc_sei1_header sei1_header;
	__u8 sei_type;
	__u8 mc_ch[NUM_CH_PER_MC_CMN];
	__u8 mc_pc[NUM_CH_PER_MC_CMN][NUM_PC_PER_CH];
	__u8 pad;
};
/**
 * struct hl_eq_hbm_mc_sei_ca_par_intr_info- HBM MC SEI CA PAR ERR information.
 * @ca_err_cnt: CA error counter.
 * @odd_row_data: Holds information for row bus data on odd clk cycle
 * @even_row_data: Holds information for row bus data on even clk cycle
 * @odd_col_data: Holds information for column bus data on odd clk cycle
 * @even_col_data: Holds information for column bus data on even clk cycle
 * @pad: Padding to 8B.
 */
struct hl_eq_hbm_mc_sei_ca_par_intr_info {
	__le32 ca_err_cnt;
	/* 14 LSBs */
	__le16 odd_row_data[HBM_CA_ERR_CMD_LIFO_LEN];
	__le16 even_row_data[HBM_CA_ERR_CMD_LIFO_LEN];
	/* 18 LSBs */
	__le32 odd_col_data[HBM_CA_ERR_CMD_LIFO_LEN];
	__le32 even_col_data[HBM_CA_ERR_CMD_LIFO_LEN];
	__u8 pad[4];
};

/**
 * struct hl_eq_hbm_mc_sei_rd_err_intr_info - HBM MC SEI RD ERR information.
 * @rd_err_addr: Holds last data command address.
 * @rd_err_beat: Hold the data bus information related to the interrupt per BEAT.
 *               4 BEAT per 1 HBM burst per 1 PC.
 * @rd_par_cnt: Read parity error counter.
 * @serr_cnt: ECC SERR error counter.
 * @scrb_serr_cnt: ECC patrol scrubber SERR error counter.
 * @derr_cnt: ECC DERR error counter.
 */
struct hl_eq_hbm_mc_sei_rd_err_intr_info {
	struct hbm_rd_err_addr rd_err_addr;
	struct hbm_rd_err_beat_data rd_err_beat[NUM_BEAT_PER_PC];
	__le32 rd_par_cnt;
	__le32 serr_cnt;
	__le32 scrb_serr_cnt;
	__le32 derr_cnt;
};

/**
 * struct hl_eq_hbm_mc_sei_wr_par_intr_info- HBM MC SEI WR PAR ERR information.
 * @last_wr_cmds: Lifo that holds last data related to the interrupt.
 *                entry 0: WR command address from the 1st cycle prior to the error
 *                entry 1: WR command address from the 2nd cycle prior to the error
 *                and so on...
 * @wr_par_cnt: Write parity error counter.
 * @pad: Padding to 8B.
 */
struct hl_eq_hbm_mc_sei_wr_par_intr_info {
	struct hbm_wr_err_addr last_wr_cmds[HBM_WR_PAR_CMD_LIFO_LEN];
	__le32 wr_par_cnt;
	__u8 pad[4];
};

/**
 * struct hl_eq_hbm_mc_sei_data- HBM MC SEI severe interrupt information.
 * @ca_parity_info: Holds the data, address and counter information of CA ERR interrupt.
 * @rd_error_info: Holds the data, address and counter information of RD PAR, ECC SERR and
 *                 ECC DERR interrupt. ECC error and RD PAR error are fed from the same registers.
 * @wr_parity_info: Holds the data, address and counter information of WR PAR interrupt.

 */
struct hl_eq_hbm_mc_sei_data {
	struct hl_eq_hbm_mc_sei_ca_par_intr_info ca_parity_info[NUM_PC_PER_MC_CMN];
	struct hl_eq_hbm_mc_sei_rd_err_intr_info rd_error_info[NUM_PC_PER_MC_CMN];
	struct hl_eq_hbm_mc_sei_wr_par_intr_info wr_parity_info[NUM_PC_PER_MC_CMN];
};

/**
 * struct hl_eq_hbm_mc_cmn_sei_info- HBM MC SEI event cause information per MC CMN(2 channels).
 * @sei_intr_header: Holds general interrupt info for SEI0 and SEI1.
 * @sei_data: Holds the data SEI interrupts: ECC and PARITY.
 * @is_fatal: Is interrupt fatal and a reset is needed or a correctable failure
 * @pad: Padding to 8B.
 */
struct hl_eq_hbm_mc_cmn_sei_info {
	struct hl_eq_hbm_mc_sei_header sei_header;
	struct hl_eq_hbm_mc_sei_data sei_data;
	__u8 is_fatal;
	__u8 pad[7];
};

/**
 * struct hl_eq_hbm_mc_spi_data- HBM MC SPI event cause information.
 * @spi_cause: SPI interrupt status for SPI signal per HBM device.
 * @temp_pins_chng: Indication for temperature read that passed the defined TH(by HBM vendor).
 * @temp_traffic_throt_eng: Indication for engagement of traffic throttling mechanism due to
 *                          temperature read higher then the configured engagement TH(85C).
 * @temp_traffic_throt_dis: Indication for disengagement of traffic throttling mechanism due to
 *                          temperature read lower then the configured disengagement TH(65C).
 * @ieee1500_op_complete: Indication for completion of ieee1500 command.
 * @ieee1500_op_paused: Indication for pausing of ieee1500 command, when ieee1500 command length is
 *                      more then the maximum bus width of ieee1500(128 bit) a pause is triggered
 *                      before continuing to the remaining of the command until completion.
 * @mc_dbg: Indication that the MC debug counters has reached their TH.
 * @pad: Padding to 8B.
 */
struct hl_eq_hbm_mc_spi_data {
	__le32 spi_cause;
	__u8 temp_pins_chng;
	__u8 temp_traffic_throt_eng;
	__u8 temp_traffic_throt_dis;
	__u8 ieee1500_op_complete;
	__u8 ieee1500_op_paused;
	__u8 mc_dbg;
	__u8 pad[6];
};

#define D2DMAC_NUM_OF_CAUSE	2

/**
 * struct hl_eq_d2dmac_sei_data - D2D MAC SEI event information
 * @intr_cause[0]: for sei cause1
 * @intr_cause[1]: for sei cause2
 * @glbl_err_data: Global error information
 *
 * For any SEI event related to D2D MAC, FW will forward
 * hl_eq_d2dmac_sei_data data structure to LKD. LKD should
 * check type and read/process data accordingly.
 */
struct hl_eq_d2dmac_sei_data {
	struct hl_eq_intr_cause intr_cause[D2DMAC_NUM_OF_CAUSE];
	struct hl_eq_glbl_err glbl_err_data;
};

/**
 * struct hl_eq_d2dphy_sei_data - D2D PHY SEI event information
 * @intr_cause: SEI cause
 * @glbl_err_data: Global error information
 *
 * For any SEI event related to D2D PHY, FW will forward
 * hl_eq_d2dphy_sei_data data structure to LKD. LKD should
 * check type and read/process data accordingly.
 */
struct hl_eq_d2dphy_sei_data {
	struct hl_eq_intr_cause intr_cause;
	struct hl_eq_glbl_err glbl_err_data;
};

/* Engine/farm arc interrupt type */
enum hl_engine_arc_interrupt_type {
	/* Qman/farm ARC DCCM QUEUE FULL interrupt type */
	ENGINE_ARC_DCCM_QUEUE_FULL_IRQ = 1
};

/* Data structure specifies details of payload of DCCM QUEUE FULL interrupt */
struct hl_engine_arc_dccm_queue_full_irq {
	/* Queue index value which caused DCCM QUEUE FULL */
	__le32 queue_index;
	__le32 pad;
};

/* Data structure specifies details of QM/FARM ARC interrupt */
struct hl_eq_engine_arc_intr_data {
	/* ARC engine id e.g.  DCORE0_TPC0_QM_ARC, DCORE0_TCP1_QM_ARC */
	__le32 engine_id;
	__le32 intr_type; /* enum hl_engine_arc_interrupt_type */
	/* More info related to the interrupt e.g. queue index
	 * incase of DCCM_QUEUE_FULL interrupt.
	 */
	__le64 payload;
	__le64 pad[5];
};

#define ADDR_DEC_ADDRESS_COUNT_MAX 4

/* Data structure specifies details of ADDR_DEC interrupt */
struct hl_eq_addr_dec_intr_data {
	struct hl_eq_intr_cause intr_cause;
	__le64 addr[ADDR_DEC_ADDRESS_COUNT_MAX];
	__u8 addr_cnt;
	__u8 pad[7];
};

#define MAX_PORTS_PER_NIC	4

/* NIC interrupt type */
enum hl_nic_interrupt_type {
	NIC_INTR_NONE = 0,
	NIC_INTR_TMR = 1,
	NIC_INTR_RXB_CORE_SPI,
	NIC_INTR_RXB_CORE_SEI,
	NIC_INTR_QPC_RESP_ERR,
	NIC_INTR_RXE_SPI,
	NIC_INTR_RXE_SEI,
	NIC_INTR_TXS,
	NIC_INTR_TXE,
};

struct hl_eq_nic_intr_cause {
	__le32 intr_type; /* enum hl_nic_interrupt_type */
	__le32 pad;
	struct hl_eq_intr_cause intr_cause[MAX_PORTS_PER_NIC];
};

/* struct hl_eq_nic_sts_req_data is the data in hl_eq_dynamic_entry */
struct hl_eq_nic_sts_req_data {
	__le64 port_en_mask;	/* enabled ports 0-23 */
	__u8 cmd;		/* 0 - one shot, 1 - periodic start, 2 - periodic stop */
	__u8 period;		/* seconds */
	__le16 reserved;
	__le32 reserved2;
};

enum hl_pcie_sei_type {
	PCIE_SEI_AXI_RESP_ERR,
	PCIE_SEI_BUS_MSTR_EN_CLR
};

enum hl_eq_pcie_mstr_if {
	PCIE_MSTR_RR_MSTR_IF,
	PCIE_ELBI_RR_MSTR_IF,
	PCIE_LBW_RR_MSTR_IF,
	PCIE_PIF_ARC_MSTR_IF,
	PCIE_MSTR_IF_MAX
};

struct hl_eq_pcie_sei_data {
	__u8 sei_type; /* enum hl_pcie_sei_type */
	__u8 pad[7];
	struct hl_eq_intr_cause intr_cause; /* relevant only to PCIE_SEI_AXI_RESP_ERR */
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data[PCIE_MSTR_IF_MAX];
	struct hl_eq_glbl_err glbl_err_data;
};

enum hl_cs_dbg_mme_sub_type {
	CS_DBG_MME0_SBTE0_1,
	CS_DBG_MME0_SBTE2_3,
	CS_DBG_MME1_SBTE0_1,
	CS_DBG_MME1_SBTE2_3,
	CS_DBG_MME_QM
};

enum hl_cs_dbg_err_type {
	CS_DBG_SPMU,
	CS_DBG_BMON0,
	CS_DBG_BMON1,
	CS_DBG_BMON2,
	CS_DBG_BMON3,
	CS_DBG_BMON4,
	CS_DBG_BMON5,
	CS_DBG_BMON6,
	CS_DBG_BMON7,
	CS_DBG_BMON_MAX
};

/**
 * struct hl_eq_tpc_data - TPC SEI and SPI event cause information
 * @intr_cause: TPC configuration cause0
 * @smt_th0_cause: TPC SMT TH0 cause
 * @smt_th1_cause: TPC SMT TH1 cause
 * @smt_th2_cause: TPC SMT TH2 cause
 * @smt_th3_cause: TPC SMT TH3 cause
 * @kernel_id: TPC kernel id
 * @pad: padding
 *
 * Note: For more than 1 bit set in intr_cause or/and th*_cause for
 * SPI/SEI event, only one kernel id information is passed. kernel id
 * should be read if either of cause is non zero.
 */
struct hl_eq_tpc_data {
	struct hl_eq_intr_cause intr_cause;
	__le32 smt_th0_cause;
	__le32 smt_th1_cause;
	__le32 smt_th2_cause;
	__le32 smt_th3_cause;
	__le16 kernel_id;
	__u8 pad[6];
};

/**
 * struct hl_eq_spmu_bmon - SPMU/BMON event information
 * @cause: SPMU and BMON cause for enum hl_cs_dbg_err_type
 * @pad: padding
 */
struct hl_eq_spmu_bmon {
	__le32 cause[CS_DBG_BMON_MAX];
	__u8 pad[4];
};

/**
 * struct hl_eq_nic_sw_err_data - NIC SW error event information
 * @qpc_cause: QPC cause information
 * @rxb_core_cause: RXB CORE cause information
 * @rxe_cause_0: RXE cause information in first register
 * @rxe_cause_1: RXE cause information in second register
 */
struct hl_eq_nic_sw_err_data {
	struct hl_eq_intr_cause qpc_cause;
	struct hl_eq_intr_cause rxb_core_cause;
	struct hl_eq_intr_cause rxe_cause_0;
	struct hl_eq_intr_cause rxe_cause_1;
};

enum hl_nic_spi_type {
	NIC_SPI_BMON_SPMU,
	NIC_SPI_SW_ERROR
};

/**
 * struct hl_eq_nic_spi_data - NIC SPI event information
 * @spi_type: enum hl_nic_spi_type
 * @pad: padding bytes
 * @hl_eq_nic_sw_err_data: sw error cause information
 * @spmu_bmon_data: bmon cause information
 */
struct hl_eq_nic_spi_data {
	__u8 spi_type;
	__u8 pad[7];
	union {
		struct hl_eq_nic_sw_err_data sw_err_data;
		struct hl_eq_spmu_bmon spmu_bmon_data;
	};
};

enum hl_pcie_spi_type {
	PCIE_SPI_FLR,
	PCIE_SPI_APB_ACCESS_TIMEOUT,
	PCIE_SPI_BMON_SPMU,
	PCIE_SPI_FATAL_ERR,
	PCIE_SPI_P2P_OR_MSIX_GW_INTR,
	PCIE_SPI_DRAIN
};

/**
 * struct hl_eq_pcie_p2p_msix_gw_spi_data -  PCIE P2P/MSIX_GW SPI event information
 * @p2p_cause: P2P Interrupt cause information
 * @msix_gw_cause: MSIX GW Interrupt cause information
 */
struct hl_eq_pcie_p2p_msix_gw_spi_data {
	struct hl_eq_intr_cause p2p;
	struct hl_eq_intr_cause msix_gw;
};

/**
 * struct hl_eq_pcie_spi_data - PCIE SPI event information
 * @spi_type: PCIE spi type
 * @flr_cause: FLR Interrupt cause information
 * @fatal_error: Fatal error cause information
 * @spmu_bmon_data: PCIE SPMU/BMON event information
 * @p2p_msix_cause: P2P/MSIX GW Interrupt cause information
 * @drain_cause: Drain cause information
 *
 * Note: For some of the pcie interrupt there is no cause data
 */
struct hl_eq_pcie_spi_data {
	__u8 spi_type; /* enum hl_pcie_spi_type */
	__u8 pad[7];
	union {
		struct hl_eq_intr_cause flr_cause;
		struct hl_eq_intr_cause fatal_error;
		struct hl_eq_spmu_bmon spmu_bmon_data;
		struct hl_eq_pcie_p2p_msix_gw_spi_data p2p_msix;
		struct hl_eq_pcie_drain_ind_data drain_cause;
	};
};

/**
 * struct hl_eq_tpc_spi_data - TPC SPI event information
 * @hl_eq_tpc_data: TPC configuration event information
 * @hl_eq_spmu_bmon: TPC SPMU/BMON event information
 *
 * For any SPI event related to TPC, FW will forward
 * hl_eq_tpc_spi_data data structure to LKD. LKD should
 * check all the data structure values to identify the event type
 * and process accordingly.
 * Note: SPMU/BMON event bit is not available as part
 * hl_eq_tpc_data for TPC SPI events.
 */
struct hl_eq_tpc_spi_data {
	struct hl_eq_tpc_data data;
	struct hl_eq_spmu_bmon spmu_bmon_data;
};

enum hl_eq_qm_undef_cmd_cq_type {
	CQ_TYPE_LEGACY,
	CQ_TYPE_ARC,
	CQ_TYPE_MAX
};

/**
 * struct hl_eq_qm_undef_cmd_data - QM undefined opcode command error information
 * @cp_curr_inst: CP current instruction
 * @cq_ptr: Current transfer base address
 * @cq_tsize: Current transfer size in bytes
 * @cq_type: CQ type refer enum hl_eq_qm_undef_cmd_cq_type
 * @pad: padding
 *
 * This structure is valid only if QM UNDEFINED OPCODE bit is set
 * in qm_cause structure.
 */
struct hl_eq_qm_undef_cmd_data {
	__le64 cp_curr_inst;
	__le64 cq_ptr;
	__le32 cq_tsize;
	__u8 cq_type;
	__u8 pad[3];
};

/**
 * struct hl_eq_qm_sei_data - QM SEI event information
 * @qm_cause: QM error cause
 * @arc_qm_cause: ARC error cause
 * @undef_op_data: Undefined opcode command error data
 */
struct hl_eq_qm_sei_data {
	struct hl_eq_intr_cause qm_cause;
	struct hl_eq_intr_cause arc_qm_cause;
	struct hl_eq_qm_undef_cmd_data undef_op_data;
};

/**
 * struct hl_eq_tpc_sei_data - TPC SEI event information
 * @data: TPC configuration event information
 * @qm_data: TPC QM event information
 * @rtr_data: RTR RAZWI LBW and HBW information
 * @mstr_if_data: MSTR IF RAZWI information
 * @glbl_err_data: Global error information
 *
 * For any SEI event related to TPC, FW will forward
 * hl_eq_tpc_sei_data data structure to LKD. LKD should
 * check all the data structure values to identify the event type
 * and process accordingly.
 */
struct hl_eq_tpc_sei_data {
	struct hl_eq_tpc_data data;
	struct hl_eq_qm_sei_data qm_data;
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data;
	struct hl_eq_glbl_err glbl_err_data;
};

#define HL_EQ_ROT_CTX_ID_MAX	28

/**
 * struct hl_eq_rot_data - ROTATOR event cause information
 * @intr_cause: ROTATOR event cause
 * @rsb_err_cause: RSB event, check based on intr_cause value
 * @wch_err_cause: WCH event, check based on intr_cause value
 * @ip_num_cause: IP NUM event, check based on intr_cause value
 * @ctx_id: context id for RSB, WCH and IP number event
 *		0 - 8: RSB event context id
 *		9 - 18: WCH event context id
 *		19 - 27: IP NUM event context id
 * @pad: padding
 */
struct hl_eq_rot_data {
	struct hl_eq_intr_cause intr_cause; /* 32 bit interrupt cause */
	__le32 rsb_err_cause;
	__le32 wch_err_cause;
	__le32 ip_num_cause;
	__le16 ctx_id[HL_EQ_ROT_CTX_ID_MAX];
	__u8 pad[4];
};

/**
 * struct hl_eq_rot_spi_data - ROTATOR SPI event information
 * @data: ROTATOR event cause information
 * @spmu_bmon_data: ROTATOR SPMU/BMON event information
 *
 * For any SPI event related to rotator, FW will forward
 * hl_eq_rot_spi_data data structure to LKD. LKD should
 * check all the data structure values to identify the event type
 * and process accordingly.
 * Note: SPMU/BMON event bit is available as part
 * hl_eq_rot_data intr_cause field for ROTATOR SPI events.
 */
struct hl_eq_rot_spi_data {
	struct hl_eq_rot_data data;
	struct hl_eq_spmu_bmon spmu_bmon_data;
};

/**
 * struct hl_eq_rot_sei_data - ROTATOR SEI event information
 * @cause: ROTATOR SEI event cause
 * @qm_data: ROTATOR QM event information
 * @rtr_data: RTR RAZWI LBW and HBW information
 * @mstr_if_data: MSTR IF RAZWI information
 * @glbl_err_data: Global error information
 *
 * For any SEI event related to rotator, FW will forward
 * hl_eq_rot_sei_data data structure to LKD. Refer QM data
 * in case QM related bits are set in cause.
 */
struct hl_eq_rot_sei_data {
	struct hl_eq_intr_cause cause;
	struct hl_eq_qm_sei_data qm_data;
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data;
	struct hl_eq_glbl_err glbl_err_data;
};

enum hl_eq_mme_acc_ctx_id {
	MME_ACC_CTX_ID_CH0_SET0,
	MME_ACC_CTX_ID_CH0_SET1,
	MME_ACC_CTX_ID_CH1_SET0,
	MME_ACC_CTX_ID_CH1_SET1,
	MME_ACC_CTX_ID_MAX
};

enum hl_cs_mme_acc_type {
	CS_DBG_MME_ACC0,
	CS_DBG_MME_ACC1
};

enum hl_eq_mme_stbe_id {
	MME_SBTE_ID0,
	MME_SBTE_ID1,
	MME_SBTE_ID2,
	MME_SBTE_ID3
};

enum hl_eq_mme_eu_id {
	MME_EU_ID0,
	MME_EU_ID1
};

enum hl_eq_mme_qm_razwi_id {
	MME_QM_RAZWI_RD,
	MME_QM_RAZWI_WR,
	MME_QM_RAZWI_MAX
};

enum hl_eq_mme_event_type {
	MME_DATA_TYPE_SBTE,
	MME_DATA_TYPE_ACC,
	MME_DATA_TYPE_CTRL,
	MME_DATA_TYPE_CS_DBG
};

/**
 * struct hl_eq_mme_acc_data - MME SPI ACC information
 * @intr_cause: MME ACC cause
 * @ctxt_id: context information, refer intr_cause to check
 *	     context array values.
 * @id: MME ACC id refer hl_cs_mme_acc_type
 * @pad: padding
 * @rtr_data: RTR RAZWI LBW and HBW information
 * @mstr_if_data: MSTR IF RAZWI information (valid in case of SEI interrupt)
 */
struct hl_eq_mme_acc_data {
	struct hl_eq_intr_cause intr_cause;
	__le16 ctx_id[MME_ACC_CTX_ID_MAX];
	__u8 id;
	__u8 pad[7];
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data;
};

/**
 * struct hl_eq_mme_spmu_bmon - MME SPI SPMU/BMON information
 * @data: MME SPMU/BMON event information
 * @comp_sub_type: Component subtype within MME
 *		   Refer enum hl_cs_dbg_mme_sub_type
 * @pad: padding
 */
struct hl_eq_mme_spmu_bmon {
	struct hl_eq_spmu_bmon data;
	__u8 comp_sub_type;
	__u8 pad[7];
};

/**
 * struct hl_eq_mme_spi_data - MME SPI event information
 * @type: MME event type, refer enum hl_eq_mme_event_type
 * @pad: padding
 * @spmu_bmon_data: MME SPMU/BMON event information
 * @acc_data: MME ACC event information
 *
 * For any SPI event related to MME, FW will forward
 * hl_eq_mme_spi_data data structure to LKD. LKD should
 * check type and read/process data accordingly.
 */
struct hl_eq_mme_spi_data {
	__u8 type;
	__u8 pad[7];
	union {
		struct hl_eq_mme_spmu_bmon spmu_bmon_data;
		struct hl_eq_mme_acc_data acc_data;
	};
};

/**
 * struct hl_eq_mme_sbte_data - MME SEI event information
 * @cause: MME SBTE event cause
 * @ctx_id: context information
 * @sbte_id: SBTE ID, refer enum hl_eq_mme_sbte_id
 * @mme_eu_id: MME EU ID, refer enum hl_eq_mme_eu_id
 * @rtr_data: RTR RAZWI LBW and HBW information
 * @mstr_if_data: MSTR IF RAZWI information
 */
struct hl_eq_mme_sbte_data {
	struct hl_eq_intr_cause cause;
	__le16 ctx_id;
	__u8 sbte_id;
	__u8 mme_eu_id;
	__u8 pad[4];
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data;
};

/**
 * struct hl_eq_mme_ctrl_data - MME CTRL event information
 * @cause: MME CTRL event cause
 * @qm_data: MME QM event information
 * @rtr_data: RTR RAZWI LBW and HBW information
 * @mstr_if_data: MSTR IF RAZWI information
 *
 * Refer QM data in case QM related bit is set in
 * cause.
 */
struct hl_eq_mme_ctrl_data {
	struct hl_eq_intr_cause cause;
	struct hl_eq_qm_sei_data qm_data;
	struct hl_eq_razwi_rtr_data qm_rtr_data[MME_QM_RAZWI_MAX];
	struct hl_eq_razwi_mstr_if_data mstr_if_data;
};

/**
 * struct hl_eq_mme_sei_data - MME SEI event information
 * @type: MME event type, refer enum hl_eq_mme_event_type
 * @pad: padding
 * @sbte_data: MME SBTE event information
 * @acc_data: MME ACC event information
 * @control_data: MME CTRL event information
 * @glbl_err_data: Global error information
 *
 * For any SEI event related to MME, FW will forward
 * hl_eq_mme_sei_data data structure to LKD. LKD should
 * check type and read/process data accordingly.
 */
struct hl_eq_mme_sei_data {
	__u8 type;
	__u8 pad[7];
	union {
		struct hl_eq_mme_sbte_data sbte_data;
		struct hl_eq_mme_acc_data acc_data;
		struct hl_eq_mme_ctrl_data control_data;
	};
	struct hl_eq_glbl_err glbl_err_data;
};

/**
 * struct hl_eq_generic_spi_data - SPI generic event information
 * @data: event cause information
 * @spmu_bmon_data:  SPMU/BMON event information
 *
 * For any SPI event which falls in generic category where
 * only single cause and BMON/SPMU event info needs to be provided,
 * FW will forward hl_eq_generic_spi_data data structure to LKD. LKD should
 * check all the data structure values to identify the event type
 * and process accordingly.
 */
struct hl_eq_generic_spi_data {
	struct hl_eq_intr_cause cause;
	struct hl_eq_spmu_bmon spmu_bmon_data;
};

enum hl_eq_dtlb_id {
	DTLB_RRTR0,
	DTLB_RRTR1,
	DTLB_RRTR2,
	DTLB_RRTR3,
	DTLB_RRTR4,
	DTLB_RRTR5,
	DTLB_RRTR6,
	DTLB_RRTR7,
	DTLB_NRTR0,
	DTLB_NRTR1,
	DTLB_ID_MAX
};

/**
 * struct hl_eq_dtlb_fault_data - DTLB fault information
 * @fault_type: Fault information
 * @addr_47_20: Address 47_20 (address bits 47..20)
 * @id: AXI ID
 *
 * Check other fields only if fault type is non zero.
 */
struct hl_eq_dtlb_fault_data {
	__le32 fault_type;
	__le32 addr_47_20;
	__le64 id;
};

/**
 * struct hl_eq_stlb_fault_data - STLB fault information
 * @syndrom_dti: DTI syndrom information
 * @syndrom_pte: PTE syndrom information
 */
struct hl_eq_stlb_fault_data {
	__le64 syndrom_dti;
	__le64 syndrom_pte;
};

/**
 * struct hl_eq_stlb_spi_data - STLB SPI event information
 * @data: STLB event cause information
 * @fault_data: fault data information
 * @dtlb_data: DTLB fault data information
 *
 * For any SPI event related to STLB, FW will forward
 * hl_eq_stlb_spi_data data structure to LKD. LKD should
 * check all the data structure values to identify the event type
 * and process accordingly. LKD should check fault data only
 * when fault cause bits are set in cause.
 */
struct hl_eq_stlb_spi_data {
	struct hl_eq_intr_cause cause;
	struct hl_eq_stlb_fault_data fault_data;
	struct hl_eq_dtlb_fault_data dtlb_data[DTLB_ID_MAX];
};

/**
 * struct hl_eq_stlb_lbw_data - STLB LBW information
 * @addr: LBW address information
 * @data: LBW data information
 */
struct hl_eq_stlb_lbw_data {
	__le32 addr;
	__le32 data;
};

/**
 * struct hl_eq_stlb_sei_data - STLB SPI event information
 * @cause: STLB event cause information
 * @fault_data:  fault data information (only syndrom_dti valid)
 * @lbw_data: LBW fault data information
 * @dtlb_data:  DTLB fault data information
 * @rtr_data: RTR RAZWI LBW and HBW information
 * @mstr_if_data: MSTR IF RAZWI information
 * @glbl_err_data: Global error information
 *
 * For any SEI event related to STLB, FW will forward
 * hl_eq_stlb_sei_data data structure to LKD. LKD should
 * refer to fault and lbw data based on relevant bits set in
 * cause data. For further fault information, read dtlb
 * fault data.
 */
struct hl_eq_stlb_sei_data {
	struct hl_eq_intr_cause cause;
	struct hl_eq_stlb_fault_data fault_data;
	struct hl_eq_stlb_lbw_data lbw_data;
	struct hl_eq_dtlb_fault_data dtlb_data[DTLB_ID_MAX];
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data;
	struct hl_eq_glbl_err glbl_err_data;
};

/**
 * struct hl_eq_cs_err_host_data - CS SEI host error information
 * @info: Info error information (valid bits 39..0)
 * @addr: Address information (valid bits 39..0)
 * @num_err: Number of errors
 * @pad: padding
 */
struct hl_eq_cs_err_host_data {
	__le64 info;
	__le64 addr;
	__u8 num_err;
	__u8 pad[7];
};

/**
 * struct hl_eq_cs_err_poison_data - CS SEI poison error information
 * @id: Info error information (valid bits 35..0)
 * @addr: Address information (valid bits 39..0)
 */
struct hl_eq_cs_err_poison_data {
	__le64 id;
	__le64 addr;
};

/**
 * struct hl_eq_cs_err_data - CS SEI error information
 * @far_data: Far host error information
 * @close_data: Close host error information
 * @poison_data: Poison error information
 * @slv_err_addr: Slave error address
 * @dn_conv_id: DN conv id
 * @aab_num_err: AAB reduce number of errors
 * @pad: padding
 */
struct hl_eq_cs_err_data {
	struct hl_eq_cs_err_host_data far_data;
	struct hl_eq_cs_err_host_data close_data;
	struct hl_eq_cs_err_poison_data poison_data;
	__le64 slv_err_addr;
	__le64 dn_conv_id;
	__u8 aab_num_err;
	__u8 pad[7];
};

/**
 * struct hl_eq_cs_sei_data - CS SEI event information
 * @cause: CS SEI event cause information
 * @err_data: CS error data
 * @glbl_err_data: Global error information
 *
 * For any SEI event related to CS, FW will forward
 * hl_eq_cs_sei_data data structure to LKD. LKD should
 * check cause bits and accordingly refer hl_eq_cs_err_data
 * data structure elements.
 */
struct hl_eq_cs_sei_data {
	struct hl_eq_intr_cause cause;
	struct hl_eq_cs_err_data err_data;
	struct hl_eq_glbl_err glbl_err_data;
};

enum hl_eq_edma_chn {
	SEDMA_CHANNEL0,
	SEDMA_CHANNEL1,
	SEDMA_CHANNEL2,
	SEDMA_CHANNEL_MAX
};

enum hl_eq_edma_id {
	SEDMA_ID0,
	SEDMA_ID1,
	SEDMA_ID_MAX
};

#define SEDMA_NUM_CHN_DATA	(SEDMA_ID_MAX * SEDMA_CHANNEL_MAX)

/**
 * struct hl_eq_edma_chn_data - EDMA channel information
 * @err_sts: EDMA0/1 CH0/1/2 error status information
 * @ctx_id: EDMA0/1 CH0/1/2 context ID
 * @pad: Padding
 */
struct hl_eq_edma_chn_data {
	__le32 err_sts;
	__le16 ctx_id;
	__u8 pad[2];
};

/**
 * struct hl_eq_edma_sei_data - EDMA SEI event information
 * @qm_data: EDMA0/1 SEI QM information
 * @chn_data: Channel data consisting of error status and context id
 * @rtr_data: RTR RAZWI LBW and HBW information
 * @mstr_if_data: MSTR IF RAZWI information
 * @glbl_err_data: Global error information
 *
 * For any SEI event related to EDMA, FW will forward
 * hl_eq_edma_sei_data data structure to LKD. LKD should
 * check all the data structure values to identify the event type
 * and process accordingly.
 */
struct hl_eq_edma_sei_data {
	struct hl_eq_qm_sei_data qm_data[SEDMA_ID_MAX];
	struct hl_eq_edma_chn_data chn_data[SEDMA_NUM_CHN_DATA];
	struct hl_eq_razwi_rtr_data rtr_data[SEDMA_ID_MAX];
	struct hl_eq_razwi_mstr_if_data mstr_if_data[SEDMA_ID_MAX];
	struct hl_eq_glbl_err glbl_err_data;
};

enum hl_eq_arcfarm_mstr_if {
	ARCFARM_MSTR_IF,
	ARCFARM_MSTR_IF_ARC0_DUP,
	ARCFARM_MSTR_IF_ARC1_DUP,
	ARCFARM_MSTR_IF_MAX
};

/**
 * struct hl_eq_arcfarm_sei_data - ARC FARM SEI event information
 * @internal_cause: Internal ARC Farm error information
 * @arc0_wrapper_cause: ARC0 wrapper system error
 * @arc1_wrapper_cause: ARC1 wrapper system error
 * @rtr_data: RTR RAZWI LBW and HBW information
 * @mstr_if_data: MSTR IF RAZWI information
 * @glbl_err_data: Global error information
 *
 * For any SEI event related to ARC FARM, FW will forward
 * hl_eq_arcfarm_sei_data data structure to LKD. If internal cause
 * value is zero, check ARC0/1 wrapper cause to identify event type
 * and process accordingly.
 */
struct hl_eq_arcfarm_sei_data {
	struct hl_eq_intr_cause internal_cause;
	struct hl_eq_intr_cause arc0_wrapper_cause;
	struct hl_eq_intr_cause arc1_wrapper_cause;
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data[ARCFARM_MSTR_IF_MAX];
	struct hl_eq_glbl_err glbl_err_data;
};

enum hl_eq_nic_mstr_if {
	NIC_MSTR_IF_CTRL,
	NIC_MSTR_IF_DATA,
	NIC_MSTR_IF_MAX
};

/**
 * struct hl_eq_nic_sei_data - NIC SEI event information
 * @rxb_core_cause: RXB CORE cause information
 * @rxe_cause: RXE cause information
 * @txe_cause: TXE cause information
 * @txs_cause: TXS cause information
 * @tmr_cause: TMR cause information
 * @qpc_cause: QPC cause information
 * @rtr_data: RTR RAZWI LBW and HBW information
 * @mstr_if_data: MSTR IF RAZWI information
 * @glbl_err_data: Global error information
 */
struct hl_eq_nic_sei_data {
	struct hl_eq_intr_cause rxb_core_cause;
	struct hl_eq_intr_cause rxe_cause;
	struct hl_eq_intr_cause txe_cause;
	struct hl_eq_intr_cause txs_cause;
	struct hl_eq_intr_cause tmr_cause;
	struct hl_eq_intr_cause qpc_cause;
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data[NIC_MSTR_IF_MAX];
	struct hl_eq_glbl_err glbl_err_data;
};

enum hl_eq_spdma_chn {
	SPDMA_CHANNEL0,
	SPDMA_CHANNEL1,
	SPDMA_CHANNEL2,
	SPDMA_CHANNEL3,
	SPDMA_CHANNEL4,
	SPDMA_CHANNEL5,
	SPDMA_CHANNEL_MAX
};

enum hl_eq_spdma_id {
	SPDMA_ID0,
	SPDMA_ID1,
	SPDMA_ID_MAX
};

enum hl_eq_spdma_mstr_if {
	SPDMA_MAIN_MSTR_IF,
	SPDMA_DUP_MSTR_IF,
	SPDMA_MSTR_IF_MAX
};

/**
 * struct hl_eq_spdma_ch_b_data - SPDMA Channel B event information
 * @pqm_chn_err_sts: PQM channel error status
 * @err_sts: Error status
 * @err_ctx_id: Error context id
 * @pad: padding
 */
struct hl_eq_spdma_ch_b_data {
	struct hl_eq_intr_cause pqm_chn_err_sts;
	struct hl_eq_intr_cause err_sts;
	__le32 err_ctx_id;
	__u8 pad[4];
};

/**
 * struct hl_eq_spdma_data - SPDMA SEI event information
 * @ch_a_ctx_id: Channel A context id
 * @ch_b_data: Channel B event information
 * @cmn_b_cause: Common cause information
 * @rtr_data: RTR RAZWI LBW and HBW information
 * @mstr_if_data: MSTR IF RAZWI information
 */
struct hl_eq_spdma_data {
	__le32 ch_a_ctx_id[SPDMA_CHANNEL_MAX];
	struct hl_eq_spdma_ch_b_data ch_b_data[SPDMA_CHANNEL_MAX];
	struct hl_eq_intr_cause cmn_b_cause;
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data[SPDMA_MSTR_IF_MAX];
};

/**
 * struct hl_eq_pdma_sei_data - PDMA SEI event information
 * @spdma_data: SPDMA0 and 1 event information
 * @glbl_err_data: Global error information
 */
struct hl_eq_pdma_sei_data {
	struct hl_eq_spdma_data spdma_data[SPDMA_ID_MAX];
	struct hl_eq_glbl_err glbl_err_data;
};

/**
 * struct hl_eq_nch_sei_data - NCH SEI event information
 * @xresp_lbw: XRESP_LBW cause information
 * @xresp_hbw: XRESP_HBW cause information
 * @apb_arb: APB_ARB cause information
 * @axi_split: AXI_SPLIT SEI cause information
 * @rtr_data: RTR RAZWI LBW and HBW information
 * @mstr_if_data: MSTR IF RAZWI information
 * @glbl_err_data: Global error information
 */
struct hl_eq_nch_sei_data {
	struct hl_eq_intr_cause xresp_lbw;
	struct hl_eq_intr_cause xresp_hbw;
	struct hl_eq_intr_cause apb_arb;
	struct hl_eq_intr_cause axi_split;
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data;
	struct hl_eq_glbl_err glbl_err_data;
};

enum hl_eq_cpu_mstr_if {
	CPU_MAIN_MSTR_IF,
	CPU_INT_AGGR_MSTR_IF,
	CPU_MSTR_IF_MAX
};

struct hl_eq_cpu_sei_data {
	struct hl_eq_intr_cause intr_cause;
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data[CPU_MSTR_IF_MAX];
	struct hl_eq_glbl_err glbl_err_data;
};

enum hl_eq_parc_mstr_if {
	PARC_MAIN_MSTR_IF,
	PARC_ARC0_MSTR_IF,
	PARC_ARC1_MSTR_IF,
	PARC_ARC2_MSTR_IF,
	PARC_MSTR_IF_MAX
};

struct hl_eq_parc_sei_data {
	struct hl_eq_intr_cause intr_cause;
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data[PARC_MSTR_IF_MAX];
	struct hl_eq_glbl_err glbl_err_data;
};

enum hl_eq_psoc_mstr_if {
	PSOC_DUP_MSTR_IF,
	PSOC_ETR_MSTR_IF,
	PSOC_JT_MSTR_IF,
	PSOC_SMI_MSTR_IF,
	PSOC_I2C_S_MSTR_IF,
	PSOC_MSTR_IF_MAX
};

struct hl_eq_psoc_sei_data {
	struct hl_eq_intr_cause intr_cause;
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data[PSOC_MSTR_IF_MAX];
	struct hl_eq_glbl_err glbl_err_data;
};

/**
 * struct hl_eq_pll_sei_data - PLL SEI event information
 * @index: PLL index. Refer to device specific pll index enum
 * @pad: Padding
 * @intr_cause: PLL cause information
 * @glbl_err_data: Global error information
 */
struct hl_eq_pll_sei_data {
	__u8 index;
	__u8 pad[7];
	struct hl_eq_intr_cause intr_cause;
	struct hl_eq_glbl_err glbl_err_data;
};

/**
 * struct hl_eq_sob_cq_data - SOB SEI event info related to CQ security event.
 * @cq_intr: 1 if this is a CQ security event, 0 otherwise
 * @cq_intr_queue_idx: If cq_intr is 1, this is the queue idx, unused otherwise.
 * @pad: Padding
 */
struct hl_eq_sob_cq_data {
	__u8 cq_intr;
	__u8 cq_intr_queue_idx;
	__u8 pad[6];
};

/**
 * struct hl_eq_sob_sei_data sob_sei_data - SOB SEI event information
 * @intr_cause: SM cause information
 * @cq_data: Info related to the cause of CQ security event.
 * @rtr_data: RTR RAZWI LBW and HBW information
 * @mstr_if_data: MSTR IF RAZWI information
 * @glbl_err_data: Global error information
 */
struct hl_eq_sob_sei_data {
	struct hl_eq_intr_cause intr_cause;
	struct hl_eq_sob_cq_data cq_data;
	struct hl_eq_razwi_rtr_data rtr_data;
	struct hl_eq_razwi_mstr_if_data mstr_if_data;
	struct hl_eq_glbl_err glbl_err_data;
};

enum hl_eq_pmmu_err_type {
	PMMU_ERR_TYPE_ACCESS_ERR,
	PMMU_ERR_TYPE_PAGE_ERR,
	PMMU_ERR_TYPE_MAX
};

/**
 * struct hl_eq_pmmu_err_data - PMMU SPI error information
 * @axid: AXI ID
 * @va: the virtual address that caused the error
 */
struct hl_eq_pmmu_err_data {
	__le64 axid;
	__le64 va;
};

/**
 * struct hl_eq_pmmu_spi_data - PMMU SPI event information
 * @intr_cause: PMMU cause information
 * @err_data: PMMU page and access error information
 */
struct hl_eq_pmmu_spi_data {
	struct hl_eq_intr_cause intr_cause;
	struct hl_eq_pmmu_err_data err_data[PMMU_ERR_TYPE_MAX];
};

/**
 * struct hl_eq_cpld_reset_reason
 * @reg: array of cause registers
 */
struct hl_eq_cpld_reset_reason {
	__u8 reg[CPLD_RESET_REASON_MAX_REGS];
	__u8 pad[7];
};

struct hl_eq_entry {
	struct hl_eq_header hdr;
	union {
		__le64 data_placeholder;
		struct hl_eq_ecc_data ecc_data;
		struct hl_eq_hbm_ecc_data hbm_ecc_data;	/* Obsolete */
		struct hl_eq_sm_sei_data sm_sei_data;
		struct cpucp_pkt_sync_err pkt_sync_err;
		struct hl_eq_fw_alive fw_alive;
		struct hl_eq_intr_cause intr_cause;
		struct hl_eq_pcie_drain_ind_data pcie_drain_ind_data;
		struct hl_eq_razwi_info razwi_info;
		struct hl_eq_razwi_with_intr_cause razwi_with_intr_cause;
		struct hl_eq_hbm_sei_data sei_data;	/* Gaudi2 HBM */
		struct hl_eq_engine_arc_intr_data arc_data;
		struct hl_eq_addr_dec_intr_data addr_dec;
		struct hl_eq_nic_intr_cause nic_intr_cause;
		__le64 data[7];
	};
};

enum hl_pvt_alarm_type {
	PVT_TS_ALARM_A,
	PVT_TS_ALARM_B,
	PVT_ALARM_MAX
};

/**
 * struct hl_eq_pvt_alarm_data
 * @hdcore: hdcore: enum hl_agg_hdcore_type
 * @die_id: die id
 * @alarm_type: enum hl_pvt_alarm_type
 * @chn_bitmask: bit mask of dts channels in pvt eg: ch 0 & 1 chn_num = 0x3, ch 8 & 2 chn_num= 0x104
 * @pad: align to 8 bytes
 */
struct hl_eq_pvt_alarm_data {
	uint8_t hdcore;
	uint8_t die_id;
	uint8_t alarm_type;
	uint16_t chn_bitmask;
	uint8_t pad[3];
};

/* entry size is dynamic and maximum size is passed during boot */
struct hl_eq_dynamic_entry {
	struct hl_eq_header hdr;
	struct hl_agg_eq_header agg_hdr; /* valid only for aggregator events */
	union {
		__le64 data_placeholder;
		struct hl_eq_ecc_data ecc_data;
		struct hl_eq_intr_cause intr_cause;
		struct hl_eq_pcie_sei_data pcie_sei_data;
		struct hl_eq_pcie_spi_data pcie_spi_data;
		struct hl_eq_nic_spi_data nic_spi_data;
		struct hl_eq_nic_sts_req_data nic_sts_req_data;
		struct hl_eq_tpc_spi_data tpc_spi_data;
		struct hl_eq_tpc_sei_data tpc_sei_data;
		struct hl_eq_rot_spi_data rot_spi_data;
		struct hl_eq_rot_sei_data rot_sei_data;
		struct hl_eq_mme_spi_data mme_spi_data;
		struct hl_eq_hbm_mc_spi_data hbm_mc_spi_data;
		struct hl_eq_mme_sei_data mme_sei_data;
		struct hl_eq_generic_spi_data spi_data;
		struct hl_eq_stlb_spi_data stlb_spi_data;
		struct hl_eq_stlb_sei_data stlb_sei_data;
		struct hl_eq_cs_sei_data cs_sei_data;
		struct hl_eq_edma_sei_data edma_sei_data;
		struct hl_eq_arcfarm_sei_data arcfarm_sei_data;
		struct hl_eq_razwi_with_intr_cause_data razwi_with_intr_cause;
		struct hl_eq_nic_sei_data nic_sei_data;
		struct hl_eq_d2dmac_sei_data d2dmac_sei_data;
		struct hl_eq_d2dphy_sei_data d2dphy_sei_data;
		struct hl_eq_pdma_sei_data pdma_sei_data;
		struct hl_eq_nch_sei_data nch_sei_data;
		struct hl_eq_cpu_sei_data cpu_sei_data;
		struct hl_eq_parc_sei_data parc_sei_data;
		struct hl_eq_psoc_sei_data psoc_sei_data;
		struct hl_eq_pll_sei_data pll_sei_data;
		struct hl_eq_sob_sei_data sob_sei_data;
		struct hl_eq_pmmu_spi_data pmmu_spi_data;
		struct hl_eq_hbm_mc_cmn_sei_info hbm_mc_cmn_sei_info;
		struct hl_eq_cpld_reset_reason cpld_reset_reason;
		struct hl_eq_pvt_alarm_data pvt_alarm_data;
	};
};

#define HL_EQ_ENTRY_SIZE		sizeof(struct hl_eq_entry)
#define HL_EQ_DYNAMIC_ENTRY_SIZE	sizeof(struct hl_eq_dynamic_entry)

#define EQ_CTL_READY_SHIFT		31
#define EQ_CTL_READY_MASK		0x80000000

/*
 * MODE 0 - struct hl_eq_header
 * MODE 1 - struct hl_agg_eq_header
 */
#define EQ_CTL_EVENT_MODE_SHIFT		28
#define EQ_CTL_EVENT_MODE_MASK		0x70000000

#define EQ_CTL_EVENT_TYPE_SHIFT		16
#define EQ_CTL_EVENT_TYPE_MASK		0x0FFF0000

#define EQ_CTL_INDEX_SHIFT		0
#define EQ_CTL_INDEX_MASK		0x0000FFFF

/*
 * 0 - non-critical event
 * 1 - critical event
 */
#define EQ_FLAGS_CRITICAL_EVENT_SHIFT	0
#define EQ_FLAGS_CRITICAL_EVENT_MASK	0x0001

enum pq_init_status {
	PQ_INIT_STATUS_NA = 0,
	PQ_INIT_STATUS_READY_FOR_CP,
	PQ_INIT_STATUS_READY_FOR_HOST,
	PQ_INIT_STATUS_READY_FOR_CP_SINGLE_MSI,
	PQ_INIT_STATUS_LEN_NOT_POWER_OF_TWO_ERR,
	PQ_INIT_STATUS_ILLEGAL_Q_ADDR_ERR
};

/*
 * CpuCP Primary Queue Packets
 *
 * During normal operation, the host's kernel driver needs to send various
 * messages to CpuCP, usually either to SET some value into a H/W periphery or
 * to GET the current value of some H/W periphery. For example, SET the
 * frequency of MME/TPC and GET the value of the thermal sensor.
 *
 * These messages can be initiated either by the User application or by the
 * host's driver itself, e.g. power management code. In either case, the
 * communication from the host's driver to CpuCP will *always* be in
 * synchronous mode, meaning that the host will send a single message and poll
 * until the message was acknowledged and the results are ready (if results are
 * needed).
 *
 * This means that only a single message can be sent at a time and the host's
 * driver must wait for its result before sending the next message. Having said
 * that, because these are control messages which are sent in a relatively low
 * frequency, this limitation seems acceptable. It's important to note that
 * in case of multiple devices, messages to different devices *can* be sent
 * at the same time.
 *
 * The message, inputs/outputs (if relevant) and fence object will be located
 * on the device DDR at an address that will be determined by the host's driver.
 * During device initialization phase, the host will pass to CpuCP that address.
 * Most of the message types will contain inputs/outputs inside the message
 * itself. The common part of each message will contain the opcode of the
 * message (its type) and a field representing a fence object.
 *
 * When the host's driver wishes to send a message to CPU CP, it will write the
 * message contents to the device DDR, clear the fence object and then write to
 * the PSOC_ARC1_AUX_SW_INTR, to issue interrupt 121 to ARC Management CPU.
 *
 * Upon receiving the interrupt (#121), CpuCP will read the message from the
 * DDR. In case the message is a SET operation, CpuCP will first perform the
 * operation and then write to the fence object on the device DDR. In case the
 * message is a GET operation, CpuCP will first fill the results section on the
 * device DDR and then write to the fence object. If an error occurred, CpuCP
 * will fill the rc field with the right error code.
 *
 * In the meantime, the host's driver will poll on the fence object. Once the
 * host sees that the fence object is signaled, it will read the results from
 * the device DDR (if relevant) and resume the code execution in the host's
 * driver.
 *
 * To use QMAN packets, the opcode must be the QMAN opcode, shifted by 8
 * so the value being put by the host's driver matches the value read by CpuCP
 *
 * Non-QMAN packets should be limited to values 1 through (2^8 - 1)
 *
 * Detailed description:
 *
 * CPUCP_PACKET_DISABLE_PCI_ACCESS -
 *       After receiving this packet the embedded CPU must NOT issue PCI
 *       transactions (read/write) towards the Host CPU. This also include
 *       sending MSI-X interrupts.
 *       This packet is usually sent before the device is moved to D3Hot state.
 *
 * CPUCP_PACKET_ENABLE_PCI_ACCESS -
 *       After receiving this packet the embedded CPU is allowed to issue PCI
 *       transactions towards the Host CPU, including sending MSI-X interrupts.
 *       This packet is usually send after the device is moved to D0 state.
 *
 * CPUCP_PACKET_TEMPERATURE_GET -
 *       Fetch the current temperature / Max / Max Hyst / Critical /
 *       Critical Hyst of a specified thermal sensor. The packet's
 *       arguments specify the desired sensor and the field to get.
 *
 * CPUCP_PACKET_VOLTAGE_GET -
 *       Fetch the voltage / Max / Min of a specified sensor. The packet's
 *       arguments specify the sensor and type.
 *
 * CPUCP_PACKET_CURRENT_GET -
 *       Fetch the current / Max / Min of a specified sensor. The packet's
 *       arguments specify the sensor and type.
 *
 * CPUCP_PACKET_FAN_SPEED_GET -
 *       Fetch the speed / Max / Min of a specified fan. The packet's
 *       arguments specify the sensor and type.
 *
 * CPUCP_PACKET_PWM_GET -
 *       Fetch the pwm value / mode of a specified pwm. The packet's
 *       arguments specify the sensor and type.
 *
 * CPUCP_PACKET_PWM_SET -
 *       Set the pwm value / mode of a specified pwm. The packet's
 *       arguments specify the sensor, type and value.
 *
 * CPUCP_PACKET_FREQUENCY_SET -
 *       Set the frequency of a specified PLL. The packet's arguments specify
 *       the PLL and the desired frequency. The actual frequency in the device
 *       might differ from the requested frequency.
 *
 * CPUCP_PACKET_FREQUENCY_GET -
 *       Fetch the frequency of a specified PLL. The packet's arguments specify
 *       the PLL.
 *
 * CPUCP_PACKET_LED_SET -
 *       Set the state of a specified led. The packet's arguments
 *       specify the led and the desired state.
 *
 * CPUCP_PACKET_I2C_WR -
 *       Write 32-bit value to I2C device. The packet's arguments specify the
 *       I2C bus, address and value.
 *
 * CPUCP_PACKET_I2C_RD -
 *       Read 32-bit value from I2C device. The packet's arguments specify the
 *       I2C bus and address.
 *
 * CPUCP_PACKET_INFO_GET -
 *       Fetch information from the device as specified in the packet's
 *       structure. The host's driver passes the max size it allows the CpuCP to
 *       write to the structure, to prevent data corruption in case of
 *       mismatched driver/FW versions.
 *
 * CPUCP_PACKET_FLASH_PROGRAM_REMOVED - this packet was removed
 *
 * CPUCP_PACKET_UNMASK_RAZWI_IRQ -
 *       Unmask the given IRQ. The IRQ number is specified in the value field.
 *       The packet is sent after receiving an interrupt and printing its
 *       relevant information.
 *
 * CPUCP_PACKET_UNMASK_RAZWI_IRQ_ARRAY -
 *       Unmask the given IRQs. The IRQs numbers are specified in an array right
 *       after the cpucp_packet structure, where its first element is the array
 *       length. The packet is sent after a soft reset was done in order to
 *       handle any interrupts that were sent during the reset process.
 *
 * CPUCP_PACKET_TEST -
 *       Test packet for CpuCP connectivity. The CPU will put the fence value
 *       in the result field.
 *
 * CPUCP_PACKET_FREQUENCY_CURR_GET -
 *       Fetch the current frequency of a specified PLL. The packet's arguments
 *       specify the PLL.
 *
 * CPUCP_PACKET_MAX_POWER_GET -
 *       Fetch the maximal power of the device.
 *
 * CPUCP_PACKET_MAX_POWER_SET -
 *       Set the maximal power of the device. The packet's arguments specify
 *       the power.
 *
 * CPUCP_PACKET_EEPROM_DATA_GET -
 *       Get EEPROM data from the CpuCP kernel. The buffer is specified in the
 *       addr field. The CPU will put the returned data size in the result
 *       field. In addition, the host's driver passes the max size it allows the
 *       CpuCP to write to the structure, to prevent data corruption in case of
 *       mismatched driver/FW versions.
 *
 * CPUCP_PACKET_NIC_INFO_GET -
 *       Fetch information from the device regarding the NIC. the host's driver
 *       passes the max size it allows the CpuCP to write to the structure, to
 *       prevent data corruption in case of mismatched driver/FW versions.
 *
 * CPUCP_PACKET_TEMPERATURE_SET -
 *       Set the value of the offset property of a specified thermal sensor.
 *       The packet's arguments specify the desired sensor and the field to
 *       set.
 *
 * CPUCP_PACKET_VOLTAGE_SET -
 *       Trigger the reset_history property of a specified voltage sensor.
 *       The packet's arguments specify the desired sensor and the field to
 *       set.
 *
 * CPUCP_PACKET_CURRENT_SET -
 *       Trigger the reset_history property of a specified current sensor.
 *       The packet's arguments specify the desired sensor and the field to
 *       set.
 *
 * CPUCP_PACKET_PCIE_THROUGHPUT_GET -
 *       Get throughput of PCIe.
 *       The packet's arguments specify the transaction direction (TX/RX).
 *       The window measurement is 10[msec], and the return value is in KB/sec.
 *
 * CPUCP_PACKET_PCIE_REPLAY_CNT_GET
 *       Replay count measures number of "replay" events, which is basicly
 *       number of retries done by PCIe.
 *
 * CPUCP_PACKET_TOTAL_ENERGY_GET -
 *       Total Energy is measurement of energy from the time FW Linux
 *       is loaded. It is calculated by multiplying the average power
 *       by time (passed from armcp start). The units are in MilliJouls.
 *
 * CPUCP_PACKET_PLL_INFO_GET -
 *       Fetch frequencies of PLL from the required PLL IP.
 *       The packet's arguments specify the device PLL type
 *       Pll type is the PLL from device pll_index enum.
 *       The result is composed of 4 outputs, each is 16-bit
 *       frequency in MHz.
 *
 * CPUCP_PACKET_POWER_GET -
 *       Fetch the present power consumption of the device (Current * Voltage).
 *
 * CPUCP_PACKET_NIC_PFC_SET -
 *       Enable/Disable the NIC PFC feature. The packet's arguments specify the
 *       NIC port, relevant lanes to configure and one bit indication for
 *       enable/disable.
 *
 * CPUCP_PACKET_NIC_FAULT_GET -
 *       Fetch the current indication for local/remote faults from the NIC MAC.
 *       The result is 32-bit value of the relevant register.
 *
 * CPUCP_PACKET_NIC_LPBK_SET -
 *       Enable/Disable the MAC loopback feature. The packet's arguments specify
 *       the NIC port, relevant lanes to configure and one bit indication for
 *       enable/disable.
 *
 * CPUCP_PACKET_NIC_MAC_INIT -
 *       Configure the NIC MAC channels. The packet's arguments specify the
 *       NIC port and the speed.
 *
 * CPUCP_PACKET_MSI_INFO_SET -
 *       set the index number for each supported msi type going from
 *       host to device
 *
 * CPUCP_PACKET_NIC_XPCS91_REGS_GET -
 *       Fetch the un/correctable counters values from the NIC MAC.
 *
 * CPUCP_PACKET_NIC_STAT_REGS_GET -
 *       Fetch various NIC MAC counters from the NIC STAT.
 *
 * CPUCP_PACKET_NIC_STAT_REGS_CLR -
 *       Clear the various NIC MAC counters in the NIC STAT.
 *
 * CPUCP_PACKET_NIC_STAT_REGS_ALL_GET -
 *       Fetch all NIC MAC counters from the NIC STAT.
 *
 * CPUCP_PACKET_IS_IDLE_CHECK -
 *       Check if the device is IDLE in regard to the DMA/compute engines
 *       and QMANs. The f/w will return a bitmask where each bit represents
 *       a different engine or QMAN according to enum cpucp_idle_mask.
 *       The bit will be 1 if the engine is NOT idle.
 *
 * CPUCP_PACKET_HBM_REPLACED_ROWS_INFO_GET -
 *       Fetch all HBM replaced-rows and prending to be replaced rows data.
 *
 * CPUCP_PACKET_HBM_PENDING_ROWS_STATUS -
 *       Fetch status of HBM rows pending replacement and need a reboot to
 *       be replaced.
 *
 * CPUCP_PACKET_POWER_SET -
 *       Resets power history of device to 0
 *
 * CPUCP_PACKET_SECURITY_SET -
 *       Enable security with fake fuse support (flash memory used to store
 *       security information) if efuse is not programmed to enable real
 *       security. If security is enabled, this request will be NACKed
 *
 * CPUCP_PACKET_ENGINE_CORE_ASID_SET -
 *       Packet to perform engine core ASID configuration
 *
 * CPUCP_PACKET_MMU_PAGES_GET -
 *       Fetch all MMU page indexes which will be used in host MMU.
 *
 * CPUCP_PACKET_SEC_ATTEST_GET -
 *       Get the attestaion data that is collected during various stages of the
 *       boot sequence. the attestation data is also hashed with some unique
 *       number (nonce) provided by the host to prevent replay attacks.
 *       public key and certificate also provided as part of the FW response.
 *
 * CPUCP_PACKET_INFO_SIGNED_GET -
 *       Get the device information signed by the Trusted Platform device.
 *       device info data is also hashed with some unique number (nonce) provided
 *       by the host to prevent replay attacks. public key and certificate also
 *       provided as part of the FW response.
 *
 * CPUCP_PACKET_NIC_SET_CHECKERS -
 *       Packet to set a specific NIC checker bit.
 *
 * CPUCP_PACKET_MONITOR_DUMP_GET -
 *       Get monitors registers dump from the CpuCP kernel.
 *       The CPU will put the registers dump in the a buffer allocated by the driver
 *       which address is passed via the CpuCp packet. In addition, the host's driver
 *       passes the max size it allows the CpuCP to write to the structure, to prevent
 *       data corruption in case of mismatched driver/FW versions.
 *       Obsolete.
 *
 * CPUCP_PACKET_BINNING_DONE -
 *       Packet is sent when binning and isolation done in lkd is completed.
 *       Before receiving this pkt, fw is not expected to handle GIC interrupts for
 *       components which are binning candidates.
 *
 * CPUCP_PACKET_NIC_WQE_ASID_SET -
 *       Packet to set nic wqe asid as the registers needed are privilege and to be configured by FW
 *
 * CPUCP_PACKET_NIC_ECC_INTRS_UNMASK -
 *       Packet to unmask NIC memory registers which are masked at preboot stage. As per the Arch
 *       team recommendation, NIC memory ECC errors should be unmasked after NIC driver is up and
 *       running
 *
 * CPUCP_PACKET_GENERIC_PASSTHROUGH -
 *       Generic opcode for all firmware info that is only passed to host
 *       through the LKD, without getting parsed there.
 *
 * CPUCP_PACKET_BINNING_SET -
 *       Packet is sent to set default binning masks in firmware shared via buffer allocated
 *       by the driver, which address is passed via the CpuCp packet.
 *       This can be used to for testing purposes to test various binning combinations.
 *       Binning masks will be taken via debugfs and sent to fw via this packet.
 *       Hard reset is required, as new masks will be applied in next boot.
 *
 * CPUCP_PACKET_ACTIVE_STATUS_SET -
 *       LKD sends FW indication whether device is free or in use, this indication is reported
 *       also to the BMC.
 *
 * CPUCP_PACKET_NIC_MAC_TX_RESET -
 *       Packet to reset the NIC MAC Tx.
 *
 * CPUCP_PACKET_WD_DISABLE -
 *       Disable Watchdog. 1 - watchdog is disabled 0 - watchdog is enabled (default)
 *       When disabled, watchdog reset is not triggered and can collect required debug information
 *       when arc is hung
 *       This packet is allowed only in pre-production environments.
 *       If security is enabled or end of manufacturing bit is set, this request will be NACKed
 *
 * CPUCP_PACKET_NIC_WQE_ASID_UNSET -
 *       Packet to unset nic wqe asid as the registers needed are privilege and to be configured
 *       by FW.
 *
 * CPUCP_PACKET_EXPECTED_EQE_SIZE_SET -
 *       LKD sends FW expected size (in bytes) of EQ entry.
 *
 * CPUCP_PACKET_AC_CONTROL -
 *       host control over the Autonomous Controller used for profiler info collection
 *
 * CPUCP_PACKET_SOFT_RESET -
 *       Packet to perform soft-reset.
 *
 * CPUCP_PACKET_INTS_REGISTER -
 *       Packet to inform FW that queues have been established and LKD is ready to receive
 *       EQ events.
 *
 * CPUCP_PACKET_NIC_INIT_TXS_MEM -
 *      Init TXS related memory in HBM.
 *
 * CPUCP_PACKET_NIC_INIT_TMR_MEM -
 *      Init HW timer related memory in HBM.
 *
 * CPUCP_PACKET_NIC_CLR_MEM -
 *      Clear NIC related memory in HBM.
 *
 * CPUCP_PACKET_SET_HOST_TIME -
 *      Host time used for UART Logging
 */

enum cpucp_packet_id {
	CPUCP_PACKET_DISABLE_PCI_ACCESS = 1,	/* internal */
	CPUCP_PACKET_ENABLE_PCI_ACCESS,		/* internal */
	CPUCP_PACKET_TEMPERATURE_GET,		/* sysfs */
	CPUCP_PACKET_VOLTAGE_GET,		/* sysfs */
	CPUCP_PACKET_CURRENT_GET,		/* sysfs */
	CPUCP_PACKET_FAN_SPEED_GET,		/* sysfs */
	CPUCP_PACKET_PWM_GET,			/* sysfs */
	CPUCP_PACKET_PWM_SET,			/* sysfs */
	CPUCP_PACKET_FREQUENCY_SET,		/* sysfs */
	CPUCP_PACKET_FREQUENCY_GET,		/* sysfs */
	CPUCP_PACKET_LED_SET,			/* debugfs */
	CPUCP_PACKET_I2C_WR,			/* debugfs */
	CPUCP_PACKET_I2C_RD,			/* debugfs */
	CPUCP_PACKET_INFO_GET,			/* IOCTL */
	CPUCP_PACKET_FLASH_PROGRAM_REMOVED,
	CPUCP_PACKET_UNMASK_RAZWI_IRQ,		/* internal */
	CPUCP_PACKET_UNMASK_RAZWI_IRQ_ARRAY,	/* internal */
	CPUCP_PACKET_TEST,			/* internal */
	CPUCP_PACKET_FREQUENCY_CURR_GET,	/* sysfs */
	CPUCP_PACKET_MAX_POWER_GET,		/* sysfs */
	CPUCP_PACKET_MAX_POWER_SET,		/* sysfs */
	CPUCP_PACKET_EEPROM_DATA_GET,		/* sysfs */
	CPUCP_PACKET_NIC_INFO_GET,		/* internal */
	CPUCP_PACKET_TEMPERATURE_SET,		/* sysfs */
	CPUCP_PACKET_VOLTAGE_SET,		/* sysfs */
	CPUCP_PACKET_CURRENT_SET,		/* sysfs */
	CPUCP_PACKET_PCIE_THROUGHPUT_GET,	/* internal */
	CPUCP_PACKET_PCIE_REPLAY_CNT_GET,	/* internal */
	CPUCP_PACKET_TOTAL_ENERGY_GET,		/* internal */
	CPUCP_PACKET_PLL_INFO_GET,		/* internal */
	CPUCP_PACKET_NIC_STATUS,		/* internal */
	CPUCP_PACKET_POWER_GET,			/* internal */
	CPUCP_PACKET_NIC_PFC_SET,		/* internal */
	CPUCP_PACKET_NIC_FAULT_GET,		/* internal */
	CPUCP_PACKET_NIC_LPBK_SET,		/* internal */
	CPUCP_PACKET_NIC_MAC_CFG,		/* internal */
	CPUCP_PACKET_MSI_INFO_SET,		/* internal */
	CPUCP_PACKET_NIC_XPCS91_REGS_GET,	/* internal */
	CPUCP_PACKET_NIC_STAT_REGS_GET,		/* internal */
	CPUCP_PACKET_NIC_STAT_REGS_CLR,		/* internal */
	CPUCP_PACKET_NIC_STAT_REGS_ALL_GET,	/* internal */
	CPUCP_PACKET_IS_IDLE_CHECK,		/* internal */
	CPUCP_PACKET_HBM_REPLACED_ROWS_INFO_GET,/* internal */
	CPUCP_PACKET_HBM_PENDING_ROWS_STATUS,	/* internal */
	CPUCP_PACKET_POWER_SET,			/* internal */
	CPUCP_PACKET_SECURITY_SET,		/* debugfs */
	CPUCP_PACKET_ENGINE_CORE_ASID_SET,	/* internal */
	CPUCP_PACKET_MMU_PAGES_GET,		/* internal */
	CPUCP_PACKET_SEC_ATTEST_GET,		/* internal */
	CPUCP_PACKET_INFO_SIGNED_GET,		/* internal */
	CPUCP_PACKET_NIC_SET_CHECKERS,		/* internal */
	CPUCP_PACKET_MONITOR_DUMP_GET,		/* debugfs */
	CPUCP_PACKET_BINNING_DONE,		/* internal */
	CPUCP_PACKET_NIC_WQE_ASID_SET,		/* internal */
	CPUCP_PACKET_NIC_ECC_INTRS_UNMASK,	/* internal */
	CPUCP_PACKET_GENERIC_PASSTHROUGH,	/* IOCTL */
	CPUCP_PACKET_BINNING_SET,		/* debugfs */
	CPUCP_PACKET_ACTIVE_STATUS_SET,		/* internal */
	CPUCP_PACKET_NIC_MAC_TX_RESET,		/* internal */
	CPUCP_PACKET_WD_DISABLE,		/* debugfs */
	CPUCP_PACKET_NIC_WQE_ASID_UNSET,	/* internal */
	CPUCP_PACKET_EXPECTED_EQE_SIZE_SET,	/* internal */
	CPUCP_PACKET_AC_CONTROL,		/* internal */
	CPUCP_PACKET_SOFT_RESET,		/* internal */
	CPUCP_PACKET_INTS_REGISTER,		/* internal */
	CPUCP_PACKET_NIC_INIT_TXS_MEM,		/* internal */
	CPUCP_PACKET_NIC_INIT_TMR_MEM,		/* internal */
	CPUCP_PACKET_NIC_CLR_MEM,		/* internal */
	CPUCP_PACKET_SET_HOST_TIME,		/* internal */
	CPUCP_PACKET_ID_MAX			/* must be last */
};

#define CPUCP_PACKET_FENCE_VAL	0xFE8CE7A5

#define CPUCP_PKT_CTL_RC_SHIFT		12
#define CPUCP_PKT_CTL_RC_MASK		0x0000F000

#define CPUCP_PKT_CTL_OPCODE_SHIFT	16
#define CPUCP_PKT_CTL_OPCODE_MASK	0x1FFF0000

#define CPUCP_PKT_RES_PLL_OUT0_SHIFT	0
#define CPUCP_PKT_RES_PLL_OUT0_MASK	0x000000000000FFFFull
#define CPUCP_PKT_RES_PLL_OUT1_SHIFT	16
#define CPUCP_PKT_RES_PLL_OUT1_MASK	0x00000000FFFF0000ull
#define CPUCP_PKT_RES_PLL_OUT2_SHIFT	32
#define CPUCP_PKT_RES_PLL_OUT2_MASK	0x0000FFFF00000000ull
#define CPUCP_PKT_RES_PLL_OUT3_SHIFT	48
#define CPUCP_PKT_RES_PLL_OUT3_MASK	0xFFFF000000000000ull

#define CPUCP_PKT_RES_EEPROM_OUT0_SHIFT	0
#define CPUCP_PKT_RES_EEPROM_OUT0_MASK	0x000000000000FFFFull
#define CPUCP_PKT_RES_EEPROM_OUT1_SHIFT	16
#define CPUCP_PKT_RES_EEPROM_OUT1_MASK	0x0000000000FF0000ull

#define CPUCP_PKT_VAL_PFC_IN1_SHIFT	0
#define CPUCP_PKT_VAL_PFC_IN1_MASK	0x0000000000000001ull
#define CPUCP_PKT_VAL_PFC_IN2_SHIFT	1
#define CPUCP_PKT_VAL_PFC_IN2_MASK	0x000000000000001Eull

#define CPUCP_PKT_VAL_LPBK_IN1_SHIFT	0
#define CPUCP_PKT_VAL_LPBK_IN1_MASK	0x0000000000000001ull
#define CPUCP_PKT_VAL_LPBK_IN2_SHIFT	1
#define CPUCP_PKT_VAL_LPBK_IN2_MASK	0x000000000000001Eull

#define CPUCP_PKT_VAL_MAC_CNT_IN1_SHIFT	0
#define CPUCP_PKT_VAL_MAC_CNT_IN1_MASK	0x0000000000000001ull
#define CPUCP_PKT_VAL_MAC_CNT_IN2_SHIFT	1
#define CPUCP_PKT_VAL_MAC_CNT_IN2_MASK	0x00000000FFFFFFFEull

#define CPUCP_PKT_VAL_YEAR_MASK		0x0000000000000FFFull
#define CPUCP_PKT_VAL_YEAR_SHIFT	0
#define CPUCP_PKT_VAL_MONTH_MASK	0x000000000000F000ull
#define CPUCP_PKT_VAL_MONTH_SHIFT	12
#define CPUCP_PKT_VAL_DAY_MASK		0x00000000001F0000ull
#define CPUCP_PKT_VAL_DAY_SHIFT		16
#define CPUCP_PKT_VAL_HOUR_MASK		0x0000000003E00000ull
#define CPUCP_PKT_VAL_HOUR_SHIFT	21
#define CPUCP_PKT_VAL_MINUTE_MASK	0x00000000FC000000ull
#define CPUCP_PKT_VAL_MINUTE_SHIFT	26
#define CPUCP_PKT_VAL_SECOND_MASK	0x0000003F00000000ull
#define CPUCP_PKT_VAL_SECOND_SHIFT	32

/* heartbeat status bits */
#define CPUCP_PKT_HB_STATUS_EQ_FAULT_SHIFT		0
#define CPUCP_PKT_HB_STATUS_EQ_FAULT_MASK		0x00000001

struct cpucp_packet {
	union {
		__le64 value;	/* For SET packets */
		__le64 result;	/* For GET packets */
		__le64 addr;	/* For PQ */
	};

	__le32 ctl;

	__le32 fence;		/* Signal to host that message is completed */

	union {
		struct {/* For temperature/current/voltage/fan/pwm get/set */
			__le16 sensor_index;
			__le16 type;
		};

		struct {	/* For I2C read/write */
			__u8 i2c_bus;
			__u8 i2c_addr;
			__u8 i2c_reg;
			/*
			 * In legacy implemetations, i2c_len was not present,
			 * was unused and just added as pad.
			 * So if i2c_len is 0, it is treated as legacy
			 * and r/w 1 Byte, else if i2c_len is specified,
			 * its treated as new multibyte r/w support.
			 */
			__u8 i2c_len;
		};

		struct {/* For PLL info fetch */
			__le16 pll_type;
			/* TODO pll_reg is kept temporary before removal */
			__le16 pll_reg;
		};

		/* For any general request */
		__le32 index;

		/* For frequency get/set */
		__le32 pll_index;

		/* For led set */
		__le32 led_index;

		/* For get CpuCP info/EEPROM data/NIC info */
		__le32 data_max_size;

		/*
		 * For any general status bitmask. Shall be used whenever the
		 * result cannot be used to hold general purpose data.
		 */
		__le32 status_mask;
	};

	union {
		/* For NIC requests */
		__le32 port_index;

		/* For NIC requests */
		__le32 macro_index;

		/* For Generic packet sub index */
		__le32 pkt_subidx;

		/* random, used once number, for security packets */
		__le32 nonce;
	};
};

struct cpucp_unmask_irq_arr_packet {
	struct cpucp_packet cpucp_pkt;
	__le32 length;
	__le32 irqs[];
};

struct cpucp_nic_status_packet {
	struct cpucp_packet cpucp_pkt;
	__le32 length;
	__le32 data[];
};

struct cpucp_array_data_packet {
	struct cpucp_packet cpucp_pkt;
	__le32 length;
	__le32 data[];
};

enum cpucp_led_index {
	CPUCP_LED0_INDEX = 0,
	CPUCP_LED1_INDEX,
	CPUCP_LED2_INDEX,
	CPUCP_LED_MAX_INDEX = CPUCP_LED2_INDEX
};

/*
 * enum cpucp_packet_rc - Error return code
 * @cpucp_packet_success	-> in case of success.
 * @cpucp_packet_invalid	-> this is to support first generation platforms.
 * @cpucp_packet_fault		-> in case of processing error like failing to
 *                                 get device binding or semaphore etc.
 * @cpucp_packet_invalid_pkt	-> when cpucp packet is un-supported.
 * @cpucp_packet_invalid_params	-> when checking parameter like length of buffer
 *				   or attribute value etc.
 * @cpucp_packet_rc_max		-> It indicates size of enum so should be at last.
 */
enum cpucp_packet_rc {
	cpucp_packet_success,
	cpucp_packet_invalid,
	cpucp_packet_fault,
	cpucp_packet_invalid_pkt,
	cpucp_packet_invalid_params,
	cpucp_packet_rc_max
};

/*
 * cpucp_temp_type should adhere to hwmon_temp_attributes
 * defined in Linux kernel hwmon.h file
 */
enum cpucp_temp_type {
	cpucp_temp_input,
	cpucp_temp_min = 4,
	cpucp_temp_min_hyst,
	cpucp_temp_max = 6,
	cpucp_temp_max_hyst,
	cpucp_temp_crit,
	cpucp_temp_crit_hyst,
	cpucp_temp_offset = 19,
	cpucp_temp_lowest = 21,
	cpucp_temp_highest = 22,
	cpucp_temp_reset_history = 23,
	cpucp_temp_warn = 24,
	cpucp_temp_max_crit = 25,
	cpucp_temp_max_warn = 26,
};

enum cpucp_in_attributes {
	cpucp_in_input,
	cpucp_in_min,
	cpucp_in_max,
	cpucp_in_lowest = 6,
	cpucp_in_highest = 7,
	cpucp_in_reset_history,
	cpucp_in_intr_alarm_a,
	cpucp_in_intr_alarm_b,
};

enum cpucp_curr_attributes {
	cpucp_curr_input,
	cpucp_curr_min,
	cpucp_curr_max,
	cpucp_curr_lowest = 6,
	cpucp_curr_highest = 7,
	cpucp_curr_reset_history
};

enum cpucp_fan_attributes {
	cpucp_fan_input,
	cpucp_fan_min = 2,
	cpucp_fan_max
};

enum cpucp_pwm_attributes {
	cpucp_pwm_input,
	cpucp_pwm_enable
};

enum cpucp_pcie_throughput_attributes {
	cpucp_pcie_throughput_tx,
	cpucp_pcie_throughput_rx
};

/* TODO temporary kept before removal */
enum cpucp_pll_reg_attributes {
	cpucp_pll_nr_reg,
	cpucp_pll_nf_reg,
	cpucp_pll_od_reg,
	cpucp_pll_div_factor_reg,
	cpucp_pll_div_sel_reg
};

/* TODO temporary kept before removal */
enum cpucp_pll_type_attributes {
	cpucp_pll_cpu,
	cpucp_pll_pci,
};

/*
 * cpucp_power_type aligns with hwmon_power_attributes
 * defined in Linux kernel hwmon.h file
 */
enum cpucp_power_type {
	CPUCP_POWER_INPUT = 8,
	CPUCP_POWER_INPUT_HIGHEST = 9,
	CPUCP_POWER_RESET_INPUT_HISTORY = 11
};

/*
 * MSI type enumeration table for all ASICs and future SW versions.
 * For future ASIC-LKD compatibility, we can only add new enumerations.
 * at the end of the table (before CPUCP_NUM_OF_MSI_TYPES).
 * Changing the order of entries or removing entries is not allowed.
 */
enum cpucp_msi_type {
	CPUCP_EVENT_QUEUE_MSI_TYPE,
	CPUCP_NIC_PORT1_MSI_TYPE,
	CPUCP_NIC_PORT3_MSI_TYPE,
	CPUCP_NIC_PORT5_MSI_TYPE,
	CPUCP_NIC_PORT7_MSI_TYPE,
	CPUCP_NIC_PORT9_MSI_TYPE,
	CPUCP_EVENT_QUEUE_ERR_MSI_TYPE,
	CPUCP_NUM_OF_MSI_TYPES
};

/*
 * PLL enumeration table used for all ASICs and future SW versions.
 * For future ASIC-LKD compatibility, we can only add new enumerations.
 * at the end of the table.
 * Changing the order of entries or removing entries is not allowed.
 */
enum pll_index {
	CPU_PLL = 0,
	PCI_PLL = 1,
	NIC_PLL = 2,
	DMA_PLL = 3,
	MESH_PLL = 4,
	MME_PLL = 5,
	TPC_PLL = 6,
	IF_PLL = 7,
	SRAM_PLL = 8,
	NS_PLL = 9,
	HBM_PLL = 10,
	MSS_PLL = 11,
	DDR_PLL = 12,
	VID_PLL = 13,
	BANK_PLL = 14,
	MMU_PLL = 15,
	IC_PLL = 16,
	MC_PLL = 17,
	EMMC_PLL = 18,
	D2D_PLL = 19,
	CS_PLL = 20,
	C2C_PLL = 21,
	NCH_PLL = 22,
	C2M_PLL = 23,
	PLL_MAX
};

enum rl_index {
	TPC_RL = 0,
	MME_RL,
	EDMA_RL,
};

enum pvt_index {
	PVT_SW,
	PVT_SE,
	PVT_NW,
	PVT_NE
};

#define NIC_CHECKERS_TYPE_SHIFT		0
#define NIC_CHECKERS_TYPE_MASK		0xFFFF
#define NIC_CHECKERS_CHECK_SHIFT	16
#define NIC_CHECKERS_CHECK_MASK		0x1
#define NIC_CHECKERS_DROP_SHIFT		17
#define NIC_CHECKERS_DROP_MASK		0x1

enum nic_checkers_types {
	RX_PKT_BAD_FORMAT = 0,
	RX_INV_OPCODE,
	RX_INV_SYNDROME,
	RX_WQE_IDX_MISMATCH,
	TX_WQE_IDX_MISMATCH = 0x80
};

enum ac_operation_types {
	AC_OP_START,
	AC_OP_STOP,
	AC_OP_GET_STATUS,
};

/* Event Queue Packets */

struct eq_generic_event {
	union {
		__le64 data[7];
		__u8 data_u8[56];
	};
};

/*
 * CpuCP info
 */

#define CARD_NAME_MAX_LEN		16
#define CPUCP_MAX_SENSORS		128
#define CPUCP_MAX_NICS			128
#define CPUCP_LANES_PER_NIC		4
#define CPUCP_NIC_QSFP_EEPROM_MAX_LEN	1024
#define CPUCP_MAX_NIC_LANES		(CPUCP_MAX_NICS * CPUCP_LANES_PER_NIC)
#define CPUCP_NIC_MASK_ARR_LEN		((CPUCP_MAX_NICS + 63) / 64)
#define CPUCP_NIC_POLARITY_ARR_LEN	((CPUCP_MAX_NIC_LANES + 63) / 64)
#define CPUCP_HBM_ROW_REPLACE_MAX	32

struct cpucp_sensor {
	__le32 type;
	__le32 flags;
};

/**
 * struct cpucp_card_types - ASIC card type.
 * @cpucp_card_type_pci: PCI card.
 * @cpucp_card_type_pmc: PCI Mezzanine Card.
 */
enum cpucp_card_types {
	cpucp_card_type_pci,
	cpucp_card_type_pmc
};

#define CPUCP_SEC_CONF_ENABLED_SHIFT	0
#define CPUCP_SEC_CONF_ENABLED_MASK	0x00000001

#define CPUCP_SEC_CONF_FLASH_WP_SHIFT	1
#define CPUCP_SEC_CONF_FLASH_WP_MASK	0x00000002

#define CPUCP_SEC_CONF_EEPROM_WP_SHIFT	2
#define CPUCP_SEC_CONF_EEPROM_WP_MASK	0x00000004

/**
 * struct cpucp_security_info - Security information.
 * @config: configuration bit field
 * @keys_num: number of stored keys
 * @revoked_keys: revoked keys bit field
 * @min_svn: minimal security version
 */
struct cpucp_security_info {
	__u8 config;
	__u8 keys_num;
	__u8 revoked_keys;
	__u8 min_svn;
};

/**
 * struct cpucp_info - Info from CpuCP that is necessary to the host's driver
 * @sensors: available sensors description.
 * @kernel_version: CpuCP linux kernel version.
 * @rot_binning_mask: Rotator binning mask, 1 bit per Rotator instance
 *                    (0 = functional 1 = binned)
 * @card_type: card configuration type.
 * @card_location: in a server, each card has different connections topology
 *                 depending on its location (relevant for PMC card type)
 * @cpld_version: CPLD programmed F/W version.
 * @infineon_version: Infineon main DC-DC version.
 * @fuse_version: silicon production FUSE information.
 * @thermal_version: thermald S/W version.
 * @cpucp_version: CpuCP S/W version.
 * @infineon_second_stage_version: Infineon 2nd stage DC-DC version.
 * @dram_size: available DRAM size.
 * @card_name: card name that will be displayed in HWMON subsystem on the host
 * @tpc_binning_mask: TPC binning mask, 1 bit per TPC instance
 *                    (0 = functional, 1 = binned)
 * @decoder_binning_mask: Decoder binning mask, 1 bit per decoder instance
 *                        (0 = functional, 1 = binned), maximum 1 per dcore
 * @sram_binning: Categorize SRAM functionality
 *                (0 = fully functional, 1 = lower-half is not functional,
 *                 2 = upper-half is not functional)
 * @sec_info: security information
 * @cpld_timestamp: CPLD programmed F/W timestamp.
 * @pll_map: Bit map of supported PLLs for current ASIC version.
 * @mme_binning_mask: MME binning mask,
 *                    bits [0:6]   <==> dcore0 mme fma
 *                    bits [7:13]  <==> dcore1 mme fma
 *                    bits [14:20] <==> dcore0 mme ima
 *                    bits [21:27] <==> dcore1 mme ima
 *                    For each group, if the 6th bit is set then first 5 bits
 *                    represent the col's idx [0-31], otherwise these bits are
 *                    ignored, and col idx 32 is binned. 7th bit is don't care.
 * @dram_binning_mask: DRAM binning mask, 1 bit per dram instance
 *                     (0 = functional 1 = binned)
 * @memory_repair_flag: eFuse flag indicating memory repair
 * @edma_binning_mask: EDMA binning mask, 1 bit per EDMA instance
 *                     (0 = functional 1 = binned)
 * @xbar_binning_mask: Xbar binning mask, 1 bit per Xbar instance
 *                     (0 = functional 1 = binned)
 * @interposer_version: Interposer version programmed in eFuse
 * @substrate_version: Substrate version programmed in eFuse
 * @eq_health_check_supported: eq health check feature supported in FW.
 * @fw_hbm_region_size: Size in bytes of FW reserved region in HBM.
 * @fw_os_version: Firmware OS Version
 */
struct cpucp_info {
	struct cpucp_sensor sensors[CPUCP_MAX_SENSORS];
	__u8 kernel_version[VERSION_MAX_LEN];
	__le32 rot_binning_mask;
	__le32 card_type;
	__le32 card_location;
	__le32 cpld_version;
	__le32 infineon_version;
	__u8 fuse_version[VERSION_MAX_LEN];
	__u8 thermal_version[VERSION_MAX_LEN];
	__u8 cpucp_version[VERSION_MAX_LEN];
	__le32 infineon_second_stage_version;
	__le64 dram_size;
	char card_name[CARD_NAME_MAX_LEN];
	__le64 tpc_binning_mask;
	__le64 decoder_binning_mask;
	__u8 sram_binning;
	__u8 dram_binning_mask;
	__u8 memory_repair_flag;
	__u8 edma_binning_mask;
	__u8 xbar_binning_mask;
	__u8 interposer_version;
	__u8 substrate_version;
	__u8 eq_health_check_supported;
	struct cpucp_security_info sec_info;
	__le32 cpld_timestamp;
	__u8 pll_map[PLL_MAP_LEN];
	__le64 mme_binning_mask;
	__u8 fw_os_version[VERSION_MAX_LEN];
};

struct cpucp_mac_addr {
	__u8 mac_addr[ETH_ALEN];
};

enum cpucp_serdes_type {
	TYPE_1_SERDES_TYPE,
	TYPE_2_SERDES_TYPE,
	HLS1_SERDES_TYPE,
	HLS1H_SERDES_TYPE,
	HLS2_SERDES_TYPE,
	HLS2_TYPE_1_SERDES_TYPE,
	HLS3_FULLSCALE_IN_SERDES_TYPE,
	HLS3_FULLSCALE_OUT_SERDES_TYPE,
	HLS3_FULL_OAM_3PORTS_SCALE_OUT_SERDES_TYPE,
	HLS3_FULL_OAM_6PORTS_SCALE_OUT_SERDES_TYPE,
	HLS3_SINGLEPORT_OAM_FULLSCALE_OUT_SERDES_TYPE,
	HLS3_SERDES_TYPE_DEPRECATED,
	HLB325_FULL_OAM_3PORTS_SCALE_OUT_SERDES_TYPE,
	HL338_SERDES_TYPE,
	MAX_NUM_SERDES_TYPE,		/* number of types */
	UNKNOWN_SERDES_TYPE = 0xFFFF	/* serdes_type is u16 */
};

struct cpucp_nic_info {
	struct cpucp_mac_addr mac_addrs[CPUCP_MAX_NICS];
	__le64 link_mask[CPUCP_NIC_MASK_ARR_LEN];
	__le64 pol_tx_mask[CPUCP_NIC_POLARITY_ARR_LEN];
	__le64 pol_rx_mask[CPUCP_NIC_POLARITY_ARR_LEN];
	__le64 link_ext_mask[CPUCP_NIC_MASK_ARR_LEN];
	__u8 qsfp_eeprom[CPUCP_NIC_QSFP_EEPROM_MAX_LEN];
	__le64 auto_neg_mask[CPUCP_NIC_MASK_ARR_LEN];
	__le16 serdes_type; /* enum cpucp_serdes_type */
	__le16 tx_swap_map[CPUCP_MAX_NICS];
	__u8 reserved[6];
};

#define PAGE_DISCARD_MAX	64

struct page_discard_info {
	__u8 num_entries;
	__u8 reserved[7];
	__le32 mmu_page_idx[PAGE_DISCARD_MAX];
};

/*
 * struct frac_val - fracture value represented by "integer.frac".
 * @integer: the integer part of the fracture value;
 * @frac: the fracture part of the fracture value.
 */
struct frac_val {
	union {
		struct {
			__le16 integer;
			__le16 frac;
		};
		__le32 val;
	};
};

/*
 * struct ser_val - the SER (symbol error rate) value is represented by "integer * 10 ^ -exp".
 * @integer: the integer part of the SER value;
 * @exp: the exponent part of the SER value.
 */
struct ser_val {
	__le16 integer;
	__le16 exp;
};

/*
 * struct cpucp_nic_status - describes the status of a NIC port.
 * @port: NIC port index.
 * @bad_format_cnt: e.g. CRC.
 * @responder_out_of_sequence_psn_cnt: e.g NAK.
 * @high_ber_reinit_cnt: link reinit due to high BER.
 * @correctable_err_cnt: e.g. bit-flip.
 * @uncorrectable_err_cnt: e.g. MAC errors.
 * @retraining_cnt: re-training counter.
 * @up: is port up.
 * @pcs_link: has PCS link.
 * @phy_ready: is PHY ready.
 * @auto_neg: is Autoneg enabled.
 * @timeout_retransmission_cnt: timeout retransmission events.
 * @high_ber_cnt: high ber events.
 * @pre_fec_ser: pre FEC SER value.
 * @post_fec_ser: post FEC SER value.
 * @throughput: measured throughput.
 * @latency: measured latency.
 * @port_toggle_cnt: counts how many times the link toggled since last port PHY init.
 */
struct cpucp_nic_status {
	__le32 port;
	__le32 bad_format_cnt;
	__le32 responder_out_of_sequence_psn_cnt;
	__le32 high_ber_reinit;
	__le32 correctable_err_cnt;
	__le32 uncorrectable_err_cnt;
	__le32 retraining_cnt;
	__u8 up;
	__u8 pcs_link;
	__u8 phy_ready;
	__u8 auto_neg;
	__le32 timeout_retransmission_cnt;
	__le32 high_ber_cnt;
	struct ser_val pre_fec_ser;
	struct ser_val post_fec_ser;
	struct frac_val bandwidth;
	struct frac_val lat;
	__le32 port_toggle_cnt;
	__u8 reserved[4];
};

enum cpucp_hbm_row_replace_cause {
	REPLACE_CAUSE_DOUBLE_ECC_ERR,
	REPLACE_CAUSE_MULTI_SINGLE_ECC_ERR,
};

struct cpucp_hbm_row_info {
	__u8 hbm_idx;
	__u8 pc;
	__u8 sid;
	__u8 bank_idx;
	__le16 row_addr;
	__u8 replaced_row_cause; /* enum cpucp_hbm_row_replace_cause */
	__u8 pad;
};

struct cpucp_hbm_row_replaced_rows_info {
	__le16 num_replaced_rows;
	__u8 pad[6];
	struct cpucp_hbm_row_info replaced_rows[CPUCP_HBM_ROW_REPLACE_MAX];
};

enum cpu_reset_status {
	CPU_RST_STATUS_NA = 0,
	CPU_RST_STATUS_SOFT_RST_DONE = 1,
};

#define SEC_PCR_DATA_BUF_SZ	256
#define SEC_PCR_QUOTE_BUF_SZ	510	/* (512 - 2) 2 bytes used for size */
#define SEC_SIGNATURE_BUF_SZ	255	/* (256 - 1) 1 byte used for size */
#define SEC_PUB_DATA_BUF_SZ	510	/* (512 - 2) 2 bytes used for size */
#define SEC_CERTIFICATE_BUF_SZ	2046	/* (2048 - 2) 2 bytes used for size */

/*
 * struct cpucp_sec_attest_info - attestation report of the boot
 * @pcr_data: raw values of the PCR registers
 * @pcr_num_reg: number of PCR registers in the pcr_data array
 * @pcr_reg_len: length of each PCR register in the pcr_data array (bytes)
 * @nonce: number only used once. random number provided by host. this also
 *	    passed to the quote command as a qualifying data.
 * @pcr_quote_len: length of the attestation quote data (bytes)
 * @pcr_quote: attestation report data structure
 * @quote_sig_len: length of the attestation report signature (bytes)
 * @quote_sig: signature structure of the attestation report
 * @pub_data_len: length of the public data (bytes)
 * @public_data: public key for the signed attestation
 *		 (outPublic + name + qualifiedName)
 * @certificate_len: length of the certificate (bytes)
 * @certificate: certificate for the attestation signing key
 */
struct cpucp_sec_attest_info {
	__u8 pcr_data[SEC_PCR_DATA_BUF_SZ];
	__u8 pcr_num_reg;
	__u8 pcr_reg_len;
	__le16 pad0;
	__le32 nonce;
	__le16 pcr_quote_len;
	__u8 pcr_quote[SEC_PCR_QUOTE_BUF_SZ];
	__u8 quote_sig_len;
	__u8 quote_sig[SEC_SIGNATURE_BUF_SZ];
	__le16 pub_data_len;
	__u8 public_data[SEC_PUB_DATA_BUF_SZ];
	__le16 certificate_len;
	__u8 certificate[SEC_CERTIFICATE_BUF_SZ];
};

/*
 * struct cpucp_dev_info_signed - device information signed by a secured device
 * @info: device information structure as defined above
 * @nonce: number only used once. random number provided by host. this number is
 *	   hashed and signed along with the device information.
 * @info_sig_len: length of the attestation signature (bytes)
 * @info_sig: signature of the info + nonce data.
 * @pub_data_len: length of the public data (bytes)
 * @public_data: public key info signed info data
 *		 (outPublic + name + qualifiedName)
 * @certificate_len: length of the certificate (bytes)
 * @certificate: certificate for the signing key
 */
struct cpucp_dev_info_signed {
	struct cpucp_info info;	/* assumed to be 64bit aligned */
	__le32 nonce;
	__le32 pad0;
	__u8 info_sig_len;
	__u8 info_sig[SEC_SIGNATURE_BUF_SZ];
	__le16 pub_data_len;
	__u8 public_data[SEC_PUB_DATA_BUF_SZ];
	__le16 certificate_len;
	__u8 certificate[SEC_CERTIFICATE_BUF_SZ];
};

#define DCORE_MON_REGS_SZ	512
/*
 * struct dcore_monitor_regs_data - DCORE monitor regs data.
 * the structure follows sync manager block layout. Obsolete.
 * @mon_pay_addrl: array of payload address low bits.
 * @mon_pay_addrh: array of payload address high bits.
 * @mon_pay_data: array of payload data.
 * @mon_arm: array of monitor arm.
 * @mon_status: array of monitor status.
 */
struct dcore_monitor_regs_data {
	__le32 mon_pay_addrl[DCORE_MON_REGS_SZ];
	__le32 mon_pay_addrh[DCORE_MON_REGS_SZ];
	__le32 mon_pay_data[DCORE_MON_REGS_SZ];
	__le32 mon_arm[DCORE_MON_REGS_SZ];
	__le32 mon_status[DCORE_MON_REGS_SZ];
};

/* contains SM data for each SYNC_MNGR (Obsolete) */
struct cpucp_monitor_dump {
	struct dcore_monitor_regs_data sync_mngr_w_s;
	struct dcore_monitor_regs_data sync_mngr_e_s;
	struct dcore_monitor_regs_data sync_mngr_w_n;
	struct dcore_monitor_regs_data sync_mngr_e_n;
};

/*
 * The Type of the generic request (and other input arguments) will be fetched from user by reading
 * from "pkt_subidx" field in struct cpucp_packet.
 *
 * HL_PASSTHROUGHT_VERSIONS	- Fetch all firmware versions.
 * HL_ECC_INJECTION		- Inject ecc, for debug and non-security only
 * HL_PASSTHROUGH_PID_CMD	- Send commands to the ARCPID
 * HL_GET_ERR_COUNTERS_CMD	- Command to get error counters
 */
enum hl_passthrough_type {
	HL_PASSTHROUGH_VERSIONS,
	HL_ECC_INJECTION,
	HL_PASSTHROUGH_PID_CMD,
	HL_GET_ERR_COUNTERS_CMD,
};

/* structure cpucp_cn_init_hw_mem_packet - used for initializing the assoicated CN(Core NIC)
 * hw(TIMER, TX-SCHEDQ) memory in HBM using the provided parameters.
 * @cpucp_pkt: basic cpucp packet, the rest of the parameters extend the packet.
 * @mem_base_addr: base address of the assoicated memory
 * @num_entries: number of entries.
 * @entry_size: size of entry.
 * @granularity: base value for first element.
 * @pad: padding
 */
struct cpucp_cn_init_hw_mem_packet {
	struct cpucp_packet cpucp_pkt;
	__le64 mem_base_addr;
	__le16 num_entries;
	__le16 entry_size;
	__le16 granularity;
	__u8 pad[2];
};

/* structure cpucp_cn_clear_mem_packet - used for clearing the assoicated CN(Core NIC)
 * memory in HBM using the provided parameters.
 * @cpucp_pkt: basic cpucp packet, the rest of the parameters extend the packet.
 * @mem_base_addr: base address of the assoicated memory
 * @size: size in bytes of the assoicated memory.
 * @pad: padding
 */
struct cpucp_cn_clear_mem_packet {
	struct cpucp_packet cpucp_pkt;
	__le64 mem_base_addr;
	__le32 size;
	__u8 pad[4];
};

#endif /* CPUCP_IF_H */
