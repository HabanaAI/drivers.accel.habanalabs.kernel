// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi3_cn.h"

/* mmNIC_TXE_SPECIAL_GLBL_SPARE_0 */
#define NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5216_BURST_SIZE_S 0
#define NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5216_BURST_SIZE_M 0x3FFFFF

#define NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5216_DISABLE_S 22
#define NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5216_DISABLE_M 0x400000

#define NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5457_ENABLE_S 23
#define NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5457_ENABLE_M 0x800000

#define NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5471_DISABLE_S 24
#define NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5471_DISABLE_M 0x1000000

/* mmNIC_TXE_SPECIAL_GLBL_SPARE_3 */
#define NIC_TXE_SPECIAL_GLBL_SPARE_3_WR_RDV_LIN_PAD_SIZE_S 0
#define NIC_TXE_SPECIAL_GLBL_SPARE_3_WR_RDV_LIN_PAD_SIZE_M 0x1F

#define NIC_TXE_SPECIAL_GLBL_SPARE_3_WR_RDV_MUL_PAD_SIZE_S 5
#define NIC_TXE_SPECIAL_GLBL_SPARE_3_WR_RDV_MUL_PAD_SIZE_M 0x3E0

#define NIC_TXE_SPECIAL_GLBL_SPARE_3_RD_RDV_LIN_PAD_SIZE_S 10
#define NIC_TXE_SPECIAL_GLBL_SPARE_3_RD_RDV_LIN_PAD_SIZE_M 0x7C00

#define NIC_TXE_SPECIAL_GLBL_SPARE_3_RD_RDV_MUL_PAD_SIZE_S 15
#define NIC_TXE_SPECIAL_GLBL_SPARE_3_RD_RDV_MUL_PAD_SIZE_M 0xF8000

#define NIC_TXE_SPECIAL_GLBL_SPARE_3_ECO_5384_DISABLE_S 20
#define NIC_TXE_SPECIAL_GLBL_SPARE_3_ECO_5384_DISABLE_M 0x100000

/* mmNIC_TXB_SPECIAL_GLBL_SPARE_0 */
#define NIC_TXB_SPECIAL_GLBL_SPARE_0_ECO_5384_DISABLE_S 0
#define NIC_TXB_SPECIAL_GLBL_SPARE_0_ECO_5384_DISABLE_M 0x1

/* mmNIC_RXB_CORE_SPECIAL_GLBL_SPARE_0 */
#define NIC_RXB_CORE_SPECIAL_GLBL_SPARE_0_ECO_5384_DISABLE_S 0
#define NIC_RXB_CORE_SPECIAL_GLBL_SPARE_0_ECO_5384_DISABLE_M 0x1

 /* mmNIC_RXE_SPECIAL_GLBL_SPARE_0 */
#define NIC_RXE_SPECIAL_GLBL_SPARE_0_BACK_PRESSURE_TH_S 0
#define NIC_RXE_SPECIAL_GLBL_SPARE_0_BACK_PRESSURE_TH_M 0x7F

#define NIC_RXE_SPECIAL_GLBL_SPARE_0_ECO_5499_DISABLE_S 7
#define NIC_RXE_SPECIAL_GLBL_SPARE_0_ECO_5499_DISABLE_M 0x80

/* mmNIC_QPC_SPECIAL_GLBL_SPARE_0 */
#define NIC_QPC_SPECIAL_GLBL_SPARE_0_ECO_4960_DISABLE_S 0
#define NIC_QPC_SPECIAL_GLBL_SPARE_0_ECO_4960_DISABLE_M 0x1

#define NIC_QPC_SPECIAL_GLBL_SPARE_0_ECO_5456_DISABLE_S 1
#define NIC_QPC_SPECIAL_GLBL_SPARE_0_ECO_5456_DISABLE_M 0x2

#define NIC_QPC_SPECIAL_GLBL_SPARE_0_ECO_5490_DISABLE_S 2
#define NIC_QPC_SPECIAL_GLBL_SPARE_0_ECO_5490_DISABLE_M 0x4

