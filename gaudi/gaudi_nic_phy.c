// SPDX-License-Identifier: GPL-2.0

/* Copyright 2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi_nic.h"
#include "../include/gaudi/asic_reg/gaudi_regs.h"
#include <linux/module.h>
#include <linux/firmware.h>
#include <asm/unaligned.h>

#define HL_PHY_DEBUG 0

#define PHY_READ_COUNTS_PER_MS	1000
#define PHY_FW_SIZE		0x1020
#define PHY_FW_FINISHED		BIT(2)
#define PHY_FW_ERROR		BIT(3)

#define NIC0_PHY_BASE		hdev->asic_prop.nic_props.phy_base_addr

#define PCS_LINK_CNT		10
#define PCS_FAULT_THRESHOLD	20
#define PCS_LINK_RETRY_MSEC	20
#define FW_TUNING_TIMEOUT_MSEC	(10 * MSEC_PER_SEC)
#define PHY_FLUSH_TIMEOUT_MSEC	1

/* enum link_status - PCS link status.
 * @LINK_STS_UP: PHY is ready and PCS has link.
 * @LINK_STS_PCS_DOWN: PCS has no link.
 * @LINK_STS_PHY_DON: PHY is not ready.
 * @LINK_STS_FAIL_RECONFIG: need to reconfigure the PHY due to PCS link failures.
 * @LINK_STS_FAULT_RECONFIG: need to reconfigure the PHY due to PCS link faults.
 */
enum link_status {
	LINK_STS_UP,
	LINK_STS_PCS_DOWN,
	LINK_STS_PHY_DOWN,
	LINK_STS_FAIL_RECONFIG,
	LINK_STS_FAULT_RECONFIG
};

#define LINK_STS_SHIFT		6
#define LINK_STS_MASK		0x3C0

#define GAUDI_PHY_FW_FILE	"habanalabs/gaudi/gaudi_nic_fw.bin"

#define PHY_READ(lane, addr)		_phy_read(hdev, port, (lane), (addr))
#define PHY_WRITE(lane, addr, data)	_phy_write(hdev, port, (lane), (addr), (data))
#define PHY_WRITE_MASK(lane, addr, raw_data, mask) \
			_phy_write_mask(hdev, port, (lane), (addr), (raw_data), (mask))

MODULE_FIRMWARE(GAUDI_PHY_FW_FILE);

const char *gaudi_nic_phy_get_fw_name(void)
{
	return GAUDI_PHY_FW_FILE;
}

static bool has_pcs_link(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 val, port = nic_port->port;

	val = NIC_MACRO_RREG32(mmNIC0_MAC_CORE_MAC_REC_STS0);
	val &= LINK_STS_MASK;
	val >>= LINK_STS_SHIFT;

	/* need to check the first lane only */
	return val &= BIT(__ffs(nic_port->fw_tuning_mask));
}

static void get_polarity(struct hl_device *hdev, u32 abs_lane_idx, u32 *pol_tx, u32 *pol_rx)
{
	struct hl_nic_properties *nic_props;
	u32 card_location;

	nic_props = &hdev->asic_prop.nic_props;
	card_location = hdev->nic.card_location;

	*pol_tx = *pol_rx = 0;

	if (abs_lane_idx >= nic_props->max_num_of_lanes) {
		dev_err(hdev->dev, "bad lane %d, can't get polarity\n", abs_lane_idx);
		return;
	}

	switch (hdev->card_type) {
	case cpucp_card_type_pci:
		switch (abs_lane_idx) {
		case 0 ... 3:
		case 10 ... 11:
			*pol_tx = 0;
			*pol_rx = 0;
			break;
		case 5 ... 8:
		case 12:
		case 16:
			*pol_tx = 0;
			*pol_rx = 1;
			break;
		case 15:
		case 19:
			*pol_tx = 1;
			*pol_rx = 0;
			break;
		case 4:
		case 9:
		case 13 ... 14:
		case 17 ... 18:
			*pol_tx = 1;
			*pol_rx = 1;
			break;
		default:
			dev_err(hdev->dev, "wrong lane idx %d in PCI card\n", abs_lane_idx);
			break;
		}
		break;

	case cpucp_card_type_pmc:
		switch (card_location) {
		case 0:
			switch (abs_lane_idx) {
			case 0 ... 1:
			case 3:
			case 5 ... 6:
			case 8 ... 9:
			case 12 ... 15:
				fallthrough;
			case 17:
			case 19:
				*pol_rx = 1;
				break;
			case 2:
			case 16:
			case 18:
				*pol_tx = 1;
				break;
			default:
				break;
			}
			break;
		case 1:
			switch (abs_lane_idx) {
			case 0 ... 1:
			case 3 ... 6:
			case 8 ... 9:
			case 12 ... 15:
				fallthrough;
			case 17:
			case 19:
				*pol_rx = 1;
				break;
			case 2:
			case 16:
			case 18:
				*pol_tx = 1;
				break;
			default:
				break;
			}
			break;
		case 2:
			switch (abs_lane_idx) {
			case 0 ... 1:
			case 3:
			case 5 ... 6:
			case 8 ... 9:
			case 12 ... 15:
				fallthrough;
			case 17:
			case 19:
				*pol_rx = 1;
				break;
			case 2:
			case 16:
			case 18:
				*pol_tx = 1;
				break;
			default:
				break;
			}
			break;
		case 3:
			switch (abs_lane_idx) {
			case 0 ... 1:
			case 3:
			case 5 ... 6:
			case 8 ... 9:
			case 12 ... 15:
				fallthrough;
			case 17:
			case 19:
				*pol_rx = 1;
				break;
			case 2:
			case 16:
			case 18:
				*pol_tx = 1;
				break;
			default:
				break;
			}
			break;
		case 4:
			switch (abs_lane_idx) {
			case 0 ... 1:
			case 3:
			case 5 ... 6:
			case 8 ... 9:
			case 12 ... 15:
				fallthrough;
			case 17:
			case 19:
				*pol_rx = 1;
				break;
			case 2:
			case 16:
			case 18:
				*pol_tx = 1;
				break;
			default:
				break;
			}
			break;
		case 5:
			switch (abs_lane_idx) {
			case 0 ... 1:
			case 3:
			case 5 ... 6:
			case 8 ... 10:
			case 12 ... 15:
				fallthrough;
			case 17:
			case 19:
				*pol_rx = 1;
				break;
			case 2:
			case 16:
			case 18:
				*pol_tx = 1;
				break;
			default:
				break;
			}
			break;
		case 6:
			switch (abs_lane_idx) {
			case 0 ... 1:
			case 3:
			case 5 ... 6:
			case 8 ... 9:
			case 12 ... 15:
				fallthrough;
			case 17:
			case 19:
				*pol_rx = 1;
				break;
			case 2:
			case 16:
			case 18:
				*pol_tx = 1;
				break;
			default:
				break;
			}
			break;
		case 7:
			switch (abs_lane_idx) {
			case 0 ... 1:
			case 3 ... 6:
			case 8 ... 9:
			case 12 ... 15:
				fallthrough;
			case 17:
			case 19:
				*pol_rx = 1;
				break;
			case 2:
			case 16:
			case 18:
				*pol_tx = 1;
				break;
			default:
				break;
			}
			break;
		}
		break;
	default:
		dev_err(hdev->dev, "wrong card type %d\n", hdev->card_type);
		break;
	}
}

