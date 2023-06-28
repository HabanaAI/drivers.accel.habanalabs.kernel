// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi2_cn.h"
#include "../include/gaudi2/asic_reg/gaudi2_regs.h"
#include "../include/gaudi2/gaudi2_async_ids_map_extended.h"
#include "../include/hw_ip/nic/nic_general.h"

enum gaudi2_setup_type {
	GAUDI2_SETUP_TYPE_HLS2,
	GAUDI2_SETUP_TYPE_HL225_S_EXT_LB,
	GAUDI2_SETUP_TYPE_HL325_S_EXT_LB,
	GAUDI2_SETUP_TYPE_HLS3
};

static bool gaudi2_cn_get_hw_cap(struct hl_device *hdev);

int gaudi2_cn_handle_sw_error_event(struct hl_device *hdev, u16 event_type, u8 macro_index,
					struct hl_eq_nic_intr_cause *nic_intr_cause)
{
	struct hl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_cn_aux_ops *aux_ops = &gaudi2->cn_aux_ops;
	u32 error_count = 0;

	if (aux_ops->sw_err_event_handler) {
		error_count = aux_ops->sw_err_event_handler(aux_dev, event_type, macro_index,
								nic_intr_cause);
	}

	return error_count;
}

int gaudi2_cn_handle_axi_error_response_event(struct hl_device *hdev, u16 event_type,
						u8 macro_index,
						struct hl_eq_nic_intr_cause *nic_intr_cause)
{
	struct hl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_cn_aux_ops *aux_ops = &gaudi2->cn_aux_ops;
	u32 error_count = 0;

	if (aux_ops->axi_error_response_event_handler) {
		error_count = aux_ops->axi_error_response_event_handler(aux_dev, event_type,
									macro_index,
									nic_intr_cause);
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
	if (hdev->cn.ports_mask) {
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

static uint64_t gaudi2_cn_override_ports_ext_mask(struct hl_device *hdev)
{
	/* For asic type GAUDI2B, the external ports mask shouldn't be changed */
	if (hdev->asic_type == ASIC_GAUDI2B)
		return hdev->cn.ports_ext_mask;

	/* If we are running on a PCI card, all the ports should be set as external */
	if (hdev->card_type == cpucp_card_type_pci)
		return hdev->cn.ports_mask;

	switch (hdev->gaudi2_setup_type) {
	case GAUDI2_SETUP_TYPE_HLS2:
		/* For HLS2 setup type, the external ports mask shouldn't be changed */
		return hdev->cn.ports_ext_mask;
	case GAUDI2_SETUP_TYPE_HL225_S_EXT_LB:
	case GAUDI2_SETUP_TYPE_HL325_S_EXT_LB:
		/* For the above setup types, all the ports should be set as external */
		return hdev->cn.ports_mask;
	case GAUDI2_SETUP_TYPE_HLS3:
		/* For HLS3 setup type, the external ports mask is determined according to the
		 * card location.
		 */
		switch (hdev->cn.card_location) {
		case 0:
			return 0x27FC00;
		case 1:
			return 0xC003FC;
		case 2:
			return 0xC003FC;
		case 3:
			return 0x27FC00;
		case 4:
			return 0x3FF000;
		case 5:
			return 0x0003FF;
		case 6:
			return 0x0003FF;
		case 7:
			return 0x3FF000;
		default:
			dev_dbg(hdev->dev, "Invalid card location %u\n", hdev->cn.card_location);
		}

		break;
	default:
		dev_dbg(hdev->dev, "Invalid gaudi2_setup_type %u\n", hdev->gaudi2_setup_type);
	}

	return hdev->cn.ports_ext_mask;
}

int gaudi2_cn_set_info(struct hl_device *hdev, bool get_from_fw)
{
	struct hl_cn_cpucp_info *cn_cpucp_info = &hdev->asic_prop.cn_props.cpucp_info;
	struct cpucp_info *cpucp_info = &hdev->asic_prop.cpucp_info;
	struct hl_cn_cpucp_mac_addr *mac_arr = cn_cpucp_info->mac_addrs;
	struct hl_cn *cn = &hdev->cn;
	u32 card_location, serdes_type = MAX_NUM_SERDES_TYPE;
	u8 mac[ETH_ALEN], *mac_addr;
	int rc, i;

	/* copy the MAC OUI in reverse */
	for (i = 0 ; i < 3 ; i++)
		mac[i] = HABANALABS_MAC_OUI_1 >> (8 * (2 - i));

	if (get_from_fw) {
		rc = hl_cn_cpucp_info_get(hdev);
		if (rc)
			return rc;

		/* Allow debugging of gaudi2B disabled ports by overriding the pci revision id and
		 * by that identifying as gaudi2.
		 */
		if ((hdev->asic_type == ASIC_GAUDI2) && hdev->pci_rev_id_override) {
			dev_dbg(hdev->dev,
				"skipping NIC FW ports info on gaudi2B device with an overridden pci revision id\n");
		} else {
			hdev->cn.ports_mask &= cn_cpucp_info->link_mask[0];
			hdev->cn.ports_ext_mask &= cn_cpucp_info->link_ext_mask[0];
			hdev->cn.auto_neg_mask &= cn_cpucp_info->auto_neg_mask[0];
		}

		serdes_type = cn_cpucp_info->serdes_type;

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
			if (!(hdev->cn.ports_mask & BIT(i)))
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
			if (!(hdev->cn.ports_mask & BIT(i)))
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
			hdev->cn.ports_ext_mask = hdev->cn.ports_mask;

		hdev->cn.auto_neg_mask &= ~hdev->cn.ports_ext_mask;
	}

	hdev->cn.ports_ext_mask = gaudi2_cn_override_ports_ext_mask(hdev);

	if (hdev->card_type == cpucp_card_type_pci ||
			hdev->gaudi2_setup_type != GAUDI2_SETUP_TYPE_HLS2)
		hdev->cn.auto_neg_mask &= ~hdev->cn.ports_ext_mask;

	/* Disable ANLT on NIC 0 ports (due to lane swapping) */
	hdev->cn.auto_neg_mask &= ~0x3;

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

static void gaudi2_cn_post_send_status(struct hl_device *hdev, u32 port)
{
	hl_fw_unmask_irq(hdev, GAUDI2_EVENT_CPU0_STATUS_NIC0_ENG0 + port);
}

static struct hl_cn_port_funcs gaudi2_cn_port_funcs = {
	.spmu_get_stats_info = gaudi2_cn_spmu_get_stats_info,
	.spmu_config = gaudi2_cn_spmu_config,
	.spmu_sample = gaudi2_cn_spmu_sample,
	.post_send_status = gaudi2_cn_post_send_status,
};

struct hl_cn_funcs gaudi2_cn_funcs = {
	.get_hw_cap = gaudi2_cn_get_hw_cap,
	.set_hw_cap = gaudi2_cn_set_hw_cap,
	.pre_core_init = gaudi2_cn_pre_core_init,
	.set_cn_data = gaudi2_cn_set_cn_data,
	.port_funcs = &gaudi2_cn_port_funcs,
};
