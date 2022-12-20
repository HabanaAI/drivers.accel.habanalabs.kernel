// SPDX-License-Identifier: GPL-2.0

/* Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi3P.h"
#include "gaudi3_nic.h"
#include "../include/gaudi3/asic_reg/gaudi3_regs.h"
#include <linux/firmware.h>
#include <asm/unaligned.h>

#define GAUDI3_PHY_FW_FILE	"habanalabs/gaudi3/gaudi3_nic_fw.bin"

void gaudi3_nic_phy_link_status_work(struct work_struct *work)
{
}

void gaudi3_nic_phy_port_start_stop(struct hl_nic_port *nic_port, bool is_start)
{
}

int gaudi3_nic_phy_port_power_up(struct hl_nic_port *nic_port)
{
	return -EOPNOTSUPP;
}

void gaudi3_nic_phy_port_reconfig(struct hl_nic_port *nic_port)
{
}

int gaudi3_nic_phy_port_init(struct hl_nic_port *nic_port)
{
	return hl_nic_phy_init(nic_port);
}

void gaudi3_nic_phy_port_fini(struct hl_nic_port *nic_port)
{
	hl_nic_phy_fini(nic_port);
}

int gaudi3_nic_phy_reset_macro(struct hl_nic_macro *nic_macro)
{
	return -EOPNOTSUPP;
}

const char *gaudi3_nic_phy_get_fw_name(void)
{
	return GAUDI3_PHY_FW_FILE;
}

int gaudi3_nic_phy_fw_load_all(struct hl_device *hdev)
{
	return -EOPNOTSUPP;
}

u16 gaudi3_nic_phy_get_crc(struct hl_device *hdev)
{
	return 0;
}
