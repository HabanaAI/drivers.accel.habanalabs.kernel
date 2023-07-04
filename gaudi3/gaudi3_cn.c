// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi3_cn.h"
#include "../include/gaudi3/asic_reg/gaudi3_regs.h"
#include "../include/hw_ip/nic/nic_general.h"

static bool is_400g_mode(struct hl_device *hdev)
{
	return hdev->cn_lanes_per_port == PORT_LANES_4;
}

static bool is_200g_mode(struct hl_device *hdev)
{
	return !is_400g_mode(hdev);
}

/* get the index of the first port in the macro */
u32 gaudi3_cn_get_first_port(struct hl_device *hdev, int macro_idx)
{
	u32 port = macro_idx;

	if (is_200g_mode(hdev))
		port *= 2;

	return port;
}

u64 gaudi3_cn_get_macro_ports_mask(struct hl_device *hdev, int macro_idx)
{
	u64 macro_ports_mask = is_400g_mode(hdev) ? 0x1 : 0x3;

	return macro_ports_mask << gaudi3_cn_get_first_port(hdev, macro_idx);
}

bool gaudi3_cn_is_macro_enabled(struct hl_device *hdev, int macro_idx)
{
	u32 port1, port2;

	/* In 400Gbps mode we have a single port in each macro.
	 * In 200Gbps mode we need to check also the second port in the macro. If any of the two
	 * ports are enabled, then the corresponding macro is enabled
	 */
	if (is_400g_mode(hdev)) {
		port1 = macro_idx;

		return (hdev->cn_ports_mask & BIT(port1));
	}

	/* 200G mode */
	port1 = macro_idx * 2;
	port2 = port1 + 1;

	return ((hdev->cn_ports_mask & BIT(port1)) || (hdev->cn_ports_mask & BIT(port2)));
}

int gaudi3_cn_set_info(struct hl_device *hdev, bool get_from_fw)
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

		if (hdev->pci_rev_id_override) {
			dev_dbg(hdev->dev,
				"skipping NIC FW ports info with an overridden pci revision id\n");
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
		cn->use_fw_serdes_info = true;
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
			card_location = RREG32(mmD0_PSOC_BOOT_CONF_BASE +
						mmPSOC_BOOT_CONF_BOOT_STRAP_PINS_H);
			hdev->asic_funcs->set_priv_assertions(hdev, true);

			serdes_type = card_location;
			card_location &= PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_MODULE_ID_M;
			card_location >>= PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_MODULE_ID_S;
			cpucp_info->card_location = cpu_to_le32(card_location);
			cn->card_location = card_location;
			serdes_type &= ~(PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_MODULE_ID_M |
						PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_SRIS_M |
						PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_SRNS_M |
						PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_ADD_SSC_M |
						PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_DIE_ID_M);
			serdes_type >>= (PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_DIE_ID_S + 1);
		} else {
			dev_warn(hdev->dev, "can't read card location as FW security is enabled\n");
		}
	}

	switch (serdes_type) {
	case HLS3_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI3_HLS3;
		break;
	default:
		hdev->asic_prop.server_type = HL_SERVER_TYPE_UNKNOWN;
		break;
	}

	/* PCI card is a testing card so set all ports as external */
	if (hdev->card_type == cpucp_card_type_pci) {
		hdev->cn_ports_ext_mask = hdev->cn_ports_mask;
		hdev->cn_auto_neg_mask &= ~hdev->cn_ports_ext_mask;
	}

	return 0;
}

static bool gaudi3_cn_get_hw_cap(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;

	return (gaudi3->hw_cap_initialized & HW_CAP_NIC_DRV);
}

static void gaudi3_cn_set_hw_cap(struct hl_device *hdev, bool enable)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;

	if (enable)
		gaudi3->hw_cap_initialized |= HW_CAP_NIC_DRV;
	else
		gaudi3->hw_cap_initialized &= ~HW_CAP_NIC_DRV;
}

static int gaudi3_cn_pre_core_init(struct hl_device *hdev)
{
	return 0;
}

