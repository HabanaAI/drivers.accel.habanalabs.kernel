// SPDX-License-Identifier: GPL-2.0

/* Copyright 2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi2P.h"
#include "gaudi2_nic.h"
#include "../include/gaudi2/asic_reg/gaudi2_regs.h"
#include <linux/module.h>
#include <linux/firmware.h>
#include <asm/unaligned.h>

#define NIC_PHY_CFG_SIZE \
		(mmNIC0_SERDES1_LANE0_REGISTER_0P00 - mmNIC0_SERDES0_LANE0_REGISTER_0P00)

#define NIC_PHY_CFG_BASE(port) \
		({ \
			u32 __port = port; \
			((u64)(NIC_MACRO_CFG_BASE(__port) + \
			NIC_PHY_CFG_SIZE * (u64)((__port) & 1))); \
		})

#define LANE_LO_OFF \
		(mmNIC0_SERDES0_LANE1_REGISTER_0P00 - mmNIC0_SERDES0_LANE0_REGISTER_0P00)

#define LANE_HI_OFF \
		(mmNIC0_SERDES0_LANE1_REGISTER_AI00 - mmNIC0_SERDES0_LANE0_REGISTER_AI00)

#define LANE_OFF(reg, lane) \
		({ \
			u32 __lane = lane; \
			((reg) < mmNIC0_SERDES0_LANE0_REGISTER_AI00) ? \
			((__lane) * LANE_LO_OFF) : ((__lane) * LANE_HI_OFF); \
		})

#define PHY_PRINT(port, lane, op, val, reg) \
	({ \
		if (hdev->nic.phy_regs_print) { \
			dev_info(hdev->dev, "[%s],Nic,%u,Port,%u,Lane,%d,%s,0x%08x,0x%08llx\n", \
				__func__, (port) >> 1, (port) & 0x1, lane, op, val, reg); \
			usleep_range(1000, 2000); \
		} \
	})

#define NIC_PHY_RREG32(reg) \
	({ \
		u32 _port = port; \
		u64 _reg = NIC_PHY_CFG_BASE(_port) + (reg); \
		u32 _val = RREG32(_reg); \
		PHY_PRINT(_port, -1, "read", _val, _reg); \
		_val; \
	})

#define NIC_PHY_WREG32(reg, val) \
	do { \
		u32 _port = port; \
		u64 _reg = NIC_PHY_CFG_BASE(_port) + (reg); \
		u32 _val = val; \
		WREG32(_reg, _val); \
		PHY_PRINT(_port, -1, "write", _val, _reg); \
	} while (0)

#define NIC_PHY_RMWREG32(reg, val, mask) \
	do { \
		u32 _port = port; \
		u64 _reg = NIC_PHY_CFG_BASE(_port) + (reg); \
		u32 _val = val; \
		u32 _mask = mask; \
		u32 _tmp = RREG32(_reg); \
		PHY_PRINT(_port, -1, "read(rmw)", _tmp, _reg); \
		_tmp &= ~_mask; \
		_tmp |= (_val << __ffs(_mask)); \
		WREG32(_reg, _tmp); \
		PHY_PRINT(_port, -1, "write(rmw)", _tmp, _reg); \
	} while (0)

#define NIC_PHY_RREG32_LANE(reg) \
	({ \
		u32 _port = port; \
		u32 _lane = lane; \
		u64 _reg = reg; \
		u64 __reg = NIC_PHY_CFG_BASE(_port) + _reg + LANE_OFF(_reg, _lane); \
		u32 _val = RREG32(__reg); \
		PHY_PRINT(_port, _lane, "read", _val, __reg); \
		_val; \
	})

#define NIC_PHY_WREG32_LANE(reg, val) \
	do { \
		u32 _port = port; \
		u32 _lane = lane; \
		u64 _reg = reg; \
		u64 __reg = NIC_PHY_CFG_BASE(_port) + _reg + LANE_OFF(_reg, _lane); \
		u32 _val = val; \
		WREG32(__reg, _val); \
		PHY_PRINT(_port, _lane, "write", _val, __reg); \
	} while (0)

#define NIC_PHY_RMWREG32_LANE(reg, val, mask) \
	do { \
		u32 _port = port; \
		u32 _lane = lane; \
		u64 _reg = reg; \
		u64 __reg = NIC_PHY_CFG_BASE(_port) + _reg + LANE_OFF(_reg, _lane); \
		u32 _val = val; \
		u32 _mask = mask; \
		u32 _tmp = RREG32(__reg); \
		PHY_PRINT(_port, _lane, "read(rmw)", _tmp, __reg); \
		_tmp &= ~_mask; \
		_tmp |= (_val << __ffs(_mask)); \
		WREG32(__reg, _tmp); \
		PHY_PRINT(_port, _lane, "write(rmw)", _tmp, __reg); \
	} while (0)

#define NIC_PHY_READ_COUNTS_PER_MS		100000
#define NIC_PHY_FW_TIME_CONSTANT_RATIO		64
#define NIC_PHY_FW_TUNING_INTERVAL_MS		100
#define NIC_PHY_FW_TUNING_TIMEOUT_MS		30000 /* 30 seconds */
#define NIC_PHY_POST_FW_TUNING_WAIT_LONG_MS	90000 /* 1.5 minutes */
#define NIC_PHY_POST_FW_TUNING_WAIT_SHORT_MS	1000 /* 1 second */
#define NIC_PHY_CHECK_LINK_INTERVAL_MS		1000 /* 1 second */
#define NIC_PHY_PORT_FW_TUNED			0x3
#define NIC_PHY_PAM4_BER_FACTOR			53125000
#define NIC_PHY_NRZ_BER_FACTOR			25781250

#define NIC_PHY_TX_POL_MASK_HL225		0xF00000000430
#define NIC_PHY_RX_POL_MASK_HL225		0x0FFFFFFFFBCF
#define NIC_PHY_TX_POL_MASK_HLS2		0x0
#define NIC_PHY_RX_POL_MASK_HLS2		0x0
#define NIC_PHY_TX_POL_MASK_HL225_S		0xB0350203A3E2
#define NIC_PHY_RX_POL_MASK_HL225_S		0x404800002000

#define NIC_PHY_PCS_LINK_CNT			10
#define NIC_PHY_MAC_REMOTE_FAULT_CNT		15

#define NIC_MAC_LANE_MAP(lane_0, lane_1, lane_2, lane_3) \
	(((lane_0) & \
	NIC0_PHY_PHY_ASYNC_LANE_SWAP_SERDES0_TX0_SWAP_ID_MASK) | \
	(((lane_1) & \
	NIC0_PHY_PHY_ASYNC_LANE_SWAP_SERDES0_TX0_SWAP_ID_MASK) << \
	NIC0_PHY_PHY_ASYNC_LANE_SWAP_SERDES0_TX1_SWAP_ID_SHIFT) | \
	(((lane_2) & \
	NIC0_PHY_PHY_ASYNC_LANE_SWAP_SERDES0_TX0_SWAP_ID_MASK) << \
	NIC0_PHY_PHY_ASYNC_LANE_SWAP_SERDES1_TX0_SWAP_ID_SHIFT) | \
	(((lane_3) & \
	NIC0_PHY_PHY_ASYNC_LANE_SWAP_SERDES0_TX0_SWAP_ID_MASK) << \
	NIC0_PHY_PHY_ASYNC_LANE_SWAP_SERDES1_TX1_SWAP_ID_SHIFT))

/* Lane map for HL-225 */
static u32 default_nic_mac_lane_remap[] = {
	/* NIC MACRO 0 */
	NIC_MAC_LANE_MAP(NIC_MAC_LANE_3, NIC_MAC_LANE_1, NIC_MAC_LANE_0, NIC_MAC_LANE_2),
	/* NIC MACRO 1-10. Use default HW power on reset mapping. */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	/* NIC MACRO 11 */
	NIC_MAC_LANE_MAP(NIC_MAC_LANE_1, NIC_MAC_LANE_0, NIC_MAC_LANE_3, NIC_MAC_LANE_2),
};

/* Firmware lane mapping per macro are nibbles.
 * e.g. 0x3210 maps to lane 3/2/1/0
 */
#define FW_PARSE_LANE_MAP(macro, lane)	((macro & (0xf << (lane * 4))) >> (lane * 4))

enum lane_state {
	READY,
	NOT_READY,
	FAILURE
};

#define GAUDI2_PHY_FW_FILE	"habanalabs/gaudi2/gaudi2_nic_fw.bin"

MODULE_FIRMWARE(GAUDI2_PHY_FW_FILE);

const char *gaudi2_nic_phy_get_fw_name(void)
{
	return GAUDI2_PHY_FW_FILE;
}

static int get_tx_lane_in_macro(struct hl_device *hdev, u32 port, int lane)
{
	u32 lane_in_macro, lane_swap_val;
	int tx_lane;

	lane_in_macro = (port & 0x1) * 2 + lane;
	lane_swap_val = hdev->nic.mac_lane_remap[port >> 1];

	if (!lane_swap_val)
		return lane_in_macro;

	for (tx_lane = 0 ; tx_lane < NIC_MAC_NUM_OF_LANES ; tx_lane++) {
		if (((lane_swap_val >> (tx_lane * 2)) & 0x3) == lane_in_macro)
			break;
	}

	return tx_lane;
}

static void get_tx_port_and_lane(struct hl_device *hdev, u32 port, int lane, u32 *tx_port,
					int *tx_lane)
{
	struct hl_nic_port *nic_port = &hdev->nic.nic_ports[port];
	u32 tx_lane_in_macro, abs_tx_lane_idx;

	/* TODO SW-61565: The lane swapping logic is important for the LT, which is not supported
	 * currently. Therefore, we can skip it for now.
	 */
	if (!nic_port->auto_neg_enable) {
		*tx_port = port;
		*tx_lane = lane;
		return;
	}

	tx_lane_in_macro = get_tx_lane_in_macro(hdev, port, lane);
	abs_tx_lane_idx = (port >> 1) * NIC_MAC_NUM_OF_LANES + tx_lane_in_macro;

	*tx_port = abs_tx_lane_idx >> 1;
	*tx_lane = abs_tx_lane_idx & 0x1;
}

static bool is_lane_swapping(struct hl_device *hdev, u32 port, int lane)
{
	u32 tx_port;
	int tx_lane;

	get_tx_port_and_lane(hdev, port, lane, &tx_port, &tx_lane);

	return (tx_port != port) || (tx_lane != lane);
}

void gaudi2_nic_get_fw_lane_mapping(struct cpucp_nic_info *nic_info, struct hl_nic *nic)
{
	int i;
	u16 cpu_macro_tx_swap_map;

	for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++) {
		cpu_macro_tx_swap_map = le16_to_cpu(nic_info->tx_swap_map[i]);
		nic->mac_lane_remap[i] = NIC_MAC_LANE_MAP(
			FW_PARSE_LANE_MAP(cpu_macro_tx_swap_map, 0), /* lane 0 */
			FW_PARSE_LANE_MAP(cpu_macro_tx_swap_map, 1), /* lane 1 */
			FW_PARSE_LANE_MAP(cpu_macro_tx_swap_map, 2), /* lane 2 */
			FW_PARSE_LANE_MAP(cpu_macro_tx_swap_map, 3)); /* lane 3 */
	}
}

static void gaudi2_mac_lane_remap(struct hl_device *hdev, u32 port)
{
	if (hdev->nic.mac_lane_remap[port >> 1])
		NIC_MACRO_WREG32(mmNIC0_PHY_PHY_ASYNC_LANE_SWAP,
					hdev->nic.mac_lane_remap[port >> 1]);
}

