// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi2_cn.h"
#include "../include/gaudi2/asic_reg/gaudi2_regs.h"
#include "../include/gaudi2/gaudi2_async_ids_map_extended.h"
#include "../include/hw_ip/nic/nic_general.h"

static bool gaudi2_cn_get_hw_cap(struct hl_device *hdev);

static_assert(MAX_PORTS_PER_NIC == HL_CN_CPUCP_MAX_PORTS_PER_NIC);

static void gaudi2_cn_convert_intr_cause(struct hl_cn_eq_intr_cause *to,
						struct hl_eq_nic_intr_cause *from)
{
	int i;

	to->intr_type = le32_to_cpu(from->intr_type);

	for (i = 0 ; i < HL_CN_CPUCP_MAX_PORTS_PER_NIC ; i++)
		to->intr_cause[i].intr_cause_data =
						le64_to_cpu(from->intr_cause[i].intr_cause_data);
}

int gaudi2_cn_handle_sw_error_event(struct hl_device *hdev, u16 event_type, u8 macro_index,
					struct hl_eq_nic_intr_cause *nic_intr_cause)
{
	struct hl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_cn_aux_ops *aux_ops = &gaudi2->cn_aux_ops;
	struct hl_cn_eq_intr_cause cn_intr_cause = {};
	u32 error_count = 0;

	if (aux_ops->sw_err_event_handler) {
		gaudi2_cn_convert_intr_cause(&cn_intr_cause, nic_intr_cause);
		error_count = aux_ops->sw_err_event_handler(aux_dev, event_type, macro_index,
								&cn_intr_cause);
	}

	return error_count;
}

int gaudi2_cn_handle_axi_error_response_event(struct hl_device *hdev, u16 event_type,
					u8 macro_index, struct hl_eq_nic_intr_cause *nic_intr_cause)
{
	struct hl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_cn_aux_ops *aux_ops = &gaudi2->cn_aux_ops;
	struct hl_cn_eq_intr_cause cn_intr_cause = {};
	u32 error_count = 0;

	if (aux_ops->axi_error_response_event_handler) {
		gaudi2_cn_convert_intr_cause(&cn_intr_cause, nic_intr_cause);
		error_count = aux_ops->axi_error_response_event_handler(aux_dev, event_type,
								macro_index, &cn_intr_cause);
	}

	return error_count;
}

/**
 * gaudi2_cn_disable_interrupts() - Disable interrupts of all ports.
 * Gaudi2 CN interrupts are enabled by default, need to disable them ASAP
 * before ports init and after hard reset.
 *
 * @hdev: habanalabs device structure.
 */
void gaudi2_cn_disable_interrupts(struct hl_device *hdev)
{
	u32 port;

	/* The CPU_IF_NIC registers are handled only by the privileged embedded code and in any
	 * case, this is relevant only on PLDM where the PHY link is always ON
	 */
	if (hdev->pldm)
		gaudi2_disable_nic_interrupts_cpu_if(hdev);

	/* Disable interrupts of all NICs */
	if (hdev->cn_ports_mask) {
		/* we only need the port number for NIC_WREG32 */
		for (port = 0 ; port < NIC_NUMBER_OF_PORTS ; port++) {
			NIC_WREG32(mmNIC0_QPC0_EVENT_QUE_CFG, 0);
			NIC_WREG32(mmNIC0_QPC0_INTERRUPT_EN, 0);
			NIC_WREG32(mmNIC0_QPC0_INTERRUPT_MASK, 0xFFFFFFFF);

			/* This registers needs to be configured only in case of PLDM */
			if (hdev->pldm) {
				NIC_WREG32(mmNIC0_QPC0_INTERRUPT_RESP_ERR_MASK, 0xFFFFFFFF);
				NIC_WREG32(mmNIC0_TXE0_INTERRUPT_MASK, 0xFFFFFFFF);
				NIC_WREG32(mmNIC0_RXE0_SPI_INTR_MASK, 0xFFFFFFFF);
				NIC_WREG32(mmNIC0_RXE0_SEI_INTR_MASK, 0xFFFFFFFF);
				NIC_WREG32(mmNIC0_TXS0_INTERRUPT_MASK, 0xFFFFFFFF);
			}

			/* WA for H/W bug H6-3339 - mask the link UP interrupt */
			NIC_MACRO_WREG32(mmNIC0_PHY_PHY_LINK_STS_INTR, 0x1);
		}

		/* flush */
		port = 0;
		NIC_RREG32(mmNIC0_QPC0_EVENT_QUE_CFG);
	}
}

