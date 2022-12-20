// SPDX-License-Identifier: GPL-2.0

/* Copyright 2018-2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "nic.h"
#include "../common/habanalabs.h"

#include <linux/module.h>
#include <linux/firmware.h>
#include <asm/unaligned.h>

static void port_reset_state(struct hl_nic_port *nic_port)
{
	kfifo_reset(&nic_port->pcs_fail_fifo);
	nic_port->prev_pcs_link = false;
	nic_port->pcs_link = false;
	nic_port->auto_neg_resolved = false;
	nic_port->auto_neg_skipped = false;
	nic_port->phy_fw_tuned = false;
	nic_port->retry_cnt = 0;
	nic_port->pcs_fail_cnt = 0;
	nic_port->pcs_remote_fault_seq_cnt = 0;
	nic_port->pcs_link_restore_cnt = 0;
	nic_port->correctable_errors_cnt = 0;
	nic_port->uncorrectable_errors_cnt = 0;
}

static u32 get_data_rate(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port, speed, data_rate;

	port = nic_port->port;
	speed = nic_port->speed;

	switch (speed) {
	case SPEED_10000:
		data_rate = NIC_DR_10;
		break;
	case SPEED_25000:
		data_rate = NIC_DR_25;
		break;
	case SPEED_50000:
		data_rate = NIC_DR_50;
		break;
	case SPEED_100000:
		data_rate = NIC_DR_50;
		break;
	default:
		data_rate = NIC_DR_50;
		dev_err(hdev->dev, "unknown NIC port %d speed, continue with 50 GHz\n", port);
		break;
	}

	dev_dbg(hdev->dev, "NIC port %d, speed %d data rate %d\n", port, speed, data_rate);

	return data_rate;
}

void hl_nic_phy_set_port_status(struct hl_nic_port *nic_port, bool up)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_en_aux_ops *aux_ops;
	struct hl_aux_dev *aux_dev;
	u32 port = nic_port->port;

	aux_dev = &hdev->nic.en_aux_dev;
	aux_ops = aux_dev->aux_ops;

	if (nic_port->eth_enable)
		aux_ops->set_port_status(aux_dev, port, up);
	else if (!hdev->reset_info.in_reset)
		dev_dbg(hdev->dev, "Card %u Port %u: link %s\n",
			hdev->nic.card_location, port, up ? "up" : "down");
}

int hl_nic_phy_init(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic *nic = &hdev->nic;
	int rc;

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;

	/* If mac_loopback is enabled on this port, move the port status to UP state */
	if (nic_port->mac_loopback) {
		nic_port->pcs_link = true;
		hl_nic_phy_set_port_status(nic_port, true);
		return 0;
	}

	if (!hdev->nic.phy_config_fw) {
		port_funcs->override_phy_readiness(nic_port, true);

		/* If EQ is supported, it will take care of setting the port status */
		if (!nic->has_eq) {
			nic_port->pcs_link = true;
			hl_nic_phy_set_port_status(nic_port, true);
		}

		return 0;
	}

	nic_port->data_rate = get_data_rate(nic_port);

	rc = port_funcs->phy_port_power_up(nic_port);
	if (rc) {
		dev_err(hdev->dev, "ASIC specific phy port power-up failed, %d\n", rc);
		return rc;
	}

	port_funcs->phy_port_start_stop(nic_port, true);

	queue_delayed_work(nic_port->wq, &nic_port->link_status_work, msecs_to_jiffies(1));

	return 0;
}

/* This function does not change the port link status in order to avoid unnecessary netdev actions
 * and prints. Hence it should be done from outside.
 */
void hl_nic_phy_fini(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_port_funcs *port_funcs;

	/* This is done before the check because we support setting mac loopback for a specific port
	 * and this function might be called when nic_port->mac_loopback is true (during the port
	 * reset after setting mac loopback), but the link status work was scheduled before (when
	 * the port was opened w/o mac loopback).
	 */
	cancel_delayed_work_sync(&nic_port->link_status_work);

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;

	if (!hdev->nic.phy_config_fw || nic_port->mac_loopback) {
		port_funcs->override_phy_readiness(nic_port, false);
		nic_port->pcs_link = false;
		return;
	}

	port_reset_state(nic_port);
	port_funcs->phy_port_start_stop(nic_port, false);
}

void hl_nic_phy_port_reconfig(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_port_funcs *port_funcs;

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;

	port_funcs->phy_port_reconfig(nic_port);

	port_reset_state(nic_port);
}

int hl_nic_phy_has_fw(struct hl_device *hdev)
{
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	const struct firmware *fw;
	const char *fw_name;
	int rc;

	fw_name = nic_funcs->get_phy_fw_name();

	rc = request_firmware(&fw, fw_name, hdev->dev);
	if (rc) {
		dev_err(hdev->dev, "Firmware file %s is not found!\n", fw_name);
		return rc;
	}

	release_firmware(fw);

	return 0;
}
