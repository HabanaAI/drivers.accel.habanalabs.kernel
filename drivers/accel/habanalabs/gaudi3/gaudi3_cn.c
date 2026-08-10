// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021-2024 HabanaLabs, Ltd.
 * Copyright (C) 2024-2025, Intel Corporation.
 * All Rights Reserved.
 */

#include "gaudi3_cn.h"
#include "../include/hw_ip/nic/nic_general.h"
#include "uapi/drm/habanalabs_accel.h"

#define GAUDI3_DEFAULT_COLL_LAG_SIZE			0x3
#define GAUDI3_HL338_COLL_LAG_SIZE			0x6
#define GAUDI3_HL338_SCALE_OUT_COLL_LAG_SIZE		0x4
#define GAUDI3_RACKSCALE_COLL_LAG_SIZE			0x0
#define GAUDI3_RACKSCALE_SCALE_OUT_COLL_LAG_SIZE	0x18

#define MAX_NUM_OF_NIC_INTERRUPTS (GAUDI3_IRQ_NUM_NIC_PORT_LAST - GAUDI3_IRQ_NUM_NIC_PORT_FIRST + 1)

bool is_400g_mode(struct hl_device *hdev)
{
	return hdev->cn.lanes_per_port == PORT_LANES_4;
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

		return (hdev->cn.ports_mask & BIT(port1));
	}

	/* 200G mode */
	port1 = macro_idx * 2;
	port2 = port1 + 1;

	return ((hdev->cn.ports_mask & BIT(port1)) || (hdev->cn.ports_mask & BIT(port2)));
}

static int gaudi3_cn_check_oui_prefix_validity(u8 *mac_addr)
{
	u8 mac[ETH_ALEN];
	int i;

	for (i = 0; i < 3; i++)
		mac[i] = HABANALABS_MAC_OUI_1 >> (8 * (2 - i));

	if (!strncmp(mac, mac_addr, 3))
		return 1;

	for (i = 0; i < 3; i++)
		mac[i] = HABANALABS_MAC_OUI_2 >> (8 * (2 - i));

	if (!strncmp(mac, mac_addr, 3))
		return 1;

	return 0;
}

/**
 * gaudi3_cn_override_ports_masks() - Override ports masks configuration.
 * @hdev: Hl device whose external ports mask to return.
 * @serdes_type: The SerDes type that was configured for this board.
 * @ignore_fw: Flag that indicates if we ignore FW configurations.
 *
 * Return: 0 on success, negative error code otherwise.
 */