/**
 * gaudi2_cn_quiescence() - make sure that NIC does not generate events nor
 *                           receives traffic.
 * Gaudi2 default values at power-up and after hard-reset are interrupts enabled
 * and Rx enabled, we need to disable them until driver configuration is
 * complete.
 *
 * @hdev: habanalabs device structure.
 */
void gaudi2_cn_quiescence(struct hl_device *hdev)
{
	/*
	 * Do not quiescence the ports during device release
	 * reset aka soft reset flow.
	 */
	if (gaudi2_cn_get_hw_cap(hdev))
		return;

	dev_dbg(hdev->dev, "Quiescence the NICs\n");

	gaudi2_cn_disable_interrupts(hdev);

	/* quiescence phy before configuring to prevent any packet entering nic */
	gaudi2_cn_quiescence_phy_no_fw(hdev);
}

static bool gaudi2_cn_get_hw_cap(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;

	return (gaudi2->hw_cap_initialized & HW_CAP_NIC_DRV);
}

static void gaudi2_cn_set_hw_cap(struct hl_device *hdev, bool enable)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;

	if (enable)
		gaudi2->hw_cap_initialized |= HW_CAP_NIC_DRV;
	else
		gaudi2->hw_cap_initialized &= ~HW_CAP_NIC_DRV;
}

