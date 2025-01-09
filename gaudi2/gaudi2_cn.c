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

int gaudi2_cn_handle_sw_error_event(struct hl_device *hdev, u16 event_type, u8 macro_index,
					struct hl_eq_nic_intr_cause *nic_intr_cause)
{
	struct hbl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_cn_aux_ops *aux_ops = &gaudi2->cn_aux_ops;
	u32 error_count = 0;

	if (aux_ops->sw_err_event_handler)
		error_count = aux_ops->sw_err_event_handler(aux_dev, event_type, macro_index,
								nic_intr_cause);

	return error_count;
}

int gaudi2_cn_handle_axi_error_response_event(struct hl_device *hdev, u16 event_type,
						u8 macro_index,
						struct hl_eq_nic_intr_cause *nic_intr_cause)
{
	struct hbl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_cn_aux_ops *aux_ops = &gaudi2->cn_aux_ops;
	u32 error_count = 0;

	if (aux_ops->axi_error_response_event_handler)
		error_count = aux_ops->axi_error_response_event_handler(aux_dev, event_type,
									macro_index,
									nic_intr_cause);

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

	if (!hdev->cn.ports_mask)
		return;

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

/**
 * gaudi2_cn_override_ports_ext_mask() - Returns the external ports mask.
 * @hdev: Hl device whose external ports mask to return.
 * @ports_ext_mask: Out, the external ports mask.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int gaudi2_cn_override_ports_ext_mask(struct hl_device *hdev, uint64_t *ports_ext_mask)
{
	/* For asic type GAUDI2B, the external ports mask shouldn't be changed */
	if (hdev->asic_type == ASIC_GAUDI2B) {
		*ports_ext_mask = hdev->cn.ports_ext_mask;
		return 0;
	}

	switch (hdev->gaudi2_setup_type) {
	case GAUDI2_SETUP_TYPE_HLS2:
		/* For HLS2 setup type, the external ports mask shouldn't be changed */
		*ports_ext_mask = hdev->cn.ports_ext_mask;
		return 0;
	case GAUDI2_SETUP_TYPE_HL225_S_EXT_LB:
	case GAUDI2_SETUP_TYPE_HL325_S_EXT_LB:
		/* For the above setup types, all the ports should be set as external */
		*ports_ext_mask = hdev->cn.ports_mask;
		return 0;
	case GAUDI2_SETUP_TYPE_HLS3:
		/* For HLS3 setup type, the external ports mask is determined according to the
		 * card location.
		 */
		switch (hdev->cn.card_location) {
		case 0:
			*ports_ext_mask = 0x27FC00;
			return 0;
		case 1:
			*ports_ext_mask = 0xC003FC;
			return 0;
		case 2:
			*ports_ext_mask = 0xC003FC;
			return 0;
		case 3:
			*ports_ext_mask = 0x27FC00;
			return 0;
		case 4:
			*ports_ext_mask = 0x3FF000;
			return 0;
		case 5:
			*ports_ext_mask = 0x0003FF;
			return 0;
		case 6:
			*ports_ext_mask = 0x0003FF;
			return 0;
		case 7:
			*ports_ext_mask = 0x3FF000;
			return 0;
		default:
			dev_dbg(hdev->dev, "Invalid card location %u\n", hdev->cn.card_location);
			break;
		}

		break;
	case GAUDI2_SETUP_TYPE_HL288:
		/* In this flavor ports 22,23 are disabled and 6,7,8,9 are external*/
		hdev->cn.ports_mask = 0x3FFFFF;
		*ports_ext_mask = 0x3C0;
		return 0;
	default:
		dev_dbg(hdev->dev, "Invalid gaudi2_setup_type %u\n", hdev->gaudi2_setup_type);
		break;
	}

	return -EINVAL;
}

static int gaudi2_cn_check_oui_prefix_validity(u8 *mac_addr)
{
	u8 mac[ETH_ALEN];
	int i;

	for (i = 0 ; i < 3 ; i++)
		mac[i] = HABANALABS_MAC_OUI_1 >> (8 * (2 - i));

	if (!strncmp(mac, mac_addr, 3))
		return 1;

	for (i = 0 ; i < 3 ; i++)
		mac[i] = HABANALABS_MAC_OUI_2 >> (8 * (2 - i));

	if (!strncmp(mac, mac_addr, 3))
		return 1;

	return 0;
}