static void gaudi3_cn_config_hw_early_init_fw(struct hl_device *hdev, u32 port)
{
	/* H9-5194: Setting timeout for PRT configurations. */
	NIC_WREG32(mmD0_NIC0_PHY_SPECIAL_BASE + mmPRT_PHY_SPECIAL_GLBL_SPARE_0,
		   hdev->nic_enable_h9_phy_mac_hang_eco ? 0x1001000 : 0);
	NIC_WREG32(mmD0_NIC0_MAC_AUX_SPECIAL_BASE + mmPRT_MAC_AUX_SPECIAL_GLBL_SPARE_0,
		   hdev->nic_enable_h9_phy_mac_hang_eco ? 0x1001000 : 0);
}

static void gaudi3_cn_config_hw_mac_fw(struct hl_device *hdev, u32 port)
{
	NIC_WREG32(mmD0_NIC0_MAC_AUX_BASE + mmPRT_MAC_AUX_MAC_CFG_SEC, 0);
}

static void gaudi3_cn_config_hw_rxe_fw(struct hl_device *hdev, u32 port)
{
	uint32_t rxe_qpc_checks_mask;
	int i;

	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_ARPROT_HBW_UNSEC, 0);

	/* Initialize AXI prot bits for all CQs to non-priv, secured, data access */
	for (i = 0 ; i < GAUDI3_NIC_MAX_CQS_NUM ; i++) {
		NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_CQ_AXI_PROT0_0 + i * sizeof(u32), 0);
		NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_CQ_AXI_PROT0_1 + i * sizeof(u32), 0);
		NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_CQ_AXI_PROT0_2 + i * sizeof(u32), 0);
	}

	/* TODO: consider refining the checks or silent-drops once we stabilize */
	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_PKT_CHECKS_EN, 0);
	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_QPC_CHECKS_EN, 0);
	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_WQE_CHECKS_EN, 0);

	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_PKT_CHECKS_EN,
			(1 << NIC_RXE_PKT_CHECKS_EN_PKT_BAD_FORMAT_S) |
			(1 << NIC_RXE_PKT_CHECKS_EN_PKT_PRS_FSM_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_EN_PKT_HDRS_SIZE_INV_S) |
			/* IPV4/6 checkers have a HW bug inside and hence should remain
			 * disabled. See SW-68016.
			 */
			(0 << NIC_RXE_PKT_CHECKS_EN_PKT_IPV4_LEN_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_EN_PKT_IPV6_LEN_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_EN_PKT_TUNNEL_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_EN_PKT_PRS_HINT_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_EN_PKT_BTH_OPCODE_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_EN_PKT_SYNDROME_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_EN_PKT_RC_MAX_SIZE_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_EN_PKT_RC_MIN_SIZE_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_EN_PKT_RAW_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_EN_PKT_RAW_INV_LEN_S) |
			(1 << NIC_RXE_PKT_CHECKS_EN_PKT_RAW_MIN_SIZE_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_EN_PKT_RAW_MAX_SIZE_INV_S));

	NIC_RMWREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_PKT_SIZE_CHECK_RC, NIC_MAX_FRM_LEN,
					NIC_RXE_PKT_SIZE_CHECK_RC_MAX_M);
	NIC_RMWREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_PKT_SIZE_CHECK_RAW, ETH_ZLEN,
					NIC_RXE_PKT_SIZE_CHECK_RAW_MIN_M);

	rxe_qpc_checks_mask = (1 << NIC_RXE_QPC_CHECKS_EN_QPC_QP_INV_S) |
				(1 << NIC_RXE_QPC_CHECKS_EN_QPC_TS_MISMATCH_S) |
				(1 << NIC_RXE_QPC_CHECKS_EN_QPC_REQ_CS_INV_S) |
				(1 << NIC_RXE_QPC_CHECKS_EN_QPC_RES_CS_INV_S) |
				(1 << NIC_RXE_QPC_CHECKS_EN_QPC_RES_RESYNC_INV_S) |
				(1 << NIC_RXE_QPC_CHECKS_EN_QPC_REQ_PSN_INV_S) |
				(1 << NIC_RXE_QPC_CHECKS_EN_QPC_REQ_PSN_UNSENT_S) |
				(1 << NIC_RXE_QPC_CHECKS_EN_QPC_REQ_SAL_NTS_INV_S) |
				(1 << NIC_RXE_QPC_CHECKS_EN_QPC_RES_SAL_PSN_INV_S);

	if (!hdev->nic_enable_h9_rx_drop_eco)
		rxe_qpc_checks_mask |= (1 << NIC_RXE_QPC_CHECKS_EN_QPC_RES_RKEY_INV_S);

	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_QPC_CHECKS_EN, rxe_qpc_checks_mask);

	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_WQE_CHECKS_EN,
			(0 << NIC_RXE_WQE_CHECKS_EN_WQE_IDX_MISMATCH_S) |
			(1 << NIC_RXE_WQE_CHECKS_EN_WQE_WR_OPCODE_INV_S) |
			(1 << NIC_RXE_WQE_CHECKS_EN_WQE_RDV_OPCODE_INV_S) |
			(1 << NIC_RXE_WQE_CHECKS_EN_WQE_RD_OPCODE_INV_S) |
			(0 << NIC_RXE_WQE_CHECKS_EN_WQE_WR_ZERO_S) |
			(0 << NIC_RXE_WQE_CHECKS_EN_WQE_MULTI_ZERO_S) |
			(0 << NIC_RXE_WQE_CHECKS_EN_WQE_WR_SEND_BIG_S) |
			(0 << NIC_RXE_WQE_CHECKS_EN_WQE_MULTI_BIG_S));

	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_WQE_WQ_WR_OP_DISABLE,
					(u32) ~WQ_WR_VALID_WQE_OPCODES);
	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_WQE_WQ_RDV_OP_DISABLE,
					(u32) ~WQ_RDV_VALID_WQE_OPCODES);
	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_WQE_WQ_RD_OP_DISABLE,
					(u32) ~WQ_RD_VALID_WQE_OPCODES);

	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_PKT_CHECKS_ACTION, 0);
	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_QPC_CHECKS_ACTION, 0);
	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_WQE_CHECKS_ACTION, 0);

	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_PKT_CHECKS_ACTION,
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_BAD_FORMAT_S) |
			(1 << NIC_RXE_PKT_CHECKS_ACTION_PKT_PRS_FSM_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_HDRS_SIZE_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_IPV4_LEN_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_IPV6_LEN_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_TUNNEL_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_ACTION_PKT_PRS_HINT_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_ACTION_PKT_BTH_OPCODE_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_SYNDROME_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_RC_MAX_SIZE_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_RC_MIN_SIZE_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_RAW_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_RAW_INV_LEN_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_RAW_MIN_SIZE_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_RAW_MAX_SIZE_INV_S));

	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_QPC_CHECKS_ACTION,
			(1 << NIC_RXE_QPC_CHECKS_ACTION_QPC_QP_INV_S) |
			(1 << NIC_RXE_QPC_CHECKS_ACTION_QPC_TS_MISMATCH_S) |
			(1 << NIC_RXE_QPC_CHECKS_ACTION_QPC_REQ_CS_INV_S) |
			(0 << NIC_RXE_QPC_CHECKS_ACTION_QPC_RES_CS_INV_S) |
			(0 << NIC_RXE_QPC_CHECKS_ACTION_QPC_RES_RESYNC_INV_S) |
			(0 << NIC_RXE_QPC_CHECKS_ACTION_QPC_REQ_PSN_INV_S) |
			(0 << NIC_RXE_QPC_CHECKS_ACTION_QPC_REQ_PSN_UNSENT_S) |
			(0 << NIC_RXE_QPC_CHECKS_ACTION_QPC_REQ_SAL_NTS_INV_S) |
			(1 << NIC_RXE_QPC_CHECKS_ACTION_QPC_RES_RKEY_INV_S) |
			(0 << NIC_RXE_QPC_CHECKS_ACTION_QPC_RES_SAL_PSN_INV_S));

	NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_WQE_CHECKS_ACTION,
			(1 << NIC_RXE_WQE_CHECKS_ACTION_WQE_IDX_MISMATCH_S) |
			(1 << NIC_RXE_WQE_CHECKS_ACTION_WQE_WR_OPCODE_INV_S) |
			(1 << NIC_RXE_WQE_CHECKS_ACTION_WQE_RDV_OPCODE_INV_S) |
			(1 << NIC_RXE_WQE_CHECKS_ACTION_WQE_RD_OPCODE_INV_S) |
			(0 << NIC_RXE_WQE_CHECKS_ACTION_WQE_WR_ZERO_S) |
			(0 << NIC_RXE_WQE_CHECKS_ACTION_WQE_MULTI_ZERO_S) |
			(0 << NIC_RXE_WQE_CHECKS_ACTION_WQE_WR_SEND_BIG_S) |
			(0 << NIC_RXE_WQE_CHECKS_ACTION_WQE_MULTI_BIG_S));

	/* H9-5454: configure back-pressure threshold */
	NIC_RMWREG32(mmD0_NIC0_RXE_SPECIAL_BASE + mmNIC_RXE_SPECIAL_GLBL_SPARE_0,
			hdev->nic_enable_h9_rxb_mem_deadlock_eco ? 0x40 : 0x3f,
			NIC_RXE_SPECIAL_GLBL_SPARE_0_BACK_PRESSURE_TH_M);
}

