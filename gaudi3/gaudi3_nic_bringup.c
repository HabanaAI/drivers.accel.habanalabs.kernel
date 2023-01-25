// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi3_nic.h"

/* mmNIC_TXE_SPECIAL_GLBL_SPARE_0 */
#define NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5457_ENABLE_S 23
#define NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5457_ENABLE_M 0x800000

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

static void gaudi3_nic_config_hw_mac_no_fw(struct hl_device *hdev, u32 port)
{
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	NIC_WREG32(mmD0_NIC0_MAC_AUX_MAC_CFG_SEC, 0);
}

static void gaudi3_nic_config_hw_rxe_no_fw(struct hl_device *hdev, u32 port)
{
	uint32_t rxe_qpc_checks_mask;
	int i;

	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	NIC_WREG32(mmD0_NIC0_RXE_ARPROT_HBW_UNSEC, 0);

	/* Initialize AXI prot bits for all CQs to non-priv, secured, data access */
	for (i = 0 ; i < hdev->asic_prop.nic_props.max_cqs ; i++) {
		NIC_WREG32(mmD0_NIC0_RXE_CQ_AXI_PROT0_0 + i * sizeof(u32), 0);
		NIC_WREG32(mmD0_NIC0_RXE_CQ_AXI_PROT0_1 + i * sizeof(u32), 0);
		NIC_WREG32(mmD0_NIC0_RXE_CQ_AXI_PROT0_2 + i * sizeof(u32), 0);
	}

	/* TODO: consider refining the checks or silent-drops once we stabilize */
	NIC_WREG32(mmD0_NIC0_RXE_PKT_CHECKS_EN, 0);
	NIC_WREG32(mmD0_NIC0_RXE_QPC_CHECKS_EN, 0);
	NIC_WREG32(mmD0_NIC0_RXE_WQE_CHECKS_EN, 0);

	NIC_WREG32(mmD0_NIC0_RXE_PKT_CHECKS_EN,
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

	NIC_RMWREG32(mmD0_NIC0_RXE_PKT_SIZE_CHECK_RC, NIC_MAX_FRM_LEN,
					NIC_RXE_PKT_SIZE_CHECK_RC_MAX_M);
	NIC_RMWREG32(mmD0_NIC0_RXE_PKT_SIZE_CHECK_RAW, ETH_ZLEN,
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

	NIC_WREG32(mmD0_NIC0_RXE_QPC_CHECKS_EN, rxe_qpc_checks_mask);

	NIC_WREG32(mmD0_NIC0_RXE_WQE_CHECKS_EN,
			(1 << NIC_RXE_WQE_CHECKS_EN_WQE_IDX_MISMATCH_S) |
			(1 << NIC_RXE_WQE_CHECKS_EN_WQE_WR_OPCODE_INV_S) |
			(1 << NIC_RXE_WQE_CHECKS_EN_WQE_RDV_OPCODE_INV_S) |
			(1 << NIC_RXE_WQE_CHECKS_EN_WQE_RD_OPCODE_INV_S) |
			(0 << NIC_RXE_WQE_CHECKS_EN_WQE_WR_ZERO_S) |
			(0 << NIC_RXE_WQE_CHECKS_EN_WQE_MULTI_ZERO_S) |
			(0 << NIC_RXE_WQE_CHECKS_EN_WQE_WR_SEND_BIG_S) |
			(0 << NIC_RXE_WQE_CHECKS_EN_WQE_MULTI_BIG_S));

	NIC_WREG32(mmD0_NIC0_RXE_WQE_WQ_WR_OP_DISABLE, (u32) ~WQ_WR_VALID_WQE_OPCODES);
	NIC_WREG32(mmD0_NIC0_RXE_WQE_WQ_RDV_OP_DISABLE, (u32) ~WQ_RDV_VALID_WQE_OPCODES);
	NIC_WREG32(mmD0_NIC0_RXE_WQE_WQ_RD_OP_DISABLE, (u32) ~WQ_RD_VALID_WQE_OPCODES);

	NIC_WREG32(mmD0_NIC0_RXE_PKT_CHECKS_ACTION, 0);
	NIC_WREG32(mmD0_NIC0_RXE_QPC_CHECKS_ACTION, 0);
	NIC_WREG32(mmD0_NIC0_RXE_WQE_CHECKS_ACTION, 0);

	NIC_WREG32(mmD0_NIC0_RXE_PKT_CHECKS_ACTION,
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_BAD_FORMAT_S) |
			(1 << NIC_RXE_PKT_CHECKS_ACTION_PKT_PRS_FSM_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_HDRS_SIZE_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_IPV4_LEN_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_IPV6_LEN_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_ACTION_PKT_TUNNEL_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_ACTION_PKT_PRS_HINT_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_ACTION_PKT_BTH_OPCODE_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_ACTION_PKT_SYNDROME_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_RC_MAX_SIZE_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_RC_MIN_SIZE_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_RAW_INV_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_RAW_INV_LEN_S) |
			(0 << NIC_RXE_PKT_CHECKS_ACTION_PKT_RAW_MIN_SIZE_INV_S) |
			(1 << NIC_RXE_PKT_CHECKS_ACTION_PKT_RAW_MAX_SIZE_INV_S));

	NIC_WREG32(mmD0_NIC0_RXE_QPC_CHECKS_ACTION,
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

	NIC_WREG32(mmD0_NIC0_RXE_WQE_CHECKS_ACTION,
			(1 << NIC_RXE_WQE_CHECKS_ACTION_WQE_IDX_MISMATCH_S) |
			(1 << NIC_RXE_WQE_CHECKS_ACTION_WQE_WR_OPCODE_INV_S) |
			(1 << NIC_RXE_WQE_CHECKS_ACTION_WQE_RDV_OPCODE_INV_S) |
			(1 << NIC_RXE_WQE_CHECKS_ACTION_WQE_RD_OPCODE_INV_S) |
			(0 << NIC_RXE_WQE_CHECKS_ACTION_WQE_WR_ZERO_S) |
			(0 << NIC_RXE_WQE_CHECKS_ACTION_WQE_MULTI_ZERO_S) |
			(0 << NIC_RXE_WQE_CHECKS_ACTION_WQE_WR_SEND_BIG_S) |
			(0 << NIC_RXE_WQE_CHECKS_ACTION_WQE_MULTI_BIG_S));

	/* H9-5454: configure back-pressure threshold */
	NIC_RMWREG32(mmD0_NIC0_RXE_SPECIAL_BASE + mmNIC_RXE_SPECIAL_GLBL_SPARE_0, 64,
			NIC_RXE_SPECIAL_GLBL_SPARE_0_BACK_PRESSURE_TH_M);
}

static void gaudi3_nic_config_hw_qpc_no_fw(struct hl_device *hdev, u32 port)
{
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	NIC_WREG32(mmD0_NIC0_QPC_AXI_PROT, 0);

	/* QP checkers and cfg bits reside in CFG7 and CFG8 and all should be enabled */
	NIC_WREG32(mmD0_NIC0_QPC_QP_UPDATE_ERR_CFG7,
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

	NIC_WREG32(mmD0_NIC0_QPC_QP_UPDATE_ERR_CFG8,
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
	NIC_RMWREG32(mmD0_NIC0_QPC_LBW_PROT, 0, NIC_QPC_LBW_PROT_INTERRUPT_M);
}

static void gaudi3_nic_config_hw_txe_no_fw(struct hl_device *hdev, u32 port)
{
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	/* SW-33416: For rendezvous read request if WQE size is 0,
	 * TX gets stuck. So enable WQE check in HW to check for WQE size == 0
	 * by setting NIC0_TXE_WQE_CHECK_EN_RDV_WR_RD_SIZE_ZERO_EN_S bit.
	 * This will avoid the hang and fail gracefully.
	 */
	NIC_WREG32(mmD0_NIC0_TXE_WQE_CHECK_EN,
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

	NIC_WREG32(mmD0_NIC0_TXE_WQE_CHECK_EN2,
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


	NIC_WREG32(mmD0_NIC0_TXE_WQE_CHECK_QPC_OPC_WR_WQ_TYPE_ERR, (u32) ~WQ_WR_VALID_WQE_OPCODES);
	NIC_WREG32(mmD0_NIC0_TXE_WQE_CHECK_QPC_OPC_RD_WQ_TYPE_ERR, (u32) ~WQ_RD_VALID_WQE_OPCODES);

	/* invalid WQE opcode bit map for WQ type RDV */
	NIC_WREG32(mmD0_NIC0_TXE_WQE_CHECK_CFG6, (u32) ~WQ_RDV_VALID_WQE_OPCODES);
	/* H9-5500 : HW blocks remote log sizes which are equal to MIN_REMOTE_LOG_SIZE, therefore
	 * the value set below is actually 1 less than the actual minimum.
	 */
	NIC_RMWREG32(mmD0_NIC0_TXE_WQE_CHECK_CFG7,
			3, NIC_TXE_WQE_CHECK_CFG7_MIN_REMOTE_LOG_SIZE_M);

	NIC_WREG32(mmD0_NIC0_TXE_WQE_CHECK_WQE_OPCODE_ERR, (u32) ~VALID_WQE_OPCODES);
	NIC_WREG32(mmD0_NIC0_TXE_WQE_CHECK_UPSCALE_ERR, (u32) ~UPSCALE_VALID_WQE_OPCODES);
	NIC_WREG32(mmD0_NIC0_TXE_WQE_CHECK_PLAIN_RDMA_ERR, (u32) ~PLAIN_RDMA_VALID_WQE_OPCODES);

	NIC_WREG32(mmD0_NIC0_TXE_WQE_CHECK_CONST4, 16);
	NIC_WREG32(mmD0_NIC0_TXE_WQE_CHECK_CONST3, 28);

	/* This cfg is added to reject any ethernet packet which is not within the range
	 * defined here. The hardware registers were actually put in place for a different
	 * purpose but we are using those for this check. This is the reason for writing the
	 * minimum value on to the max value register and max value onto the min value
	 * register.
	 */
	NIC_WREG32(mmD0_NIC0_TXE_WQE_CHECK_CFG4, NIC_MAX_FRM_LEN);
	NIC_WREG32(mmD0_NIC0_TXE_WQE_CHECK_CFG5, ETH_ZLEN);

	/* configure read/write wqe transaction on AXI bus to be secured */
	NIC_WREG32(mmD0_NIC0_TXE_WQE_FETCH_AXI_PROT_UNSEC, SECURED_LVL);
	NIC_WREG32(mmD0_NIC0_QPC_WQE_MEM_WRITE_AXI_PROT, 0);

	/* H9-5457: enable the ECO by default */
	NIC_RMWREG32(mmD0_NIC0_TXE_SPECIAL_BASE + mmNIC_TXE_SPECIAL_GLBL_SPARE_0, 1,
			NIC_TXE_SPECIAL_GLBL_SPARE_0_ECO_5457_ENABLE_M);
}

void gaudi3_nic_override_phy_readiness(struct hl_nic_port *nic_port, bool set_ready)
{
	u32 enable_mask, port = nic_port->port, val = 0;
	struct hl_device *hdev = nic_port->hdev;
	struct gaudi3_device *gaudi3;

	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	/* Simulator and PLDM don't receive indication from PHY to MAC as there is no
	 * PHY, hence we need to override this registers manually
	 */
	hdev->asic_funcs->set_priv_assertions(hdev, false);

	if (hdev->nic_lanes_per_port == PORT_LANES_4) {
		if (set_ready)
			val = D0_NIC0_MAC_AUX_PHY_SIG_DETECT_OVRD_SIG_DETECT_ASSERT_M;

		NIC_WREG32(mmD0_NIC0_MAC_AUX_PHY_SIG_DETECT_OVRD, val);
	} else if (hdev->nic_lanes_per_port == PORT_LANES_2) {
		gaudi3 = hdev->asic_specific;

		if (set_ready)
			val = 0x3;

		enable_mask = 0x3 << get_lane_offset(&gaudi3->nic_ports[port]);
		NIC_RMWREG32(mmD0_NIC0_MAC_AUX_PHY_SIG_DETECT_OVRD, val, enable_mask);
	}

	hdev->asic_funcs->set_priv_assertions(hdev, true);
}

int gaudi3_nic_disable_wqe_index_checker_fw(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	struct cpucp_packet pkt;
	int rc;

	/* This is a privilege register that is modified on the go, hence we should disable
	 * assertion on simulator to allow us the modification. At the end of this section we
	 * enable security assertion back. We enter this section only if FW security
	 * is not enabled.
	 */

	if (!(hdev->fw_components & FW_TYPE_BOOT_CPU)) {
		hdev->asic_funcs->set_priv_assertions(hdev, false);

		/* Disable the WQE index checker on the RX side */
		NIC_RMWREG32(mmD0_NIC0_RXE_WQE_CHECKS_EN, 0,
			NIC_RXE_WQE_CHECKS_EN_WQE_IDX_MISMATCH_M);

		hdev->asic_funcs->set_priv_assertions(hdev, true);
	}

	/* Disable the WQE index checker on the RX side */
	memset(&pkt, 0, sizeof(pkt));
	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_SET_CHECKERS << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.value = cpu_to_le64(RX_WQE_IDX_MISMATCH);
	pkt.macro_index = cpu_to_le32(nic_port->nic_macro->idx);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc)
		dev_err(hdev->dev,
			"Failed to disable Rx WQE idx mismatch checker, port %d, rc %d\n",
			port, rc);

	return rc;
}

static void gaudi3_nic_set_rx_drop_eco_no_fw(struct hl_nic_macro *nic_macro)
{
	u32 port, txe_val, txb_disable_eco, rxb_disable_eco;
	struct hl_device *hdev = nic_macro->hdev;
	bool qpc_res_rkey_check_en;

	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	port = gaudi3_nic_get_first_port(nic_macro);

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

	NIC_RMWREG32(mmD0_NIC0_RXE_QPC_CHECKS_EN, qpc_res_rkey_check_en,
			NIC_RXE_QPC_CHECKS_EN_QPC_RES_RKEY_INV_M);
}

static void gaudi3_nic_hw_macro_config_no_fw(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev = nic_macro->hdev;
	struct hl_nic_properties *nic_prop;
	u32 port;

	nic_prop = &hdev->asic_prop.nic_props;

	port = gaudi3_nic_get_first_port(nic_macro);

	gaudi3_nic_config_hw_mac_no_fw(hdev, port);

	gaudi3_nic_config_hw_rxe_no_fw(hdev, port);

	gaudi3_nic_config_hw_qpc_no_fw(hdev, port);

	gaudi3_nic_config_hw_txe_no_fw(hdev, port);

	gaudi3_nic_set_rx_drop_eco_no_fw(nic_macro);
}

void gaudi3_nic_macros_fw_config(struct hl_device *hdev)
{
	struct hl_nic_macro *nic_macro;
	int i;

	if (hdev->reset_info.in_compute_reset)
		return;

	for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++) {
		nic_macro = &hdev->nic.nic_macros[i];

		/* It's not allowed to configure a macro that its port or ports are disabled.
		 * In 400Gbps mode we have a single port in each macro.
		 * In 200Gbps mode we need to check also the second port in the macro. Only if both
		 * of the ports are disabled, we should skip this macro.
		 */
		if (!gaudi3_nic_is_macro_enabled(hdev, nic_macro))
			continue;

		gaudi3_nic_hw_macro_config_no_fw(nic_macro);
	}
}