int gaudi2_cn_set_info(struct hl_device *hdev, bool get_from_fw)
{
	struct hbl_cn_cpucp_info *cn_cpucp_info = &hdev->asic_prop.cn_props.cpucp_info;
	struct cpucp_info *cpucp_info = &hdev->asic_prop.cpucp_info;
	struct hbl_cn_cpucp_mac_addr *mac_arr = cn_cpucp_info->mac_addrs;
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
			if (!gaudi2_cn_check_oui_prefix_validity(mac_addr)) {
				if (hdev->ignore_eeprom_errors) {
					dev_dbg(hdev->dev,
						"bad MAC OUI %pM, port %d - setting a valid MAC\n",
						mac_addr, i);
					mac[ETH_ALEN - 1] = i;
					memcpy(mac_addr, mac, ETH_ALEN);
				} else {
					dev_warn(hdev->dev, "unrecognized MAC OUI %pM, port %d\n",
						mac_addr, i);
				}
			}
		}

		cn->card_location = le32_to_cpu(cpucp_info->card_location);
		cn->use_fw_serdes_info = hdev->gaudi2_setup_type == GAUDI2_SETUP_TYPE_HLS2;
	} else {
		/* SW-169172: For HLS3 setup, as a w/a get the card_location from F/W. */
		if (hdev->gaudi2_setup_type == GAUDI2_SETUP_TYPE_HLS3) {
			rc = hl_cn_cpucp_info_get(hdev);
			if (rc)
				return rc;

			cn->card_location = le32_to_cpu(cpucp_info->card_location);
		}

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
			/* TODO: SW-214295 - Remove this WA once resolved.
			 * The RERERVED_STRAP_MASK field is 3 bits only hence the serdes type
			 * which are common to all Gaudi family cannot represent HL288, hence if
			 * value in Gaudi2 is of HLS3_FULLSCALE_IN_SERDES_TYPE that is actually a
			 * HL288 type.
			 */
			if (serdes_type == HLS3_FULLSCALE_IN_SERDES_TYPE)
				serdes_type = HL288_SERDES_TYPE;
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
	case HL288_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI2_HL288;
		break;
	default:
		hdev->asic_prop.server_type = HL_SERVER_TYPE_UNKNOWN;

		/* SW-169172: For HLS3 setup don't fail device init on invalid serdes_type. */
		if (get_from_fw && hdev->gaudi2_setup_type != GAUDI2_SETUP_TYPE_HLS3) {
			dev_err(hdev->dev, "bad SerDes type %d\n", serdes_type);
			return -EFAULT;
		}
		break;
	}

	/* If running on non HLS2 setup, we set the external ports according to the module param
	 * setup type.
	 */
	if (hdev->gaudi2_setup_type != GAUDI2_SETUP_TYPE_HLS2) {
		rc = gaudi2_cn_override_ports_ext_mask(hdev, &hdev->cn.ports_ext_mask);
		if (rc)
			return rc;

		hdev->cn.auto_neg_mask &= ~hdev->cn.ports_ext_mask;
	}

	/* Disable ANLT on NIC 0 ports (due to lane swapping) */
	hdev->cn.auto_neg_mask &= ~0x3;

	return 0;
}

static int gaudi2_cn_pre_core_init(struct hl_device *hdev)
{
	return 0;
}

static char *gaudi2_cn_get_event_name(struct hbl_aux_dev *aux_dev, u16 event_type)
{
	return gaudi2_irq_map_table[event_type].valid ? gaudi2_irq_map_table[event_type].name :
			"N/A Event";
}

static int gaudi2_cn_poll_mem(struct hbl_aux_dev *aux_dev, u32 *addr, u32 *val,
				hbl_cn_poll_cond_func func)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hl_poll_timeout_memory(hdev, addr, *val, func(*val, NULL), 10,
					HL_DEVICE_TIMEOUT_USEC, true);
}

static void *gaudi2_cn_dma_alloc_coherent(struct hbl_aux_dev *aux_dev, size_t size,
					  dma_addr_t *dma_handle, gfp_t flag)
{
	return hl_cn_dma_alloc_coherent(aux_dev, size, dma_handle, flag);
}

static void gaudi2_cn_dma_free_coherent(struct hbl_aux_dev *aux_dev, size_t size, void *cpu_addr,
					dma_addr_t dma_handle)
{
	hl_cn_dma_free_coherent(aux_dev, size, cpu_addr, dma_handle);
}