static int gaudi3_cn_override_ports_masks(struct hl_device *hdev, u32 serdes_type, bool ignore_fw)
{
	enum gaudi3_setup_type setup_type;
	u64 ports_ext_mask, ports_mask;
	int rc = 0;

	setup_type = hdev->gaudi3_setup_type;
	ports_mask = hdev->cn.ports_mask;
	ports_ext_mask = hdev->cn.ports_ext_mask;

	/* If we are running on a PCI card or 400G mode, all the ports should be set as external */
	if (is_400g_mode(hdev) || hdev->pldm) {
		ports_ext_mask = hdev->cn.ports_mask;
		goto out;
	}

	/* In case we are running on a simulator or we ignore FW information, and no setup type
	 * module param was passed we should override the ports masks both of the enable mask
	 * and the external ports mask, according to the read SerDes type from a bootstrap register.
	 * Otherwise there will be a mismatch between the default masks and the masks that represent
	 * the read SerDes type.
	 */
	if (!ignore_fw && hdev->gaudi3_setup_type == GAUDI3_SETUP_TYPE_HLS3)
		goto out;

	switch (setup_type) {
	case GAUDI3_SETUP_TYPE_HLS3:
		switch (serdes_type) {
		case HLS3_FULLSCALE_IN_SERDES_TYPE:
			ports_ext_mask = 0;
			goto out;
		case HLS3_FULLSCALE_OUT_SERDES_TYPE:
			switch (hdev->cn.card_location) {
			case 0:
				ports_ext_mask = 0xFFC000;
				goto out;
			case 1:
				ports_ext_mask = 0x000FFC;
				goto out;
			case 2:
				ports_ext_mask = 0x000FFC;
				goto out;
			case 3:
				ports_ext_mask = 0xFFC000;
				goto out;
			case 4:
				ports_ext_mask = 0x3FF000;
				goto out;
			case 5:
				ports_ext_mask = 0x0003FF;
				goto out;
			case 6:
				ports_ext_mask = 0x0003FF;
				goto out;
			case 7:
				ports_ext_mask = 0x3FF000;
				goto out;
			default:
				hl_err(hdev, "Invalid card location %u\n",
					hdev->cn.card_location);
				rc = -EINVAL;
				break;
			}

			break;
		case HLS3_FULL_OAM_3PORTS_SCALE_OUT_SERDES_TYPE:
			fallthrough;
		case HLB325_FULL_OAM_3PORTS_SCALE_OUT_SERDES_TYPE:
			switch (hdev->cn.card_location) {
			case 0:
				ports_ext_mask = 0x320000;
				goto out;
			case 1:
				ports_ext_mask = 0x000320;
				goto out;
			case 2:
				ports_ext_mask = 0x000320;
				goto out;
			case 3:
				ports_ext_mask = 0x320000;
				goto out;
			case 4:
				ports_ext_mask = 0x08C000;
				goto out;
			case 5:
				ports_ext_mask = 0x00008C;
				goto out;
			case 6:
				ports_ext_mask = 0x00008C;
				goto out;
			case 7:
				ports_ext_mask = 0x08C000;
				goto out;
			default:
				hl_err(hdev, "Invalid card location %u\n",
					hdev->cn.card_location);
				rc = -EINVAL;
				break;
			}

			break;
		case HLS3_FULL_OAM_6PORTS_SCALE_OUT_SERDES_TYPE:
			switch (hdev->cn.card_location) {
			case 0:
				ports_ext_mask = 0x374000;
				goto out;
			case 1:
				ports_ext_mask = 0x000374;
				goto out;
			case 2:
				ports_ext_mask = 0x000374;
				goto out;
			case 3:
				ports_ext_mask = 0x374000;
				goto out;
			case 4:
				ports_ext_mask = 0x1DC000;
				goto out;
			case 5:
				ports_ext_mask = 0x0001DC;
				goto out;
			case 6:
				ports_ext_mask = 0x0001DC;
				goto out;
			case 7:
				ports_ext_mask = 0x1DC000;
				goto out;
			default:
				hl_err(hdev, "Invalid card location %u\n",
					hdev->cn.card_location);
				rc = -EINVAL;
				break;
			}

			break;
		case HLS3_SINGLEPORT_OAM_FULLSCALE_OUT_SERDES_TYPE:
			switch (hdev->cn.card_location) {
			case 0:
				ports_ext_mask = 0x320000;
				goto out;
			case 1:
				ports_ext_mask = 0x000320;
				goto out;
			case 2:
				ports_ext_mask = 0x000320;
				goto out;
			case 3:
				ports_ext_mask = 0x320000;
				goto out;
			case 4:
				ports_ext_mask = 0x08C000;
				goto out;
			case 5:
				ports_ext_mask = 0x00008C;
				goto out;
			case 6:
				ports_ext_mask = 0x00008C;
				goto out;
			case 7:
				ports_ext_mask = 0x08C000;
				goto out;
			default:
				hl_err(hdev, "Invalid card location %u\n",
					hdev->cn.card_location);
				rc = -EINVAL;
				break;
			}

			break;
		case HL338_SERDES_TYPE:
			/* Ports 0,1 are disabled, so need to update also the ports_mask */
			ports_mask = 0xFFFFFC;
			/* In all cards, ports 8-11 are external */
			ports_ext_mask = 0xF00;
			goto out;
		case GAUDI3_RACK_SERDES_TYPE:
			ports_ext_mask = hdev->cn.ports_mask;
			break;
		case GAUDI3_RACK_WHITEBOX_SERDES_TYPE:
			ports_ext_mask = hdev->cn.ports_mask;
			break;
		default:
			hl_err(hdev, "Invalid serdes_type %u\n", serdes_type);
			rc = -EINVAL;
			break;
		}

		break;
	case GAUDI3_SETUP_TYPE_HL325_S_EXT_LB:
		/* For the above setup types, all the ports should be set as external */
		ports_ext_mask = hdev->cn.ports_mask;
		goto out;
	case GAUDI3_SETUP_TYPE_HLB325:
		switch (hdev->cn.card_location) {
		case 0:
			ports_ext_mask = 0x320000;
			goto out;
		case 1:
			ports_ext_mask = 0x000320;
			goto out;
		case 2:
			ports_ext_mask = 0x000320;
			goto out;
		case 3:
			ports_ext_mask = 0x320000;
			goto out;
		case 4:
			ports_ext_mask = 0x08C000;
			goto out;
		case 5:
			ports_ext_mask = 0x00008C;
			goto out;
		case 6:
			ports_ext_mask = 0x00008C;
			goto out;
		case 7:
			ports_ext_mask = 0x08C000;
			goto out;
		default:
			hl_err(hdev, "Invalid card location %u\n", hdev->cn.card_location);
			rc = -EINVAL;
			break;
		}

		break;
	case GAUDI3_SETUP_TYPE_HL338:
		/* Ports 0,1 are disabled, so need to update also the ports_mask */
		ports_mask = 0xFFFFFC;
		/* In all cards, ports 8-11 are external */
		ports_ext_mask = 0xF00;
		goto out;
	case GAUDI3_SETUP_TYPE_HL338_S_EXT_LB:
		ports_mask = 0xFFFFFF;
		/* All the enabled ports should be set as external */
		ports_ext_mask = hdev->cn.ports_mask;
		goto out;
	case GAUDI3_SETUP_TYPE_RACK:
		ports_ext_mask = hdev->cn.ports_mask;
		goto out;
	case GAUDI3_SETUP_TYPE_RACK_WHITEBOX:
		ports_ext_mask = hdev->cn.ports_mask;
		goto out;
	default:
		hl_err(hdev, "Invalid gaudi3_setup_type %u\n", hdev->gaudi3_setup_type);
		rc = -EINVAL;
		break;
	}

out:
	if (!rc) {
		hdev->cn.ports_mask = ports_mask;
		hdev->cn.ports_ext_mask = ports_ext_mask;
	}

	return rc;
}