static void set_default_polarity_values(struct hl_device *hdev)
{
	struct cpucp_nic_info *nic_info = &hdev->asic_prop.cpucp_nic_info;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	u32 pol_tx, pol_rx;
	int abs_lane_idx;
	u64 val;

	for (abs_lane_idx = 0 ; abs_lane_idx < nic_props->max_num_of_lanes; abs_lane_idx++) {
		get_polarity(hdev, abs_lane_idx, &pol_tx, &pol_rx);

		val = le64_to_cpu(nic_info->pol_tx_mask[0]);
		val &= ~BIT_ULL(abs_lane_idx);
		val |= ((u64) pol_tx) << abs_lane_idx;
		nic_info->pol_tx_mask[0] = cpu_to_le64(val);

		val = le64_to_cpu(nic_info->pol_rx_mask[0]);
		val &= ~BIT_ULL(abs_lane_idx);
		val |= ((u64) pol_rx) << abs_lane_idx;
		nic_info->pol_rx_mask[0] = cpu_to_le64(val);
	}
}

static void _phy_write_all(struct hl_device *hdev, u32 addr, u32 data)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	int lane, port;

	for (port = 0 ; port < nic_props->max_num_of_ports ; port += 2) {
		for (lane = 0 ; lane < 4 ; lane++) {
			NIC_MACRO_WREG32(NIC0_PHY_BASE + 0xF60 + lane * 4, addr);

			/* only the lower 16 bits are in use */
			NIC_MACRO_WREG32(NIC0_PHY_BASE - 0x8000 + 0x2000 * lane, data & 0xFFFF);
		}
	}
}

static u32 _phy_read(struct hl_device *hdev, int port, int lane, u32 addr)
{
	NIC_MACRO_WREG32(NIC0_PHY_BASE + 0xF60 + lane * 4, addr);

	/* only the lower 16 bits are in use */
	return NIC_MACRO_RREG32(NIC0_PHY_BASE - 0x8000 + 0x2000 * lane) & 0xFFFF;
}

static void _phy_write(struct hl_device *hdev, int port, int lane, u32 addr, u32 data)
{
	NIC_MACRO_WREG32(NIC0_PHY_BASE + 0xF60 + lane * 4, addr);

	/* only the lower 16 bits are in use */
	NIC_MACRO_WREG32(NIC0_PHY_BASE - 0x8000 + 0x2000 * lane, data & 0xFFFF);
}

static void _phy_write_mask(struct hl_device *hdev, int port, int lane, u32 addr, u32 raw_data,
				u32 mask)
{
	u32 data;

	NIC_MACRO_WREG32(NIC0_PHY_BASE + 0xF60 + lane * 4, addr);

	data = (NIC_MACRO_RREG32(NIC0_PHY_BASE - 0x8000 + 0x2000 * lane)) &
									0xFFFF;
	data = (data & ~mask) | (((raw_data << (__ffs(mask) % 32))) & 0xFFFF);

	NIC_MACRO_WREG32(NIC0_PHY_BASE - 0x8000 + 0x2000 * lane, data);
}

static u32 twos_to_int(s32 twos_val, u32 bit_width)
{
	return (u32)((s32)(twos_val) - ((s32)((twos_val << 1) & (1 << bit_width))));
}

static int fw_cmd_port(struct hl_device *hdev, int port, int lane, u32 cmd, u32 detail,
			u32 expected_res, u32 *res_ptr)
{
	u32 res, val;
	int checks;

	if (detail)
		PHY_WRITE(lane, 0x9816, detail);

	PHY_WRITE(lane, 0x9815, cmd);

	checks = 0;
	do {
		usleep_range(1000, 2000);
		res = PHY_READ(lane, 0x9815);
		if (checks++ > PHY_READ_COUNTS_PER_MS) {
			dev_err(hdev->dev, "timeout for PHY cmd 0x%x\n", cmd);
			return -ETIMEDOUT;
		}
	} while (res == cmd);

	val = (res >> 8) & 0xF;
	if (val != expected_res) {
		dev_err(hdev->dev, "cmd 0x%x returned error 0x%x\n", cmd, val);
		return -EFAULT;
	}

	*res_ptr = res;

	return 0;
}

static int fw_cmd(struct hl_nic_port *nic_port, int lane, u32 cmd, u32 detail, u32 expected_res,
			u32 *res_ptr)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	u32 res, val;
	int checks;

	if (detail)
		PHY_WRITE(lane, 0x9816, detail);

	PHY_WRITE(lane, 0x9815, cmd);

	checks = 0;
	do {
		usleep_range(1000, 2000);
		res = PHY_READ(lane, 0x9815);
		if (checks++ > PHY_READ_COUNTS_PER_MS) {
			dev_dbg(hdev->dev,
				"timeout for PHY cmd 0x%x port %d lane %d\n",
				cmd, port, lane);
			return -ETIMEDOUT;
		}
	} while (res == cmd);

	val = (res >> 8) & 0xF;
	if (val != expected_res) {
		dev_dbg(hdev->dev,
			"cmd 0x%x returned error 0x%x port %d lane %d\n", cmd, val, port, lane);
		return -EFAULT;
	}

	*res_ptr = res;

	return 0;
}

static int hash_port(struct hl_device *hdev, int port, int lane, u32 *hash)
{
	u32 res, low_word;
	int rc;

	rc = fw_cmd_port(hdev, port, lane, 0xF000, 0, 0xF, &res);
	if (rc) {
		dev_err(hdev->dev, "F/W hash failed for port %d lane %d\n",
			port, lane);
		return rc;
	}

	low_word = PHY_READ(lane, 0x9816);

	*hash = ((res & 0xFF) << 16) | low_word;

	return 0;
}

static int crc_port(struct hl_device *hdev, int port, int lane, u16 *crc)
{
	u32 res;
	int rc;

	rc = fw_cmd_port(hdev, port, lane, 0xF001, 0, 0xF, &res);
	if (rc) {
		dev_err(hdev->dev, "F/W crc failed for port %d lane %d\n", port,
			lane);
		return rc;
	}

	*crc = PHY_READ(lane, 0x9816) & 0xFFFF;

	return 0;
}