static void soft_reset(struct hl_device *hdev, u32 port)
{
	NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_980D, 0x888,
				NIC0_SERDES0_REGISTER_980D_DOMAIN_RESET_MASK);
	NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_980D, 0x0,
				NIC0_SERDES0_REGISTER_980D_DOMAIN_RESET_MASK);
}

static void logic_reset(struct hl_device *hdev, u32 port)
{
	NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_980D, 0x777,
				NIC0_SERDES0_REGISTER_980D_DOMAIN_RESET_MASK);
	NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_980D, 0x0,
				NIC0_SERDES0_REGISTER_980D_DOMAIN_RESET_MASK);
}

static void cpu_reset(struct hl_device *hdev, u32 port)
{
	NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_980D, 0xAAA,
				NIC0_SERDES0_REGISTER_980D_DOMAIN_RESET_MASK);
	NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_980D, 0x0,
				NIC0_SERDES0_REGISTER_980D_DOMAIN_RESET_MASK);
}

static int fw_cmd(struct hl_device *hdev, u32 port, u32 cmd, u32 *detail, u32 expected_res,
			u32 *res_ptr)
{
	u32 res, val, checks = 0;

	if (detail)
		NIC_PHY_WREG32(mmNIC0_SERDES0_REGISTER_9816, *detail);

	NIC_PHY_WREG32(mmNIC0_SERDES0_REGISTER_9815, cmd);

	do {
		usleep_range(1000, 2000);
		res = NIC_PHY_RREG32(mmNIC0_SERDES0_REGISTER_9815);
		if (checks++ > NIC_PHY_READ_COUNTS_PER_MS) {
			dev_dbg(hdev->dev, "timeout for PHY cmd 0x%x port %u\n", cmd, port);
			return -ETIMEDOUT;
		}
	} while (res == cmd);

	val = (res >> 8) & 0xF;

	if (val != expected_res) {
		dev_dbg(hdev->dev, "cmd 0x%x returned error 0x%x port %u\n",
			cmd, val, port);
		return -EFAULT;
	}

	*res_ptr = res;

	return 0;
}

static void clock_init(struct hl_device *hdev, u32 port, int lane)
{
	u32 first_val, second_val;

	if (port & 0x1) { /* raven 1 */
		if (lane == 0) {
			first_val = 0xA9E0;
			second_val = 0x9B9E;
		} else { /* lane 1 */
			first_val = 0xA9E0;
			second_val = 0x9B9E;
		}
	} else { /* raven 0 */
		if (lane == 0) {
			first_val = 0x59E0;
			second_val = 0x9B5E;
		} else { /* lane 1 */
			first_val = 0xA9E0;
			second_val = 0x9B9E;
		}
	}

	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PCC, first_val);
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0NF3, second_val);
}

static u32 int_to_twos(s32 val, u8 bitwidth)
{
	return val < 0 ? (1 << bitwidth) + val : val;
}

static int twos_to_int(unsigned int val, u8 bitwidth)
{
	u32 mask = 1 << (bitwidth - 1);

	return -(val & mask) + (val & ~mask);
}

static void set_tx_taps(struct hl_device *hdev, u32 port, int lane, s32 tx_pre2, s32 tx_pre1,
			s32 tx_main, s32 tx_post1, s32 tx_post2)
{
	u32 card_location = hdev->nic.card_location;

	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA5, int_to_twos(tx_pre2, 8),
				NIC0_SERDES0_LANE0_REGISTER_0PA5_TX_PRE_2_MASK);
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA7, int_to_twos(tx_pre1, 8),
				NIC0_SERDES0_LANE0_REGISTER_0PA7_TX_PRE_1_MASK);
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA9, int_to_twos(tx_main, 8),
				NIC0_SERDES0_LANE0_REGISTER_0PA9_TX_MAIN_MASK);
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PAB, int_to_twos(tx_post1, 8),
				NIC0_SERDES0_LANE0_REGISTER_0PAB_TX_POST_1_MASK);
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PAD, int_to_twos(tx_post2, 8),
				NIC0_SERDES0_LANE0_REGISTER_0PAD_TX_POST_2_MASK);

	dev_dbg(hdev->dev,
		"Card %u Port %u lane %d tx taps: pre2 %d, pre1 %d, main %d, post1 %d, post2 %d\n",
		card_location, port, lane, tx_pre2, tx_pre1, tx_main, tx_post1, tx_post2);
}

static void init_pam4_tx(struct hl_device *hdev, u32 port, int lane)
{
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA1, 0x0);
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA2, 0x0);
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA3, 0x0);
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA4, 0x0);
	/* data quite */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, 0x6320);
	/* auto symmetric, scale */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PAF, 0xFAC9);
	/* data, prbs */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PB0, 0x4000);
	/* cursor -2 */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA5, 0x100);
	/* cursor -1 */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA7, 0xF900);
	/* cursor -main */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA9, 0x1700);
	/* cursor +1 */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PAB, 0x0);
	/* cursor +2 */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PAD, 0x0);
}

static void init_pam4_rx(struct hl_device *hdev, u32 port, int lane)
{
	/* ac-couple always */
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PF8, 0x1,
				NIC0_SERDES0_LANE0_REGISTER_0PF8_AC_COUPLE_EN_MASK);
}

static void set_lane_mode_tx(struct hl_device *hdev, u32 port, int lane, bool pam4)
{
	if (pam4) {
		/* Disable NRZ mode */
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PB0, 0x0,
					NIC0_SERDES0_LANE0_REGISTER_0PB0_TX_NRZ_MODE_MASK);
		/* Disable NRZ PRBS Generator */
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PB0, 0x0,
					NIC0_SERDES0_LANE0_REGISTER_0PB0_TX_NRZ_PRBS_GEN_EN_MASK);
		/* Enable PAM4 PRBS Generator */
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, 0x1,
					NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_PRBS_CLK_EN_MASK);
	} else {
		/* Disable PAM4 PRBS Generator */
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, 0x0,
					NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_PRBS_CLK_EN_MASK);
		/* Enable NRZ mode */
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PB0, 0x1,
					NIC0_SERDES0_LANE0_REGISTER_0PB0_TX_NRZ_MODE_MASK);
		/* Enable NRZ PRBS Generator */
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PB0, 0x1,
					NIC0_SERDES0_LANE0_REGISTER_0PB0_TX_NRZ_PRBS_GEN_EN_MASK);
	}
}

static void set_lane_mode_rx(struct hl_device *hdev, u32 port, int lane, bool pam4)
{
	if (pam4)
		/* Enable PAM4 mode */
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P41, 0x1,
					NIC0_SERDES0_LANE0_REGISTER_0P41_PAM4_EN_MASK);
	else
		/* Disable PAM4 mode */
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P41, 0x0,
					NIC0_SERDES0_LANE0_REGISTER_0P41_PAM4_EN_MASK);
}

static void prbs_mode_select_tx(struct hl_device *hdev, u32 port, int lane, bool pam4, char *mode)
{
	u32 val;

	if (!mode || strncmp(mode, "PRBS", strlen("PRBS")))
		return;

	if (pam4) {
		if (!strncmp(mode, "PRBS9", strlen("PRBS9")))
			val = 0;
		else if (!strncmp(mode, "PRBS13", strlen("PRBS13")))
			val = 1;
		else if (!strncmp(mode, "PRBS15", strlen("PRBS15")))
			val = 2;
		else /* PRBS31 */
			val = 3;

		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, val,
					NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_PRBS_MODE_MASK);
	} else {
		if (!strncmp(mode, "PRBS9", strlen("PRBS9")))
			val = 0;
		else if (!strncmp(mode, "PRBS15", strlen("PRBS15")))
			val = 1;
		else if (!strncmp(mode, "PRBS23", strlen("PRBS23")))
			val = 2;
		else /* PRBS31 */
			val = 3;

		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, val,
					NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_PRBS_MODE_MASK);
	}

	val = pam4 ? 0 : 1;

	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PB0, val,
				NIC0_SERDES0_LANE0_REGISTER_0PB0_TX_NRZ_PRBS_CLK_EN_MASK);
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PB0, val,
				NIC0_SERDES0_LANE0_REGISTER_0PB0_TX_NRZ_PRBS_GEN_EN_MASK);
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PB0, val,
				NIC0_SERDES0_LANE0_REGISTER_0PB0_TX_NRZ_MODE_MASK);

	if (pam4)
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PB0, 0x0,
					NIC0_SERDES0_LANE0_REGISTER_0PB0_TX_HALF_RATE_EN_MASK);

	val = pam4 ? 1 : 0;

	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, 0x1,
				NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_TEST_DATA_SRC_MASK);
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, val,
				NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_PRBS_CLK_EN_MASK);
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, 0x1,
				NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_PAM4_TEST_EN_MASK);
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, val,
				NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_PRBS_GEN_EN_MASK);
}

static void prbs_mode_select_rx(struct hl_device *hdev, u32 port, int lane, bool pam4, char *mode)
{
	u32 val;

	if (!mode || strncmp(mode, "PRBS", strlen("PRBS")))
		return;

	if (pam4) {
		if (!strncmp(mode, "PRBS9", strlen("PRBS9")))
			val = 0;
		else if (!strncmp(mode, "PRBS13", strlen("PRBS13")))
			val = 1;
		else if (!strncmp(mode, "PRBS15", strlen("PRBS15")))
			val = 2;
		else /* PRBS31 */
			val = 3;

		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P43, val,
					NIC0_SERDES0_LANE0_REGISTER_0P43_PRBS_MODE_SEL_MASK);
	} else {
		if (!strncmp(mode, "PRBS9", strlen("PRBS9")))
			val = 0;
		else if (!strncmp(mode, "PRBS15", strlen("PRBS15")))
			val = 1;
		else if (!strncmp(mode, "PRBS23", strlen("PRBS23")))
			val = 2;
		else /* PRBS31 */
			val = 3;

		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0N61, val,
					NIC0_SERDES0_LANE0_REGISTER_0N61_NRZ_PRBS_MODE_SEL_MASK);
	}

	if (pam4) {
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P43, 0x1,
					NIC0_SERDES0_LANE0_REGISTER_0P43_PU_PRBS_CHKR_MASK);
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P43, 0x1,
					NIC0_SERDES0_LANE0_REGISTER_0P43_PU_PRBS_SYNC_CHKR_MASK);
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P43, 0x1,
					NIC0_SERDES0_LANE0_REGISTER_0P43_RX_PRBS_AUTO_SYNC_EN_MASK);
	} else {
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0N61, 0x1,
					NIC0_SERDES0_LANE0_REGISTER_0N61_PRBS_CHKR_EN_MASK);
	}
}

static void set_default_polarity_values(struct hl_device *hdev)
{
	enum gaudi2_setup_type setup_type = (enum gaudi2_setup_type) hdev->gaudi2_setup_type;
	struct cpucp_nic_info *nic_info = &hdev->asic_prop.cpucp_nic_info;
	u64 pol_tx, pol_rx;

	switch (setup_type) {
	case GAUDI2_SETUP_TYPE_HLS2:
		pol_tx = NIC_PHY_TX_POL_MASK_HL225 ^ NIC_PHY_TX_POL_MASK_HLS2;
		pol_rx = NIC_PHY_RX_POL_MASK_HL225 ^ NIC_PHY_RX_POL_MASK_HLS2;
		break;
	case GAUDI2_SETUP_TYPE_HL225_S_EXT_LB:
		pol_tx = NIC_PHY_TX_POL_MASK_HL225 ^ NIC_PHY_TX_POL_MASK_HL225_S;
		pol_rx = NIC_PHY_RX_POL_MASK_HL225 ^ NIC_PHY_RX_POL_MASK_HL225_S;
		break;
	default:
		dev_err(hdev->dev, "Wrong setup type %d\n", setup_type);
		return;
	}

	nic_info->pol_tx_mask[0] = cpu_to_le64(pol_tx);
	nic_info->pol_rx_mask[0] = cpu_to_le64(pol_rx);
}