int gaudi3_cn_set_info(struct hl_device *hdev, bool get_from_fw)
{
	struct hbl_cn_cpucp_info *cn_cpucp_info = &hdev->asic_prop.cn_props.cpucp_info;
	struct cpucp_info *cpucp_info = &hdev->asic_prop.cpucp_info;
	struct hbl_cn_cpucp_mac_addr *mac_arr = cn_cpucp_info->mac_addrs;
	struct hl_cn *cn = &hdev->cn;
	u32 card_location, serdes_type = MAX_NUM_SERDES_TYPE;
	u8 mac[ETH_ALEN], *mac_addr;
	int rc, i;

	/* copy the MAC OUI in reverse */
	for (i = 0; i < 3; i++)
		mac[i] = HABANALABS_MAC_OUI_1 >> (8 * (2 - i));

	if (get_from_fw) {
		rc = hl_cn_cpucp_info_get(hdev);
		if (rc)
			return rc;

		if (hdev->pci_rev_id_override) {
			hl_dbg(hdev,
				"skipping NIC FW ports info with an overridden pci revision id\n");
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
		for (i = 0; i < NIC_NUMBER_OF_PORTS; i++) {
			if (!(hdev->cn.ports_mask & BIT(i)))
				continue;

			mac_addr = mac_arr[i].mac_addr;
			if (!gaudi3_cn_check_oui_prefix_validity(mac_addr)) {
				if (hdev->ignore_eeprom_errors) {
					hl_dbg(hdev,
						"bad MAC OUI %pM, port %d - setting a valid MAC\n",
						mac_addr, i);
					mac[ETH_ALEN - 1] = i;
					memcpy(mac_addr, mac, ETH_ALEN);
				} else {
					hl_warn(hdev, "unrecognized MAC OUI %pM, port %d\n",
						 mac_addr, i);
				}
			}
		}

		cn->card_location = le32_to_cpu(cpucp_info->card_location);
		cn->use_fw_serdes_info = hdev->gaudi3_setup_type == GAUDI3_SETUP_TYPE_HLS3;
	} else {
		/* No F/W, hence need to set the MACs manually (randomize) */
		get_random_bytes(&mac[3], 2);

		for (i = 0; i < NIC_NUMBER_OF_PORTS; i++) {
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
			if (hdev->ignore_fw_nic_info) {
				serdes_type = hdev->serdes_type;
				cn->card_location = hdev->card_location_override;
			} else {
				hl_warn(hdev,
					 "can't read card location as FW security is enabled\n");
			}
		}
	}

	switch (serdes_type) {
	case HLS3_FULLSCALE_IN_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI3_HLS3_FULLSCALE_IN;
		break;
	case HLS3_FULLSCALE_OUT_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI3_HLS3_FULLSCALE_OUT;
		break;
	case HLS3_FULL_OAM_3PORTS_SCALE_OUT_SERDES_TYPE:
		fallthrough;
	case HLB325_FULL_OAM_3PORTS_SCALE_OUT_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI3_HLS3_FULL_OAM_3PORTS_SCALE_OUT;
		break;
	case HLS3_FULL_OAM_6PORTS_SCALE_OUT_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI3_HLS3_FULL_OAM_6PORTS_SCALE_OUT;
		break;
	case HLS3_SINGLEPORT_OAM_FULLSCALE_OUT_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI3_HLS3_SINGLEPORT_OAM_FULLSCALE_OUT;
		break;
	case HL338_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI3_HL338;
		break;
	case GAUDI3_RACK_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI3_RACK;
		/* TODO - SW-220228 : define this param based on feature bitmask received from FW */
		cn_cpucp_info->use_taps_from_fw = true;
		break;
	case GAUDI3_RACK_WHITEBOX_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI3_RACK_WHITEBOX;
		/* TODO - SW-220228 : define this param based on feature bitmask received from FW */
		cn_cpucp_info->use_taps_from_fw = true;
		break;
	default:
		hdev->asic_prop.server_type = HL_SERVER_TYPE_UNKNOWN;
		/* pldm needs to be verified since not handled by pldm FW */
		if (get_from_fw && !hdev->pldm) {
			hl_err(hdev, "bad SerDes type %d\n", serdes_type);
			return -EFAULT;
		}
		break;
	}

	/* As there are several code flows which can require us to modify the ports masks, whether
	 * for debug purposes or when working without FW, we might need to override the ports
	 * masks, both of the enable ports and the external ports.
	 */
	rc = gaudi3_cn_override_ports_masks(hdev, serdes_type, !get_from_fw);
	if (rc)
		return rc;

	if (is_400g_mode(hdev) || hdev->gaudi3_setup_type != GAUDI3_SETUP_TYPE_HLS3)
		hdev->cn.auto_neg_mask &= ~hdev->cn.ports_ext_mask;

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

static int gaudi3_cn_irq_vector(struct hbl_aux_dev *aux_dev, unsigned int nr)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hl_irq_vector(hdev, nr);
}

static void gaudi3_cn_axuser_hbw_mmu_bp_set(struct hbl_aux_dev *aux_dev, u32 axuser_hbw_reg_base,
					    bool bypass)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	gaudi3_axuser_hbw_mmu_bp_set(hdev, axuser_hbw_reg_base, bypass);
}