static void set_pll(struct hl_nic_port *nic_port, int lane, u32 data_rate, bool pam4)
{
	u32 port = nic_port->port, pll_n_val = 0, pll_cap_val = 0;
	struct hl_device *hdev = nic_port->hdev;
	bool div4 = true; /* for easy debug in the future */

	PHY_WRITE_MASK(lane, 0xFF, 1, 1 << 5);

	if (!pam4)
		PHY_WRITE_MASK(lane, 0x179, data_rate == NIC_DR_10, 1);

	if (data_rate == NIC_DR_50) {
		if (div4)
			pll_n_val = 170;
		else
			pll_n_val = 42;

		pll_cap_val = 10;
	} else if (data_rate == NIC_DR_25) {
		if (div4)
			pll_n_val = 165;
		else
			pll_n_val = 41;

		pll_cap_val = 12;
	} else if (data_rate == NIC_DR_10) {
		if (div4)
			pll_n_val = 132;
		else
			pll_n_val = 33;

		pll_cap_val = 34;
	}

	PHY_WRITE_MASK(lane, 0xFD, pll_n_val, 0xFF80);
	PHY_WRITE_MASK(lane, 0xFC, pll_cap_val, 0xFC00);
}

static void set_tx_taps(struct hl_nic_port *nic_port, int lane, s32 tx_pre2, s32 tx_pre1,
			s32 tx_main, s32 tx_post1, s32 tx_post2)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	PHY_WRITE_MASK(lane, 0xAD, twos_to_int(tx_pre2, 8), 0xFF00);
	PHY_WRITE_MASK(lane, 0xAB, twos_to_int(tx_pre1, 8), 0xFF00);
	PHY_WRITE_MASK(lane, 0xA9, twos_to_int(tx_main, 8), 0xFF00);
	PHY_WRITE_MASK(lane, 0xA7, twos_to_int(tx_post1, 8), 0xFF00);
	PHY_WRITE_MASK(lane, 0xA5, twos_to_int(tx_post2, 8), 0xFF00);
}

static void config_nrz_tx(struct hl_nic_port *nic_port, int lane, bool half_rate)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 lane_idx, port = nic_port->port;
	struct hl_nic *nic = &hdev->nic;
	s32 *taps;

	lane_idx = (port >> 1) * NIC_MAC_LANES + lane;
	taps = nic->phy_tx_taps[lane_idx].nrz_taps;

	PHY_WRITE(lane, 0xAF, 0xF83E);
	PHY_WRITE(lane, 0xB0, 0x4802);
	PHY_WRITE_MASK(lane, 0xB0, half_rate ? 1 : 0, 1);
	PHY_WRITE_MASK(lane, 0xB0, 0, 0x800);
	PHY_WRITE_MASK(lane, 0xB0, 1, 0x800);
	PHY_WRITE(lane, 0xA0, 0xE300);
	set_tx_taps(nic_port, lane, taps[0], taps[1], taps[2], taps[3], taps[4]);
}

static void config_pam4_tx(struct hl_nic_port *nic_port, int lane)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 lane_idx, port = nic_port->port;
	struct hl_nic *nic = &hdev->nic;
	s32 *taps;

	lane_idx = (port >> 1) * NIC_MAC_LANES + lane;
	taps = nic->phy_tx_taps[lane_idx].pam4_taps;

	PHY_WRITE(lane, 0xAF, 0xF83E);
	PHY_WRITE(lane, 0xB0, 0);
	PHY_WRITE(lane, 0xB0, 0x800);
	PHY_WRITE(lane, 0xB0, 0);
	PHY_WRITE(lane, 0xA0, 0xEF00);
	set_tx_taps(nic_port, lane, taps[0], taps[1], taps[2], taps[3], taps[4]);
}

static void set_pol(struct hl_nic_port *nic_port, int lane, bool pam4, u32 tx_pol, u32 rx_pol)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	PHY_WRITE_MASK(lane, 0xA0, tx_pol, 0x20);
	PHY_WRITE_MASK(lane, 0x161, rx_pol, 0x4000); /* nrz */
	PHY_WRITE_MASK(lane, 0x43, rx_pol, 0x80); /* pam4 */
}

static void set_msblsb(struct hl_nic_port *nic_port, int lane, u32 tx_msblsb, u32 rx_msblsb)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	PHY_WRITE_MASK(lane, 0xAF, tx_msblsb, 0x400);
	PHY_WRITE_MASK(lane, 0x43, rx_msblsb, 0x8000);
}

static void set_gc(struct hl_nic_port *nic_port, int lane, u32 tx_gc, u32 rx_gc)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	PHY_WRITE_MASK(lane, 0xAF, tx_gc, 0x200);
	PHY_WRITE_MASK(lane, 0x42, rx_gc, 1);
}

static void set_pc(struct hl_nic_port *nic_port, int lane, u32 tx_pc, u32 rx_pc)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	PHY_WRITE_MASK(lane, 0xAF, tx_pc, 0x100);
	PHY_WRITE_MASK(lane, 0x42, rx_pc, 2);
}

static void set_prbs_type(struct hl_nic_port *nic_port, int lane, bool pam4, char *pat)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	u32 prbs_mode_sel_addr;
	u32 prbs_mode_sel_mask;
	u32 pat_sel = 0;

	if (pam4) {
		prbs_mode_sel_addr = 0x43;
		prbs_mode_sel_mask = 0x60;
	} else {
		prbs_mode_sel_addr = 0x161;
		prbs_mode_sel_mask = 0x3000;
	}

	if (pam4) {
		if (!strncmp(pat, "PRBS9", strlen(pat)))
			pat_sel = 0;
		else if (!strncmp(pat, "PRBS13", strlen(pat)))
			pat_sel = 1;
		else if (!strncmp(pat, "PRBS15", strlen(pat)))
			pat_sel = 2;
		else if (!strncmp(pat, "PRBS31", strlen(pat)))
			pat_sel = 3;
	} else {
		if (!strncmp(pat, "PRBS9", strlen(pat)))
			pat_sel = 0;
		else if (!strncmp(pat, "PRBS15", strlen(pat)))
			pat_sel = 1;
		else if (!strncmp(pat, "PRBS23", strlen(pat)))
			pat_sel = 2;
		else if (!strncmp(pat, "PRBS31", strlen(pat)))
			pat_sel = 3;
	}

	PHY_WRITE_MASK(lane, 0xA0, pat_sel, 0x300);
	PHY_WRITE_MASK(lane, prbs_mode_sel_addr, pat_sel, prbs_mode_sel_mask);
}

static void config_qp(struct hl_nic_port *nic_port, int lane, bool pam4, bool do_auto_neg,
			u32 _msblsb_tx)
{
	struct hl_device *hdev = nic_port->hdev;
	struct cpucp_nic_info *nic_info;
	u32 port = nic_port->port;
	u32 msblsb_tx = _msblsb_tx;
	char *prbs = "PRBS31";
	u32 msblsb_rx = 0;
	u32 pol_tx = 0;
	u32 pol_rx = 0;
	u32 gc_tx = 1;
	u32 gc_rx = 1;
	u32 pc_tx = 0;
	u32 pc_rx = 0;
	u32 lane_idx;

	lane_idx = (port >> 1) * NIC_MAC_LANES + lane;
	nic_info = &hdev->asic_prop.cpucp_nic_info;

	if (!pam4) {
		gc_tx = 0;
		gc_rx = 0;
	}

	pol_tx = (le64_to_cpu(nic_info->pol_tx_mask[0]) >> lane_idx) & 1;
	pol_rx = (le64_to_cpu(nic_info->pol_rx_mask[0]) >> lane_idx) & 1;

	/* input coupling mode - DC */
	PHY_WRITE_MASK(lane, 0xF7, 0, 0x1000);
	set_pol(nic_port, lane, pam4, pol_tx, pol_rx);
	set_msblsb(nic_port, lane, msblsb_tx, msblsb_rx);
	set_gc(nic_port, lane, gc_tx, gc_rx);
	set_pc(nic_port, lane, pc_tx, pc_rx);

	set_prbs_type(nic_port, lane, pam4, prbs);
}