static void gaudi3_cn_config_hw_qpc_fw(struct hl_device *hdev, u32 port)
{
	NIC_WREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_AXI_PROT, 0);

	/* QP checkers and cfg bits reside in CFG7 and CFG8 and all should be enabled */
	NIC_WREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_QP_UPDATE_ERR_CFG7,
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_SEC_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_ASID_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_PI_EX_WQ_SIZE_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_WQ_TYPE_RD_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_QPC_VALID_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_SEC_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_ASID_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_PI_EX_WQ_SIZE_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_WQ_TYPE_RD_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_QPC_VALID_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_SEC_ERR_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_ASID_ERR_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_PI_EX_WQ_SIZE_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_WQ_TYPE_RD_ERR_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_DB_QPC_VALID_ERR_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_PATCHER_SEC_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_PATCHER_ASID_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_PATCHER_QPC_VALID_CHECK_E_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_PATCHER_SEC_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_PATCHER_ASID_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_PATCHER_QPC_VALID_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_PATCHER_SEC_ERR_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_PATCHER_ASID_ERR_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_PATCHER_QPC_ERR_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_TX_REQ_WQ_TYPE_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_TX_REQ_QPC_VALID_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_TX_REQ_WQ_TYPE_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_TX_REQ_QPC_VLD_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_TX_REQ_WQ_TYPE_ERR_SET_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG7_TX_REQ_QPC_VLD_ERR_SET_S));

	NIC_WREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_QP_UPDATE_ERR_CFG8,
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_BBR_SEC_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_BBR_ASID_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_BBR_QPC_VALID_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_BBR_SEC_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_BBR_ASID_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_BBR_QPC_VALID_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_BBR_SEC_ERR_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_BBR_ASID_ERR_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_BBR_QPC_VALID_ERR_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_TX_RESP_QPC_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_TX_RESP_QPC_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_TX_RESP_QPC_ERR_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_REQ_QPC_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_REQ_PSE_EXCD_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_REQ_QPC_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_REQ_PSE_EXCD_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_REQ_QPC_ERR_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_REQ_PSE_EXCD_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_REQ_RDV_QPC_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_REQ_RDV_WQ_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_REQ_RDV_QPC_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_REQ_RDV_WQ_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_REQ_RDV_QPC_ERR_SET_ER_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_REQ_RDV_WQ_ERR_SET_ERR_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_RESP_QPC_CHECK_EN_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_RESP_QPC_ERR_TO_EQ_S) |
			(1 << NIC_QPC_QP_UPDATE_ERR_CFG8_RX_RESP_QPC_ERR_SET_ERR_S));

	/* The NIC HW generates an MSI-X interrupt by writing to the
	 * PCIE_MSIX_INTR register, for non-secured accesses the HW verifies access-permission via
	 * a table holding all MSI-X indexes which are allocated to the user.
	 * In order to bypass the above check we need to configure the NIC LBW access level to be
	 * secured unprivileged.
	 */
	NIC_RMWREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_LBW_PROT, 0, NIC_QPC_LBW_PROT_INTERRUPT_M);

	/* ECO H9-5216 - solves performance issue for single QP, the ECO can be disabled through
	 * a BFE parameter. The burst size is the same as we configure in QPC.
	 */
	NIC_RMWREG32(mmD0_NIC0_TXE_SPECIAL_BASE + mmNIC_TXE_SPECIAL_GLBL_SPARE_0,
			QPC_REQ_BURST_SIZE, NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5216_BURST_SIZE_M);
}

