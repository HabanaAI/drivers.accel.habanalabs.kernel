// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi2_cn.h"

#define NIC_SKB_PAD_SIZE	187

static void set_txe_checkers(struct hl_device *hdev, u32 port)
{
	/* SW-33416: For rendezvous read request if WQE size is 0, TX gets stuck.
	 * So enable WQE check in HW to check for WQE size == 0 by setting
	 * NIC0_TXE0_WQE_CHECK_EN_RDV_WR_RD_SIZE_ZERO_EN_SHIFT bit.
	 * This will avoid the hang and fail gracefully.
	 */

	NIC_WREG32(mmNIC0_TXE0_WQE_CHECK_EN,
		(1 << NIC0_TXE0_WQE_CHECK_EN_WRITE_VALID_OPCODE_EN_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN_RDV_VALID_OPCODE_EN_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN_READ_VALID_OPCODE_EN_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN_GAUDI1_VALID_OPCODE_EN_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN_WRITE_SIZE_ZERO_EN_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN_STRIDE_SIZE_ZERO_EN_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN_SEND_SIZE_ZERO_EN_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN_RDV_WR_RD_SIZE_ZERO_EN_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN_WRITE_SEND_SIZE_2BIG_EN_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN_STRIDE_SIZE_2BIG_EN_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN_RDV_REMOTE_LOG_SIZE_EN_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN_RDV_WR_SIZE_BAD_EN_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN_RDV_RD_SIZE_BAD_EN_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN_INLINE_SIZE_BAD_EN_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN_GAUDI1_INLINE_VALID_EN_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN_STRIDE_BAD_GRAN_EN_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN_WQE_RESERVED_NOT_ZERO_EN_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN_WQE_INDEX_EN_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN_STRIDE_WQE_SIZE_SMALL_EN_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN_UPSCALE_ALIGN_EN_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN_UPSCALE_VALID_OPCODE_EN_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN_RAW_UNSUPPORTED_SIZE_EN_SHIFT));

	NIC_WREG32(mmNIC0_TXE0_WQE_CHECK_EN2,
		(0 << NIC0_TXE0_WQE_CHECK_EN2_QOS_INLINE_ERR_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN2_OPCODE_ABOVE_15_ERR_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN2_RAW_ABOVE_MIN_RAW_UNSUPPO_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN2_RAW_BELOW_MAX_RAW_UNSUPPO_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN2_DIS_REDUCTION_NOT_ZERO_ER_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN2_RDV_READ_AND_INLINE_ERR_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN2_RDV_FETCH_AND_INLINE_ERR_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN2_WQE_FETCH_WR_SIZE_NOT4_ER_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN2_WQE_FETCH_WR_ADDR_MOD4_ER_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN2_LAST_INDEX_RDV_ERR_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN2_GUADI1_MULTI_DUAL_ERR_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN2_WQE_BAD_OPCODE_ERR_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN2_WQE_BAD_SIZE_ERR_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN2_WQE_SE_NOT_RAW_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN2_GAUDI1_TUNNEL_ERR_SHIFT) |
		(1 << NIC0_TXE0_WQE_CHECK_EN2_TUNNEL_ZERO_SIZE_ERR_SHIFT) |
		(0 << NIC0_TXE0_WQE_CHECK_EN2_TUNNEL_MAX_SIZE_ERR_SHIFT));

	NIC_RMWREG32(mmNIC0_TXE0_WQE_CHECK_CFG1,
			~0x7E, NIC0_TXE0_WQE_CHECK_CFG1_QPC_OPC_WR_WQE_TYPE_ERR_MASK);
	NIC_RMWREG32(mmNIC0_TXE0_WQE_CHECK_CFG1,
			~0x1C, NIC0_TXE0_WQE_CHECK_CFG1_QPC_OPC_RD_WQE_TYPE_ERR_MASK);
	NIC_RMWREG32(mmNIC0_TXE0_WQE_CHECK_CFG6,
			~0x1C, NIC0_TXE0_WQE_CHECK_CFG6_QPC_OPC_RDV_WQE_TYPE_ERR_MASK);
	NIC_RMWREG32(mmNIC0_TXE0_WQE_CHECK_CFG6,
			4, NIC0_TXE0_WQE_CHECK_CFG6_MIN_REMOTE_LOG_SIZE_MASK);
	NIC_RMWREG32(mmNIC0_TXE0_WQE_CHECK_CFG3,
			~0x7E, NIC0_TXE0_WQE_CHECK_CFG3_UPSCALE_WQE_TYPE_ERR_MASK);
	NIC_RMWREG32(mmNIC0_TXE0_WQE_CHECK_CFG2,
			~0x7E, NIC0_TXE0_WQE_CHECK_CFG2_ERR_WQE_OPCODE_MASK);
	NIC_RMWREG32(mmNIC0_TXE0_WQE_CHECK_CONST4,
			16, NIC0_TXE0_WQE_CHECK_CONST4_RDV_GRAN0_WQE_SIZE_MASK);
	NIC_RMWREG32(mmNIC0_TXE0_WQE_CHECK_CONST3,
			48, NIC0_TXE0_WQE_CHECK_CONST3_RDV_GRAN1_WQE_SIZE_MASK);

	/* This cfg is added to reject any ethernet packet which is not within the range
	 * defined here. The hardware registers were actually put in place for a different
	 * purpose but we are using those for this check. This is the reason for writing
	 * the minimum value on to the max value register and max value onto the min value
	 * register.
	 */
	NIC_RMWREG32(mmNIC0_TXE0_WQE_CHECK_CFG4, NIC_MAX_FRM_LEN,
			NIC0_TXE0_WQE_CHECK_CFG4_MIN_RAW_UNSUPPORTED_SIZE_MASK);

	/* As a W/A for H/W bug H6-3399, we increase our Tx packets by padding them with bigger
	 * value than the default. Reflect the minimum Ethernet packet size in the below checker.
	 */
	NIC_RMWREG32(mmNIC0_TXE0_WQE_CHECK_CFG5, NIC_SKB_PAD_SIZE,
			NIC0_TXE0_WQE_CHECK_CFG5_MAX_RAW_UNSUPPORTED_SIZE_MASK);
}