static void set_functional_mode(struct hl_nic_port *nic_port, int lane, bool pam4)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	if (!pam4) {
		PHY_WRITE_MASK(lane, 0xA0, 0, 0x2000);
		PHY_WRITE_MASK(lane, 0x161, 0, 0x400);
	} else {
		PHY_WRITE_MASK(lane, 0xA0, 0, 0x2000);
		PHY_WRITE_MASK(lane, 0x43, 0, 0x10);
	}
}

static void config_pam4_fw_rx(struct hl_nic_port *nic_port, int lane)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	PHY_WRITE_MASK(lane, 0x980F, 0x1, 0x1000);
	PHY_WRITE_MASK(lane, 0x980F, 0x1, 0x0400);
	PHY_WRITE_MASK(lane, 0x980F, 0x1, 0x0800);
	PHY_WRITE_MASK(lane, 0x980F, 0x1, 0x0200);

	PHY_WRITE(lane, 0x43, 0x8CFA);
	PHY_WRITE(lane, 0x44, 0x1035);
	PHY_WRITE(lane, 0x45, 0x1008);
}

static int config_speed_nrz(struct hl_nic_port *nic_port, int lane, u32 data_rate, u32 speed,
				bool half_rate, bool fmode, bool pam4)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, ignore;
	int rc, i;

	/* clear go bit */
	PHY_WRITE_MASK(lane, 0x980F, 0, 0x8000);

	rc = fw_cmd(nic_port, lane, 0x80C0, speed, 0x8, &ignore);
	if (rc) {
		dev_err(hdev->dev,
			"F/W cmd failed for speed nrz configuration of lane %d\n",
			lane);
		return rc;
	}

	config_nrz_tx(nic_port, lane, half_rate);
	PHY_WRITE_MASK(lane, 0x0161, 0x1D, 0xFC00);
	config_qp(nic_port, lane, pam4, false, 0);
	set_functional_mode(nic_port, lane, pam4);

	/* clock configuration */
	for (i = 0 ; i < 4 ; i++)
		if (i == 0)
			PHY_WRITE(i, 0x00C9, 0x390);
		else
			PHY_WRITE(i, 0x00C9, 0x310);

	set_pll(nic_port, lane, data_rate, pam4);
	PHY_WRITE_MASK(lane, 0x980F, 1, 0x8000);

	return 0;
}

static int config_speed_pam4(struct hl_nic_port *nic_port, int lane, u32 data_rate, u32 speed,
				bool fmode, bool pam4)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, ignore;
	int rc;

	dev_dbg(hdev->dev,
		"port: %d, lane: %d, data rate: %d, pam4: %d, speed: %d\n",
		nic_port->port, lane, data_rate, pam4, speed);

	/* clear go bit */
	PHY_WRITE_MASK(lane, 0x980F, 0, 0x8000);

	PHY_WRITE_MASK(lane, 0x8440, 0, 0x8000);

	rc = fw_cmd(nic_port, lane, 0x80D0, speed, 0x8, &ignore);
	if (rc) {
		dev_err(hdev->dev,
			"F/W cmd failed for speed pam4 configuration of lane %d\n", lane);
		return rc;
	}

	config_pam4_tx(nic_port, lane);
	config_pam4_fw_rx(nic_port, lane);
	config_qp(nic_port, lane, pam4, false, 0);
	set_functional_mode(nic_port, lane, pam4);

	/* set go bit */
	PHY_WRITE_MASK(lane, 0x980F, 1, 0x8000);

	return 0;
}

static int fw_config(struct hl_nic_port *nic_port, int lane, u32 data_rate,
				bool fmode, bool pam4)
{
	struct hl_device *hdev = nic_port->hdev;

	set_pll(nic_port, lane, data_rate, pam4);

	if (data_rate == NIC_DR_10)
		return config_speed_nrz(nic_port, lane, data_rate, 1, 1, fmode, pam4);
	else if (data_rate == NIC_DR_25 || data_rate == NIC_DR_26)
		return config_speed_nrz(nic_port, lane, data_rate, 3, 0, fmode, pam4);
	else if (data_rate == NIC_DR_50)
		return config_speed_pam4(nic_port, lane, data_rate, 9, fmode, pam4);

	dev_err(hdev->dev, "invalid data_rate %d\n", data_rate);

	return -EFAULT;
}

static int config_auto_neg(struct hl_nic_port *nic_port, int lane)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, ignore, pflags;
	u64 basepage = 0x800000001ull;
	int rc;

	pflags = hl_nic_get_pflags(nic_port);

	usleep_range(500, 1000);

	/* clear go bit */
	PHY_WRITE_MASK(lane, 0x980F, 0, 0x8000);

	set_pll(nic_port, lane, NIC_DR_25, false);

	/* Disable AN/LT lane swapping */
	PHY_WRITE_MASK(lane, 0x8440, 0, 0x8000);
	config_nrz_tx(nic_port, lane, 0);

	/* config_nrz_fw_rx */
	PHY_WRITE_MASK(lane, 0x0161, 0x1D, 0x0);
	config_qp(nic_port, lane, false, true, 1);

	PHY_WRITE_MASK(lane, 0x8300, 7, 0xE000);

	/* AN mode */
	PHY_WRITE(lane, 0x8010, basepage & 0xffff);
	PHY_WRITE(lane, 0x8011, (basepage >> 16) & 0xffff);
	PHY_WRITE(lane, 0x8012, (basepage >> 32) & 0xffff);

	/* IEEE */
	PHY_WRITE_MASK(lane, 0x8300, 1, 0x1000);

	if (pflags & PFLAGS_PHY_AUTO_NEG_LPBK)
		PHY_WRITE_MASK(lane, 0x8300, 1, 0x400);

	/* set FW to start AN */
	rc = fw_cmd(nic_port, lane, 0x8000, 0, 0x8, &ignore);
	if (rc) {
		dev_err(hdev->dev,
			"F/W cmd 0x8000 failed for auto neg, port %d, lane %d\n", port, lane);
		return rc;
	}

	/* set go bit */
	PHY_WRITE_MASK(lane, 0x980F, 1, 0x8000);

	return 0;
}