static void *gaudi2_cn_dma_pool_zalloc(struct hbl_aux_dev *aux_dev, size_t size, gfp_t mem_flags,
				       dma_addr_t *dma_handle)
{
	return hl_cn_dma_pool_zalloc(aux_dev, size, mem_flags, dma_handle);
}

static void gaudi2_cn_dma_pool_free(struct hbl_aux_dev *aux_dev, void *vaddr, dma_addr_t dma_addr)
{
	hl_cn_dma_pool_free(aux_dev, vaddr, dma_addr);
}

static int gaudi2_cn_get_tx_swap_map(struct hbl_aux_dev *aux_dev, u16 *tx_swap_map,
					u32 tx_swap_map_size)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct hbl_cn_cpucp_info *cn_cpucp_info;

	cn_cpucp_info = &hdev->asic_prop.cn_props.cpucp_info;

	if (tx_swap_map_size > CPUCP_MAX_NICS) {
		dev_dbg(hdev->dev, "tx_swap_map_size (%d) > %d\n", tx_swap_map_size,
			CPUCP_MAX_NICS);
		return -EINVAL;
	}

	memcpy(tx_swap_map, cn_cpucp_info->tx_swap_map, sizeof(*tx_swap_map) * tx_swap_map_size);

	return 0;
}

static void gaudi2_cn_device_reset(struct hbl_aux_dev *aux_dev)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	hl_device_cond_reset(hdev, HL_DRV_RESET_HARD, HL_NOTIFIER_EVENT_DEVICE_RESET);
}

static void gaudi2_cn_set_cn_data(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_cn_aux_data *gaudi2_aux_data;
	struct gaudi2_cn_aux_ops *gaudi2_aux_ops;
	struct hbl_cn_aux_ops *aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi2_aux_data = &gaudi2->cn_aux_data;
	cn->asic_specific_dev_info = gaudi2_aux_data;
	aux_ops = aux_dev->aux_ops;
	gaudi2_aux_ops = &gaudi2->cn_aux_ops;
	aux_ops->asic_ops = gaudi2_aux_ops;

	gaudi2_aux_data->fw_security_enabled = prop->fw_security_enabled;
	gaudi2_aux_data->irq_num_port_base = GAUDI2_IRQ_NUM_NIC_PORT_FIRST;
	gaudi2_aux_data->sob_id_base = GAUDI2_RESERVED_SOB_NIC_PORT_FIRST;
	gaudi2_aux_data->sob_inc_cfg_val = GAUDI2_SOB_INCREMENT_BY_ONE;
	gaudi2_aux_data->setup_type = hdev->gaudi2_setup_type;
	gaudi2_aux_data->vendor_part_id = prop->pci_id;
	gaudi2_aux_data->kernel_asid = HL_KERNEL_ASID_ID;
	gaudi2_aux_data->card_location = hdev->ignore_fw_nic_info ? hdev->card_location_override :
								cn->card_location;
	gaudi2_aux_data->fw_major_version = hdev->fw_inner_major_ver;
	gaudi2_aux_data->fw_minor_version = hdev->fw_inner_minor_ver;
	gaudi2_aux_data->fw_app_cpu_boot_dev_sts0 = prop->fw_app_cpu_boot_dev_sts0;
	gaudi2_aux_data->fw_app_cpu_boot_dev_sts1 = prop->fw_app_cpu_boot_dev_sts1;
	gaudi2_aux_data->minor = hdev->id;
	gaudi2_aux_data->dev_mgmt_fw = !!(hdev->fw_components & FW_TYPE_BOOT_CPU);

	/* cn2accel */
	gaudi2_aux_ops->get_event_name = gaudi2_cn_get_event_name;
	gaudi2_aux_ops->poll_mem = gaudi2_cn_poll_mem;
	gaudi2_aux_ops->dma_alloc_coherent = gaudi2_cn_dma_alloc_coherent;
	gaudi2_aux_ops->dma_free_coherent = gaudi2_cn_dma_free_coherent;
	gaudi2_aux_ops->dma_pool_zalloc = gaudi2_cn_dma_pool_zalloc;
	gaudi2_aux_ops->dma_pool_free = gaudi2_cn_dma_pool_free;
	gaudi2_aux_ops->spmu_get_stats_names = hl_cn_spmu_get_stats_names;
	gaudi2_aux_ops->spmu_get_stats_event_types = hl_cn_spmu_get_stats_event_types;
	gaudi2_aux_ops->spmu_config = hl_cn_spmu_config;
	gaudi2_aux_ops->spmu_sample = hl_cn_spmu_sample;
	gaudi2_aux_ops->set_priv_assertions = hl_cn_set_priv_assertions;
	gaudi2_aux_ops->poll_reg = hl_cn_poll_reg;
	gaudi2_aux_ops->send_cpu_message = hl_cn_send_cpu_message;
	gaudi2_aux_ops->post_send_status = hl_cn_post_send_status;
	gaudi2_aux_ops->get_tx_swap_map = gaudi2_cn_get_tx_swap_map;
	gaudi2_aux_ops->device_reset = gaudi2_cn_device_reset;
}