static void set_rxe_checkers(struct hl_device *hdev, u32 port)
{
	/* TODO: consider refining the checks or silent-drops once we stabilize */
	NIC_WREG32(mmNIC0_RXE0_RXE_CHECKS,
		(1 << NIC0_RXE0_RXE_CHECKS_QP_INVALID_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_TS_MISMATCH_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_REQ_CS_INVALID_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_RES_CS_INVALID_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_REQ_PSN_INVALID_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_REQ_PSN_UNSENT_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_RES_RKEY_INVALID_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_RES_RESYNC_INVALID_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_PKT_BAD_FORMAT_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_INV_OPCODE_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_INV_SYNDROME_EN_SHIFT) |
		(0 << NIC0_RXE0_RXE_CHECKS_INV_MIN_PKT_SIZE_RC_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_INV_MAX_PKT_SIZE_RC_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_INV_MIN_PKT_SIZE_RAW_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_INV_MAX_PKT_SIZE_RAW_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_TUNNEL_INV_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_WQE_IDX_MISMATCH_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_WQ_WR_OPCODE_INV_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_WQ_RDV_OPCODE_INV_EN_SHIFT) |
		(1 << NIC0_RXE0_RXE_CHECKS_WQ_RD_OPCODE_INV_EN_SHIFT) |
		(0 << NIC0_RXE0_RXE_CHECKS_WQE_WR_ZERO_EN_SHIFT) |
		(0 << NIC0_RXE0_RXE_CHECKS_WQE_MULTI_ZERO_EN_SHIFT) |
		(0 << NIC0_RXE0_RXE_CHECKS_WQE_WR_SEND_BIG_EN_SHIFT) |
		(0 << NIC0_RXE0_RXE_CHECKS_WQE_MULTI_BIG_EN_SHIFT));

	NIC_WREG32(mmNIC0_RXE0_PKT_DROP,
		(1 << NIC0_RXE0_PKT_DROP_ERR_QP_INVALID_SHIFT) |
		(1 << NIC0_RXE0_PKT_DROP_ERR_TS_MISMATCH_SHIFT) |
		(1 << NIC0_RXE0_PKT_DROP_ERR_REQ_CS_INVALID_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_RES_CS_INVALID_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_REQ_PSN_INVALID_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_REQ_PSN_UNSENT_SHIFT) |
		(1 << NIC0_RXE0_PKT_DROP_ERR_RES_RKEY_INVALID_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_RES_RESYNC_INVALID_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_PKT_BAD_FORMAT_SHIFT) |
		(1 << NIC0_RXE0_PKT_DROP_ERR_INV_OPCODE_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_INV_SYNDROME_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_INV_MIN_PKT_SIZE_RC_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_INV_MAX_PKT_SIZE_RC_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_INV_MIN_PKT_SIZE_RAW_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_INV_MAX_PKT_SIZE_RAW_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_TUNNEL_INV_SHIFT) |
		(1 << NIC0_RXE0_PKT_DROP_ERR_WQE_IDX_MISMATCH_SHIFT) |
		(1 << NIC0_RXE0_PKT_DROP_ERR_WQ_WR_OPCODE_INV_SHIFT) |
		(1 << NIC0_RXE0_PKT_DROP_ERR_WQ_RDV_OPCODE_INV_SHIFT) |
		(1 << NIC0_RXE0_PKT_DROP_ERR_WQ_RD_OPCODE_INV_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_WQE_WR_ZERO_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_WQE_MULTI_ZERO_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_WQE_WR_SEND_BIG_SHIFT) |
		(0 << NIC0_RXE0_PKT_DROP_ERR_WQE_MULTI_BIG_SHIFT));

	NIC_RMWREG32(mmNIC0_RXE0_WQE_WQ_WR_OP_DISABLE,
			~0x7E, NIC0_RXE0_WQE_WQ_WR_OP_DISABLE_VAL_MASK);
	NIC_RMWREG32(mmNIC0_RXE0_WQE_WQ_RDV_OP_DISABLE,
			~0x1C, NIC0_RXE0_WQE_WQ_RDV_OP_DISABLE_VAL_MASK);
	NIC_RMWREG32(mmNIC0_RXE0_WQE_WQ_RD_OP_DISABLE,
			~0x1C, NIC0_RXE0_WQE_WQ_RD_OP_DISABLE_VAL_MASK);
	NIC_RMWREG32(mmNIC0_RXE0_PKT_SIZE_CHECK_RC, NIC_MAX_FRM_LEN,
			NIC0_RXE0_PKT_SIZE_CHECK_RC_MAX_MASK);
	NIC_RMWREG32(mmNIC0_RXE0_PKT_SIZE_CHECK_RAW, ETH_ZLEN,
			NIC0_RXE0_PKT_SIZE_CHECK_RAW_MIN_MASK);
}