static int lane_power_up(struct hl_nic_port *nic_port, int lane, bool do_auto_neg)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, data_rate = nic_port->data_rate;
	bool pam4, fmode = 0;
	int rc;

	pam4 = (data_rate == NIC_DR_50);

	dev_dbg(hdev->dev, "PHY power up port %d lane %d auto_neg %d\n", port, lane, do_auto_neg);

	/* F/W configurations */
	if (nic_port->auto_neg_enable) {
		if (do_auto_neg) {
			rc = config_auto_neg(nic_port, lane);
			if (rc) {
				dev_err(hdev->dev,
					"port %d lane %d: PHY F/W Autoneg configuration failed\n",
					port, lane);
				return rc;
			}
		}
	} else {
		rc = fw_config(nic_port, lane, data_rate, fmode, pam4);
		if (rc) {
			dev_err(hdev->dev,
				"port %d lane %d: F/W configuration failed\n", port, lane);
			return rc;
		}
	}

	return 0;
}

static enum link_status update_pcs_link_failure(struct hl_nic_port *nic_port)
{
	struct kfifo *pcs_fifo = &nic_port->pcs_fail_fifo;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic *nic = &hdev->nic;
	u32 port = nic_port->port;
	ktime_t now, before;
	int count;

	if (!nic_port->auto_neg_enable)
		return LINK_STS_PCS_DOWN;

	now = ktime_get();

	count = kfifo_in(pcs_fifo, &now, sizeof(now));
	if (count != sizeof(now)) {
		dev_err(hdev->dev, "Failed to push to PCS fifo, size: %d, count: %d, port: %d\n",
			nic_port->pcs_fail_cnt, count, port);
		return LINK_STS_PCS_DOWN;
	}

	nic_port->pcs_fail_cnt++;

	if (nic_port->pcs_fail_cnt < nic->pcs_fail_threshold)
		return LINK_STS_PCS_DOWN;

	/* Here we reached the threshold count of failures to reconfigure the
	 * link. Now need to check if all of the failure are in the needed time
	 * frame. It is sufficient to check the first item in the queue as it is
	 * the earliest failure and if it is in the needed time frame, all the
	 * rest if failures are in it too.
	 */
	count = kfifo_out_peek(pcs_fifo, &before, sizeof(before));
	if (count != sizeof(before))
		dev_err(hdev->dev, "Failed to peek in PCS fifo, size: %d, count: %d, port: %d\n",
			nic_port->pcs_fail_cnt, count, port);

	if (ktime_ms_delta(now, before) <= (nic->pcs_fail_time_frame * MSEC_PER_SEC)) {
		dev_dbg(hdev->dev, "PHY reconfig due to PCS link failure cnt, port: %d\n", port);
		return LINK_STS_FAIL_RECONFIG;
	}

	/* The earliest failure is not in the needed time frame, hence
	 * we can remove it.
	 */
	count = kfifo_out(pcs_fifo, &before, sizeof(before));
	if (count != sizeof(before))
		dev_err(hdev->dev, "Failed to pop from PCS fifo, size: %d, count: %d, port: %d\n",
			nic_port->pcs_fail_cnt, count, port);

	nic_port->pcs_fail_cnt--;

	return LINK_STS_PCS_DOWN;
}

static u32 get_fault_counters(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	struct asic_fixed_properties *prop;
	u32 port = nic_port->port;
	struct cpucp_packet pkt;
	bool fw_nic_mac_en;
	u64 result = 0;
	int rc;

	prop = &hdev->asic_prop;

	fw_nic_mac_en = !!(prop->fw_app_cpu_boot_dev_sts0 & CPU_BOOT_DEV_STS0_FW_NIC_MAC_EN);

	if (!fw_nic_mac_en)
		/* need to check the first lane only */
		return gaudi_nic_mac_read(nic_port, __ffs(nic_port->fw_tuning_mask), "mac", 0x40);

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_FAULT_GET << CPUCP_PKT_CTL_OPCODE_SHIFT);
	/* we used this field as port_index didn't exist yet */
	pkt.index = cpu_to_le32(port);
	/* send the mask rather than the lane index for possible future use */
	pkt.value = cpu_to_le64(nic_port->fw_tuning_mask);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, &result);

	if (rc) {
		dev_err(hdev->dev, "Failed to get remote fault cnt for port %d, error %d\n",
			port, rc);
		return 0;
	}

	/* the counters value is 32-bit wide */
	return (u32) result;
}

static int check_link_status(struct hl_nic_port *nic_port, int lane)
{
	bool phy_ready, pam4 = nic_port->data_rate == NIC_DR_50;
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
#if HL_PHY_DEBUG
	bool signal_detect;
#endif
	u32 phy_status;

	if (pam4) {
		phy_status = PHY_READ(lane, 0x6A);
		phy_ready = ((phy_status & 0x8000) >> 15) & 1;
#if HL_PHY_DEBUG
		signal_detect = ((phy_status & 0x80) >> 7) & 1;
#endif
	} else {
		phy_status = PHY_READ(lane, 0x12E);
		phy_ready = ((phy_status & 0x4) >> 2) & 1;
#if HL_PHY_DEBUG
		signal_detect = ((phy_status & 0x8) >> 3) & 1;
#endif
	}

#if HL_PHY_DEBUG
	{
		struct hl_device *hdev = nic_port->hdev;

		dev_dbg_ratelimited(hdev->dev,
					"port %d, lane %d, phy ready: %d, signal detect: %d\n",
					port, lane, phy_ready, signal_detect);
	}
#endif

	return phy_ready ? 0 : -EFAULT;
}

static enum link_status _check_pcs_link(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, mac_val;
	int i, rc;

	mac_val = get_fault_counters(nic_port);

	if (mac_val & 1)
		nic_port->pcs_local_fault_cnt++;

	if (mac_val & 2) {
		nic_port->pcs_remote_fault_cnt++;
		nic_port->pcs_remote_fault_seq_cnt++;
	} else {
		nic_port->pcs_remote_fault_seq_cnt = 0;
	}

	if (nic_port->pcs_remote_fault_seq_cnt == PCS_FAULT_THRESHOLD) {
		dev_dbg(hdev->dev, "PHY reconfig due to PCS remote fault cnt, port: %d\n", port);
		nic_port->pcs_remote_fault_reconfig_cnt++;
		return LINK_STS_FAULT_RECONFIG;
	}

	if (has_pcs_link(nic_port))
		return LINK_STS_UP;

	/* check if the PCS is down because the PHY isn't ready */
	for (i = 0 ; i < NIC_MAC_LANES ; i++) {
		if (!(nic_port->fw_tuning_mask & BIT(i)))
			continue;

		rc = check_link_status(nic_port, i);
		if (rc)
			return LINK_STS_PHY_DOWN;
	}

	/* the PHY is ready, only the PCS is down */
	return LINK_STS_PCS_DOWN;
}