static bool gaudi3_cn_is_preboot_fw_enabled(struct hbl_aux_dev *aux_dev)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hdev->fw_components & FW_TYPE_PREBOOT_CPU;
}

static bool gaudi3_cn_is_full_fw_enabled(struct hbl_aux_dev *aux_dev)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hdev->fw_components & FW_TYPE_BOOT_CPU;
}

static bool gaudi3_cn_is_fw_security_enabled(struct hbl_aux_dev *aux_dev)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hdev->asic_prop.fw_security_enabled;
}

static void *gaudi3_cn_dma_alloc_coherent(struct hbl_aux_dev *aux_dev, size_t size,
					  dma_addr_t *dma_handle, gfp_t flag)
{
	return hl_cn_dma_alloc_coherent(aux_dev, size, dma_handle, flag);
}

static void gaudi3_cn_dma_free_coherent(struct hbl_aux_dev *aux_dev, size_t size, void *cpu_addr,
					dma_addr_t dma_handle)
{
	hl_cn_dma_free_coherent(aux_dev, size, cpu_addr, dma_handle);
}

static void *gaudi3_cn_dma_pool_zalloc(struct hbl_aux_dev *aux_dev, size_t size, gfp_t mem_flags,
				       dma_addr_t *dma_handle)
{
	return hl_cn_dma_pool_zalloc(aux_dev, size, mem_flags, dma_handle);
}