static void gaudi3_cn_axuser_hbw_mmu_bp_set(struct hl_aux_dev *aux_dev, u32 axuser_hbw_reg_base)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	gaudi3_axuser_hbw_mmu_bp_set(hdev, axuser_hbw_reg_base, true);
}

static void gaudi3_cn_axuser_hbw_asid_set(struct hl_aux_dev *aux_dev, u32 axuser_hbw_reg_base)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	gaudi3_axuser_hbw_asid_set(hdev, axuser_hbw_reg_base, hdev->kernel_ctx->asid);
}

static int gaudi3_cn_irq_vector(struct hl_aux_dev *aux_dev, unsigned int nr)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hl_irq_vector(hdev, nr);
}

static void gaudi3_cn_set_cn_data(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_cn_aux_data *gaudi3_aux_data;
	struct gaudi3_cn_aux_ops *gaudi3_aux_ops;
	struct hl_cn_aux_data *aux_data;
	struct hl_cn_aux_ops *aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	aux_data = aux_dev->aux_data;
	gaudi3_aux_data = &gaudi3->cn_aux_data;
	aux_data->asic_specific = gaudi3_aux_data;
	gaudi3_aux_ops = &gaudi3->cn_aux_ops;
	aux_ops = aux_dev->aux_ops;
	aux_ops->asic_ops = gaudi3_aux_ops;

	gaudi3_aux_data->msix_enabled = !!(gaudi3->hw_cap_initialized & HW_CAP_MSIX);
	gaudi3_aux_data->num_of_hdcores = hdev->asic_prop.num_of_hdcores;
	gaudi3_aux_data->cfg_base_address = hdev->asic_prop.cfg_base_address;
	gaudi3_aux_data->lbw_base_address = LBW_BASE;
	gaudi3_aux_data->irq_num_port_base = GAUDI3_IRQ_NUM_NIC_PORT_FIRST;
	gaudi3_aux_data->enable_h9_rx_drop_eco = hdev->nic_enable_h9_rx_drop_eco;

	gaudi3_aux_ops->axuser_hbw_mmu_bp_set = gaudi3_cn_axuser_hbw_mmu_bp_set;
	gaudi3_aux_ops->axuser_hbw_asid_set = gaudi3_cn_axuser_hbw_asid_set;
	gaudi3_aux_ops->irq_vector = gaudi3_cn_irq_vector;
}

/**
 * gaudi3_cn_disable_nics_interrupts() - Disable interrupts of all NICs.
 * Gaudi3 NIC interrupts are enabled by default, need to disable them ASAP
 * before ports init and after hard reset.
 *
 * @hdev: habanalabs device structure.
 */
static void gaudi3_cn_disable_nics_interrupts(struct hl_device *hdev)
{
	u32 port;
	int i;

	/* The CPU_IF_NIC registers are handled only by the privileged embedded code and in any
	 * case, this is relevant only on PLDM where the PHY link is always ON
	 */
	if (hdev->pldm)
		gaudi3_disable_nic_interrupts_cpu_if(hdev);

	/* Disable interrupts of all NICs */
	for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++) {
		/* skip non-present macros in pldm as we may run on partial-nics image */
		if (hdev->pldm && !(hdev->cn_ports_mask &
					gaudi3_cn_get_macro_ports_mask(hdev, i)))
			continue;

		port = gaudi3_cn_get_first_port(hdev, i);

		NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_INTERRUPT_MASK,
				NIC_TXE_INTERRUPT_MASK_R_M);
		NIC_WREG32(mmD0_NIC0_TXS_BASE + mmNIC_TXS_INTERRUPT_MASK,
				NIC_TXS_INTERRUPT_MASK_R_M);

		/* interrupt MSI and WIRE regs determine if the interrupt
		 * being generated is directed to the MSI or Wire Path
		 * (can also be both or neither). By default interrupts are
		 * directed to both locations.
		 */
		NIC_WREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_INTERRUPT_MSI, 0);
		NIC_WREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_INTERRUPT_WIRE, 0);

		/* Interrupt mask reg disables/enables each interrupt (1 is disabled)
		 * The meaning of the bits in the interrupt mask register is:
		 *    [3..0] - EQ event interrupt (1 bit per EQ)
		 *    [4..7] - EQ error interrupt (1 bit per EQ)
		 * interrupts are disabled by default
		 */
		NIC_WREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_INTERRUPT_MASK,
						NIC_QPC_INTERRUPT_MASK_R_M);

		NIC_WREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_INTERRUPT_RESP_ERR_MASK,
					NIC_QPC_INTERRUPT_RESP_ERR_MASK_R_M);

		NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SPI_INTR_MASK_0,
						NIC_RXE_SPI_INTR_MASK_VAL_M);
		NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SEI_INTR_MASK, 0xFFFFFFFF);

		NIC_WREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_EVENT_QUE_CFG_0, 0);
		NIC_WREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_EVENT_QUE_CFG_1, 0);
		NIC_WREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_EVENT_QUE_CFG_2, 0);
		NIC_WREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_EVENT_QUE_CFG_3, 0);

		/* flush */
		NIC_RREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SEI_INTR_MASK);
	}
}