static void check_pcs_link(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	enum link_status link_status;
	u32 port = nic_port->port, pflags;

	pflags = hl_nic_get_pflags(nic_port);

	if (!(pflags & PFLAGS_PCS_LINK_CHECK))
		return;

	link_status = _check_pcs_link(nic_port);
	if (link_status == LINK_STS_PCS_DOWN || link_status == LINK_STS_PHY_DOWN) {
		/* Try again to overcome a momentary glitch */
		msleep(PCS_LINK_RETRY_MSEC);

		link_status = _check_pcs_link(nic_port);

		if (link_status == LINK_STS_UP) {
			nic_port->pcs_link_restore_cnt++;
			dev_dbg(hdev->dev, "PCS link restore, port %d\n", port);
		}
	}

	if (link_status == LINK_STS_UP)
		return;

	hl_nic_phy_set_port_status(nic_port, false);
	nic_port->pcs_link = false;
	nic_port->last_pcs_link_drop_ts = ktime_get();

	dev_dbg(hdev->dev, "%s lost signal, port %d\n",
		link_status == LINK_STS_PHY_DOWN ? "PHY" : "PCS", port);

	/* No point in updating about the PCS failure if a PHY reconfiguration
	 * is needed.
	 */
	if (link_status != LINK_STS_FAULT_RECONFIG)
		link_status = update_pcs_link_failure(nic_port);

	if (link_status == LINK_STS_FAULT_RECONFIG || link_status == LINK_STS_FAIL_RECONFIG)
		hl_nic_phy_port_reconfig(nic_port);
}

static void acquire_pcs_link(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	nic_port->pcs_link = has_pcs_link(nic_port);
	nic_port->retry_cnt++;

	if (nic_port->pcs_link) {
		hl_nic_phy_set_port_status(nic_port, true);
		nic_port->retry_cnt = 0;
	} else if (nic_port->retry_cnt == PCS_LINK_CNT) {
		if (ktime_after(nic_port->last_fw_tuning_ts,
				nic_port->last_pcs_link_drop_ts))
			dev_dbg(hdev->dev,
				"PHY_reconfig due to PCS link down after F/W tuning, port %d\n",
				port);
		else
			dev_dbg(hdev->dev,
				"PHY reconfig due to PCS link cnt, port %d\n",
				port);

		hl_nic_phy_port_reconfig(nic_port);
	}
}

static void print_eye(struct hl_nic_port *nic_port, int lane, bool pam4)
{
	u32 port = nic_port->port, dac, eye, mask, val1, val2;
	s32 plus_margin, minus_margin, result, diff;
	struct hl_device *hdev = nic_port->hdev;
	int pam4_eye[3], eye_index, i, sel;

	if (pam4) {
		dac = (PHY_READ(lane, 0x28) & 0x1E0) >> 5;
		for (eye_index = 0; eye_index < 3; eye_index++) {
			result = 0xffff;
			for (i = 0; i < 3; i++) {
				sel = 3 * i + eye_index;
				PHY_WRITE_MASK(lane, 0x88, sel, 0xF00);
				PHY_WRITE_MASK(lane, 0x88, sel, 0xF000);

				msleep(100);

				val1 = PHY_READ(lane, 0x32);
				plus_margin = (val1 & 0xFFF0) >> 4;
				if (plus_margin > 0x7ff)
					plus_margin = plus_margin - 0x1000;

				val1 = PHY_READ(lane, 0x32);
				val2 = PHY_READ(lane, 0x33);
				minus_margin = ((val1 & 0xF) << 8) + ((val2 & 0xFF00) >> 8);
				if (minus_margin > 0x7ff)
					minus_margin = minus_margin - 0x1000;

				diff = plus_margin - minus_margin;
				if (diff < result)
					result = diff;
			}

			pam4_eye[eye_index] = (result * (100 + (50 * dac))) / 2048;
		}

		dev_dbg(hdev->dev, "port %d lane %d, PAM4 dac: %d eye0: %d eye1: %d eye2: %d\n",
			port, lane, dac, pam4_eye[0], pam4_eye[1], pam4_eye[2]);
	} else {
		mask = 0xF000;
		dac = (PHY_READ(lane, 0x17F) & mask) >> __ffs(mask);
		mask = 0xFFF;
		eye = (PHY_READ(lane, 0x12A) & mask) >> __ffs(mask);

		dev_dbg(hdev->dev, "dac: %d, eye: %d\n", dac, eye);

		if (eye)
			dev_dbg(hdev->dev, "port %d lane %d: F/W eye is %d\n", port, lane,
				(eye * (200 + 50 * dac)) / 2048);
		else
			dev_err(hdev->dev, "port %d lane %d: F/W got no eye\n", port, lane);
	}
}

static int _do_fw_tuning(struct hl_nic_port *nic_port, int lane,
			       bool check_status)
{
	bool pam4 = (nic_port->data_rate == NIC_DR_50);
	struct hl_device *hdev = nic_port->hdev;
	u32 status, port = nic_port->port;

	status = PHY_READ(lane, 0x9811);

	if (status & PHY_FW_FINISHED) {
		if (status & PHY_FW_ERROR) {
			dev_dbg(hdev->dev, "port %d lane %d F/W tuning failed\n", port, lane);
			return -EFAULT;
		}
#if HL_PHY_DEBUG
		dev_dbg(hdev->dev, "port %d lane %d F/W Tuning is done\n", port, lane);
#endif
	} else {
		return -EAGAIN;
	}

	if (!nic_port->auto_neg_enable) {
		PHY_WRITE_MASK(lane, 0x14D, 1, 1 << 15);
		print_eye(nic_port, lane, pam4);
	} else if (!check_status) {
		return 0;
	}

	return check_link_status(nic_port, lane);
}

static void do_fw_tuning(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	int i, rc = 0;

	for (i = 0 ; i < NIC_MAC_LANES ; i++) {
		if (!(nic_port->fw_tuning_mask & BIT(i)))
			continue;

		rc = _do_fw_tuning(nic_port, i, true);
		if (rc) {
			if (rc == -EAGAIN) {
				if (ktime_after(ktime_get(), nic_port->fw_tuning_limit_ts)) {
					dev_dbg(hdev->dev,
						"PHY F/W tuning limit, port %d, lane %d\n",
						port, i);
					hl_nic_phy_port_reconfig(nic_port);
				}
			} else {
				dev_dbg(hdev->dev,
					"PHY F/W tuning failed for port %d, lane %d, rc %d\n",
					port, i, rc);
				hl_nic_phy_port_reconfig(nic_port);
			}
			break;
		}
	}

	if (!rc) {
		nic_port->phy_fw_tuned = true;
		nic_port->retry_cnt = 0;
		nic_port->last_fw_tuning_ts = ktime_get();
	}
}

static int config_pam4_link_training(struct hl_nic_port *nic_port,
					       int lane)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	u32 ignore, speed = 9;
	int rc;

#if HL_PHY_DEBUG
	dev_dbg(hdev->dev, "port %d lane: %d, speed: %d\n", port, lane, speed);