static void set_default_mac_lane_remap(struct hl_device *hdev)
{
	memcpy(hdev->nic.mac_lane_remap, default_nic_mac_lane_remap,
		sizeof(default_nic_mac_lane_remap));
}

static s32 get_pam4_tap_pre2(struct hl_device *hdev, u32 card_location, u32 abs_lane_idx)
{
	enum gaudi2_setup_type setup_type = (enum gaudi2_setup_type) hdev->gaudi2_setup_type;

	switch (setup_type) {
	case GAUDI2_SETUP_TYPE_HLS2:
		return 2;
	case GAUDI2_SETUP_TYPE_HL225_S_EXT_LB:
		return 2;
	default:
		dev_err(hdev->dev, "Wrong setup type %d\n", setup_type);
	}

	return 2;
}

static s32 get_pam4_tap_pre1(struct hl_device *hdev, u32 card_location, u32 abs_lane_idx)
{
	enum gaudi2_setup_type setup_type = (enum gaudi2_setup_type) hdev->gaudi2_setup_type;

	switch (setup_type) {
	case GAUDI2_SETUP_TYPE_HLS2:
		return -10;
	case GAUDI2_SETUP_TYPE_HL225_S_EXT_LB:
		return -12;
	default:
		dev_err(hdev->dev, "Wrong setup type %d\n", setup_type);
	}

	return -12;
}

static s32 get_pam4_tap_main(struct hl_device *hdev, u32 card_location, u32 abs_lane_idx)
{
	enum gaudi2_setup_type setup_type = (enum gaudi2_setup_type) hdev->gaudi2_setup_type;

	switch (setup_type) {
	case GAUDI2_SETUP_TYPE_HLS2:
		return 23;
	case GAUDI2_SETUP_TYPE_HL225_S_EXT_LB:
		return 22;
	default:
		dev_err(hdev->dev, "Wrong setup type %d\n", setup_type);
	}

	return 22;
}

static s32 get_pam4_tap_post1(struct hl_device *hdev, u32 card_location, u32 abs_lane_idx)
{
	return 0;
}

static s32 get_pam4_tap_post2(struct hl_device *hdev, u32 card_location, u32 abs_lane_idx)
{
	return 0;
}

static void set_default_tx_taps_values(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic *nic = &hdev->nic;
	u32 card_location;
	int abs_lane_idx;
	s32 *taps;

	card_location = nic->card_location;

	for (abs_lane_idx = 0 ; abs_lane_idx < nic_props->max_num_of_lanes ; abs_lane_idx++) {
		/* PAM4 */
		taps = nic->phy_tx_taps[abs_lane_idx].pam4_taps;
		taps[0] = get_pam4_tap_pre2(hdev, card_location, abs_lane_idx);
		taps[1] = get_pam4_tap_pre1(hdev, card_location, abs_lane_idx);
		taps[2] = get_pam4_tap_main(hdev, card_location, abs_lane_idx);
		taps[3] = get_pam4_tap_post1(hdev, card_location, abs_lane_idx);
		taps[4] = get_pam4_tap_post2(hdev, card_location, abs_lane_idx);

		/* NRZ */
		taps = nic->phy_tx_taps[abs_lane_idx].nrz_taps;
		taps[0] = 0;
		taps[1] = -10;
		taps[2] = 26;
		taps[3] = 0;
		taps[4] = 0;
	}
}

static void set_pol_tx(struct hl_device *hdev, u32 port, int lane, u32 tx_pol)
{
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, tx_pol,
				NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_ANA_OUT_FLIP_MASK);

	dev_dbg(hdev->dev, "Port %u lane %d: tx_pol %u\n", port, lane, tx_pol);
}

static void set_pol_rx(struct hl_device *hdev, u32 port, int lane, u32 rx_pol)
{
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P43, rx_pol,
				NIC0_SERDES0_LANE0_REGISTER_0P43_RX_DATA_FLIP_MASK);
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0N61, rx_pol,
				NIC0_SERDES0_LANE0_REGISTER_0N61_PRBS_CHECK_FLIP_MASK);

	dev_dbg(hdev->dev, "Port %u lane %d: rx_pol %u\n", port, lane, rx_pol);
}

static void set_gc_tx(struct hl_device *hdev, u32 port, int lane, u32 tx_gc)
{
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PAF, tx_gc,
				NIC0_SERDES0_LANE0_REGISTER_0PAF_TX_GRAYCODE_EN_MASK);
}

static void set_gc_rx(struct hl_device *hdev, u32 port, int lane, u32 rx_gc)
{
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P42, rx_gc,
				NIC0_SERDES0_LANE0_REGISTER_0P42_RX_GRAYCODE_EN_MASK);
}

static void set_pc_tx(struct hl_device *hdev, u32 port, int lane, u32 tx_pc)
{
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PAF, tx_pc,
				NIC0_SERDES0_LANE0_REGISTER_0PAF_TX_PRECODE_EN_MASK);
}

static void set_pc_rx(struct hl_device *hdev, u32 port, int lane, u32 rx_pc)
{
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P42, rx_pc,
				NIC0_SERDES0_LANE0_REGISTER_0P42_RX_PRECODE_EN_MASK);
}

static void set_msblsb_tx(struct hl_device *hdev, u32 port, int lane, u32 tx_msblsb)
{
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PAF, tx_msblsb,
				NIC0_SERDES0_LANE0_REGISTER_0PAF_TX_SWAP_MSB_LSB_MASK);
}

static void set_msblsb_rx(struct hl_device *hdev, u32 port, int lane, u32 rx_msblsb)
{
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P43, rx_msblsb,
				NIC0_SERDES0_LANE0_REGISTER_0P43_RX_SWAP_MSB_LSB_MASK);
}

static void init_lane_for_fw_tx(struct hl_device *hdev, u32 port, int lane, bool pam4, bool do_lt)
{
	struct cpucp_nic_info *nic_info;
	u32 abs_lane_idx, tx_pol, tx_gc, tx_msblsb;

	nic_info = &hdev->asic_prop.cpucp_nic_info;

	abs_lane_idx = (port << 1) + lane;
	tx_pol = (le64_to_cpu(nic_info->pol_tx_mask[0]) >> abs_lane_idx) & 1;

	/* The polairy values which are provided by F/W version < v1.4 are flipped,
	 * so need to set them correctly.
	 */
	if (hdev->nic.use_fw_serdes_info && (hdev->fw_major_version < 35))
		tx_pol = !tx_pol;

	tx_gc = (pam4 && !do_lt) ? 1 : 0;
	tx_msblsb = do_lt ? 1 : 0;

	set_lane_mode_tx(hdev, port, lane, pam4);
	set_gc_tx(hdev, port, lane, tx_gc);
	set_pc_tx(hdev, port, lane, 0);
	set_msblsb_tx(hdev, port, lane, tx_msblsb);
	set_pol_tx(hdev, port, lane, tx_pol);
}

static void init_lane_for_fw_rx(struct hl_device *hdev, u32 port, int lane, bool pam4, bool do_lt)
{
	struct cpucp_nic_info *nic_info;
	u32 abs_lane_idx, rx_pol, rx_gc, rx_msblsb;

	nic_info = &hdev->asic_prop.cpucp_nic_info;

	abs_lane_idx = (port << 1) + lane;
	rx_pol = (le64_to_cpu(nic_info->pol_rx_mask[0]) >> abs_lane_idx) & 1;

	/* The polairy values which are provided by F/W version < v1.4 are flipped,
	 * so need to set them correctly.
	 */
	if (hdev->nic.use_fw_serdes_info && (hdev->fw_major_version < 35))
		rx_pol = !rx_pol;

	rx_gc = (pam4 && !do_lt) ? 1 : 0;
	rx_msblsb = do_lt ? 1 : 0;

	set_lane_mode_rx(hdev, port, lane, pam4);
	set_gc_rx(hdev, port, lane, rx_gc);
	set_pc_rx(hdev, port, lane, 0);
	set_msblsb_rx(hdev, port, lane, rx_msblsb);
	set_pol_rx(hdev, port, lane, rx_pol);
}

static void set_functional_mode_lane(struct hl_device *hdev, u32 port, int lane, bool do_lt)
{
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, 0,
				NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_PRBS_CLK_EN_MASK);
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, 0,
				NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_PAM4_TEST_EN_MASK);
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, 0,
				NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_PRBS_GEN_EN_MASK);

	if (do_lt)
		NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_AN10, 0x5);
	else
		NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_AN10, 0);

	dev_dbg(hdev->dev, "Card %u Port %u lane %d: Switched to functional mode\n",
		hdev->nic.card_location, port, lane);
}

static void set_functional_mode(struct hl_device *hdev, u32 port)
{
	struct hl_nic_port *nic_port = &hdev->nic.nic_ports[port];
	u32 tx_port;
	int lane, tx_lane;
	bool do_lt;

	do_lt = nic_port->auto_neg_enable;

	for (lane = 0 ; lane < 2 ; lane++) {
		get_tx_port_and_lane(hdev, port, lane, &tx_port, &tx_lane);
		set_functional_mode_lane(hdev, tx_port, tx_lane, do_lt);
	}

	nic_port->phy_func_mode_en = true;
}

static u32 get_fw_reg(struct hl_device *hdev, u32 port, u32 fw_addr)
{
	u32 ignore;

	fw_cmd(hdev, port, 0xE010, &fw_addr, 0xE, &ignore);

	return NIC_PHY_RREG32(mmNIC0_SERDES0_REGISTER_9812);
}

static int set_fw_reg(struct hl_device *hdev, u32 port, u32 fw_addr, u32 val)
{
	u32 ignore;

	NIC_PHY_WREG32(mmNIC0_SERDES0_REGISTER_9812, val);

	return fw_cmd(hdev, port, 0xE020, &fw_addr, 0xE, &ignore);
}

static void enable_lane_swapping(struct hl_device *hdev, u32 port, int lane, bool do_an,
					bool do_lt)
{
	if (do_an || do_lt)
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_AJ40, 0x1,
				NIC0_SERDES0_LANE0_REGISTER_AJ40_ANLT_LANE_SWAPPING_EN_MASK);

	if (do_an)
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_AJ40, 0x0, 0x40);
}

static void disable_lane_swapping(struct hl_device *hdev, u32 port, int lane, bool do_an,
					bool do_lt)
{
	if (do_an)
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_AJ40, 0x0,
				NIC0_SERDES0_LANE0_REGISTER_AJ40_ANLT_LANE_SWAPPING_EN_MASK);
}

static void lane_swapping_config(struct hl_device *hdev, u32 port, int lane, bool do_an,
					bool do_lt)
{
	u32 tx_port, lt_option;
	int tx_lane;

	get_tx_port_and_lane(hdev, port, lane, &tx_port, &tx_lane);

	lt_option = get_fw_reg(hdev, port, 366);

	if (is_lane_swapping(hdev, port, lane)) {
		enable_lane_swapping(hdev, tx_port, tx_lane, do_an, do_lt);
		enable_lane_swapping(hdev, port, lane, do_an, do_lt);

		lt_option |= (1 << (3 + 8 * (1 - lane)));
	} else {
		disable_lane_swapping(hdev, tx_port, tx_lane, do_an, do_lt);
		disable_lane_swapping(hdev, port, lane, do_an, do_lt);

		lt_option &= ~(1 << (3 + 8 * (1 - lane)));
	}

	set_fw_reg(hdev, port, 366, lt_option);
}