static int gaudi2_cn_mmap(struct hl_device *hdev, u32 asid, struct vm_area_struct *vma)
{
	return -EINVAL;
}

void gaudi2_cn_compute_reset_prepare(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_cn_aux_ops *gaudi2_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

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
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi2_aux_ops = &gaudi2->cn_aux_ops;

	if (gaudi2_aux_ops->reset_late_init)
		gaudi2_aux_ops->reset_late_init(aux_dev);
}

static void gaudi2_cn_post_send_status(struct hl_device *hdev, u32 port)
{
	hl_fw_unmask_irq(hdev, GAUDI2_EVENT_CPU0_STATUS_NIC0_ENG0 + port);
}

static void gaudi2_cn_ports_stop_prepare(struct hl_device *hdev, bool fw_reset, bool in_teardown)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_cn_aux_ops *gaudi2_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi2_aux_ops = &gaudi2->cn_aux_ops;

	if (gaudi2_aux_ops->ports_stop_prepare)
		gaudi2_aux_ops->ports_stop_prepare(aux_dev, fw_reset, in_teardown);
}

static int gaudi2_cn_send_port_cpucp_status(struct hl_device *hdev, u32 port, u8 cmd, u8 period)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_cn_aux_ops *gaudi2_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi2_aux_ops = &gaudi2->cn_aux_ops;

	if (gaudi2_aux_ops->send_port_cpucp_status)
		return gaudi2_aux_ops->send_port_cpucp_status(aux_dev, port, cmd, period);

	return -ENODEV;
}

static int gaudi2_cn_dump_port_statistics(struct hl_device *hdev, u32 port, u64 str_buf_ptr,
						u64 val_buf_ptr, u32 *num_of_stat)
{
	struct gaudi2_device *gaudi = hdev->asic_specific;
	struct gaudi2_cn_aux_ops *gaudi2_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi2_aux_ops = &gaudi->cn_aux_ops;

	if (gaudi2_aux_ops->dump_port_statistics)
		return gaudi2_aux_ops->dump_port_statistics(aux_dev, port, str_buf_ptr, val_buf_ptr,
								num_of_stat);

	return -ENODEV;
}

static int gaudi2_cn_cmd_control(struct hl_device *hdev, u32 op, void *input, void *output,
					u32 asid)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_cn_aux_ops *gaudi2_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi2_aux_ops = &gaudi2->cn_aux_ops;

	if (gaudi2_aux_ops->cmd_control)
		return gaudi2_aux_ops->cmd_control(aux_dev, op, input, output, asid);

	return -ENODEV;
}

static struct hl_cn_port_funcs gaudi2_cn_port_funcs = {
	.spmu_get_stats_names = gaudi2_cn_spmu_get_stats_names,
	.spmu_get_stats_event_types = gaudi2_cn_spmu_get_stats_event_types,
	.spmu_config = gaudi2_cn_spmu_config,
	.spmu_sample = gaudi2_cn_spmu_sample,
	.post_send_status = gaudi2_cn_post_send_status,
	.ports_stop_prepare = gaudi2_cn_ports_stop_prepare,
	.send_port_cpucp_status = gaudi2_cn_send_port_cpucp_status,
	.dump_port_statistics = gaudi2_cn_dump_port_statistics,
};

struct hl_cn_funcs gaudi2_cn_funcs = {
	.get_hw_cap = gaudi2_cn_get_hw_cap,
	.set_hw_cap = gaudi2_cn_set_hw_cap,
	.pre_core_init = gaudi2_cn_pre_core_init,
	.set_cn_data = gaudi2_cn_set_cn_data,
	.mmap = gaudi2_cn_mmap,
	.cmd_control = gaudi2_cn_cmd_control,
	.port_funcs = &gaudi2_cn_port_funcs,
};