#endif

	/* clear go bit */
	PHY_WRITE_MASK(lane, 0x980F, 0, 0x8000);

	/* Disable lane swapping */
	PHY_WRITE_MASK(lane, 0x8440, 0, 0x8000);

	/* Enable Link Training */
	speed |= 0x100;

	config_pam4_tx(nic_port, lane);
	PHY_WRITE_MASK(lane, 0xA0, 0, 0x2000);
	config_pam4_fw_rx(nic_port, lane);
	config_qp(nic_port, lane, true, false, 1);

	rc = fw_cmd(nic_port, lane, 0x80D0, speed, 0x8, &ignore);
	if (rc) {
		dev_err(hdev->dev,
			"F/W cmd failed for speed pam4 configuration of port %d lane %d\n",
			port, lane);
		return rc;
	}

	PHY_WRITE_MASK(lane, 0xAF, 0, 0x200);
	PHY_WRITE_MASK(lane, 0xAF, 0, 0x100);
	PHY_WRITE_MASK(lane, 0x42, 0, 0x2);
	PHY_WRITE_MASK(lane, 0x42, 0, 0x1);

	/* set go bit */
	PHY_WRITE_MASK(lane, 0x980F, 1, 0x8000);

	return 0;
}

static int do_anlt(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	int i, rc;

	for (i = 0 ; i < NIC_MAC_LANES ; i++) {
		if (!(nic_port->auto_neg_mask & BIT(i)))
			continue;

		/* only the first lane should do AN so squelch the other lane */
		PHY_WRITE(i + 1, 0xA1, 0);
		PHY_WRITE(i + 1, 0xA2, 0);
		PHY_WRITE(i + 1, 0xA3, 0);
		PHY_WRITE(i + 1, 0xA4, 0);
		PHY_WRITE_MASK(i + 1, 0xA0, 2, 0xF000);

		rc = _do_fw_tuning(nic_port, i, false);
		if (rc) {
			if (rc == -EAGAIN)
				dev_dbg_ratelimited(hdev->dev,
						    "PHY auto neg fw is not ready, port %d, lane %d\n",
						    port, i);
			else
				dev_dbg(hdev->dev,
					"PHY auto neg failed, port %d, lane %d, rc %d\n",
					port, i, rc);
			return rc;
		}
	}

	for (i = 0 ; i < NIC_MAC_LANES ; i++) {
		if (!(nic_port->fw_tuning_mask & BIT(i)))
			continue;

		rc = config_pam4_link_training(nic_port, i);
		if (rc) {
			dev_dbg(hdev->dev,
				"PHY link training failed, port %d, lane %d, rc %d\n",
				port, i, rc);
			hl_nic_phy_port_reconfig(nic_port);

			return rc;
		}
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

	nic_port->fw_tuning_limit_ts = ktime_add_ms(ktime_get(), FW_TUNING_TIMEOUT_MSEC);
	do_fw_tuning(nic_port);
}

void gaudi_nic_phy_link_status_work(struct work_struct *work)
{
	struct hl_nic_port *nic_port;
	u32 timeout_ms;

	nic_port = container_of(work, struct hl_nic_port, link_status_work.work);

	if (nic_port->phy_fw_tuned) {
		if (nic_port->pcs_link)
			check_pcs_link(nic_port);
		else
			acquire_pcs_link(nic_port);
	} else {
		if (nic_port->auto_neg_resolved || nic_port->auto_neg_skipped)
			do_fw_tuning(nic_port);
		else
			do_fw_tuning_auto_neg(nic_port);
	}

	if (nic_port->pcs_link)
		timeout_ms = 1000;
	else if (nic_port->phy_fw_tuned)
		timeout_ms = 500;
	else
		timeout_ms = 1;

	queue_delayed_work(nic_port->wq, &nic_port->link_status_work,
			   msecs_to_jiffies(timeout_ms));
}

void gaudi_nic_phy_port_start_stop(struct hl_nic_port *nic_port, bool is_start)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	int i;

	for (i = 0 ; i < NIC_MAC_LANES ; i++) {
		if (!(nic_port->power_up_mask & BIT(i)))
			continue;

		if (is_start) {
			/* Enable TX driver in SerDes */
			PHY_WRITE_MASK(i, 0xE3, 1, 0x2000);
			/* Enable F/W Rx tuning is done during power up flow */
		} else {
			/* Disable TX driver in SerDes */
			PHY_WRITE_MASK(i, 0xE3, 0, 0x2000);
			/* Silence F/W Rx tuning */
			PHY_WRITE(i, 0x9815, 0x9000);
		}
	}
}

int gaudi_nic_phy_port_power_up(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port, pcs_fifo_size;
	bool do_auto_neg;
	int rc, i;

	port = nic_port->port;

	pcs_fifo_size = hdev->nic.pcs_fail_threshold * sizeof(ktime_t);
	if (!is_power_of_2(pcs_fifo_size)) {
		dev_err(hdev->dev, "PCS fifo size must be a power of 2, port: %d\n", port);
		return -EFAULT;
	}

	rc = kfifo_alloc(&nic_port->pcs_fail_fifo, pcs_fifo_size, GFP_KERNEL);
	if (rc) {
		dev_err(hdev->dev, "PCS fifo alloc failed, port: %d\n", port);
		return rc;
	}

	for (i = 0 ; i < NIC_MAC_LANES ; i++) {
		if (!(nic_port->power_up_mask & BIT(i)))
			continue;

		do_auto_neg = nic_port->auto_neg_enable && (nic_port->auto_neg_mask & BIT(i));

		rc = lane_power_up(nic_port, i, do_auto_neg);
		if (rc) {
			dev_err(hdev->dev, "PHY power up failed for port %d\n", port);
			goto power_up_fail;
		}
	}

	return 0;

power_up_fail:
	kfifo_free(&nic_port->pcs_fail_fifo);

	return rc;
}

void gaudi_nic_phy_port_reconfig(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	int i, rc;

	if (!hdev->nic.phy_config_fw)
		return;

	dev_dbg(hdev->dev, "reconfiguring PHY, port %d\n", port);

	if (nic_port->auto_neg_enable) {
		for (i = 0 ; i < NIC_MAC_LANES ; i++) {
			if (!(nic_port->auto_neg_mask & BIT(i)))
				continue;

			rc = config_auto_neg(nic_port, i);
			if (rc)
				dev_dbg(hdev->dev,
					"F/W reconfig autoneg failed, port: %d, lane: %d\n",
					port, i);
		}
	} else {
		for (i = 0 ; i < NIC_MAC_LANES ; i++) {
			if (!(nic_port->power_up_mask & BIT(i)))
				continue;

			rc = lane_power_up(nic_port, i, false);
			if (rc) {
				dev_err(hdev->dev,
					"PHY reconfig power up failed for port %d lane %d\n",
					port, i);
				break;
			}
		}
	}
}

int gaudi_nic_phy_port_init(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	int rc;

	rc = hl_nic_phy_init(nic_port);
	if (rc)
		dev_err(hdev->dev, "Failed to init NIC PHY, port: %d, %d", port, rc);

	return rc;
}

void gaudi_nic_phy_port_fini(struct hl_nic_port *nic_port)
{
	hl_nic_phy_fini(nic_port);

	msleep(PHY_FLUSH_TIMEOUT_MSEC);

	nic_port->pcs_local_fault_cnt = 0;
	nic_port->pcs_remote_fault_cnt = 0;
	nic_port->pcs_remote_fault_reconfig_cnt = 0;

	kfifo_free(&nic_port->pcs_fail_fifo);
}