static void gaudi3_cn_config_hw_txe_fw(struct hl_device *hdev, u32 port)
{
	/* SW-33416: For rendezvous read request if WQE size is 0,
	 * TX gets stuck. So enable WQE check in HW to check for WQE size == 0
	 * by setting NIC0_TXE_WQE_CHECK_EN_RDV_WR_RD_SIZE_ZERO_EN_S bit.
	 * This will avoid the hang and fail gracefully.
	 */
	NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_WQE_CHECK_EN,
		(1 << NIC_TXE_WQE_CHECK_EN_WRITE_VALID_OPCODE_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN_RDV_VALID_OPCODE_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN_READ_VALID_OPCODE_EN_S) |
		(0 << NIC_TXE_WQE_CHECK_EN_INLINE_VALID_OPCODE_EN_S) |
		(0 << NIC_TXE_WQE_CHECK_EN_WRITE_SIZE_ZERO_EN_S) |
		(0 << NIC_TXE_WQE_CHECK_EN_STRIDE_SIZE_ZERO_EN_S) |
		(0 << NIC_TXE_WQE_CHECK_EN_SEND_SIZE_ZERO_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN_RDV_WR_RD_SIZE_ZERO_EN_S) |
		(0 << NIC_TXE_WQE_CHECK_EN_WRITE_SEND_SIZE_2BIG_EN_S) |
		(0 << NIC_TXE_WQE_CHECK_EN_STRIDE_SIZE_2BIG_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN_RDV_REMOTE_LOG_SIZE_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN_RDV_WR_SIZE_BAD_EN_S) |
		(0 << NIC_TXE_WQE_CHECK_EN_RDV_RD_SIZE_BAD_EN_S) |
		(0 << NIC_TXE_WQE_CHECK_EN_INLINE_SIZE_BAD_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN_STRIDE_BAD_GRAN_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN_RAW_COMP_CONVERT_EN_S) |
		(0 << NIC_TXE_WQE_CHECK_EN_STRIDE_WQE_SIZE_SMALL_EN_S) |
		(0 << NIC_TXE_WQE_CHECK_EN_UPSCALE_ALIGN_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN_UPSCALE_VALID_OPCODE_EN_S) |
		(0 << NIC_TXE_WQE_CHECK_EN_RAW_UNSUPPORTED_SIZE_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN_SACK_NOP_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN_RAW_BAD_OPCODE_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN_PLAIN_VALID_OPCODE_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN_PLAIN_CONVERT_EN_S) |
		(0 << NIC_TXE_WQE_CHECK_EN_CONVERT_BAD_ALIGN_EN_S) |
		(0 << NIC_TXE_WQE_CHECK_EN_RSVD_S));

	NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_WQE_CHECK_EN2,
		(0 << NIC_TXE_WQE_CHECK_EN2_RSVD_S) |
		(0 << NIC_TXE_WQE_CHECK_EN2_WQE_SENT_OVERFLOW_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN2_MIN_LOCAL_WQ_LOG_SIZE_ERR_S) |
		(1 << NIC_TXE_WQE_CHECK_EN2_MAX_LOCAL_WQ_LOG_SIZE_ERR_S) |
		(1 << NIC_TXE_WQE_CHECK_EN2_RAW_ABOVE_MIN_RAW_UNSUPPO_S) |
		(1 << NIC_TXE_WQE_CHECK_EN2_RAW_BELOW_MAX_RAW_UNSUPPO_S) |
		(1 << NIC_TXE_WQE_CHECK_EN2_RDV_READ_AND_INLINE_ERR_S) |
		(1 << NIC_TXE_WQE_CHECK_EN2_ATOMIC_FNA_BAD_SIZE_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN2_ATOMIC_FNA_BAD_ADDR_EN_S) |
		(1 << NIC_TXE_WQE_CHECK_EN2_OPCODE_ABOVE_15_ERR_S) |
		(1 << NIC_TXE_WQE_CHECK_EN2_WQE_SE_NOT_RAW_S) |
		(0 << NIC_TXE_WQE_CHECK_EN2_WQE_BAD_SIZE_ERR_S) |
		(0 << NIC_TXE_WQE_CHECK_EN2_TUNNEL_ZERO_SIZE_ERR_S) |
		(0 << NIC_TXE_WQE_CHECK_EN2_TUNNEL_MAX_SIZE_ERR_S));


	NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_WQE_CHECK_QPC_OPC_WR_WQ_TYPE_ERR,
						(u32) ~WQ_WR_VALID_WQE_OPCODES);
	NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_WQE_CHECK_QPC_OPC_RD_WQ_TYPE_ERR,
					(u32) ~WQ_RD_VALID_WQE_OPCODES);

	/* invalid WQE opcode bit map for WQ type RDV */
	NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_WQE_CHECK_CFG6, (u32) ~WQ_RDV_VALID_WQE_OPCODES);
	/* H9-5500 : HW blocks remote log sizes which are equal to MIN_REMOTE_LOG_SIZE, therefore
	 * the value set below is actually 1 less than the actual minimum.
	 */
	NIC_RMWREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_WQE_CHECK_CFG7,
			3, NIC_TXE_WQE_CHECK_CFG7_MIN_REMOTE_LOG_SIZE_M);

	NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_WQE_CHECK_WQE_OPCODE_ERR,
						(u32) ~VALID_WQE_OPCODES);
	NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_WQE_CHECK_UPSCALE_ERR,
					(u32) ~UPSCALE_VALID_WQE_OPCODES);
	NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_WQE_CHECK_PLAIN_RDMA_ERR,
					(u32) ~PLAIN_RDMA_VALID_WQE_OPCODES);

	NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_WQE_CHECK_CONST4, 16);
	NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_WQE_CHECK_CONST3, 28);

	/* This cfg is added to reject any ethernet packet which is not within the range
	 * defined here. The hardware registers were actually put in place for a different
	 * purpose but we are using those for this check. This is the reason for writing the
	 * minimum value on to the max value register and max value onto the min value
	 * register.
	 */
	NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_WQE_CHECK_CFG4, NIC_MAX_FRM_LEN);
	NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_WQE_CHECK_CFG5, ETH_ZLEN);

	/* configure read/write wqe transaction on AXI bus to be secured */
	NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_WQE_FETCH_AXI_PROT_UNSEC, SECURED_LVL);
	NIC_WREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_WQE_MEM_WRITE_AXI_PROT, 0);

	/* H9-5457: ECO to solve daedlock in SACK, enabled by default, but can be disabled
	 * through a BFE.
	 */
	NIC_RMWREG32(mmD0_NIC0_TXE_SPECIAL_BASE + mmNIC_TXE_SPECIAL_GLBL_SPARE_0,
			hdev->nic_enable_h9_sack_deadlock_eco ? 1 : 0,
			NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5457_ENABLE_M);
}