static void gaudi2_cn_set_correct_address_for_errors(struct hl_device *hdev)
{
	int port;

	/* SW-67666: a workaround to omit PCIE_ADDR_DEC_ERR interrupt when ports are down.
	 * Correct address to return error for PRT region only otherwise
	 * will hit the MX0 region and lead to wrong results.
	 * Write only for the odd ports as we need to write to TXS1, TXS0 is unused.
	 */
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	for (port = 0 ; port < NIC_NUMBER_OF_PORTS ; port++)
		if (port & 1)
			NIC_WREG32(mmNIC0_TXS0_ASYNC_NICD_APB_SPLIT_ADDR3, 0x39000);
}

static void gaudi2_cn_config_hw_txs_no_fw(struct hl_device *hdev, u32 port)
{
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	NIC_WREG32(mmNIC0_TXS0_FORCE_HIT_EN, 0);
}

static void gaudi2_cn_config_hw_txe_no_fw(struct hl_device *hdev, u32 port)
{
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	/* Make sure data fetch can never be privileged */
	NIC_WREG32(mmNIC0_TXE0_DATA_FETCH_AXI_PROT, 0x80);

	/* Make sure WQE fetch can never be privileged */
	NIC_WREG32(mmNIC0_TXE0_WQE_FETCH_AXI_PROT, 0);

	/* Due to H/W bug H6-3379, we disable the WQ cache */
	NIC_WREG32(mmNIC0_TXE0_TXWQC, 0);

	set_txe_checkers(hdev, port);
}