static void gaudi3_cn_dma_pool_free(struct hbl_aux_dev *aux_dev, void *vaddr, dma_addr_t dma_addr)
{
	hl_cn_dma_pool_free(aux_dev, vaddr, dma_addr);
}

static void gaudi3_cn_device_reset(struct hbl_aux_dev *aux_dev)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	hl_device_cond_reset(hdev, HL_DRV_RESET_HARD, HL_NOTIFIER_EVENT_DEVICE_RESET);
}

static void gaudi3_cn_set_cn_data(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_cn_aux_data *gaudi3_aux_data;
	struct gaudi3_cn_aux_ops *gaudi3_aux_ops;
	struct hbl_cn_aux_ops *aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi3_aux_data = &gaudi3->cn_aux_data;
	cn->asic_specific_dev_info = gaudi3_aux_data;
	gaudi3_aux_ops = &gaudi3->cn_aux_ops;
	aux_ops = aux_dev->aux_ops;
	aux_ops->asic_ops = gaudi3_aux_ops;

	gaudi3_aux_data->num_of_hdcores = prop->num_of_hdcores;

	switch (hdev->asic_prop.server_type) {
	case HL_SERVER_GAUDI3_HL338:
		gaudi3_aux_data->coll_lag_size = GAUDI3_HL338_COLL_LAG_SIZE;
		gaudi3_aux_data->scale_out_coll_lag_size = GAUDI3_HL338_SCALE_OUT_COLL_LAG_SIZE;
		break;
	case HL_SERVER_GAUDI3_RACK:
	case HL_SERVER_GAUDI3_RACK_WHITEBOX:
		gaudi3_aux_data->coll_lag_size = GAUDI3_RACKSCALE_COLL_LAG_SIZE;
		gaudi3_aux_data->scale_out_coll_lag_size = GAUDI3_RACKSCALE_SCALE_OUT_COLL_LAG_SIZE;
		break;
	default:
		gaudi3_aux_data->coll_lag_size = GAUDI3_DEFAULT_COLL_LAG_SIZE;
		gaudi3_aux_data->scale_out_coll_lag_size = GAUDI3_DEFAULT_COLL_LAG_SIZE;
	}

	gaudi3_aux_data->enable_h9_rx_drop_eco = hdev->nic_enable_h9_rx_drop_eco;
	gaudi3_aux_data->setup_type = hdev->gaudi3_setup_type;
	gaudi3_aux_data->vendor_part_id = prop->pci_id;
	gaudi3_aux_data->kernel_asid = HL_KERNEL_ASID_ID;
	gaudi3_aux_data->card_location = hdev->ignore_fw_nic_info ? hdev->card_location_override :
								    cn->card_location;
	gaudi3_aux_data->minor = hdev->id;
	gaudi3_aux_data->dev_mgmt_fw = !!(hdev->fw_components & FW_TYPE_BOOT_CPU);
	gaudi3_aux_data->cpucp_checkers_shift = NIC_CHECKERS_CHECK_SHIFT;
	gaudi3_aux_data->num_of_dies = prop->num_of_dies;

	gaudi3_aux_ops->irq_vector = gaudi3_cn_irq_vector;
	gaudi3_aux_ops->get_bfe_status = gaudi3_get_bfe_status;
	gaudi3_aux_ops->axuser_hbw_mmu_bp_set = gaudi3_cn_axuser_hbw_mmu_bp_set;
	gaudi3_aux_ops->is_preboot_fw_enabled = gaudi3_cn_is_preboot_fw_enabled;
	gaudi3_aux_ops->is_full_fw_enabled = gaudi3_cn_is_full_fw_enabled;
	gaudi3_aux_ops->is_fw_security_enabled = gaudi3_cn_is_fw_security_enabled;
	gaudi3_aux_ops->dma_alloc_coherent = gaudi3_cn_dma_alloc_coherent;
	gaudi3_aux_ops->dma_free_coherent = gaudi3_cn_dma_free_coherent;
	gaudi3_aux_ops->dma_pool_zalloc = gaudi3_cn_dma_pool_zalloc;
	gaudi3_aux_ops->dma_pool_free = gaudi3_cn_dma_pool_free;
	gaudi3_aux_ops->spmu_get_stats_names = hl_cn_spmu_get_stats_names;
	gaudi3_aux_ops->spmu_get_stats_event_types = hl_cn_spmu_get_stats_event_types;
	gaudi3_aux_ops->spmu_config = hl_cn_spmu_config;
	gaudi3_aux_ops->spmu_sample = hl_cn_spmu_sample;
	gaudi3_aux_ops->set_priv_assertions = hl_cn_set_priv_assertions;
	gaudi3_aux_ops->poll_reg = hl_cn_poll_reg;
	gaudi3_aux_ops->send_cpu_message = hl_cn_send_cpu_message;
	gaudi3_aux_ops->post_send_status = hl_cn_post_send_status;
	gaudi3_aux_ops->device_reset = gaudi3_cn_device_reset;
}