static void gaudi3_cn_config_hw_tmr_fw(struct hl_device *hdev, u32 port)
{
	u32 axuser_hbw_reg_base = NIC_REG(mmD0_NIC0_TMR_AXUSER_AXUSER_BASE);

	/* configure MMU-BP for TIMERS */
	gaudi3_axuser_hbw_mmu_bp_set(hdev, axuser_hbw_reg_base, true);

	/* Perform read to flush the writes */
	NIC_RREG32(mmD0_NIC0_TMR_AXUSER_AXUSER_BASE + mmNIC_TMR_AXUSER_AXUSER_HB_MMU_BYPASS);
}

static void gaudi3_cn_set_rx_drop_eco_fw(struct hl_device *hdev, u32 port)
{
	u32 txe_val, txb_disable_eco, rxb_disable_eco;
	bool qpc_res_rkey_check_en;

	if (hdev->nic_enable_h9_rx_drop_eco) {
		txe_val = (0x1F << NIC_TXE_SPECIAL_GLBL_SPARE_3_WR_RDV_LIN_PAD_SIZE_S) |
				(0x1D << NIC_TXE_SPECIAL_GLBL_SPARE_3_WR_RDV_MUL_PAD_SIZE_S) |
				(0x1D << NIC_TXE_SPECIAL_GLBL_SPARE_3_RD_RDV_LIN_PAD_SIZE_S) |
				(0x1B << NIC_TXE_SPECIAL_GLBL_SPARE_3_RD_RDV_MUL_PAD_SIZE_S);

		txb_disable_eco = 0;
		rxb_disable_eco = 0;
		qpc_res_rkey_check_en  = 0;
	} else {
		txe_val = 0x1 << NIC_TXE_SPECIAL_GLBL_SPARE_3_ECO_5384_DISABLE_S;

		txb_disable_eco = 1;
		rxb_disable_eco = 1;
		qpc_res_rkey_check_en  = 1;
	}

	/* Set padding per WQE type */
	NIC_WREG32(mmD0_NIC0_TXE_SPECIAL_BASE + mmNIC_TXE_SPECIAL_GLBL_SPARE_3, txe_val);

	NIC_RMWREG32(mmD0_NIC0_TXB_SPECIAL_BASE + mmNIC_TXB_SPECIAL_GLBL_SPARE_0, txb_disable_eco,
			NIC_TXB_SPECIAL_GLBL_SPARE_0_ECO_5384_DISABLE_M);

	NIC_RMWREG32(mmD0_NIC0_RXB_CORE_SPECIAL_BASE + mmNIC_RXB_CORE_SPECIAL_GLBL_SPARE_0,
			rxb_disable_eco, NIC_RXB_CORE_SPECIAL_GLBL_SPARE_0_ECO_5384_DISABLE_M);

	NIC_RMWREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_QPC_CHECKS_EN, qpc_res_rkey_check_en,
			NIC_RXE_QPC_CHECKS_EN_QPC_RES_RKEY_INV_M);
}