static void gaudi2_cn_config_hw_rxe_no_fw(struct hl_device *hdev, u32 port)
{
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	NIC_WREG32(mmNIC0_RXE0_LBW_BASE_LO, lower_32_bits(CFG_BASE & U32_MAX));
	NIC_WREG32(mmNIC0_RXE0_LBW_BASE_HI, upper_32_bits(CFG_BASE) & 0x3FFFF);

	/* Make sure LBW write access (for SM) can never be privileged */
	NIC_WREG32(mmNIC0_RXE0_AWPROT_LBW, 0x2);

	/* Make sure HBW read access (for WQE) is always unprivileged */
	NIC_WREG32(mmNIC0_RXE0_ARPROT_HBW, 0);

	set_rxe_checkers(hdev, port);
}

static void gaudi2_cn_config_hw_qpc_no_fw(struct hl_device *hdev, u32 port)
{
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	NIC_WREG32(mmNIC0_QPC0_AXI_PROT, 0); /* secured */
	NIC_WREG32(mmNIC0_QPC0_WQE_MEM_WRITE_AXI_PROT, 0);
	NIC_WREG32(mmNIC0_QPC0_ERR_FIFO_CFG, 0);
	NIC_WREG32(mmNIC0_QPC0_ERR_FIFO_PRODUCER_INDEX, 0);
	NIC_WREG32(mmNIC0_QPC0_ERR_FIFO_CONSUMER_INDEX, 0);
	NIC_WREG32(mmNIC0_QPC0_ERR_FIFO_WRITE_INDEX, 0);
	NIC_WREG32(mmNIC0_QPC0_QPC_CLOCK_GATE_DIS, 0x1);

	/* Set the protection level as following:
	 * Qman (bits 0-1): 0x0
	 * Unsecured (bits 2-3): 0x0
	 * Secured (bits 4-5): 0x1
	 * Privilege (bits 6-7): 0x2
	 *
	 * Note that the encoding here is NOT the AXI-prot encoding, but a sequential number that
	 * is compared to the QPC trust level.
	 */
	NIC_WREG32(mmNIC0_QPC0_DOORBELL_SECURITY,
		(0x0 << NIC0_QPC0_DOORBELL_SECURITY_QMAN_SHIFT) |
		(0x0 << NIC0_QPC0_DOORBELL_SECURITY_UNSECURED_SHIFT) |
		(0x1 << NIC0_QPC0_DOORBELL_SECURITY_SECURED_SHIFT) |
		(0x2 << NIC0_QPC0_DOORBELL_SECURITY_PRIVILEGE_SHIFT));
}

static void gaudi2_cn_config_hw_tmr_no_fw(struct hl_device *hdev, u32 port)
{
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_FORCE_HIT_EN, 0);
}

static void gaudi2_cn_config_hw_eq_no_fw(struct hl_device *hdev, u32 port)
{
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	/* The NIC HW generates an MSI-X interrupt by writing to the PCIE_DBI_MSIX_DOORBELL
	 * register which is a secured register, we therefore need to configure the NIC LBW access
	 * level to be secured before letting it to issue interrupts.
	 */
	NIC_RMWREG32(mmNIC0_QPC0_LBW_PROT, 0, NIC0_QPC0_LBW_PROT_INTERRUPT_MASK);
}