static int fw_start(struct hl_device *hdev, u32 port, int lane, bool pam4, bool do_lt)
{
	u32 cmd, speed, ignore;

	cmd = pam4 ? (0x80D0 | lane) : (0x80C0 | lane);
	speed = pam4 ? 0x9 : 0x3;

	if (do_lt)
		speed |= 0x100;

	return fw_cmd(hdev, port, cmd, &speed, 0x8, &ignore);
}

static int fw_start_tx(struct hl_device *hdev, u32 port, int lane, bool pam4, bool do_lt)
{
	u32 speed, cmd, ignore;
	int rc;

	speed = pam4 ? 0x9 : 0x3;

	if (pam4)
		cmd = do_lt ? (0x7030 | lane) : (0x7010 | lane);
	else
		cmd = do_lt ? (0x7020 | lane) : (0x7000 | lane);

	rc = fw_cmd(hdev, port, cmd, &speed, 0x7, &ignore);
	if (rc)
		return rc;

	if (do_lt)
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, 0x1,
					NIC0_SERDES0_LANE0_REGISTER_0PA0_RSVD_0PA0_04_MASK);

	return 0;
}

static int fw_config_vcocap(struct hl_device *hdev, u32 port, int lane, u32 mode,
				u32 counter_value)
{
	u32 ignore;

	return fw_cmd(hdev, port, 0x6000 | (mode << 4) | lane, &counter_value, 14, &ignore);
}

static int set_pll_tx(struct hl_device *hdev, u32 port, int lane, u32 data_rate)
{
	u32 card_location, msbc, lsbc;
	int rc;

	card_location = hdev->nic.card_location;

	if (lane == 0)
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_9825, 0x0,
					NIC0_SERDES0_REGISTER_9825_PLL_0_LOCK_32T_CLK_SEL_MASK);
	else
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_982E, 0x0,
					NIC0_SERDES0_REGISTER_982E_PLL_1_LOCK_32T_CLK_SEL_MASK);

	switch (data_rate) {
	case NIC_DR_50:
		/* toggle FRACN LSB for better phase noise */
		NIC_PHY_RMWREG32_LANE(0x54587D4, 0x0, 0x1);
		msbc = 0x5;
		lsbc = 0x4FFA;
		break;
	case NIC_DR_26:
		/* toggle FRACN LSB for better phase noise */
		NIC_PHY_RMWREG32_LANE(0x54587D4, 0x0, 0x1);
		NIC_PHY_RMWREG32_LANE(0x5458320, 0x0, 0x1);
		msbc = 0x5;
		lsbc = 0x4FFA;
		break;
	case NIC_DR_25:
		msbc = 0x5;
		lsbc = 0x27FA;
		break;
	case NIC_DR_10:
		msbc = 0x2;
		lsbc = 0xFFD;
		break;
	default:
		dev_err(hdev->dev, "Card %u Port %u lane %d: unsupported data rate\n",
			card_location, port, lane);
		return -EFAULT;
	}

	rc = fw_config_vcocap(hdev, port, lane, 1, msbc);
	if (rc)
		return rc;

	rc = fw_config_vcocap(hdev, port, lane, 2, lsbc);
	if (rc)
		return rc;

	rc = fw_config_vcocap(hdev, port, lane, 3, 0x40);
	if (rc)
		return rc;

	rc = fw_config_vcocap(hdev, port, lane, 4, 0x0);
	if (rc)
		return rc;

	usleep_range(500, 1000);

	if (lane == 0) {
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_9825, 0x1,
					NIC0_SERDES0_REGISTER_9825_PLL_LOCK_SRC_SEL_MASK);
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_9825, 0x0,
					NIC0_SERDES0_REGISTER_9825_PLL_0_LOCK_EN_MASK);
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_9825, 0x1,
					NIC0_SERDES0_REGISTER_9825_PLL_0_LOCK_EN_MASK);
	} else {
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_9825, 0x2,
					NIC0_SERDES0_REGISTER_9825_PLL_LOCK_SRC_SEL_MASK);
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_982E, 0x0,
					NIC0_SERDES0_REGISTER_982E_PLL_1_LOCK_EN_MASK);
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_982E, 0x1,
					NIC0_SERDES0_REGISTER_982E_PLL_1_LOCK_EN_MASK);
	}

	usleep_range(500, 1000);

	return 0;
}

static int set_pll_rx(struct hl_device *hdev, u32 port, int lane, u32 data_rate)
{
	u32 card_location, msbc, lsbc, third_val;
	int rc;

	card_location = hdev->nic.card_location;

	if (lane == 0)
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_9825, 0x1,
					NIC0_SERDES0_REGISTER_9825_PLL_0_LOCK_32T_CLK_SEL_MASK);
	else
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_982E, 0x1,
					NIC0_SERDES0_REGISTER_982E_PLL_1_LOCK_32T_CLK_SEL_MASK);

	switch (data_rate) {
	case NIC_DR_50:
	case NIC_DR_26:
		msbc = 0x5;
		lsbc = 0x4FFA;
		third_val = 0x30;
		break;
	case NIC_DR_25:
		msbc = 0x5;
		lsbc = 0x27FA;
		third_val = 0x30;
		break;
	case NIC_DR_10:
		msbc = 0x2;
		lsbc = 0xFFD;
		third_val = 0x40;
		break;
	default:
		dev_err(hdev->dev, "Card %u Port %u lane %d: unsupported data rate\n",
			card_location, port, lane);
		return -EFAULT;
	}

	rc = fw_config_vcocap(hdev, port, lane, 1, msbc);
	if (rc)
		return rc;

	rc = fw_config_vcocap(hdev, port, lane, 2, lsbc);
	if (rc)
		return rc;

	rc = fw_config_vcocap(hdev, port, lane, 3, third_val);
	if (rc)
		return rc;

	rc = fw_config_vcocap(hdev, port, lane, 4, 0x1);
	if (rc)
		return rc;

	usleep_range(500, 1000);

	if (lane == 0) {
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_9825, 0x1,
					NIC0_SERDES0_REGISTER_9825_PLL_LOCK_SRC_SEL_MASK);
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_9825, 0x0,
					NIC0_SERDES0_REGISTER_9825_PLL_0_LOCK_EN_MASK);
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_9825, 0x1,
					NIC0_SERDES0_REGISTER_9825_PLL_0_LOCK_EN_MASK);
	} else {
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_9825, 0x2,
					NIC0_SERDES0_REGISTER_9825_PLL_LOCK_SRC_SEL_MASK);
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_982E, 0x0,
					NIC0_SERDES0_REGISTER_982E_PLL_1_LOCK_EN_MASK);
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_982E, 0x1,
					NIC0_SERDES0_REGISTER_982E_PLL_1_LOCK_EN_MASK);
	}

	usleep_range(500, 1000);

	return 0;
}

static int set_pll(struct hl_device *hdev, u32 port, int lane, u32 data_rate)
{
	u32 card_location, tx_port;
	int tx_lane, rc;

	card_location = hdev->nic.card_location;

	get_tx_port_and_lane(hdev, port, lane, &tx_port, &tx_lane);

	rc = set_pll_tx(hdev, tx_port, tx_lane, data_rate);
	if (rc) {
		dev_err(hdev->dev, "Card %u Port %u lane %d: set Tx PLL failed, rc %d\n",
			card_location, tx_port, tx_lane, rc);
		return rc;
	}

	rc = set_pll_rx(hdev, port, lane, data_rate);
	if (rc) {
		dev_err(hdev->dev, "Card %u Port %u lane %d: set Rx PLL failed, rc %d\n",
			card_location, port, lane, rc);
		return rc;
	}

	return 0;
}

static void set_tx_taps_scale(struct hl_device *hdev, u32 port, int lane)
{
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PAF, 0x4, 0x3E);
}

static int fw_config_speed_pam4(struct hl_device *hdev, u32 port, int lane, bool do_lt)
{
	u32 tx_port, card_location, abs_lane_idx, val;
	int tx_lane, rc;
	s32 *taps;

	card_location = hdev->nic.card_location;

	get_tx_port_and_lane(hdev, port, lane, &tx_port, &tx_lane);

	init_pam4_tx(hdev, tx_port, tx_lane);
	init_pam4_rx(hdev, port, lane);

	/* Disable AN/LT lane swapping */
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_AJ40, 0x0,
				NIC0_SERDES0_LANE0_REGISTER_AJ40_ANLT_LANE_SWAPPING_EN_MASK);

	lane_swapping_config(hdev, port, lane, false, do_lt);

	init_lane_for_fw_tx(hdev, tx_port, tx_lane, true, do_lt);
	init_lane_for_fw_rx(hdev, port, lane, true, do_lt);

	prbs_mode_select_tx(hdev, tx_port, tx_lane, true, "PRBS31");
	prbs_mode_select_rx(hdev, port, lane, true, "PRBS31");

	rc = fw_start(hdev, port, lane, true, do_lt);
	if (rc) {
		dev_err(hdev->dev,
			"Card %u Port %u lane %d: F/W config speed PAM4 failed (LT %s), rc %d\n",
			card_location, port, lane, do_lt ? "enabled" : "disable", rc);
		return rc;
	}

	if (is_lane_swapping(hdev, port, lane)) {
		rc = fw_start_tx(hdev, tx_port, tx_lane, true, do_lt);
		if (rc) {
			dev_err(hdev->dev,
				"Card %u Port %u lane %d: F/W config speed PAM4 failed (LT %s), rc %d\n",
				card_location, tx_port, tx_lane, do_lt ? "enabled" : "disable",
				rc);
			return rc;
		}

		if (do_lt)
			NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, 0x1,
					NIC0_SERDES0_LANE0_REGISTER_0PA0_RSVD_0PA0_04_MASK);
	}

	if (do_lt) {
		if (!hdev->nic.phy_show_ber) {
			/* tell the F/W to do LT with PCS data instead of PRBS */
			val = get_fw_reg(hdev, port, 366);
			val &= 0xFEFE;
			set_fw_reg(hdev, port, 366, val);
		}

		set_tx_taps_scale(hdev, tx_port, tx_lane);
		set_gc_tx(hdev, tx_port, tx_lane, 0);
		set_pc_tx(hdev, tx_port, tx_lane, 0);
		set_gc_rx(hdev, port, lane, 0);
		set_pc_rx(hdev, port, lane, 0);
	} else {
		abs_lane_idx = (tx_port << 1) + tx_lane;
		taps = hdev->nic.phy_tx_taps[abs_lane_idx].pam4_taps;
		set_tx_taps(hdev, tx_port, tx_lane, taps[0], taps[1], taps[2], taps[3], taps[4]);
	}

	return 0;
}

static void init_nrz_tx(struct hl_device *hdev, u32 port, int lane)
{
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA1, 0x0);
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA2, 0x0);
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA3, 0x0);
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA4, 0x0);
	/* data quiet */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, 0x6320);
	/* auto symmetric, scale */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PAF, 0xF8C9);
	/* data, prbs */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PB0, 0x4820);
	/* cursor -2 */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA5, 0x0);
	/* cursor -1 */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA7, 0xFC00);
	/* cursor -main */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA9, 0x1800);
	/* cursor +1 */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PAB, 0x0);
	/* cursor +2 */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PAD, 0x0);
}

static void init_nrz_rx(struct hl_device *hdev, u32 port, int lane)
{
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PF8, 0xEC06);
}