int gaudi2_cn_set_info(struct hl_device *hdev, bool get_from_fw)
{
	struct cpucp_nic_info *nic_info = &hdev->asic_prop.cpucp_nic_info;
	struct cpucp_info *cpucp_info = &hdev->asic_prop.cpucp_info;
	struct cpucp_mac_addr *mac_arr = nic_info->mac_addrs;
	struct hl_cn *cn = &hdev->cn;
	u32 card_location, serdes_type = MAX_NUM_SERDES_TYPE;
	u8 mac[ETH_ALEN], *mac_addr;
	int rc, i;

	/* copy the MAC OUI in reverse */
	for (i = 0 ; i < 3 ; i++)
		mac[i] = HABANALABS_MAC_OUI_1 >> (8 * (2 - i));

	if (get_from_fw) {
		rc = hl_fw_cpucp_nic_info_get(hdev);
		if (rc)
			return rc;

		/* Allow debugging of gaudi2B disabled ports by overriding the pci revision id and
		 * by that identifying as gaudi2.
		 */
		if ((hdev->asic_type == ASIC_GAUDI2) && hdev->pci_rev_id_override) {
			dev_dbg(hdev->dev,
				"skipping NIC FW ports info on gaudi2B device with an overridden pci revision id\n");
		} else {
			hdev->cn_ports_mask &= le64_to_cpu(nic_info->link_mask[0]);
			hdev->cn_ports_ext_mask &= le64_to_cpu(nic_info->link_ext_mask[0]);
			hdev->cn_auto_neg_mask &= le64_to_cpu(nic_info->auto_neg_mask[0]);
		}

		serdes_type = le16_to_cpu(nic_info->serdes_type);

		/* In case of invalid MAC from F/W, and if the user asked to ignore eeprom related
		 * errors, the MAC addresses will be set manually according to the bus address and
		 * the port id.
		 * here we prepare the 3rd (bus id) and the 4th (device id) octates for such a case.
		 */
		if (hdev->ignore_eeprom_errors) {
			mac[3] = hdev->pdev->bus->number;
			mac[4] = PCI_SLOT(hdev->pdev->devfn);
		}

		/* check for invalid MAC addresses from F/W (bad OUI) */
		for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
			if (!(hdev->cn_ports_mask & BIT(i)))
				continue;

			mac_addr = mac_arr[i].mac_addr;
			if (strncmp(mac, mac_addr, 3)) {
				if (hdev->ignore_eeprom_errors) {
					dev_dbg(hdev->dev,
						"bad MAC OUI %pM, port %d - setting a valid MAC\n",
						mac_addr, i);
					mac[ETH_ALEN - 1] = i;
					memcpy(mac_addr, mac, ETH_ALEN);
				} else {
					dev_err(hdev->dev,
						"bad MAC OUI %pM, port %d - failing the initialization\n",
						mac_addr, i);
					return -EFAULT;
				}
			}
		}

		cn->card_location = le32_to_cpu(cpucp_info->card_location);
		cn->use_fw_serdes_info = hdev->gaudi2_setup_type == GAUDI2_SETUP_TYPE_HLS2;
	} else {
		/* No F/W, hence need to set the MACs manually (randomize) */
		get_random_bytes(&mac[3], 2);

		for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
			if (!(hdev->cn_ports_mask & BIT(i)))
				continue;

			mac[ETH_ALEN - 1] = i;
			memcpy(mac_arr[i].mac_addr, mac, ETH_ALEN);
		}

		if (!(hdev->fw_components & FW_TYPE_BOOT_CPU)) {
			/* This section reads privilege register, hence we should disable
			 * assertion on simulator to allow this read.
			 * Assertion is turned on right after the register is read.
			 */
			hdev->asic_funcs->set_priv_assertions(hdev, false);
			card_location = RREG32(mmPSOC_GLOBAL_CONF_BOOT_STRAP_PINS_H);
			hdev->asic_funcs->set_priv_assertions(hdev, true);

			serdes_type = card_location;
			card_location &= PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_H_I2C_SLV_ADDR_MASK;
			card_location >>= PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_H_I2C_SLV_ADDR_SHIFT;
			cpucp_info->card_location = cpu_to_le32(card_location);
			cn->card_location = card_location;
			serdes_type &= PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_H_RERERVED_STRAP_MASK;
			serdes_type >>= PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_H_RERERVED_STRAP_SHIFT;
		} else {
			dev_warn(hdev->dev, "can't read card location as FW security is enabled\n");
		}
	}

	switch (serdes_type) {
	case HLS2_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI2_HLS2;
		break;
	case HLS2_TYPE_1_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI2_TYPE1;
		break;
	default:
		hdev->asic_prop.server_type = HL_SERVER_TYPE_UNKNOWN;
		break;
	}

	/* If we are running on non HLS2 setup or a PCI card, all the ports should be set as
	 * external (the only exception is when the asic type is GADUI2B).
	 */
	if (hdev->card_type == cpucp_card_type_pci ||
			hdev->gaudi2_setup_type != GAUDI2_SETUP_TYPE_HLS2) {
		if (hdev->asic_type != ASIC_GAUDI2B)
			hdev->cn_ports_ext_mask = hdev->cn_ports_mask;

		hdev->cn_auto_neg_mask &= ~hdev->cn_ports_ext_mask;
	}

	/* Disable ANLT on NIC 0 ports (due to lane swapping) */
	hdev->cn_auto_neg_mask &= ~0x3;

	return 0;
}

static int gaudi2_cn_pre_core_init(struct hl_device *hdev)
{
	return 0;
}

static bool gaudi2_cn_can_unset_asid_cfg(struct hl_aux_dev *aux_dev)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	/* For FW below 1.8 there is no option to unset the ASID. */
	return !gaudi2_is_fw_ver_below_1_8(hdev);
}

static int gaudi2_cn_reset_mac_tx(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct cpucp_packet pkt;
	int rc;

	memset(&pkt, 0, sizeof(pkt));
	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_MAC_TX_RESET << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.port_index = cpu_to_le32(port);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc) {
		dev_err(hdev->dev, "Card %u Port %u: Failed to reset MAC Tx, rc %d\n",
			cn->card_location, port, rc);
		return rc;
	}

	return 0;
}

static char *gaudi2_cn_get_event_name(struct hl_aux_dev *aux_dev, u16 event_type)
{
	return gaudi2_irq_map_table[event_type].valid ? gaudi2_irq_map_table[event_type].name :
			"N/A Event";
}