static void gaudi3_cn_hw_macro_config_fw(struct hl_device *hdev, int macro_idx)
{
	u32 port;

	port = gaudi3_cn_get_first_port(hdev, macro_idx);

	gaudi3_cn_config_hw_early_init_fw(hdev, port);

	gaudi3_cn_config_hw_mac_fw(hdev, port);

	gaudi3_cn_config_hw_tmr_fw(hdev, port);

	gaudi3_cn_config_hw_rxe_fw(hdev, port);

	gaudi3_cn_config_hw_qpc_fw(hdev, port);

	gaudi3_cn_config_hw_txe_fw(hdev, port);

	gaudi3_cn_set_rx_drop_eco_fw(hdev, port);
}

void gaudi3_cn_macros_fw_config(struct hl_device *hdev)
{
	int i;

	if (hdev->reset_info.in_compute_reset || (hdev->fw_components & FW_TYPE_PREBOOT_CPU))
		return;

	for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++) {
		/* It's not allowed to configure a macro that its port or ports are disabled.
		 * In 400Gbps mode we have a single port in each macro.
		 * In 200Gbps mode we need to check also the second port in the macro. Only if both
		 * of the ports are disabled, we should skip this macro.
		 */
		if (!gaudi3_cn_is_macro_enabled(hdev, i))
			continue;

		gaudi3_cn_hw_macro_config_fw(hdev, i);
	}
}