static int fw_config_speed_nrz(struct hl_device *hdev, u32 port, int lane)
{
	u32 tx_port, card_location, abs_lane_idx;
	int tx_lane, rc;
	s32 *taps;

	card_location = hdev->nic.card_location;

	get_tx_port_and_lane(hdev, port, lane, &tx_port, &tx_lane);

	lane_swapping_config(hdev, port, lane, false, false);

	init_nrz_tx(hdev, tx_port, tx_lane);
	init_nrz_rx(hdev, port, lane);

	init_lane_for_fw_tx(hdev, tx_port, tx_lane, false, false);
	init_lane_for_fw_rx(hdev, port, lane, false, false);

	prbs_mode_select_tx(hdev, tx_port, tx_lane, false, "PRBS31");
	prbs_mode_select_rx(hdev, port, lane, false, "PRBS31");

	rc = fw_start(hdev, port, lane, false, false);
	if (rc) {
		dev_err(hdev->dev,
			"Card %u Port %u lane %d: F/W config speed NRZ failed, rc %d\n",
			card_location, port, lane, rc);
		return rc;
	}

	if (is_lane_swapping(hdev, port, lane)) {
		rc = fw_start_tx(hdev, tx_port, tx_lane, false, false);
		if (rc) {
			dev_err(hdev->dev,
				"Card %u Port %u lane %d: F/W config speed NRZ failed, rc %d\n",
				card_location, tx_port, tx_lane, rc);
			return rc;
		}
	}

	abs_lane_idx = (port << 1) + lane;
	taps = hdev->nic.phy_tx_taps[abs_lane_idx].nrz_taps;
	set_tx_taps(hdev, tx_port, tx_lane, taps[0], taps[1], taps[2], taps[3], taps[4]);

	return 0;
}

static int reset_mac_tx(struct hl_device *hdev, u32 port)
{
	/* For F/W version 37.1.0 and above, the reset will be done by the F/W */
	if (((hdev->fw_major_version == 37) && (hdev->fw_minor_version > 1)) ||
			(hdev->fw_major_version > 37)) {
		struct cpucp_packet pkt;
		int rc;

		memset(&pkt, 0, sizeof(pkt));
		pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_MAC_TX_RESET << CPUCP_PKT_CTL_OPCODE_SHIFT);
		pkt.port_index = cpu_to_le32(port);

		rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
		if (rc) {
			dev_err(hdev->dev,
				"Card %u Port %u: Failed to reset MAC Tx, rc %d\n",
				hdev->nic.card_location, port, rc);

			return rc;
		}
	} else if (!hdev->asic_prop.fw_security_enabled) {
		u32 tx_ch_mask;

		tx_ch_mask = 1 << PRT0_MAC_CORE_MAC_RST_CFG_SD_TX_SW_RST_N_SHIFT;
		tx_ch_mask <<= (port & 0x1) ? 2 : 0;

		NIC_MACRO_RMWREG32(mmPRT0_MAC_CORE_MAC_RST_CFG, 0, tx_ch_mask);
		msleep(100);
		NIC_MACRO_RMWREG32(mmPRT0_MAC_CORE_MAC_RST_CFG, 1, tx_ch_mask);
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

static int fw_config(struct hl_device *hdev, u32 port, u32 data_rate, bool do_lt)
{
	struct hl_nic_port *nic_port;
	struct hl_nic *nic;
	u32 card_location;
	int lane, rc;
	bool pam4;

	nic = &hdev->nic;
	nic_port = &nic->nic_ports[port];
	card_location = nic->card_location;
	pam4 = (data_rate == NIC_DR_50);

	/* clear go bit */
	if (pam4) {
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_980F, 0x1, 0x800);
		NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_980F, 0x1, 0x100);
	}

	NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_980F, 0x0, 0x8000);

	for (lane = 0 ; lane < 2 ; lane++) {
		if (pam4) {
			rc = fw_config_speed_pam4(hdev, port, lane, do_lt);
			if (rc) {
				dev_err(hdev->dev, "Card %u Port %u lane %d: F/W PAM4 config failed, rc %d\n",
					card_location, port, lane, rc);
				return rc;
			}

			NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PEA, 0x60,
					NIC0_SERDES0_LANE0_REGISTER_0PEA_VDACCLKPHASE0_MASK);
		} else {
			rc = fw_config_speed_nrz(hdev, port, lane);
			if (rc) {
				dev_err(hdev->dev, "Card %u Port %u lane %d: F/W NRZ config failed, rc %d\n",
					card_location, port, lane, rc);
				return rc;
			}
		}
	}

	for (lane = 0 ; lane < 2 ; lane++) {
		rc = set_pll(hdev, port, lane, data_rate);
		if (rc)
			return rc;
	}

	msleep(100);

	reset_mac_tx(hdev, port);

	if (!nic->phy_show_ber)
		set_functional_mode(hdev, port);

	/* set go bit */
	NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_980F, 0x1, 0x8000);

	return 0;
}

static void phy_port_reset(struct hl_device *hdev, u32 port)
{
	int lane;

	dev_dbg(hdev->dev, "Port %u: reset PHY\n", port);

	soft_reset(hdev, port);
	usleep_range(500, 1000);

	for (lane = 0 ; lane < 2 ; lane++)
		clock_init(hdev, port, lane);

	cpu_reset(hdev, port);
	logic_reset(hdev, port);

	usleep_range(500, 1000);
}

static void prbs_reset(struct hl_nic_port *nic_port, int lane, bool pam4)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P43, 1,
				NIC0_SERDES0_LANE0_REGISTER_0P43_RX_PRBS_AUTO_SYNC_EN_MASK);

	if (pam4) {
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P43, 1,
				NIC0_SERDES0_LANE0_REGISTER_0P43_PRBS_SYNC_CNTR_RESET_MASK);
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P43, 0,
				NIC0_SERDES0_LANE0_REGISTER_0P43_PRBS_SYNC_CNTR_RESET_MASK);
	} else {
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0N61, 1,
					NIC0_SERDES0_LANE0_REGISTER_0N61_PRBS_CNTR_RESET_MASK);
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0N61, 0,
					NIC0_SERDES0_LANE0_REGISTER_0N61_PRBS_CNTR_RESET_MASK);
	}

	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P43, 0,
				NIC0_SERDES0_LANE0_REGISTER_0P43_RX_PRBS_AUTO_SYNC_EN_MASK);
}

static u64 _get_prbs_cnt(struct hl_nic_port *nic_port, int lane, bool pam4)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	u64 cnt;

	if (pam4)
		cnt = (((u64) NIC_PHY_RREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P50)) << 16)
			+ NIC_PHY_RREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P51);
	else
		cnt = (((u64) NIC_PHY_RREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0N66)) << 16)
			+ NIC_PHY_RREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0N67);

	return cnt;
}

static enum lane_state get_prbs_cnt(struct hl_nic_port *nic_port, int lane, bool pam4,
				    u64 prbs_prev_cnt, u64 *prbs_new_cnt)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, phy_ready;
	u64 cnt;

	if (pam4) {
		phy_ready = (NIC_PHY_RREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P6A) &
				NIC0_SERDES0_LANE0_REGISTER_0P6A_RX_READ_PHY_READY_MASK) >>
				NIC0_SERDES0_LANE0_REGISTER_0P6A_RX_READ_PHY_READY_SHIFT;
	} else {
		phy_ready = (NIC_PHY_RREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0N2E) &
				NIC0_SERDES0_LANE0_REGISTER_0N2E_NRZ_READ_PHY_READY_MASK) >>
				NIC0_SERDES0_LANE0_REGISTER_0N2E_NRZ_READ_PHY_READY_SHIFT;
	}

	if (!phy_ready)
		return NOT_READY;

	cnt = _get_prbs_cnt(nic_port, lane, pam4);

	/* check PRBS counter wrapped around */
	if (cnt < prbs_prev_cnt) {
		if ((prbs_prev_cnt - cnt) < 0x10000)
			return FAILURE;

		cnt = _get_prbs_cnt(nic_port, lane, pam4);
	}

	*prbs_new_cnt = cnt;

	return READY;
}

static void _print_ber(struct hl_nic_port *nic_port, int lane, u64 total_cnt, u64 error_cnt)
{
	struct hl_device *hdev = nic_port->hdev;
	u64 total_high_digits, error_high_digits, integer, frac;
	u32 card_location, port;
	u8 total_num_digits, error_num_digits, exp;
	int i;

	card_location = hdev->nic.card_location;
	port = nic_port->port;

	total_num_digits = hl_nic_get_num_of_digits(total_cnt);
	error_num_digits = hl_nic_get_num_of_digits(error_cnt);

	if (total_num_digits > 2) {
		total_high_digits = total_cnt;

		for (i = 0 ; i < total_num_digits - 2 ; i++)
			total_high_digits = total_high_digits / 10;
	} else {
		total_high_digits = total_cnt;
	}

	if (!total_high_digits)
		return;

	if (error_num_digits > 2) {
		error_high_digits = error_cnt;

		for (i = 0 ; i < error_num_digits - 2 ; i++)
			error_high_digits = error_high_digits / 10;
	} else {
		error_high_digits = error_cnt;
	}

	exp = total_num_digits - error_num_digits;

	if (error_high_digits < total_high_digits) {
		error_high_digits *= 10;
		exp++;
	}

	integer = div_u64(error_high_digits, total_high_digits);
	frac = div_u64(((error_high_digits - (integer * total_high_digits)) * 10),
			total_high_digits);

	dev_info(hdev->dev,
		"Card %u Port %u lane %d: total_cnt %llu error_cnt %llu BER %llu.%llue-%u\n",
		card_location, port, lane, total_cnt, error_cnt, integer, frac, exp);
}

static void print_ber(struct hl_nic_port *nic_port, int lane, bool pam4)
{
	u64 prbs_err_cnt_pre, prbs_prev_cnt, prbs_err_cnt_post, prbs_err_cnt,
	    prbs_reset_time_jiffies, prbs_accum_time_jiffies, prbs_accum_time_ms,
	    factor, error_cnt, total_cnt;
	struct hl_device *hdev = nic_port->hdev;
	u32 card_location, port;
	enum lane_state state;
	u8 iter_num = 50;

	card_location = hdev->nic.card_location;
	port = nic_port->port;

	prbs_reset(nic_port, lane, pam4);
	prbs_reset_time_jiffies = jiffies;
	prbs_err_cnt_pre = _get_prbs_cnt(nic_port, lane, pam4);

	prbs_prev_cnt = prbs_err_cnt_pre;

	while (true) {
		msleep(500);

		state = get_prbs_cnt(nic_port, lane, pam4, prbs_prev_cnt, &prbs_err_cnt_post);
		prbs_accum_time_jiffies = jiffies - prbs_reset_time_jiffies;
		prbs_accum_time_ms = jiffies_to_msecs(prbs_accum_time_jiffies);
		prbs_err_cnt = prbs_err_cnt_post - prbs_err_cnt_pre;

		if (state != READY) {
			dev_err(hdev->dev, "Card %u Port %u lane %d: No BER (state = %s)\n",
				card_location, port, lane,
				(state == NOT_READY) ? "NOT_READY" : "FAILURE");
			return;
		}

		if (prbs_accum_time_ms >= 5000 || prbs_err_cnt >= 10000000)
			break;

		prbs_prev_cnt = prbs_err_cnt_post;
		iter_num--;
	}

	factor = pam4 ? NIC_PHY_PAM4_BER_FACTOR : NIC_PHY_NRZ_BER_FACTOR;

	dev_info(hdev->dev, "Card %u Port %u lane %d: measurement duration - %llu ms\n",
		card_location, port, lane, prbs_accum_time_ms);

	error_cnt = prbs_err_cnt;
	total_cnt = prbs_accum_time_ms * factor;

	_print_ber(nic_port, lane, total_cnt, error_cnt);
}