int gaudi_nic_phy_reset_macro(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev = nic_macro->hdev;
	u32 port = nic_macro->idx << 1; /* set to the first port in the macro */
	s32 chip_reset_addr = 0x980D;
	int i;

	dev_dbg(hdev->dev, "PHY reset macro %d\n", nic_macro->idx);

	/* soft reset */
	for (i = 0 ; i < NIC_MAC_LANES ; i++)
		PHY_WRITE(i, chip_reset_addr, 0x888);

	usleep_range(500, 1000);

	/* clock configuration */
	for (i = 0 ; i < NIC_MAC_LANES ; i++)
		if (i == 0)
			PHY_WRITE(i, 0x00C9, 0x390);
		else
			PHY_WRITE(i, 0x00C9, 0x310);

	for (i = 0 ; i < NIC_MAC_LANES ; i++) {
		PHY_WRITE(i, 0x8000, 0xC000);
		PHY_WRITE(i, 0x8210, 0);
		PHY_WRITE(i, 0x8100, 0);
	}

	/* PHY controller reset - to force F/W to start from pointer 0 */
	for (i = 0 ; i < NIC_MAC_LANES ; i++) {
		PHY_WRITE(i, chip_reset_addr, 0xAAA);
		PHY_WRITE(i, chip_reset_addr, 0);
	}

	/* logic reset */
	for (i = 0 ; i < NIC_MAC_LANES ; i++) {
		PHY_WRITE(i, chip_reset_addr, 0x777);
		PHY_WRITE(i, chip_reset_addr, 0);
	}

	usleep_range(500, 1000);

	return 0;
}

static void fw_unload_all(struct hl_device *hdev, bool pam4)
{
	_phy_write_all(hdev, 0x9814, 0xFFF0);
	_phy_write_all(hdev, 0x980D, 0xAAA);
	_phy_write_all(hdev, 0x980D, 0);

	msleep(100);

	_phy_write_all(hdev, 0x9814, 0);

	if (pam4)
		_phy_write_all(hdev, 0x11, 0);
	else
		_phy_write_all(hdev, 0x10B, 0);
}

int gaudi_nic_phy_fw_load_all(struct hl_device *hdev)
{
	u32 entry_point, length, ram_addr, sections, status, checks, hash = 0,
		checksum = 0x800C, fw0 = 0x9F00, fw1 = 0x980D, fw2 = 0x9814;
	int rc, i, j, port, data_ptr = 0, lane = 0;
	const struct firmware *fw;
	u16 mdio_data, crc = 0;
	const void *fw_data;
	const char *fw_name;
	bool pam4 = true; /* for debug */

	fw_name = gaudi_nic_phy_get_fw_name();

	fw_unload_all(hdev, pam4);

	rc = request_firmware(&fw, fw_name, hdev->dev);
	if (rc) {
		dev_err(hdev->dev, "Firmware file %s is not found!\n", fw_name);
		return rc;
	}

	if (fw->size < PHY_FW_SIZE) {
		dev_err(hdev->dev, "Illegal %s firmware size %zu\n", fw_name,
			fw->size);
		release_firmware(fw);
		return -EFAULT;
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

	_phy_write_all(hdev, fw2, 0xFFF0);
	_phy_write_all(hdev, fw1, 0x0AAA);
	_phy_write_all(hdev, fw1, 0);

	msleep(500);

	checks = 0;
	do {
		usleep_range(10000, 20000);
		status = _phy_read(hdev, 0, 0, fw2);
		dev_dbg(hdev->dev, "lane: %d, status: 0x%x\n", lane, status);
		if (checks++ > PHY_READ_COUNTS_PER_MS) {
			dev_err(hdev->dev,
				"failed to load NIC F/W, fw2 timeout 0x%x\n",
				status);
			release_firmware(fw);
			return -ETIMEDOUT;
		}
	} while (status);

	_phy_write_all(hdev, fw2, 0);

	for (i = 0 ; i <= sections ; i++) {
		checksum = 0x800C;
		_phy_write_all(hdev, fw0 + 12, ram_addr >> 16);
		_phy_write_all(hdev, fw0 + 13, ram_addr & 0xFFFF);
		checksum += (ram_addr >> 16) + (ram_addr & 0xFFFF);
		for (j = 0 ; j < 12 ; j++) {
			if (data_ptr >= length)
				mdio_data = 0;
			else
				mdio_data =
					get_unaligned_be16(fw_data + data_ptr);

			_phy_write_all(hdev, fw0 + j, mdio_data);
			checksum += mdio_data;
			data_ptr += 2;
			ram_addr += 2;
		}

		_phy_write_all(hdev, fw0 + 14, (~checksum + 1) & 0xFFFF);
		_phy_write_all(hdev, fw0 + 15, 0x800C);

		checks = 0;

		do {
			usleep_range(1000, 2000);
			status = _phy_read(hdev, 0, 0, fw0 + 15);
			if (checks++ > PHY_READ_COUNTS_PER_MS) {
				dev_err(hdev->dev,
					"failed to load NIC F/W, fw0 timeout 0x%x\n",
					status);
				release_firmware(fw);
				return -ETIMEDOUT;
			}
		} while (status == 0x800C);
	}

	_phy_write_all(hdev, fw0 + 12, entry_point >> 16);
	_phy_write_all(hdev, fw0 + 13, entry_point & 0xFFFF);
	checksum = (entry_point >> 16) + (entry_point & 0xFFFF) + 0x4000;
	_phy_write_all(hdev, fw0 + 14, (~checksum + 1) & 0xFFFF);
	_phy_write_all(hdev, fw0 + 15, 0x4000);

	for (port = 0 ; port < 1 ; port += 2)
		for (lane = 0 ; lane < 1 ; lane++) {
			crc_port(hdev, port, lane, &crc);
			dev_dbg(hdev->dev, "port: %d lane: %d crc: 0x%x\n", port, lane, crc);
			hash_port(hdev, port, lane, &hash);
			dev_dbg(hdev->dev, "port: %d lane: %d hash: 0x%x\n", port, lane, hash);
		}

	return 0;
}

u16 gaudi_nic_phy_get_crc(struct hl_device *hdev)
{
	u16 crc = 0;

	crc_port(hdev, 0, 0, &crc);

	return crc;
}

void gaudi_nic_phy_init(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic *nic = &hdev->nic;
	s32 *taps;
	int i;

	/* In case we didn't get serdes info from FW, set to default values */
	if (!nic->use_fw_serdes_info)
		set_default_polarity_values(hdev);

	/* set the default Tx taps */
	for (i = 0 ; i < nic_props->max_num_of_lanes ; i++) {
		/* PAM4 */
		taps = nic->phy_tx_taps[i].pam4_taps;
		taps[0] = 1;
		taps[1] = -5;
		taps[2] = 25;
		taps[3] = 0;
		taps[4] = 0;

		/* NRZ */
		taps = nic->phy_tx_taps[i].nrz_taps;
		taps[0] = 1;
		taps[1] = -5;
		taps[2] = 25;
		taps[3] = 0;
		taps[4] = 0;
	}
}