static int gaudi3_cn_mmap(struct hl_device *hdev, u32 asid, struct vm_area_struct *vma)
{
	return -EINVAL;
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

#ifdef HL_DOWNSTREAM
	/* The CPU_IF_NIC registers are handled only by the privileged embedded code and in any
	 * case, this is relevant only on PLDM where the PHY link is always ON
	 */
	if (hdev->pldm)
		gaudi3_disable_nic_interrupts_cpu_if(hdev);
#endif /* HL_DOWNSTREAM */

	/* Disable interrupts of all NICs */
	for (i = 0; i < NIC_NUMBER_OF_MACROS; i++) {
		/* skip non-present macros in pldm as we may run on partial-nics image */
		if (hdev->pldm && !(hdev->cn.ports_mask & gaudi3_cn_get_macro_ports_mask(hdev, i)))
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

	hl_dbg(hdev, "Quiescence the NICs\n");

	gaudi3_cn_disable_nics_interrupts(hdev);
}

u32 gaudi3_cn_handle_bmon_spmu_event(struct hl_device *hdev)
{
	/* We're not supposed to get this event, however it should be safe to ignore */
	hl_dbg_ratelimited(hdev, "Got an SPI BMON SPMU event");
	return 0;
}

void gaudi3_cn_compute_reset_prepare(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_cn_aux_ops *gaudi3_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi3_aux_ops = &gaudi3->cn_aux_ops;

	if (gaudi3_aux_ops->reset_prepare)
		gaudi3_aux_ops->reset_prepare(aux_dev);
}

void gaudi3_cn_compute_reset_late_init(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_cn_aux_ops *gaudi3_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi3_aux_ops = &gaudi3->cn_aux_ops;

	if (gaudi3_aux_ops->reset_late_init)
		gaudi3_aux_ops->reset_late_init(aux_dev);
}

static void gaudi3_cn_post_send_status(struct hl_device *hdev, u32 port)
{
	/* FW does not mask MSG interrupts, so unmask_irq is not needed */
}

static void gaudi3_cn_ports_stop_prepare(struct hl_device *hdev, bool hard_reset, bool in_teardown)
{
	struct gaudi3_device *gaudi = hdev->asic_specific;
	struct gaudi3_cn_aux_ops *gaudi3_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi3_aux_ops = &gaudi->cn_aux_ops;

	if (gaudi3_aux_ops->ports_stop_prepare)
		gaudi3_aux_ops->ports_stop_prepare(aux_dev, hard_reset, in_teardown);
}

static int gaudi3_cn_send_port_cpucp_status(struct hl_device *hdev, u32 port, u8 cmd, u8 period)
{
	struct gaudi3_device *gaudi = hdev->asic_specific;
	struct gaudi3_cn_aux_ops *gaudi3_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi3_aux_ops = &gaudi->cn_aux_ops;

	if (gaudi3_aux_ops->send_port_cpucp_status)
		return gaudi3_aux_ops->send_port_cpucp_status(aux_dev, port, cmd, period);

	return -ENODEV;
}

static int gaudi3_cn_dump_port_statistics(struct hl_device *hdev, u32 port, u64 str_buf_ptr,
					  u64 val_buf_ptr, u32 *num_of_stat)
{
	struct gaudi3_device *gaudi = hdev->asic_specific;
	struct gaudi3_cn_aux_ops *gaudi3_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi3_aux_ops = &gaudi->cn_aux_ops;

	if (gaudi3_aux_ops->dump_port_statistics)
		return gaudi3_aux_ops->dump_port_statistics(aux_dev, port, str_buf_ptr, val_buf_ptr,
							    num_of_stat);

	return -ENODEV;
}

static int gaudi3_reserve_irqs(struct hl_device *hdev, u32 num, int *base_irq)
{
	if (num > MAX_NUM_OF_NIC_INTERRUPTS) {
		hl_err(hdev, "CN requested %u interrupts but only %u may be reserved\n",
			num, MAX_NUM_OF_NIC_INTERRUPTS);
		return -EINVAL;
	}

	*base_irq = GAUDI3_IRQ_NUM_NIC_PORT_FIRST;

	return 0;
}

static int gaudi3_cn_cmd_control(struct hl_device *hdev, u32 op, void *input, void *output,
				 u32 asid)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_cn_aux_ops *gaudi3_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi3_aux_ops = &gaudi3->cn_aux_ops;

	if (gaudi3_aux_ops->cmd_control)
		return gaudi3_aux_ops->cmd_control(aux_dev, op, input, output, asid);

	return -ENODEV;
}

static struct hl_cn_port_funcs gaudi3_cn_port_funcs = {
	.spmu_get_stats_names = gaudi3_cn_spmu_get_stats_names,
	.spmu_get_stats_event_types = gaudi3_cn_spmu_get_stats_event_types,
	.spmu_config = gaudi3_cn_spmu_config,
	.spmu_sample = gaudi3_cn_spmu_sample,
	.post_send_status = gaudi3_cn_post_send_status,
	.ports_stop_prepare = gaudi3_cn_ports_stop_prepare,
	.send_port_cpucp_status = gaudi3_cn_send_port_cpucp_status,
	.dump_port_statistics = gaudi3_cn_dump_port_statistics,
};

struct hl_cn_funcs gaudi3_cn_funcs = {
	.get_hw_cap = gaudi3_cn_get_hw_cap,
	.set_hw_cap = gaudi3_cn_set_hw_cap,
	.pre_core_init = gaudi3_cn_pre_core_init,
	.set_cn_data = gaudi3_cn_set_cn_data,
	.mmap = gaudi3_cn_mmap,
	.reserve_irqs = gaudi3_reserve_irqs,
	.cmd_control = gaudi3_cn_cmd_control,
	.port_funcs = &gaudi3_cn_port_funcs,
};