void gaudi3_cn_restore_dynamic_cfg_soft_reset_fw(struct hl_device *hdev)
{
	u32 port;
	int i;


	for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++) {
		port = gaudi3_cn_get_first_port(hdev, i);
		/* It's not allowed to configure a macro that its port or ports are disabled. */
		if (!gaudi3_cn_is_macro_enabled(hdev, i))
			continue;

		NIC_RMWREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_WQE_CHECKS_EN, 0,
				NIC_RXE_WQE_CHECKS_EN_WQE_IDX_MISMATCH_M);
	}
}

static void gaudi3_cn_set_qpc_doorbells_eco_pldm(struct hl_device *hdev, int macro_idx)
{
	u32 port = gaudi3_cn_get_first_port(hdev, macro_idx);

	/* H9-4960 register is active low == 0 is enabled. It is enabled by default. */
	NIC_RMWREG32(mmD0_NIC0_QPC_SPECIAL_BASE + mmNIC_QPC_SPECIAL_GLBL_SPARE_0,
			hdev->nic_enable_h9_qp_doorbells_eco ? 0 : 1,
			NIC_QPC_SPECIAL_GLBL_SPARE_0_ECO_4960_DISABLE_M);
}

static void gaudi3_cn_set_cc_message_drops_eco_pldm(struct hl_device *hdev, int macro_idx)
{
	u32 port = gaudi3_cn_get_first_port(hdev, macro_idx);

	/* H9-5456 register is active low == 0 is enabled. It is enabled by default. */
	NIC_RMWREG32(mmD0_NIC0_QPC_SPECIAL_BASE + mmNIC_QPC_SPECIAL_GLBL_SPARE_0,
			hdev->nic_enable_h9_cc_msg_drops_eco ? 0 : 1,
			NIC_QPC_SPECIAL_GLBL_SPARE_0_ECO_5456_DISABLE_M);
}