static void check_pcs_link(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 card_location, port, mac_gnrl_sts;

	card_location = hdev->nic.card_location;
	port = nic_port->port;

	mac_gnrl_sts = (port & 0x1) ?
		NIC_MACRO_RREG32(mmPRT0_MAC_CORE_MAC_GNRL_STS_2) :
		NIC_MACRO_RREG32(mmPRT0_MAC_CORE_MAC_GNRL_STS_0);

	if (FIELD_GET(PRT0_MAC_CORE_MAC_GNRL_STS_LOC_FAULT_MASK, mac_gnrl_sts))
		nic_port->pcs_local_fault_cnt++;

	if (FIELD_GET(PRT0_MAC_CORE_MAC_GNRL_STS_REM_FAULT_MASK, mac_gnrl_sts)) {
		nic_port->pcs_remote_fault_cnt++;
		nic_port->pcs_remote_fault_seq_cnt++;
	} else {
		nic_port->pcs_remote_fault_seq_cnt = 0;
	}

	if (nic_port->pcs_remote_fault_seq_cnt == NIC_PHY_MAC_REMOTE_FAULT_CNT) {
		dev_dbg(hdev->dev, "Card %u Port %u: got %d sequential remote faults\n",
			card_location, port, NIC_PHY_MAC_REMOTE_FAULT_CNT);
		nic_port->pcs_remote_fault_seq_cnt = 0;
	}

	if (nic_port->pcs_link) {
		nic_port->retry_cnt = 0;
		return;
	}

	nic_port->retry_cnt++;

	if (nic_port->retry_cnt == NIC_PHY_PCS_LINK_CNT) {
		dev_dbg(hdev->dev, "Card %u Port %u: PHY reconfig due to PCS link cnt\n",
			card_location, port);

		hl_nic_phy_port_reconfig(nic_port);
	}
}

static u32 rv_debug(struct hl_device *hdev, u32 port, int lane, u32 mode, u32 index)
{
	u32 cmd, res;

	cmd = 0xB000 + ((mode & 0xF) << 4) + lane;

	fw_cmd(hdev, port, cmd, &index, 0xB, &res);

	return NIC_PHY_RREG32(mmNIC0_SERDES0_REGISTER_9816);
}

static int fw_tuning(struct hl_device *hdev, u32 port, int lane, bool pam4)
{
	u32 state, mode;

	mode = pam4 ? 2 : 1;
	state = rv_debug(hdev, port, lane, mode, 0);

	if (pam4) {
		if (((u16) state) != 0x8F00 && ((u16) state) != 0x8F80)
			return -EAGAIN;
	} else {
		if (((u16) state) != 0x9A00)
			return -EAGAIN;
	}

	return 0;
}

static void print_taps_after_tuning(struct hl_device *hdev, u32 port)
{
	u32 card_location, tx_pre2, tx_pre1, tx_main, tx_post1, tx_post2;
	int lane;

	card_location = hdev->nic.card_location;

	for (lane = 0 ; lane < 2 ; lane++) {
		tx_pre2 = (NIC_PHY_RREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA5) &
				NIC0_SERDES0_LANE0_REGISTER_0PA5_TX_PRE_2_MASK) >>
				NIC0_SERDES0_LANE0_REGISTER_0PA5_TX_PRE_2_SHIFT;
		tx_pre1 = (NIC_PHY_RREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA7) &
				NIC0_SERDES0_LANE0_REGISTER_0PA7_TX_PRE_1_MASK) >>
				NIC0_SERDES0_LANE0_REGISTER_0PA7_TX_PRE_1_SHIFT;
		tx_main = (NIC_PHY_RREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA9) &
				NIC0_SERDES0_LANE0_REGISTER_0PA9_TX_MAIN_MASK) >>
				NIC0_SERDES0_LANE0_REGISTER_0PA9_TX_MAIN_SHIFT;
		tx_post1 = (NIC_PHY_RREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PAB) &
				NIC0_SERDES0_LANE0_REGISTER_0PAB_TX_POST_1_MASK) >>
				NIC0_SERDES0_LANE0_REGISTER_0PAB_TX_POST_1_SHIFT;
		tx_post2 = (NIC_PHY_RREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PAD) &
				NIC0_SERDES0_LANE0_REGISTER_0PAD_TX_POST_2_MASK) >>
				NIC0_SERDES0_LANE0_REGISTER_0PAD_TX_POST_2_SHIFT;

		dev_dbg(hdev->dev,
			"Card %u Port %u lane %d tx taps after F/W tuning: pre2 %d, pre1 %d, main %d, post1 %d, post2 %d\n",
			card_location, port, lane,
			twos_to_int(tx_pre2, 8), twos_to_int(tx_pre1, 8), twos_to_int(tx_main, 8),
			twos_to_int(tx_post1, 8), twos_to_int(tx_post2, 8));
	}
}

static void do_fw_tuning(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 card_location, port;
	int lane, rc;
	bool pam4;

	card_location = hdev->nic.card_location;
	port = nic_port->port;
	pam4 = (nic_port->data_rate == NIC_DR_50);

	for (lane = 0 ; lane < 2 ; lane++) {
		rc = fw_tuning(hdev, port, lane, pam4);
		if (rc) {
			if (ktime_after(ktime_get(), nic_port->fw_tuning_limit_ts)) {
				dev_dbg(hdev->dev, "Card %u Port %u lane %d: F/W tuning limit\n",
					card_location, port, lane);

				hl_nic_phy_port_reconfig(nic_port);
				return;
			}

			break;
		}
	}

	if (!rc) {
		/* The control lock needs to be taken here in order to protect against a parallel
		 * status set from the link event handler.
		 * This lock also protects port close flow that destroys this thread synchronically,
		 * so a potential deadlock could happen here.
		 * In order to avoid this deadlock, we need to check if this lock was taken.
		 * If it was taken and the port is marked as closed (i.e., we are now during port
		 * close flow), we can return immediately.
		 * Otherwise, we need to keep trying to take this lock before we enter the critial
		 * section.
		 */
		while (!mutex_trylock(&nic_port->control_lock))
			if (!hl_nic_is_port_open(nic_port))
				return;

		nic_port->phy_fw_tuned = true;
		dev_dbg(hdev->dev, "Card %u Port %u: F/W tuning passed\n", card_location, port);

		/* If we got link up event - print it now when PHY is ready */
		if (nic_port->pcs_link)
			hl_nic_phy_set_port_status(nic_port, true);

		mutex_unlock(&nic_port->control_lock);

		print_taps_after_tuning(hdev, port);
		nic_port->retry_cnt = 0;

		if (hdev->nic.phy_show_ber) {
			dev_dbg(hdev->dev,
				"Card %u Port %u: Waiting %d seconds before switching to functional mode\n",
				card_location, port, NIC_PHY_POST_FW_TUNING_WAIT_LONG_MS / 1000);

			/* Wait 5 seconds before calculating BER */
			msleep(5000);

			for (lane = 0; lane < 2 ; lane++)
				print_ber(nic_port, lane, pam4);
		}
	}
}

static int fw_tuning_an(struct hl_device *hdev, u32 port, int lane)
{
	u32 state = rv_debug(hdev, port, lane, 1, 0);

	if (((u16) state) != 0xA01F && ((u16) state) != 0xA020 && ((u16) state) != 0xAF00) {
		u32 error_status = rv_debug(hdev, port, lane, 0, 3);

		dev_dbg_ratelimited(hdev->dev, "Port %u lane %d: state 0x%x error 0x%x\n",
					port, lane, state, error_status);
		return -EAGAIN;
	}

	return 0;
}

static void tx_quite(struct hl_device *hdev, u32 port, int lane)
{
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA1, 0x0);
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA2, 0x0);
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA3, 0x0);
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA4, 0x0);
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, 0x0,
				NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_TEST_DATA_SRC_MASK);
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, 0x0,
				NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_PAM4_TEST_EN_MASK);
}

static int do_anlt(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 card_location, port, tx_port;
	int tx_lane, rc;

	card_location = hdev->nic.card_location;
	port = nic_port->port;

	/* fw_tuning_an needs to be done only on lane 0 */
	rc = fw_tuning_an(hdev, port, 0);
	if (rc) {
		dev_dbg_ratelimited(hdev->dev,
					"Card %u Port %u lane 0: PHY auto neg fw is not ready\n",
					card_location, port);

		return rc;
	}

	get_tx_port_and_lane(hdev, port, 0, &tx_port, &tx_lane);
	tx_quite(hdev, tx_port, tx_lane);

	rc = fw_config(hdev, port, NIC_DR_50, true);
	if (rc) {
		dev_dbg(hdev->dev,
			"Card %u Port %u: PHY link training failed, rc %d\n",
			card_location, port, rc);

		hl_nic_phy_port_reconfig(nic_port);

		return rc;
	}

	nic_port->auto_neg_resolved = true;

	return 0;
}

static void do_fw_tuning_auto_neg(struct hl_nic_port *nic_port)
{
	if (nic_port->auto_neg_enable) {
		if (do_anlt(nic_port))
			return;
	} else {
		nic_port->auto_neg_skipped = true;
	}

	nic_port->fw_tuning_limit_ts = ktime_add_ms(ktime_get(), NIC_PHY_FW_TUNING_TIMEOUT_MS);
	do_fw_tuning(nic_port);
}

void gaudi2_nic_phy_link_status_work(struct work_struct *work)
{
	struct hl_nic_port *nic_port;
	struct hl_device *hdev;
	u32 port, timeout_ms;

	nic_port = container_of(work, struct hl_nic_port, link_status_work.work);
	hdev = nic_port->hdev;
	port = nic_port->port;

	if (nic_port->phy_fw_tuned) {
		if (nic_port->phy_func_mode_en)
			check_pcs_link(nic_port);
		else
			set_functional_mode(hdev, port);
	} else {
		if (nic_port->auto_neg_resolved || nic_port->auto_neg_skipped)
			do_fw_tuning(nic_port);
		else
			do_fw_tuning_auto_neg(nic_port);
	}

	if (!nic_port->phy_fw_tuned)
		timeout_ms = NIC_PHY_FW_TUNING_INTERVAL_MS;
	else if (!nic_port->phy_func_mode_en)
		timeout_ms = hdev->nic.phy_show_ber ?
			NIC_PHY_POST_FW_TUNING_WAIT_LONG_MS : NIC_PHY_POST_FW_TUNING_WAIT_SHORT_MS;
	else
		timeout_ms = NIC_PHY_CHECK_LINK_INTERVAL_MS;

	queue_delayed_work(nic_port->wq, &nic_port->link_status_work,
			msecs_to_jiffies(timeout_ms));
}

static void set_tx(struct hl_device *hdev, u32 port, int lane, bool enable)
{
	u32 val = enable ? 0x1 : 0x0;

	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0NF8, val,
				NIC0_SERDES0_LANE0_REGISTER_0NF8_PU_VDRV_MASK);
}

void gaudi2_nic_phy_port_start_stop(struct hl_nic_port *nic_port, bool is_start)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, tx_port;
	int lane, tx_lane;

	for (lane = 0 ; lane < 2 ; lane++) {
		get_tx_port_and_lane(hdev, port, lane, &tx_port, &tx_lane);

		if (is_start) {
			/* Enable TX driver in SerDes */
			set_tx(hdev, tx_port, tx_lane, true);
			/* Enable F/W Rx tuning is done during power up flow */
		} else {
			/* Disable TX driver in SerDes */
			set_tx(hdev, tx_port, tx_lane, false);
			/* Silence F/W Rx tuning */
			NIC_PHY_WREG32(mmNIC0_SERDES0_REGISTER_9815, 0x9000 | lane);
		}
	}
}