void gaudi2_cn_quiescence_phy_no_fw(struct hl_device *hdev)
{
	u32 port, force_link_down = 1 << NIC0_PHY_PHY_RX_CFG_SW_PHY_READY_OVERRIDE_SHIFT;
	int i, num_ports_in_macro = NIC_NUMBER_OF_PORTS / NIC_NUMBER_OF_MACROS;

	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	/* if any NIC is present and running on PLDM or simulator,
	 * prevent traffic from entering the device.
	 */
	if ((hdev->pldm || !hdev->pdev) && hdev->cn.ports_mask) {

		/* This is a privilege register that is modified prior golden register modification,
		 * hence we should disable assertion on simulator to allow us modifying it.
		 * At the end of this section we enable security assertion back.
		 * We enter this section only if we run on simulator of PLDM.
		 */
		hdev->asic_funcs->set_priv_assertions(hdev, false);

		for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++) {
			port = i * num_ports_in_macro;

			/* force PHY link down */
			NIC_MACRO_WREG32(mmNIC0_PHY_PHY_RX_CFG_0,
					force_link_down);
			NIC_MACRO_WREG32(mmNIC0_PHY_PHY_RX_CFG_1,
					force_link_down);
			NIC_MACRO_WREG32(mmNIC0_PHY_PHY_RX_CFG_2,
					force_link_down);
			NIC_MACRO_WREG32(mmNIC0_PHY_PHY_RX_CFG_3,
					force_link_down);
		}

		hdev->asic_funcs->set_priv_assertions(hdev, true);
	}
}

static void gaudi2_cn_hw_macro_config_no_fw(struct hl_device *hdev, u8 macro_idx)
{
	int port = macro_idx << 1; /* the index of the first port in the macro */

	/* TMR Configuration */
	gaudi2_cn_config_hw_tmr_no_fw(hdev, port);
}

static bool gaudi2_cn_is_macro_enabled(struct hl_device *hdev, u8 macro_idx)
{
	u32 port1, port2;

	port1 = macro_idx << 1; /* the index of the first port in the macro */
	port2 = port1 + 1;

	return (hdev->cn.ports_mask & BIT(port1)) || (hdev->cn.ports_mask & BIT(port2));
}

static void gaudi2_cn_macros_hw_config_no_fw(struct hl_device *hdev)
{
	int i;

	for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++) {
		if (!gaudi2_cn_is_macro_enabled(hdev, i))
			continue;

		gaudi2_cn_hw_macro_config_no_fw(hdev, i);
	}
}

static void gaudi2_cn_ports_config_no_fw(struct hl_device *hdev)
{
	int port;

	for (port = 0 ; port < NIC_NUMBER_OF_PORTS ; port++) {
		/* SW-42306: Due to H/W bug on gaudi2, link events for both even and odd ports
		 * arrive only on the odd port in the macro. Therefore, need to initialize
		 * all EQs of all ports regardless of their enablement.
		 */
		gaudi2_cn_config_hw_eq_no_fw(hdev, port);

		/* Don't initialize disabled ports */
		if (!(hdev->cn.ports_mask & BIT(port)))
			continue;

		/* TXS Configuration */
		gaudi2_cn_config_hw_txs_no_fw(hdev, port);

		/* TXE Configuration */
		gaudi2_cn_config_hw_txe_no_fw(hdev, port);

		/* QPC Configuration */
		gaudi2_cn_config_hw_qpc_no_fw(hdev, port);

		/* RXE Configuration */
		gaudi2_cn_config_hw_rxe_no_fw(hdev, port);
	}
}

void gaudi2_cn_blocks_fw_config(struct hl_device *hdev)
{
	if (!hdev->cn.ports_mask)
		return;

	if (hdev->reset_info.in_compute_reset)
		return;

	gaudi2_cn_macros_hw_config_no_fw(hdev);

	gaudi2_cn_ports_config_no_fw(hdev);

	gaudi2_cn_set_correct_address_for_errors(hdev);
}

void gaudi2_cn_restore_dynamic_cfg_soft_reset_fw(struct hl_device *hdev)
{
	int port;

	for (port = 0 ; port < NIC_NUMBER_OF_PORTS ; port++) {
		/* Don't initialize disabled ports */
		if (!(hdev->cn.ports_mask & BIT(port)))
			continue;

		NIC_RMWREG32(mmNIC0_RXE0_RXE_CHECKS, 1,
			NIC0_RXE0_RXE_CHECKS_WQE_IDX_MISMATCH_EN_MASK);
		NIC_RMWREG32(mmNIC0_TXE0_WQE_CHECK_EN, 1,
			NIC0_TXE0_WQE_CHECK_EN_WQE_INDEX_EN_MASK);
	}
}