static void gaudi3_cn_set_remote_pi_update_eco_pldm(struct hl_device *hdev, int macro_idx)
{
	u32 port = gaudi3_cn_get_first_port(hdev, macro_idx);

	/* H9-5490 register is active low == 0 is enabled. It is enabled by default. */
	NIC_RMWREG32(mmD0_NIC0_QPC_SPECIAL_BASE + mmNIC_QPC_SPECIAL_GLBL_SPARE_0,
			hdev->nic_enable_h9_remote_pi_update_eco ? 0 : 1,
			NIC_QPC_SPECIAL_GLBL_SPARE_0_ECO_5490_DISABLE_M);
}

static void gaudi3_cn_set_sal_override_eco_pldm(struct hl_device *hdev, int macro_idx)
{
	u32 port = gaudi3_cn_get_first_port(hdev, macro_idx);

	/* H9-5499 is by default enabled in HW, this function disables it upon request */
	if (hdev->nic_enable_h9_sal_override_eco)
		return;

	NIC_RMWREG32(mmD0_NIC0_RXE_SPECIAL_BASE + mmNIC_RXE_SPECIAL_GLBL_SPARE_0, 1,
			NIC_RXE_SPECIAL_GLBL_SPARE_0_ECO_5499_DISABLE_M);
}

static void gaudi3_cn_set_txe_buff_alloc_eco_pldm(struct hl_device *hdev, int macro_idx)
{
	u32 port = gaudi3_cn_get_first_port(hdev, macro_idx);

	/* H9-5471 register is active low == 0 is enabled. It is enabled by default. */
	NIC_RMWREG32(mmD0_NIC0_TXE_SPECIAL_BASE + mmNIC_TXE_SPECIAL_GLBL_SPARE_0,
			hdev->nic_enable_h9_txe_buff_alloc_eco ? 0 : 1,
			NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5471_DISABLE_M);
}

static void gaudi3_cn_set_single_qp_perf_eco_pldm(struct hl_device *hdev, int macro_idx)
{
	u32 port = gaudi3_cn_get_first_port(hdev, macro_idx);

	/* H9-5216 register is active low == 0 is enabled. It is enabled by default.
	 * The configuration of the burst size for that ECO resides in QPC configuration function.
	 */
	NIC_RMWREG32(mmD0_NIC0_TXE_SPECIAL_BASE + mmNIC_TXE_SPECIAL_GLBL_SPARE_0,
			hdev->nic_enable_h9_single_qp_perf_fix_eco ? 0 : 1,
			NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5216_DISABLE_M);
}

void gaudi3_cn_ecos_override(struct hl_device *hdev)
{
	int i;

	/* This ECOs override function is relvant as long as security is not enabled,
	 * regardless of FW presence.
	 */
	if (hdev->reset_info.in_compute_reset || hdev->asic_prop.fw_security_enabled)
		return;

	for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++) {
		/* It's not allowed to configure a macro that its port or ports are disabled.
		 * In 400Gbps mode we have a single port in each macro.
		 * In 200Gbps mode we need to check also the second port in the macro. Only if both
		 * of the ports are disabled, we should skip this macro.
		 */
		if (!gaudi3_cn_is_macro_enabled(hdev, i))
			continue;

		/* ECO BFE flag configuration flow - used for debug on PLDM
		 * regardless if there is FW or no
		 */

		gaudi3_cn_set_qpc_doorbells_eco_pldm(hdev, i);

		gaudi3_cn_set_cc_message_drops_eco_pldm(hdev, i);

		gaudi3_cn_set_remote_pi_update_eco_pldm(hdev, i);

		gaudi3_cn_set_sal_override_eco_pldm(hdev, i);

		gaudi3_cn_set_txe_buff_alloc_eco_pldm(hdev, i);

		gaudi3_cn_set_single_qp_perf_eco_pldm(hdev, i);
	}
}