static int fw_start_an(struct hl_device *hdev, u32 port, int lane)
{
	u32 detail = 0, ignore;

	return fw_cmd(hdev, port, 0x80A0 | lane, &detail, 0x8, &ignore);
}

static int fw_start_an_tx(struct hl_device *hdev, u32 port, int lane)
{
	u32 detail = 0, ignore;
	int rc;

	rc = fw_cmd(hdev, port, 0x7040 | lane, &detail, 0x7, &ignore);
	if (rc)
		return rc;

	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0PA0, 0x0,
				NIC0_SERDES0_LANE0_REGISTER_0PA0_TX_PAM4_TEST_EN_MASK);

	return 0;
}

static int fw_config_auto_neg(struct hl_device *hdev, u32 port, int lane)
{
	struct hl_nic_port *nic_port = &hdev->nic.nic_ports[port];
	u64 basepage = 0x800000001ull;
	u32 tx_port, pflags;
	u32 card_location;
	int tx_lane, rc;

	card_location = hdev->nic.card_location;
	pflags = hl_nic_get_pflags(nic_port);

	get_tx_port_and_lane(hdev, port, lane, &tx_port, &tx_lane);

	/* clear go bit */
	NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_980F, 0x0, 0x8000);

	init_nrz_tx(hdev, tx_port, tx_lane);
	init_nrz_rx(hdev, port, lane);

	init_lane_for_fw_tx(hdev, tx_port, tx_lane, false, true);
	init_lane_for_fw_rx(hdev, port, lane, false, true);

	prbs_mode_select_tx(hdev, tx_port, tx_lane, false, "PRBS31");
	prbs_mode_select_rx(hdev, port, lane, false, "PRBS31");

	lane_swapping_config(hdev, port, lane, true, true);

	/* set FW to start AN */

	rc = fw_start_an(hdev, port, lane);
	if (rc) {
		dev_err(hdev->dev,
			"Card %u Port %u lane %d: start auto neg failed, rc %d\n",
			card_location, tx_port, tx_lane, rc);
		return rc;
	}

	if (is_lane_swapping(hdev, port, lane)) {
		rc = fw_start_an_tx(hdev, tx_port, tx_lane);
		if (rc) {
			dev_err(hdev->dev,
				"Card %u Port %u lane %d: start auto neg failed, rc %d\n",
				card_location, tx_port, tx_lane, rc);
			return rc;
		}
	}

	/* AN reset */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_AK00, 0xE000);

	/* AN mode */
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_AI10, basepage & 0xFFFF);
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_AI11, (basepage >> 16) & 0xFFFF);
	NIC_PHY_WREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_AI12, (basepage >> 32) & 0xFFFF);

	/* IEEE */
	NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_AK00, 0x1,
				NIC0_SERDES0_LANE0_REGISTER_AK00_ARG_ANEG_IEEE_MODE_S_MASK);

	if (pflags & PFLAGS_PHY_AUTO_NEG_LPBK)
		NIC_PHY_RMWREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_AK00, 0x1,
				NIC0_SERDES0_LANE0_REGISTER_AK00_ARG_DIS_NONCE_MATCH_S_MASK);

	rc = set_pll(hdev, port, lane, NIC_DR_25);
	if (rc)
		return rc;

	/* set go bit */
	NIC_PHY_RMWREG32(mmNIC0_SERDES0_REGISTER_980F, 0x1, 0x8000);

	return 0;
}

int gaudi2_nic_phy_port_power_up(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 data_rate = nic_port->data_rate;
	u32 card_location, port;
	int rc;

	card_location = hdev->nic.card_location;
	port = nic_port->port;

	phy_port_reset(hdev, port);

	nic_port->phy_func_mode_en = false;

	if (nic_port->auto_neg_enable) {
		/* AN config should be done only on lane 0 */
		rc = fw_config_auto_neg(hdev, port, 0);
		if (rc) {
			dev_err(hdev->dev,
				"Card %u Port %u: F/W config auto_neg failed, rc %d\n",
				card_location, port, rc);
			return rc;
		}
	} else {
		rc = fw_config(hdev, port, data_rate, false);
		if (rc) {
			dev_err(hdev->dev,
				"Card %u Port %u: F/W config failed, rc %d\n",
				card_location, port, rc);
			return rc;
		}
	}

	return 0;
}

void gaudi2_nic_phy_port_reconfig(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 card_location, port;
	int rc;

	if (!hdev->nic.phy_config_fw)
		return;

	card_location = hdev->nic.card_location;
	port = nic_port->port;

	dev_dbg(hdev->dev, "Card %u Port %u: reconfiguring PHY\n", card_location, port);

	rc = gaudi2_nic_phy_port_power_up(nic_port);
	if (rc)
		dev_err(hdev->dev, "Card %u Port %u: PHY reconfig failed\n", card_location, port);
}

int gaudi2_nic_phy_port_init(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	int rc;

	gaudi2_mac_lane_remap(hdev, port);

	rc = hl_nic_phy_init(nic_port);
	if (rc)
		dev_err(hdev->dev, "Port %u: failed to init NIC PHY, rc %d\n", port, rc);

	return rc;
}

void gaudi2_nic_phy_port_fini(struct hl_nic_port *nic_port)
{
	hl_nic_phy_fini(nic_port);
}

int gaudi2_nic_phy_reset_macro(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev = nic_macro->hdev;
	u32 port;

	/* Reset the two ports under the given nic_macro */
	port = nic_macro->idx << 1;

	/* Enable PHY refclk */
	NIC_MACRO_WREG32(mmNIC0_PHY_PHY_IDDQ_0, 0);
	NIC_MACRO_WREG32(mmNIC0_PHY_PHY_IDDQ_1, 0);

	phy_port_reset(hdev, port);
	phy_port_reset(hdev, port + 1);

	return 0;
}

static int find_first_enabled_port(struct hl_device *hdev, u32 *port)
{
	int i;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		*port = i;
		return 0;
	}

	return -EINVAL;
}

static void fw_write_all(struct hl_device *hdev, u32 addr, u32 data)
{
	int port;

	for (port = 0 ; port < NIC_NUMBER_OF_PORTS ; port++) {
		if (!(hdev->nic_ports_mask & BIT(port)))
			continue;

		NIC_PHY_WREG32(addr, data);
	}
}

static void fw_write_all_lanes(struct hl_device *hdev, u32 addr, u32 data)
{
	int port, lane;

	for (port = 0 ; port < NIC_NUMBER_OF_PORTS ; port++) {
		if (!(hdev->nic_ports_mask & BIT(port)))
			continue;

		for (lane = 0 ; lane < 2 ; lane++)
			NIC_PHY_WREG32_LANE(addr, data);
	}
}

static void fw_unload_all(struct hl_device *hdev)
{
	u32 port;

	fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_9814, 0xFFF0);

	for (port = 0 ; port < NIC_NUMBER_OF_PORTS ; port++) {
		if (!(hdev->nic_ports_mask & BIT(port)))
			continue;

		cpu_reset(hdev, port);
	}

	msleep(100);

	fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_9814, 0x0);

	/* PAM4 */
	fw_write_all_lanes(hdev, mmNIC0_SERDES0_LANE0_REGISTER_0P11, 0);
	usleep_range(1000, 2000);
	fw_write_all_lanes(hdev, mmNIC0_SERDES0_LANE0_REGISTER_0P11, 0x2000);

	/* NRZ */
	fw_write_all_lanes(hdev, mmNIC0_SERDES0_LANE0_REGISTER_0N0B, 0);
	fw_write_all_lanes(hdev, mmNIC0_SERDES0_LANE0_REGISTER_0N0C, 0);
	usleep_range(1000, 2000);
	fw_write_all_lanes(hdev, mmNIC0_SERDES0_LANE0_REGISTER_0N0C, 0x8000);
}

static u32 fw_crc(struct hl_device *hdev, u32 port)
{
	u32 checksum_code, ignore;

	fw_cmd(hdev, port, 0xF001, NULL, 0xF, &ignore);
	checksum_code = NIC_PHY_RREG32(mmNIC0_SERDES0_REGISTER_9816);

	return checksum_code;
}

static u32 fw_hash(struct hl_device *hdev, u32 port)
{
	u32 low_word, hash_code, res;

	fw_cmd(hdev, port, 0xF000, NULL, 0xF, &res);
	low_word = NIC_PHY_RREG32(mmNIC0_SERDES0_REGISTER_9816);
	hash_code = ((res & 0xFF) << 16) | low_word;

	return hash_code;
}

static int mcu_cal_enable_all(struct hl_device *hdev)
{
	u32 port;
	int rc;

	for (port = 0 ; port < NIC_NUMBER_OF_PORTS ; port++) {
		if (!(hdev->nic_ports_mask & BIT(port)))
			continue;

		rc = set_fw_reg(hdev, port, 357, NIC_PHY_FW_TIME_CONSTANT_RATIO);
		if (rc) {
			dev_dbg(hdev->dev, "Port %u: MCU calibration failed\n", port);
			return rc;
		}
	}

	return 0;
}

int gaudi2_nic_phy_fw_load_all(struct hl_device *hdev)
{
	u32 entry_point, length, ram_addr, sections, status, checks, checksum;
	const struct firmware *fw;
	int rc, i, j, data_ptr = 0;
	const void *fw_data;
	const char *fw_name;
	u16 mdio_data;
	u32 port; /* For regs read */

	rc = find_first_enabled_port(hdev, &port);
	if (rc)
		return rc;

	fw_name = gaudi2_nic_phy_get_fw_name();

	fw_unload_all(hdev);

	rc = request_firmware(&fw, fw_name, hdev->dev);
	if (rc) {
		dev_err(hdev->dev, "Firmware file %s is not found\n", fw_name);
		return rc;
	}

	fw_data = (const void *)fw->data;
	fw_data += 0x1000;

	/* skip hash, crc and date */
	entry_point = get_unaligned_be32(fw_data + 8);
	length = get_unaligned_be32(fw_data + 12);
	ram_addr = get_unaligned_be32(fw_data + 16);

	dev_dbg(hdev->dev, "entry_point: 0x%x\n", entry_point);
	dev_dbg(hdev->dev, "length: 0x%x\n", length);

	fw_data += 20;

	sections = DIV_ROUND_UP(length, 24);

	dev_dbg(hdev->dev, "sections: %d\n", sections);

	fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_9814, 0xFFF0); /* FW2 */
	fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_980D, 0x0AAA); /* FW1 */
	fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_980D, 0x0); /* FW1 */

	checks = 0;

	do {
		usleep_range(10000, 20000);
		status = NIC_PHY_RREG32(mmNIC0_SERDES0_REGISTER_9814); /* FW2 */
		dev_dbg(hdev->dev, "port %d, status: 0x%x\n", port, status);
		if (checks++ > NIC_PHY_READ_COUNTS_PER_MS) {
			dev_err(hdev->dev,
				"failed to load NIC F/W, fw2 timeout 0x%x\n",
				status);
			rc = -ETIMEDOUT;
			goto release_fw;
		}
	} while (status);

	fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_9814, 0x0);

	for (i = 0 ; i <= sections ; i++) {
		checksum = 0x800C;

		fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_9F0C, ram_addr >> 16); /* FW0 + 12 */
		fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_9F0D, ram_addr & 0xFFFF); /* FW0 + 13 */
		checksum += (ram_addr >> 16) + (ram_addr & 0xFFFF);

		for (j = 0 ; j < 12 ; j++) {
			if (data_ptr >= length)
				mdio_data = 0;
			else
				mdio_data = get_unaligned_be16(fw_data + data_ptr);

			fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_9F00 + 4*j, mdio_data);

			checksum += mdio_data;
			data_ptr += 2;
			ram_addr += 2;
		}

		/* FW0 + 14 */
		fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_9F0E, (~checksum + 1) & 0xFFFF);
		fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_9F0F, 0x800C); /* FW0 + 15 */

		checks = 0;

		do {
			usleep_range(1000, 2000);
			status = NIC_PHY_RREG32(mmNIC0_SERDES0_REGISTER_9F0F); /* FW0 + 15 */
			if (checks++ > NIC_PHY_READ_COUNTS_PER_MS) {
				dev_err(hdev->dev,
					"failed to load NIC F/W, fw0 timeout 0x%x\n", status);
				rc = -ETIMEDOUT;
				goto release_fw;
			}
		} while (status == 0x800C);
	}

	fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_9F0C, entry_point >> 16); /* FW0 + 12 */
	fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_9F0D, entry_point & 0xFFFF); /* FW0 + 13 */
	checksum = (entry_point >> 16) + (entry_point & 0xFFFF) + 0x4000;
	fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_9F0E, (~checksum + 1) & 0xFFFF); /* FW0 + 14 */
	fw_write_all(hdev, mmNIC0_SERDES0_REGISTER_9F0F, 0x4000); /* FW0 + 15 */

	msleep(500);

	dev_dbg(hdev->dev, "F/W CRC = 0x%x\n", fw_crc(hdev, port));
	dev_dbg(hdev->dev, "F/W hash = 0x%x\n", fw_hash(hdev, port));

	rc = mcu_cal_enable_all(hdev);