/**
 * gaudi3_cn_quiescence() - make sure that CN does not generate events nor
 *                           receives traffic.
 * Gaudi3 default values at power-up and after hard-reset are interrupts enabled
 * and Rx enabled, we need to disable them until driver configuration is
 * complete.
 *
 * @hdev: habanalabs device structure.
 */
void gaudi3_cn_quiescence(struct hl_device *hdev)
{
	/* Do not quiescence the ports during device release
	 * reset aka soft reset flow.
	 */
	if (gaudi3_cn_get_hw_cap(hdev))
		return;

	dev_dbg(hdev->dev, "Quiescence the NICs\n");

	gaudi3_cn_disable_nics_interrupts(hdev);
}

u32 gaudi3_cn_handle_bmon_spmu_event(struct hl_device *hdev, u32 macro_index)
{
	int rc;

	/* For this point a profiler is not configuring the BMON and SPMU block and
	 * therefore we can't deduce which port and entity triggered the interrupt.
	 * So for now we only clear all interrupt registers to prevent interrupt flood
	 */
	rc = gaudi3_cn_ack_spmu_bmon_interrupt(hdev, macro_index);
	if (rc) {
		dev_err_ratelimited(hdev->dev,
				"failed to ack nic-macro %d SPMU/BMON interrupt(rc %d)\n",
				macro_index, rc);
		return 0;
	}

	return 1;
}

static int gaudi3_cn_send_cpucp_packet(struct hl_device *hdev, u32 port,
					enum cpucp_packet_id packet_id, int val)
{
	struct cpucp_packet pkt;
	int rc = 0;

	memset(&pkt, 0, sizeof(pkt));
	pkt.ctl = cpu_to_le32(packet_id << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.value = cpu_to_le64(val);
	pkt.macro_index = cpu_to_le32(is_400g_mode(hdev) ? port : (port >> 1));

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc)
		dev_err(hdev->dev,
			"Failed to send cpucp packet, port %d packet id %d, val %d, error %d\n",
			port, packet_id, val, rc);

	return rc;
}

static void gaudi3_cn_post_send_status(struct hl_device *hdev, u32 port)
{
	/* FW does not mask MSG interrupts, so unmask_irq is not needed */
}

static struct hl_cn_port_funcs gaudi3_cn_port_funcs = {
	.spmu_get_stats_info = gaudi3_cn_spmu_get_stats_info,
	.spmu_config = gaudi3_cn_spmu_config,
	.spmu_sample = gaudi3_cn_spmu_sample,
	.send_cpucp_packet = gaudi3_cn_send_cpucp_packet,
	.post_send_status = gaudi3_cn_post_send_status,
};

struct hl_cn_funcs gaudi3_cn_funcs = {
	.get_hw_cap = gaudi3_cn_get_hw_cap,
	.set_hw_cap = gaudi3_cn_set_hw_cap,
	.pre_core_init = gaudi3_cn_pre_core_init,
	.set_cn_data = gaudi3_cn_set_cn_data,
	.port_funcs = &gaudi3_cn_port_funcs,
};