static int gaudi2_cn_poll_mem(struct hl_aux_dev *aux_dev, u32 *addr, u32 *val,
				hl_cn_poll_cond_func func)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hl_poll_timeout_memory(hdev, addr, *val, func(*val, NULL), 10,
					HL_DEVICE_TIMEOUT_USEC, true);
}

static void gaudi2_cn_set_cn_data(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_cn_aux_data *gaudi2_aux_data;
	struct gaudi2_cn_aux_ops *gaudi2_aux_ops;
	struct hl_cn_aux_data *aux_data;
	struct hl_cn_aux_ops *aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	aux_data = aux_dev->aux_data;
	gaudi2_aux_data = &gaudi2->cn_aux_data;
	aux_data->asic_specific = gaudi2_aux_data;
	aux_ops = aux_dev->aux_ops;
	gaudi2_aux_ops = &gaudi2->cn_aux_ops;
	aux_ops->asic_ops = gaudi2_aux_ops;

	gaudi2_aux_data->cfg_base = CFG_BASE;
	gaudi2_aux_data->fw_security_enabled = hdev->asic_prop.fw_security_enabled;
	gaudi2_aux_data->msix_enabled = !!(gaudi2->hw_cap_initialized & HW_CAP_MSIX);
	gaudi2_aux_data->irq_num_port_base = GAUDI2_IRQ_NUM_NIC_PORT_FIRST;
	gaudi2_aux_data->sob_id_base = GAUDI2_RESERVED_SOB_NIC_PORT_FIRST;
	gaudi2_aux_data->sob_inc_cfg_val = GAUDI2_SOB_INCREMENT_BY_ONE;

	/* cn2accel */
	gaudi2_aux_ops->can_unset_asid_cfg = gaudi2_cn_can_unset_asid_cfg;
	gaudi2_aux_ops->reset_mac_tx = gaudi2_cn_reset_mac_tx;
	gaudi2_aux_ops->get_event_name = gaudi2_cn_get_event_name;
	gaudi2_aux_ops->poll_mem = gaudi2_cn_poll_mem;
}

void gaudi2_cn_compute_reset_prepare(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_cn_aux_ops *gaudi2_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi2_aux_ops = &gaudi2->cn_aux_ops;

	if (gaudi2_aux_ops->reset_prepare)
		gaudi2_aux_ops->reset_prepare(aux_dev);
}

void gaudi2_cn_compute_reset_late_init(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_cn_aux_ops *gaudi2_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi2_aux_ops = &gaudi2->cn_aux_ops;

	if (gaudi2_aux_ops->reset_late_init)
		gaudi2_aux_ops->reset_late_init(aux_dev);
}

static int gaudi2_cn_send_cpucp_packet(struct hl_device *hdev, u32 port,
					enum cpucp_packet_id packet_id, int val)
{
	struct cpucp_packet pkt;
	int rc = 0;

	memset(&pkt, 0, sizeof(pkt));
	pkt.ctl = cpu_to_le32(packet_id << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.value = cpu_to_le64(val);
	pkt.macro_index = cpu_to_le32(port);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc)
		dev_err(hdev->dev,
			"Failed to send cpucp packet, port %d packet id %d, val %d, error %d\n",
			port, packet_id, val, rc);

	return rc;
}

static void gaudi2_cn_post_send_status(struct hl_device *hdev, u32 port)
{
	hl_fw_unmask_irq(hdev, GAUDI2_EVENT_CPU0_STATUS_NIC0_ENG0 + port);
}

static struct hl_cn_port_funcs gaudi2_cn_port_funcs = {
	.spmu_get_stats_info = gaudi2_cn_spmu_get_stats_info,
	.spmu_config = gaudi2_cn_spmu_config,
	.spmu_sample = gaudi2_cn_spmu_sample,
	.send_cpucp_packet = gaudi2_cn_send_cpucp_packet,
	.post_send_status = gaudi2_cn_post_send_status,
};

struct hl_cn_funcs gaudi2_cn_funcs = {
	.get_hw_cap = gaudi2_cn_get_hw_cap,
	.set_hw_cap = gaudi2_cn_set_hw_cap,
	.pre_core_init = gaudi2_cn_pre_core_init,
	.set_cn_data = gaudi2_cn_set_cn_data,
	.port_funcs = &gaudi2_cn_port_funcs,
};