release_fw:
	release_firmware(fw);
	return rc;
}

u16 gaudi2_nic_phy_get_crc(struct hl_device *hdev)
{
	u32 port;
	int rc;

	rc = find_first_enabled_port(hdev, &port);
	if (rc)
		return rc;

	return fw_crc(hdev, port);
}

static bool is_old_phy_fw_loaded(struct hl_device *hdev)
{
	return gaudi2_nic_phy_get_crc(hdev) == 0x1723;
}

static bool is_phy_fw_with_anlt_support(struct hl_device *hdev)
{
	return gaudi2_nic_phy_get_crc(hdev) == 0x185E;
}

int gaudi2_nic_phy_init(struct hl_device *hdev)
{
	struct hl_nic *nic = &hdev->nic;

	if (!nic->phy_config_fw)
		return 0;

	/* Fail the initialization in case of an old PHY F/W, as the current PHY init flow won't
	 * work with it.
	 */
	if (is_old_phy_fw_loaded(hdev)) {
		dev_err(hdev->dev, "PHY F/W is very old - failing the initialization\n");
		return -EINVAL;
	}

	/* In case the PHY F/W has ANLT support we will enable it according to the mask.
	 * Otherwise, set the mask to 0 (ANLT is disabled on all ports).
	 * Such a PHY FW can be loaded by embedded F/W with version >= 1.8.1 or manually by the
	 * driver (for debug purposes).
	 *
	 * NOTE - this code doesn't cover the case that the user manually loaded PHY F/W w/o ANLT
	 * support with an embedded F/W >= 1.8.1 - for such a case he can set the nic_auto_neg_mask
	 * module parameter to 0.
	 */
	if (!gaudi2_is_fw_ver_below_1_8_1(hdev) ||
			(nic->phy_load_fw && is_phy_fw_with_anlt_support(hdev)))
		nic->auto_neg_mask = hdev->nic_auto_neg_mask;
	else
		nic->auto_neg_mask = 0;

	/* In case we didn't get serdes info from FW, set to default values */
	if (!nic->use_fw_serdes_info) {
		set_default_mac_lane_remap(hdev);
		set_default_polarity_values(hdev);
	}

	/* set the default Tx taps */
	set_default_tx_taps_values(hdev);

	return 0;
}

static int fw_read_s16(struct hl_device *hdev, u32 port, u32 offset)
{
	u32 t = NIC_PHY_RREG32(mmNIC0_SERDES0_REGISTER_9F00 + 4 * offset);

	return (t & 0x8000) ? t - 0x10000 : t;
}

static void get_channel_estimation_params(struct hl_device *hdev, u32 port, int lane, u32 *of,
						u32 *hf)
{
	struct hl_nic_port *nic_port = &hdev->nic.nic_ports[port];

	if (nic_port->auto_neg_enable) {
		*of = rv_debug(hdev, port, lane, 5, 22);
		*hf = rv_debug(hdev, port, lane, 5, 23);
	} else {
		*of = rv_debug(hdev, port, lane, 2, 4);
		*hf = rv_debug(hdev, port, lane, 2, 5);
	}
}

static void copy_info(char *buf, char *name, int *data, u8 count)
{
	int i;

	sprintf(buf + strlen(buf), "%s:", name);

	for (i = 0 ; i < count ; i++)
		sprintf(buf + strlen(buf), " %d", data[i]);

	sprintf(buf + strlen(buf), "\n");
}

static void dump_mac_params(struct hl_device *hdev, u32 port, char *buf)
{
	u32 mac_rec_sts, mac_sd_sts, mac_gnrl_sts[4], phy_rx_sts[4];
	int i;

	mac_rec_sts = NIC_MACRO_RREG32(mmPRT0_MAC_CORE_MAC_REC_STS0);
	mac_sd_sts = NIC_MACRO_RREG32(mmPRT0_MAC_CORE_MAC_SD_STS);

	for (i = 0 ; i < 4 ; i++) {
		mac_gnrl_sts[i] = NIC_MACRO_RREG32(mmPRT0_MAC_CORE_MAC_GNRL_STS_0 + 4*i);
		phy_rx_sts[i] = NIC_MACRO_RREG32(mmNIC0_PHY_PHY_RX_STS_0 + 4*i);
	}

	sprintf(buf + strlen(buf),
		"MAC_REC_STS0: REC_ALIGN_DONE 0x%x , REC_HIGH_BER 0x%x , REC_LINK_STS 0x%x , REC_AMPS_LOCK 0x%x , REC_RSFEC_ALIGNED 0x%x\n",
		FIELD_GET(PRT0_MAC_CORE_MAC_REC_STS0_REC_ALIGN_DONE_MASK, mac_rec_sts),
		FIELD_GET(PRT0_MAC_CORE_MAC_REC_STS0_REC_HIGH_BER_MASK, mac_rec_sts),
		FIELD_GET(PRT0_MAC_CORE_MAC_REC_STS0_REC_LINK_STS_MASK, mac_rec_sts),
		FIELD_GET(PRT0_MAC_CORE_MAC_REC_STS0_REC_AMPS_LOCK_MASK, mac_rec_sts),
		FIELD_GET(PRT0_MAC_CORE_MAC_REC_STS0_REC_RSFEC_ALIGNED_MASK, mac_rec_sts));

	sprintf(buf + strlen(buf),
		"MAC_SD_STS: SD_SID_DET 0x%x\n",
		FIELD_GET(PRT0_MAC_CORE_MAC_SD_STS_SD_SIG_DET_MASK, mac_sd_sts));

	for (i = 0 ; i < 4 ; i++) {
		sprintf(buf + strlen(buf),
			"MAC_GNRL_STS%d: TX_OVR_ERR 0x%x , TX_UNDERFLOW 0x%x , LOC_FAULT 0x%x , REM_FAULT 0x%x , LI_FAULT 0x%x\n",
			i,
			FIELD_GET(PRT0_MAC_CORE_MAC_GNRL_STS_TX_OVR_ERR_MASK, mac_gnrl_sts[i]),
			FIELD_GET(PRT0_MAC_CORE_MAC_GNRL_STS_TX_UNDERFLOW_MASK, mac_gnrl_sts[i]),
			FIELD_GET(PRT0_MAC_CORE_MAC_GNRL_STS_LOC_FAULT_MASK, mac_gnrl_sts[i]),
			FIELD_GET(PRT0_MAC_CORE_MAC_GNRL_STS_REM_FAULT_MASK, mac_gnrl_sts[i]),
			FIELD_GET(PRT0_MAC_CORE_MAC_GNRL_STS_LI_FAULT_MASK, mac_gnrl_sts[i]));
	}

	for (i = 0 ; i < 4 ; i++) {
		sprintf(buf + strlen(buf),
			"PHY_RX_STS%d: PHY_READY 0x%x , SIGNAL_DETECT 0x%x , PLL_LOCK 0x%x , PHY_RX_CLK_TICK 0x%x\n",
			i,
			FIELD_GET(NIC0_PHY_PHY_RX_STS_PHY_READY_MASK, phy_rx_sts[i]),
			FIELD_GET(NIC0_PHY_PHY_RX_STS_SIGNAL_DETECT_MASK, phy_rx_sts[i]),
			FIELD_GET(NIC0_PHY_PHY_RX_STS_PLL_LOCK_MASK, phy_rx_sts[i]),
			FIELD_GET(NIC0_PHY_PHY_RX_STS_PHY_RX_CLK_TICK_MASK, phy_rx_sts[i]));
	}
}

void gaudi2_nic_phy_dump_serdes_params(struct hl_device *hdev, char *buf, size_t size)
{
	u32 port, card_location, sd, phy_ready, ch_est_of, ch_est_hf, ppm_twos, adapt_state;
	int lane, i, ppm, eye[3], isi[18];

	port = hdev->nic.phy_port_to_dump;
	card_location = hdev->nic.card_location;

	sprintf(buf + strlen(buf), "\n");

	for (lane = 0 ; lane < 2 ; lane++) {
		sd = (NIC_PHY_RREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P6A) &
				NIC0_SERDES0_LANE0_REGISTER_0P6A_READ_SIG_DET_MASK) >>
				NIC0_SERDES0_LANE0_REGISTER_0P6A_READ_SIG_DET_SHIFT;

		phy_ready = (NIC_PHY_RREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P6A) &
				NIC0_SERDES0_LANE0_REGISTER_0P6A_RX_READ_PHY_READY_MASK) >>
				NIC0_SERDES0_LANE0_REGISTER_0P6A_RX_READ_PHY_READY_SHIFT;
		ppm_twos = (NIC_PHY_RREG32_LANE(mmNIC0_SERDES0_LANE0_REGISTER_0P73) &
				NIC0_SERDES0_LANE0_REGISTER_0P73_READ_FREQ_ACC_MASK) >>
				NIC0_SERDES0_LANE0_REGISTER_0P73_READ_FREQ_ACC_SHIFT;
		ppm = twos_to_int(ppm_twos, 11);
		adapt_state = rv_debug(hdev, port, lane, 2, 0);

		get_channel_estimation_params(hdev, port, lane, &ch_est_of, &ch_est_hf);

		rv_debug(hdev, port, lane, 0xA, 5);
		for (i = 0 ; i < 3 ; i++)
			eye[i] = fw_read_s16(hdev, port, i);

		rv_debug(hdev, port, lane, 0xA, 0);
		for (i = 0 ; i < 16 ; i++)
			isi[i] = fw_read_s16(hdev, port, i);

		rv_debug(hdev, port, lane, 0xA, 8);
		for (i = 0 ; i < 2 ; i++)
			isi[16+i] = fw_read_s16(hdev, port, i);

		sprintf(buf + strlen(buf),
			"Card %u Port %u lane %d:\n", card_location, port, lane);
		sprintf(buf + strlen(buf),
			"sd: %u\nphy_ready: %u\nppm: %d\nch_est_of: %u\nch_est_hf: %u\n"
			"adaptation state: 0x%x\n",
			sd, phy_ready, ppm, ch_est_of, ch_est_hf, adapt_state);
		copy_info(buf, "eyes", eye, 3);
		copy_info(buf, "isi", isi, 18);
		sprintf(buf + strlen(buf), "\n");
	}

	dump_mac_params(hdev, port, buf);
}
