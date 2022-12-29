// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "nic.h"
#include "../common/habanalabs.h"
#include "../include/common/pci_ids.h"
#ifdef _HAS_CHECK_ADD_OVERFLOW
#include <linux/overflow.h>
#endif
#include <linux/file.h>

#define NIC_PCS_FAIL_TIME_FRAME_SEC	(60 * 5) /* 5 minutes */
#define NIC_PCS_FAIL_THRESHOLD		8
#define NIC_MIN_WQS_PER_PORT		2
#define NIC_MIN_COLL_WQS_PER_PORT	1

#define HL_NIC_IPv4_PROTOCOL_UDP	17

/* SOB mask is not expected to change across ASIC. Hence common defines. */
#define NIC_SOB_INC_MASK		0x80000000
#define NIC_SOB_VAL_MASK		0x7fff

#define RAND_STAT_CNT(cnt) \
	do { \
		u32 __tmp = (u32)get_random_int(); \
		(cnt) = cpu_to_le32(__tmp); \
		dev_info(hdev->dev, "port %d, %s: %u\n", port, #cnt, __tmp); \
	} while (0)

#define HL_AUX2NIC(aux_dev)	((aux_dev->type == HL_AUX_DEV_ETH) ? \
				container_of(aux_dev, struct hl_nic, en_aux_dev) : \
				container_of(aux_dev, struct hl_nic, ib_aux_dev))

struct hl_en_stat hl_nic_mac_fec_stats[] = {
	{"correctable_errors", 0x2, 0x3},
	{"uncorrectable_errors", 0x4, 0x5}
};

struct hl_en_stat hl_nic_mac_stats_rx[] = {
	{"etherStatsOctets", 0x0},
	{"OctetsReceivedOK", 0x4},
	{"aAlignmentErrors", 0x8},
	{"aPAUSEMACCtrlFramesReceived", 0xC},
	{"aFrameTooLongErrors", 0x10},
	{"aInRangeLengthErrors", 0x14},
	{"aFramesReceivedOK", 0x18},
	{"aFrameCheckSequenceErrors", 0x1C},
	{"VLANReceivedOK", 0x20},
	{"ifInErrors", 0x24},
	{"ifInUcastPkts", 0x28},
	{"ifInMulticastPkts", 0x2C},
	{"ifInBroadcastPkts", 0x30},
	{"etherStatsDropEvents", 0x34},
	{"etherStatsPkts", 0x38},
	{"etherStatsUndersizePkts", 0x3C},
	{"etherStatsPkts64Octets", 0x40},
	{"etherStatsPkts65to127Octets", 0x44},
	{"etherStatsPkts128to255Octets", 0x48},
	{"etherStatsPkts256to511Octets", 0x4C},
	{"etherStatsPkts512to1023Octets", 0x50},
	{"etherStatsPkts1024to1518Octets", 0x54},
	{"etherStatsPkts1519toMaxOctets", 0x58},
	{"etherStatsOversizePkts", 0x5C},
	{"etherStatsJabbers", 0x60},
	{"etherStatsFragments", 0x64},
	{"aCBFCPAUSEFramesReceived_0", 0x68},
	{"aCBFCPAUSEFramesReceived_1", 0x6C},
	{"aCBFCPAUSEFramesReceived_2", 0x70},
	{"aCBFCPAUSEFramesReceived_3", 0x74},
	{"aCBFCPAUSEFramesReceived_4", 0x78},
	{"aCBFCPAUSEFramesReceived_5", 0x7C},
	{"aCBFCPAUSEFramesReceived_6", 0x80},
	{"aCBFCPAUSEFramesReceived_7", 0x84},
	{"aMACControlFramesReceived", 0x88}
};

struct hl_en_stat hl_nic_mac_stats_tx[] = {
	{"etherStatsOctets", 0x0},
	{"OctetsTransmittedOK", 0x4},
	{"aPAUSEMACCtrlFramesTransmitted", 0x8},
	{"aFramesTransmittedOK", 0xC},
	{"VLANTransmittedOK", 0x10},
	{"ifOutErrors", 0x14},
	{"ifOutUcastPkts", 0x18},
	{"ifOutMulticastPkts", 0x1C},
	{"ifOutBroadcastPkts", 0x20},
	{"etherStatsPkts64Octets", 0x24},
	{"etherStatsPkts65to127Octets", 0x28},
	{"etherStatsPkts128to255Octets", 0x2C},
	{"etherStatsPkts256to511Octets", 0x30},
	{"etherStatsPkts512to1023Octets", 0x34},
	{"etherStatsPkts1024to1518Octets", 0x38},
	{"etherStatsPkts1519toMaxOctets", 0x3C},
	{"aCBFCPAUSEFramesTransmitted_0", 0x40},
	{"aCBFCPAUSEFramesTransmitted_1", 0x44},
	{"aCBFCPAUSEFramesTransmitted_2", 0x48},
	{"aCBFCPAUSEFramesTransmitted_3", 0x4C},
	{"aCBFCPAUSEFramesTransmitted_4", 0x50},
	{"aCBFCPAUSEFramesTransmitted_5", 0x54},
	{"aCBFCPAUSEFramesTransmitted_6", 0x58},
	{"aCBFCPAUSEFramesTransmitted_7", 0x5C},
	{"aMACControlFramesTransmitted", 0x60},
	{"etherStatsPkts", 0x64}
};

static const char pcs_counters_str[][ETH_GSTRING_LEN] = {
	{"pcs_local_faults"},
	{"pcs_remote_faults"},
	{"pcs_remote_fault_reconfig"},
	{"pcs_link_restores"},
};

static size_t pcs_counters_str_len = ARRAY_SIZE(pcs_counters_str);
size_t hl_nic_mac_fec_stats_len = ARRAY_SIZE(hl_nic_mac_fec_stats);
size_t hl_nic_mac_stats_rx_len = ARRAY_SIZE(hl_nic_mac_stats_rx);
size_t hl_nic_mac_stats_tx_len = ARRAY_SIZE(hl_nic_mac_stats_tx);

static void qps_stop(struct hl_device *hdev);
static void qp_destroy_work(struct work_struct *work);
static int __user_wq_arr_unset(struct hl_nic_port *nic_port, u32 type, struct hl_ctx *ctx);
static void user_cq_destroy(struct kref *kref);
static void set_app_params_clear(struct hl_device *hdev);
static int hl_nic_ib_cmd_ctrl(struct hl_aux_dev *aux_dev, void *core_ctx, u32 op, void *input,
				void *output);
static int hl_nic_ib_mmap(struct hl_aux_dev *aux_dev, void *core_ctx,
				struct vm_area_struct *vma);
static void wq_arrays_pool_destroy(struct hl_ctx *ctx);

int hl_nic_read_spmu_counters(struct hl_nic_port *nic_port, u64 out_data[], u32 *num_out_data)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_en_stat *ignore;
	int rc;

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;

	port_funcs->spmu_get_stats_info(nic_port, &ignore, num_out_data);

	/* this function can be called from ethtool, get_statistics ioctl and nic_status thread */
	mutex_lock(&nic_port->cnt_lock);
	rc = port_funcs->spmu_sample(nic_port, *num_out_data, out_data);
	mutex_unlock(&nic_port->cnt_lock);

	return rc;
}

static int __hl_nic_get_cnts_num(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;

	return pcs_counters_str_len +
		hdev->asic_funcs->nic_funcs->port_funcs->get_cnts_num(nic_port);
}

static void __hl_nic_get_cnts_names(struct hl_nic_port *nic_port, u8 *data)
{
	struct hl_device *hdev = nic_port->hdev;
	int i;

	for (i = 0 ; i < pcs_counters_str_len ; i++)
		memcpy(data + i * ETH_GSTRING_LEN, pcs_counters_str[i], ETH_GSTRING_LEN);
	data += i * ETH_GSTRING_LEN;

	hdev->asic_funcs->nic_funcs->port_funcs->get_cnts_names(nic_port, data);
}

static void __hl_nic_get_cnts_values(struct hl_nic_port *nic_port, u64 *data)
{
	struct hl_device *hdev = nic_port->hdev;

	data[0] = nic_port->pcs_local_fault_cnt;
	data[1] = nic_port->pcs_remote_fault_cnt;
	data[2] = nic_port->pcs_remote_fault_reconfig_cnt;
	data[3] = nic_port->pcs_link_restore_cnt;
	data += pcs_counters_str_len;

	hdev->asic_funcs->nic_funcs->port_funcs->get_cnts_values(nic_port, data);
}

static int __hl_nic_port_hw_init(struct hl_nic_port *nic_port)
{
	return nic_port->hdev->asic_funcs->nic_funcs->port_funcs->port_hw_init(nic_port);
}

static void __hl_nic_port_hw_fini(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;

	cancel_work_sync(&nic_port->nic_status_work);

	/* in hard reset the QPs were stopped by hl_nic_stop called from halt engines */
	if (!hdev->reset_info.hard_reset_pending)
		hl_nic_qps_stop(nic_port);

	nic_funcs->port_funcs->port_hw_fini(nic_port);
}

static bool hl_nic_device_operational(struct hl_aux_dev *aux_dev)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);

	return hl_device_operational(hdev, NULL);
}

static void hl_nic_hw_access_lock(struct hl_aux_dev *aux_dev)
	__acquires(&hdev->nic_hw_access_lock)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);

	mutex_lock(&hdev->nic_hw_access_lock);
}

static void hl_nic_hw_access_unlock(struct hl_aux_dev *aux_dev)
	__releases(&hdev->nic_hw_access_lock)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);

	mutex_unlock(&hdev->nic_hw_access_lock);
}

static bool hl_nic_is_eth_lpbk(struct hl_aux_dev *aux_dev)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);

	return hdev->nic.eth_loopback;
}

static int hl_nic_port_hw_init(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	return __hl_nic_port_hw_init(nic_port);
}

static void hl_nic_port_hw_fini(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	__hl_nic_port_hw_fini(nic_port);
}

static int hl_nic_phy_port_init(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	return hdev->asic_funcs->nic_funcs->port_funcs->phy_port_init(nic_port);
}

static void hl_nic_phy_port_fini(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	hdev->asic_funcs->nic_funcs->port_funcs->phy_port_fini(nic_port);
}

static int hl_nic_set_pfc(struct hl_aux_dev *aux_dev, u32 port, bool enable)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	nic_port->pfc_enable = enable;

	return hdev->asic_funcs->nic_funcs->port_funcs->set_pfc(nic_port);
}

static int hl_nic_get_cnts_num(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	return __hl_nic_get_cnts_num(nic_port);
}

static void hl_nic_get_cnts_names(struct hl_aux_dev *aux_dev, u32 port, u8 *data)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	__hl_nic_get_cnts_names(nic_port, data);
}

static void hl_nic_get_cnts_values(struct hl_aux_dev *aux_dev, u32 port, u64 *data)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	__hl_nic_get_cnts_values(nic_port, data);
}

static bool hl_nic_get_mac_lpbk(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	return nic_port->mac_loopback;
}

static int hl_nic_set_mac_lpbk(struct hl_aux_dev *aux_dev, u32 port, bool enable)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	if (atomic_read(&nic_port->num_of_allocated_qps)) {
		dev_dbg(hdev->dev,
			"There are active QPs under this port - Can't %s mac loopback\n",
			enable ? "enable" : "disable");
		return -EBUSY;
	}

	nic_port->mac_loopback = enable;

	if (enable)
		nic->mac_loopback |= BIT(port);
	else
		nic->mac_loopback &= ~BIT(port);

	return 0;
}

static int hl_nic_update_mtu(struct hl_aux_dev *aux_dev, u32 port, u32 mtu)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];
	struct hl_nic_port_funcs *port_funcs;
	struct hl_qp *qp;
	int rc = 0;
	u32 qp_id;

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	mtu += HL_EN_MAX_HEADERS_SZ;

	port_funcs->cfg_lock(nic_port);
	idr_for_each_entry(&nic_port->qp_ids, qp, qp_id) {
		if (qp->mtu_type == MTU_FROM_NETDEV && qp->mtu != mtu) {
			rc = hdev->asic_funcs->nic_funcs->port_funcs->update_qp_mtu(nic_port, qp,
					mtu);
			if (rc) {
				dev_err(hdev->dev, "Failed to update MTU, port: %d, qpn: %d, %d\n",
					port, qp_id, rc);
				break;
			}
		}
	}
	port_funcs->cfg_unlock(nic_port);

	return rc;
}

static int hl_nic_qpc_write(struct hl_aux_dev *aux_dev, u32 port, void *qpc,
				struct qpc_mask *qpc_mask, u32 qpn, bool is_req)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];
	struct hl_nic_port_funcs *port_funcs;
	int rc;

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;

	port_funcs->cfg_lock(nic_port);
	rc = hdev->asic_funcs->nic_funcs->port_funcs->qpc_write(nic_port, qpc, qpc_mask, qpn,
									is_req);
	port_funcs->cfg_unlock(nic_port);

	return rc;
}

static void hl_nic_ctrl_lock(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	mutex_lock(&nic_port->control_lock);
}

static void hl_nic_ctrl_unlock(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	mutex_unlock(&nic_port->control_lock);
}

static int hl_nic_dispatcher_register_qp(struct hl_aux_dev *aux_dev, u32 port, u32 asid,
						u32 qp_id)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	return hl_nic_eq_dispatcher_register_qp(nic_port, asid, qp_id);
}

static int hl_nic_dispatcher_unregister_qp(struct hl_aux_dev *aux_dev, u32 port, u32 qp_id)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	return hl_nic_eq_dispatcher_unregister_qp(nic_port, qp_id);
}

static u32 hl_nic_get_speed(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);

	return nic->nic_ports[port].speed;
}

static int hl_nic_get_asic_type(struct hl_device *hdev, enum hl_nic_asic_type *asic_type)
{
	switch (hdev->asic_type) {
	case ASIC_GAUDI_SIM:
	case ASIC_GAUDI_HL2000M_SIM:
	case ASIC_GAUDI:
	case ASIC_GAUDI_SEC:
	case ASIC_GAUDI_HL2000M:
	case ASIC_GAUDI_HL2000M_SEC:
		*asic_type = HL_ASIC_GAUDI;
		break;
	case ASIC_GAUDI2_SIM:
	case ASIC_GAUDI2B_SIM:
	case ASIC_GAUDI2_SIM_ARC:
	case ASIC_GAUDI2B_SIM_ARC:
	case ASIC_GAUDI2:
	case ASIC_GAUDI2B:
	case ASIC_GAUDI2_FPGA:
		*asic_type = HL_ASIC_GAUDI2;
		break;
	case ASIC_GAUDI3:
	case ASIC_GAUDI3_SINGLE_DIE:
	case ASIC_GAUDI3_SIM:
	case ASIC_GAUDI3_SIM_ARC:
	case ASIC_GAUDI3_SIM_SINGLE_DIE:
	case ASIC_GAUDI3_SIM_SINGLE_DIE_ARC:
	case ASIC_GAUDI3_FPGA:
		*asic_type = HL_ASIC_GAUDI3;
		break;
	default:
		dev_err(hdev->dev, "Unrecognized ASIC type %d\n", hdev->asic_type);
		return -EINVAL;
	}

	return 0;
}


void hl_nic_reset_core_mac_stats(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct hl_nic_port_funcs *port_funcs;

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;

	port_funcs->reset_mac_stats(nic_port);
}

static int hl_nic_en_aux_data_init(struct hl_device *hdev)
{
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	struct asic_fixed_properties *asic_props = &hdev->asic_prop;
	struct hl_nic_properties *nic_props;
	struct hl_en_core_info *core_info;
	struct hl_nic *nic = &hdev->nic;
	struct hl_en_aux_ops *aux_ops;
	struct hl_aux_dev *aux_dev;
	char **mac_addr;
	int rc, i;

	aux_dev = &nic->en_aux_dev;
	aux_dev->type = HL_AUX_DEV_ETH;
	core_info = aux_dev->core_info;
	aux_ops = aux_dev->aux_ops;
	nic_props = &asic_props->nic_props;

	core_info->pdev = hdev->pdev;
	core_info->dev = hdev->dev;
	core_info->driver_ver = hdev->driver_ver;
	core_info->ports_mask = nic->eth_ports_mask;
	core_info->auto_neg_mask = nic->auto_neg_mask;
	core_info->max_num_of_ports = nic_props->max_num_of_ports;
	core_info->minor = hdev->id;
	core_info->fw_ver = asic_props->cpucp_info.cpucp_version;
	core_info->qsfp_eeprom = hdev->asic_prop.cpucp_nic_info.qsfp_eeprom;
	core_info->sb_base_addr = nic_props->sb_base_addr;
	core_info->sb_base_size = nic_props->sb_base_size;
	core_info->swq_base_addr = nic_props->swq_base_addr;
	core_info->swq_base_size = nic_props->swq_base_size;
	core_info->pending_reset_long_timeout = HL_PENDING_RESET_LONG_SEC;
	core_info->max_frm_len = nic_props->max_frm_len;
	core_info->raw_elem_size = nic_props->raw_elem_size;
	core_info->max_raw_mtu = nic_props->max_raw_mtu;
	core_info->min_raw_mtu = nic_props->min_raw_mtu;

	rc = hl_nic_get_asic_type(hdev, &core_info->asic_type);
	if (rc) {
		dev_err(hdev->dev, "failed to set eth aux data asic type\n");
		return rc;
	}

	mac_addr = kcalloc(core_info->max_num_of_ports, sizeof(*mac_addr), GFP_KERNEL);
	if (!mac_addr)
		return -ENOMEM;

	for (i = 0 ; i < core_info->max_num_of_ports ; i++) {
		if (!(core_info->ports_mask & BIT(i)))
			continue;

		mac_addr[i] = hdev->asic_prop.cpucp_nic_info.mac_addrs[i].mac_addr;
	}

	core_info->mac_addr = mac_addr;

	/* set eth -> core ops */
	/* device functions */
	aux_ops->device_operational = hl_nic_device_operational;
	aux_ops->hw_access_lock = hl_nic_hw_access_lock;
	aux_ops->hw_access_unlock = hl_nic_hw_access_unlock;
	aux_ops->is_eth_lpbk = hl_nic_is_eth_lpbk;
	/* port functions */
	aux_ops->port_hw_init = hl_nic_port_hw_init;
	aux_ops->port_hw_fini = hl_nic_port_hw_fini;
	aux_ops->phy_init = hl_nic_phy_port_init;
	aux_ops->phy_fini = hl_nic_phy_port_fini;
	aux_ops->set_pfc = hl_nic_set_pfc;
	aux_ops->get_cnts_num = hl_nic_get_cnts_num;
	aux_ops->get_cnts_names = hl_nic_get_cnts_names;
	aux_ops->get_cnts_values = hl_nic_get_cnts_values;
	aux_ops->get_mac_lpbk = hl_nic_get_mac_lpbk;
	aux_ops->set_mac_lpbk = hl_nic_set_mac_lpbk;
	aux_ops->update_mtu = hl_nic_update_mtu;
	aux_ops->qpc_write = hl_nic_qpc_write;
	aux_ops->ctrl_lock = hl_nic_ctrl_lock;
	aux_ops->ctrl_unlock = hl_nic_ctrl_unlock;
	aux_ops->eq_dispatcher_register_qp = hl_nic_dispatcher_register_qp;
	aux_ops->eq_dispatcher_unregister_qp = hl_nic_dispatcher_unregister_qp;
	aux_ops->get_speed = hl_nic_get_speed;
	aux_ops->reset_core_mac_stats = hl_nic_reset_core_mac_stats;

	nic_funcs->set_en_core_data(hdev);

	return 0;
}

static void hl_nic_en_aux_data_fini(struct hl_device *hdev)
{
	struct hl_aux_dev *aux_dev = &hdev->nic.en_aux_dev;
	struct hl_en_core_info *core_info = aux_dev->core_info;

	kfree(core_info->mac_addr);
	core_info->mac_addr = NULL;
}

static int hl_nic_ib_alloc_ucontext(struct hl_aux_dev *aux_dev, int core_fd, void **core_ctx)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct hl_fpriv *hpriv;
	struct file *file;
	int rc = 0;

	/* IB core can independently manages its resources and context.
	 * However, for HL devices, corresponding HW resources can also be
	 * managed by core. To avoid contention (e.g. abrupt application close)
	 * between them, enforce orderly FD closure. Thsi facilitates that IB destroy
	 * runs first, followed by core fini.
	 */
	file = fget(core_fd);
	if (!file)
		return -EBADF;

	mutex_lock(&hdev->fpriv_list_lock);

	if (list_empty(&hdev->fpriv_list)) {
		dev_dbg(hdev->dev, "no open core context\n");
		rc = -ESRCH;
		goto out;
	}

	/* The list should contain a single element as currently only a single user context is
	 * allowed. Therefore get the first entry.
	 */
	hpriv = list_first_entry(&hdev->fpriv_list, struct hl_fpriv, dev_node);

	if (hpriv == file->private_data) {
		*core_ctx = hpriv->ctx;
	} else {
		dev_dbg(hdev->dev, "core FD mismatch\n");
		rc = -EINVAL;
	}

out:
	mutex_unlock(&hdev->fpriv_list_lock);

	if (rc)
		fput(file);

	return rc;
}

static void hl_nic_ib_dealloc_ucontext(struct hl_aux_dev *aux_dev, void *core_ctx)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct hl_fpriv *hpriv = ((struct hl_ctx *) core_ctx)->hpriv;
	struct file *file = hpriv->filp;

	dev_dbg(hdev->dev, "IB context dealloc\n");

	set_app_params_clear(hdev);

	/* We can assert here that all IB resources which might have
	 * dependency on core are already released. Hence, release reference
	 * to core file.
	 */
	fput(file);
}

static void hl_nic_ib_query_port(struct hl_aux_dev *aux_dev, u32 port,
					struct hl_ib_port_attr *port_attr)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	struct hl_nic_port *nic_port = &nic->nic_ports[port];
	struct hl_nic_properties *nic_prop = &hdev->asic_prop.nic_props;

	/* hl_nic_ioctl_port_check returns '0' in case the port is open and -ve in case of error */
	port_attr->open = !hl_nic_ioctl_port_check(hdev, port, NIC_PORT_CHECK_OPEN);
	port_attr->link_up = nic_port->pcs_link;
	port_attr->speed = nic_port->speed;
	port_attr->max_msg_sz = nic_funcs->get_max_msg_sz(hdev);
	port_attr->num_lanes = hdev->nic_lanes_per_port;
	port_attr->max_mtu = SZ_8K;
	port_attr->swqe_size = nic_port->swqe_size;
	port_attr->rwqe_size = nic_prop->rwqe_size;
}

static void hl_nic_ib_query_device(struct hl_aux_dev *aux_dev,
					struct hl_ib_device_attr *dev_attr)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_nic_properties *nic_props;
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct asic_fixed_properties *asic_props = &hdev->asic_prop;
	struct hl_ib_core_info *core_info;

	core_info = aux_dev->core_info;
	nic_props = &asic_props->nic_props;

	/* TODO: SW-101785 don't expose Gaudi fw ver */
	dev_attr->fw_ver = 0;

	dev_attr->max_mr_size = core_info->dram_size;
	dev_attr->page_size_cap = PAGE_SIZE;

	if (hdev->pdev) {
		dev_attr->vendor_id = hdev->pdev->vendor;
		dev_attr->vendor_part_id = hdev->pdev->device;
		dev_attr->hw_ver = hdev->pdev->subsystem_device;
	} else {
		dev_attr->vendor_id = PCI_VENDOR_ID_HABANALABS;
	}

	/* TODO: SW-99351: handle QPs per port */
	dev_attr->max_qp = nic_props->max_qps_num;

	dev_attr->max_qp_wr = core_info->max_num_of_wqes;
	dev_attr->max_cqe = nic_props->user_cq_max_entries;

	dev_attr->cqe_size = nic_props->cqe_size;
	dev_attr->min_cq_entries = nic_props->user_cq_min_entries;
}

static void hl_nic_ib_set_ip_addr_encap(struct hl_aux_dev *aux_dev, u32 ip_addr, u32 port)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	struct hl_nic_port *nic_port = &nic->nic_ports[port];
	u32 encap_id;

	nic_funcs->port_funcs->set_ip_addr_encap(nic_port, &encap_id, ip_addr);
}

static char *hl_nic_ib_qp_syndrome_to_str(struct hl_aux_dev *aux_dev, u32 syndrome)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;

	return nic_funcs->qp_syndrome_to_str(syndrome);
}

static int hl_nic_ib_verify_qp_id(struct hl_aux_dev *aux_dev, u32 qp_id, u32 port, uint8_t is_coll)
{
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_port *nic_port;
	struct hl_device *hdev;
	struct hl_nic *nic;
	struct hl_qp *qp;
	int rc = 0;

	nic = HL_AUX2NIC(aux_dev);
	hdev = container_of(nic, struct hl_device, nic);
	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	nic_port = &nic->nic_ports[port];

	if (is_coll) {
		hl_nic_cfg_lock_all(hdev);
		qp = hl_nic_get_qp_from_coll_conn_id(nic_port, qp_id);
	} else {
		port_funcs->cfg_lock(nic_port);
		qp = idr_find(&nic_port->qp_ids, qp_id);
	}

	if (IS_ERR_OR_NULL(qp)) {
		dev_dbg(hdev->dev, "Failed to find matching QP for handle %d, port %d\n",
			qp_id, port);
		rc = -EINVAL;
		goto cfg_unlock;
	}

	/* sanity test the port IDs */
	if (qp->port != port) {
		dev_dbg(hdev->dev, "QP port %d does not match requested port %d\n", qp->port, port);
		rc = -EINVAL;
		goto cfg_unlock;
	}

cfg_unlock:
	if (is_coll)
		hl_nic_cfg_unlock_all(hdev);
	else
		port_funcs->cfg_unlock(nic_port);

	return rc;
}

static int hl_nic_ib_aux_data_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *asic_props = &hdev->asic_prop;
	struct hl_nic_properties *nic_props;
	struct hl_ib_core_info *core_info;
	struct hl_nic *nic = &hdev->nic;
	struct hl_ib_aux_ops *aux_ops;
	struct hl_aux_dev *aux_dev;
	u64 dram_kmd_size;
	int rc, i;
	struct hl_en_aux_ops *en_aux_ops;

	aux_dev = &nic->ib_aux_dev;
	aux_dev->type = HL_AUX_DEV_IB;
	core_info = aux_dev->core_info;
	aux_ops = aux_dev->aux_ops;
	nic_props = &asic_props->nic_props;
	en_aux_ops = hdev->nic.en_aux_dev.aux_ops;

	core_info->pdev = hdev->pdev;
	core_info->dev = hdev->dev;
	core_info->fw_ver = asic_props->cpucp_info.cpucp_version;
	core_info->ports_mask = hdev->nic_ports_mask;
	core_info->max_num_of_wqes = nic_props->max_hw_user_wqs_num;
	core_info->pending_reset_long_timeout = HL_PENDING_RESET_LONG_SEC;
	core_info->max_num_of_ports = nic_props->max_num_of_ports;
	core_info->id = hdev->cdev_idx;

	core_info->ndev = kcalloc(core_info->max_num_of_ports, sizeof(*core_info->ndev),
					GFP_KERNEL);
	if (!core_info->ndev)
		return -ENOMEM;

	for (i = 0; i < nic_props->max_num_of_ports; i++) {
		if (!(core_info->ports_mask & BIT(i)))
			continue;

		/* There is a possibility that the ethernet driver was not loaded or not yet
		 * initialized. In such case, the function pointer is not initialized and we
		 * will crash.
		 */
		if (!en_aux_ops->get_netdev)
			break;

		/* For the internal ports, we would get NULL. We don't care for now if the netdev is
		 * null as the ib_device_set_netdev accepts a NULL as input.
		 */
		core_info->ndev[i] = en_aux_ops->get_netdev(&hdev->nic.en_aux_dev, i);
	}

	dram_kmd_size = asic_props->dram_user_base_address - asic_props->dram_base_address;
	core_info->dram_size = (asic_props->dram_size < dram_kmd_size) ? 0 : dram_kmd_size;

	rc = hl_nic_get_asic_type(hdev, &core_info->asic_type);
	if (rc) {
		dev_err(hdev->dev, "failed to set ib aux data asic type\n");
		goto free_ndev;
	}

	/* set ib -> core ops */
	/* the following functions are used even if the IB verbs API is disabled */
	aux_ops->device_operational = hl_nic_device_operational;
	aux_ops->hw_access_lock = hl_nic_hw_access_lock;
	aux_ops->hw_access_unlock = hl_nic_hw_access_unlock;
	aux_ops->alloc_ucontext = hl_nic_ib_alloc_ucontext;
	aux_ops->dealloc_ucontext = hl_nic_ib_dealloc_ucontext;
	aux_ops->query_port = hl_nic_ib_query_port;
	aux_ops->query_device = hl_nic_ib_query_device;
	aux_ops->set_ip_addr_encap = hl_nic_ib_set_ip_addr_encap;
	aux_ops->qp_syndrome_to_str = hl_nic_ib_qp_syndrome_to_str;
	aux_ops->verify_qp_id = hl_nic_ib_verify_qp_id;

	/* these functions are used only if the IB verbs API is enabled */
	aux_ops->cmd_ctrl = hl_nic_ib_cmd_ctrl;
	aux_ops->mmap = hl_nic_ib_mmap;

	return 0;

free_ndev:
	kfree(core_info->ndev);

	return rc;
}

static void hl_nic_ib_aux_data_fini(struct hl_device *hdev)
{
	struct hl_ib_core_info *core_info;
	struct hl_nic *nic = &hdev->nic;
	struct hl_aux_dev *aux_dev;

	aux_dev = &nic->ib_aux_dev;
	core_info = aux_dev->core_info;

	/* Free the netdev structs */
	kfree(core_info->ndev);
}

#ifdef _HAS_AUX_BUS_H
static void eth_adev_release(struct device *dev)
{
	struct hl_aux_dev *aux_dev = container_of(dev, struct hl_aux_dev, adev.dev);
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);

	nic->is_eth_aux_dev_initialized = false;
}

static int hl_nic_en_aux_drv_init(struct hl_device *hdev)
{
	struct hl_nic *nic = &hdev->nic;
	struct hl_aux_dev *aux_dev = &nic->en_aux_dev;
	struct auxiliary_device *adev;
	int rc;

	rc = hl_nic_en_aux_data_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "eth aux data init failed\n");
		return rc;
	}

	adev = &aux_dev->adev;
	adev->id = hdev->id;
	adev->name = "eth";
	adev->dev.parent = hdev->dev;
	adev->dev.release = eth_adev_release;

	rc = auxiliary_device_init(adev);
	if (rc) {
		dev_err(hdev->dev, "eth auxiliary_device_init failed\n");
		goto aux_data_free;
	}

	rc = auxiliary_device_add(adev);
	if (rc) {
		dev_err(hdev->dev, "eth auxiliary_device_add failed\n");
		goto uninit_adev;
	}

	nic->is_eth_aux_dev_initialized = true;

	return 0;

uninit_adev:
	auxiliary_device_uninit(adev);
aux_data_free:
	hl_nic_en_aux_data_fini(hdev);

	return rc;
}

static void hl_nic_en_aux_drv_fini(struct hl_device *hdev)
{
	struct hl_nic *nic = &hdev->nic;
	struct auxiliary_device *adev;

	if (!nic->is_eth_aux_dev_initialized)
		return;

	adev = &nic->en_aux_dev.adev;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);

	hl_nic_en_aux_data_fini(hdev);
}

static void ib_adev_release(struct device *dev)
{
	struct hl_aux_dev *aux_dev = container_of(dev, struct hl_aux_dev, adev.dev);
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, ib_aux_dev);

	nic->is_ib_aux_dev_initialized = false;
}

static int hl_nic_ib_aux_drv_init(struct hl_device *hdev)
{
	struct hl_nic *nic = &hdev->nic;
	struct hl_aux_dev *aux_dev = &nic->ib_aux_dev;
	struct auxiliary_device *adev;
	int rc;

	if (!nic->ib_support)
		return 0;

	rc = hl_nic_ib_aux_data_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "IB aux data init failed\n");
		return rc;
	}

	adev = &aux_dev->adev;
	adev->id = hdev->id;
	adev->name = "ib";
	adev->dev.parent = hdev->dev;
	adev->dev.release = ib_adev_release;

	rc = auxiliary_device_init(adev);
	if (rc) {
		dev_err(hdev->dev, "ib auxiliary_device_init failed\n");
		goto aux_data_free;
	}

	rc = auxiliary_device_add(adev);
	if (rc) {
		dev_err(hdev->dev, "ib auxiliary_device_add failed\n");
		goto uninit_adev;
	}

	nic->is_ib_aux_dev_initialized = true;

	return 0;

uninit_adev:
	auxiliary_device_uninit(adev);
aux_data_free:
	hl_nic_ib_aux_data_fini(hdev);

	return rc;
}

static void hl_nic_ib_aux_drv_fini(struct hl_device *hdev)
{
	struct hl_nic *nic = &hdev->nic;
	struct auxiliary_device *adev;

	if (!nic->ib_support || !nic->is_ib_aux_dev_initialized)
		return;

	adev = &nic->ib_aux_dev.adev;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);

	hl_nic_ib_aux_data_fini(hdev);
}
#else
static int hl_nic_en_aux_drv_init(struct hl_device *hdev)
{
	int rc;

	rc = hl_nic_en_aux_data_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "eth aux data init failed\n");
		return rc;
	}

	rc = hl_en_probe(&hdev->nic.en_aux_dev);
	if (rc) {
		dev_err(hdev->dev, "eth probe failed\n");
		goto probe_fail;
	}

	return 0;

probe_fail:
	hl_nic_en_aux_data_fini(hdev);

	return rc;
}

static void hl_nic_en_aux_drv_fini(struct hl_device *hdev)
{
	hl_en_remove(&hdev->nic.en_aux_dev);
	hl_nic_en_aux_data_fini(hdev);
}

/* in case that the IB driver is not loaded */
#ifndef HL_LOAD_IB
static int hl_ib_probe(struct hl_aux_dev *aux_dev)
{
	return 0;
}

static void hl_ib_remove(struct hl_aux_dev *aux_dev)
{

}
#endif

static int hl_nic_ib_aux_drv_init(struct hl_device *hdev)
{
	struct hl_nic *nic = &hdev->nic;
	int rc;

	if (!nic->ib_support)
		return 0;

	rc = hl_nic_ib_aux_data_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "IB aux data init failed\n");
		return rc;
	}

	rc = hl_ib_probe(&hdev->nic.ib_aux_dev);
	if (rc) {
		dev_err(hdev->dev, "IB probe failed\n");
		goto probe_fail;
	}

	return 0;

probe_fail:
	hl_nic_ib_aux_data_fini(hdev);

	return rc;
}

static void hl_nic_ib_aux_drv_fini(struct hl_device *hdev)
{
	struct hl_nic *nic = &hdev->nic;

	if (!nic->ib_support)
		return;

	hl_ib_remove(&hdev->nic.ib_aux_dev);
	hl_nic_ib_aux_data_fini(hdev);
}
#endif

void hl_nic_internal_port_fini_locked(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;

	if (!nic_port->port_open)
		return;

	nic_port->port_open = false;

	/* verify that the port is marked as closed before continuing */
	mb();

	nic_funcs->port_funcs->phy_port_fini(nic_port);

	__hl_nic_port_hw_fini(nic_port);
}

static void hl_nic_internal_ports_fini(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_port *nic_port;
	int i;

	for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)) || (nic->eth_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];

		mutex_lock(&nic_port->control_lock);

		hl_nic_internal_port_fini_locked(nic_port);

		mutex_unlock(&nic_port->control_lock);
	}
}

int hl_nic_internal_port_init_locked(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	u32 port = nic_port->port;
	int rc;

	rc = __hl_nic_port_hw_init(nic_port);
	if (rc) {
		dev_err(hdev->dev, "Failed to configure the HW, port: %d, %d", port, rc);
		return rc;
	}

	rc = nic_funcs->port_funcs->phy_port_init(nic_port);
	if (rc) {
		dev_err(hdev->dev, "Failed to configure the HW, port: %d, %d", port, rc);
		goto phy_fail;
	}

	nic_port->port_open = true;

	return 0;

phy_fail:
	__hl_nic_port_hw_fini(nic_port);

	return rc;
}

int hl_nic_internal_ports_init(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic_port *nic_port;
	struct hl_nic *nic = &hdev->nic;
	u32 port;
	int rc, i;

	for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)) || (nic->eth_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];
		port = nic_port->port;

		mutex_lock(&nic_port->control_lock);

		rc = hl_nic_internal_port_init_locked(nic_port);
		if (rc) {
			dev_err(hdev->dev, "Failed to configure the HW, port: %d, %d", port, rc);
			mutex_unlock(&nic_port->control_lock);
			goto port_init_fail;
		}

		mutex_unlock(&nic_port->control_lock);
	}

	return 0;

port_init_fail:
	hl_nic_internal_ports_fini(hdev);

	return rc;
}

static void hl_nic_mac_loopback_init(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic *nic = &hdev->nic;
	struct hl_en_aux_ops *aux_ops;
	struct hl_aux_dev *aux_dev;
	u32 port = nic_port->port;
	bool enable;

	aux_dev = &nic->en_aux_dev;
	aux_ops = aux_dev->aux_ops;

	enable = !!(nic->mac_loopback & BIT(port));
	nic_port->mac_loopback = enable;

	if (nic_port->eth_enable && aux_ops->set_dev_lpbk)
		aux_ops->set_dev_lpbk(aux_dev, port, enable);
}

int hl_nic_core_init(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	struct hl_nic_port *nic_port;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_macro *nic_macro;
	int rc, i, nics_init = 0;
	u32 port;

	if (nic->phy_load_fw) {
		rc = hl_nic_phy_has_fw(hdev);
		if (rc) {
			dev_err(hdev->dev, "NIC F/W file was not found\n");
			return rc;
		}

		rc = nic_funcs->phy_fw_load_all(hdev);
		if (rc) {
			dev_err(hdev->dev, "NIC F/W load for all failed\n");
			return rc;
		}
	}

	if (nic->phy_config_fw)
		dev_dbg(hdev->dev, "NIC F/W CRC: 0x%x\n", nic_funcs->phy_get_crc(hdev));

	rc = nic_funcs->core_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "NIC core init failed\n");
		return rc;
	}

	for (i = 0 ; i < nic_props->max_num_of_ports ; i++, nics_init++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];
		nic_macro = nic_port->nic_macro;
		port = nic_port->port;

		/* Reset the NIC macro PHY once on boot.
		 * This function resets all the 4 lanes in the PHY macro, therefore only one of the
		 * two ports of the macro should call it.
		 */
		if (nic->phy_config_fw && nic_macro->phy_macro_needs_reset) {
			rc = nic_funcs->phy_reset_macro(nic_macro);
			if (rc) {
				dev_err(hdev->dev, "PHY reset macro failed for port %d\n", port);
				goto err;
			}

			nic_macro->phy_macro_needs_reset = false;
		}

		hl_nic_spmu_init(hdev, port, false);

		nic_port->auto_neg_enable = !!(nic->auto_neg_mask & BIT(port));

		if (!hdev->reset_info.in_reset) {
			nic_port->eth_enable = hdev->nic_eth_on_internal ||
							(BIT(port) & hdev->nic_ports_ext_mask);
			if (nic_port->eth_enable)
				nic->eth_ports_mask |= BIT(port);
		}

		/* This function must be called after setting nic_port->eth_enable */
		hl_nic_mac_loopback_init(nic_port);
	}

	return 0;

err:
	nic_funcs->core_fini(hdev);

	return rc;
}

static void hl_nic_core_fini(struct hl_device *hdev)
{
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;

	nic_funcs->core_fini(hdev);
}

int hl_nic_reopen(struct hl_device *hdev)
{
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	struct hl_en_aux_ops *aux_ops = hdev->nic.en_aux_dev.aux_ops;
	int rc;

	/* check if the NIC is enabled */
	if (!hdev->nic_ports_mask)
		return 0;

	rc = hl_nic_core_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to init NIC core, %d\n", rc);
		return rc;
	}

	rc = hl_nic_internal_ports_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to init NIC internal ports, %d\n", rc);
		goto internal_ports_fail;
	}

	if (aux_ops->ports_reopen) {
		rc = aux_ops->ports_reopen(&hdev->nic.en_aux_dev);
		if (rc) {
			dev_err(hdev->dev, "Failed to reopen the eth ports, %d\n", rc);
			goto reopen_fail;
		}
	}

	nic_funcs->set_hw_cap(hdev, true);

	return 0;

reopen_fail:
	hl_nic_internal_ports_fini(hdev);
internal_ports_fail:
	hl_nic_core_fini(hdev);

	return rc;
}

int hl_nic_init(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	int rc;

	/*
	 * In init flow we initialize the NIC ports from scratch. In hard reset
	 * flow, we get here after the NIC ports were halted, hence we only need to reopen them.
	 */
	if (hdev->reset_info.in_reset)
		return hl_nic_reopen(hdev);

	hdev->nic_ports_mask &= GENMASK(nic_props->max_num_of_ports - 1, 0);
	hdev->nic_ports_ext_mask &= hdev->nic_ports_mask;
	hdev->nic_auto_neg_mask &= hdev->nic_ports_mask;

	/* check if the NIC is enabled */
	if (!hdev->nic_ports_mask)
		return 0;

	rc = nic_funcs->pre_core_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to pre init the NIC, %d\n", rc);
		return rc;
	}

	/* check if all ports are disabled by the FW */
	if (!hdev->nic_ports_mask) {
		dev_dbg(hdev->dev, "all NIC ports are disabled by the FW\n");
		return 0;
	}

	rc = hl_nic_core_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to init NIC core, %d\n", rc);
		return rc;
	}

	rc = hl_nic_internal_ports_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to init NIC internal ports, %d\n", rc);
		goto internal_ports_fail;
	}

	/* verify the kernel module name as the auxiliary drivers will bind according to it */
	WARN_ONCE(strcmp(HL_NAME, KBUILD_MODNAME),
			"habanalabs name not in sync with kernel module name");

	rc = hl_nic_en_aux_drv_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to init Ethernet driver, %d\n", rc);
		goto eth_aux_drv_fail;
	}

	rc = hl_nic_ib_aux_drv_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to init IB driver, %d\n", rc);
		goto ib_aux_drv_fail;
	}

	hl_nic_debugfs_init(hdev);

	nic_funcs->set_hw_cap(hdev, true);

	return 0;

ib_aux_drv_fail:
	hl_nic_en_aux_drv_fini(hdev);
eth_aux_drv_fail:
	hl_nic_internal_ports_fini(hdev);
internal_ports_fail:
	hl_nic_core_fini(hdev);

	return rc;
}

void hl_nic_fini(struct hl_device *hdev)
{
	/* The NIC capability bit of each ASIC cannot be used as a prerequisite
	 * for this function, as we may arrive here after a failing hard reset
	 * w/o calling to hl_nic_ports_reopen().
	 * But we can check if the NIC is totally disabled.
	 */
	if (!hdev->nic_ports_mask)
		return;

	hl_nic_ib_aux_drv_fini(hdev);
	/* must be called after MSI was disabled */
	hl_nic_en_aux_drv_fini(hdev);
}

/**
 * hl_nic_stop() - stop the NIC S/W and H/W.
 * @hdev: habanalabs device structure.
 *
 * This function stops the operation of the NIC S/W and H/W, no packets are
 * processed after this call.
 */
void hl_nic_stop(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	struct hl_nic *nic = &hdev->nic;
	struct hl_en_aux_ops *aux_ops;
	struct hl_aux_dev *aux_dev;
	int i;

	aux_dev = &nic->en_aux_dev;
	aux_ops = aux_dev->aux_ops;

	if (!nic_funcs->get_hw_cap(hdev))
		return;

	qps_stop(hdev);

	if (aux_ops->ports_stop)
		aux_ops->ports_stop(aux_dev);

	for (i = 0 ; i < nic_props->num_of_macros ; i++) {
		nic->nic_macros[i].phy_macro_needs_reset = true;
		nic->nic_macros[i].rec_link_sts = 0;
	}

	hl_nic_internal_ports_fini(hdev);
	hl_nic_core_fini(hdev);
}

void hl_nic_hard_reset_prepare(struct hl_device *hdev)
{
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	struct hl_aux_dev *aux_dev = &hdev->nic.en_aux_dev;
	struct hl_en_aux_ops *aux_ops;

	aux_ops = aux_dev->aux_ops;

	if (!nic_funcs->get_hw_cap(hdev))
		return;

	if (aux_ops->ports_stop_prepare)
		aux_ops->ports_stop_prepare(aux_dev);
}

int hl_nic_ioctl_port_check(struct hl_device *hdev, u32 port, u32 flags)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_port *nic_port;
	bool check_open = flags & NIC_PORT_CHECK_OPEN,
		check_enable = (flags & NIC_PORT_CHECK_ENABLE) || check_open,
		print_on_err = flags & NIC_PORT_PRINT_ON_ERR,
		check_internal = flags & NIC_PORT_CHECK_INTERNAL;

	if (port >= nic_props->max_num_of_ports) {
		if (print_on_err)
			dev_dbg(hdev->dev, "Invalid port %d\n", port);
		return -EINVAL;
	}

	if (check_enable && !(hdev->nic_ports_mask & BIT(port))) {
		if (print_on_err)
			dev_dbg(hdev->dev, "Port %d is disabled\n", port);
		return -ENODEV;
	}

	nic_port = &nic->nic_ports[port];

	if (check_internal && nic_port->eth_enable) {
		if (print_on_err)
			dev_dbg(hdev->dev, "Port %d is external\n", port);
		return -EINVAL;
	}

	if (check_open && !hl_nic_is_port_open(nic_port)) {
		if (print_on_err)
			dev_dbg(hdev->dev, "Port %d is closed\n", port);
		return -ENODEV;
	}

	return 0;
}

static void cfg_lock_unlock_all(struct hl_device *hdev, bool lock)
{
	struct hl_nic_port_funcs *port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic_port *nic_port;
	struct hl_nic *nic = &hdev->nic;
	int i;

	/* no need to check which ports are enabled, all of them have an initialized lock */
	for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
		if (nic->skip_odd_ports_cfg_lock && (i & 1))
			continue;

		nic_port = &nic->nic_ports[i];

		if (lock)
			port_funcs->cfg_lock(nic_port);
		else
			port_funcs->cfg_unlock(nic_port);
	}
}

void hl_nic_cfg_lock_all(struct hl_device *hdev)
{
	return cfg_lock_unlock_all(hdev, true);
}

void hl_nic_cfg_unlock_all(struct hl_device *hdev)
{
	return cfg_lock_unlock_all(hdev, false);
}

/* This function must be called after taking cfg_lock for all the ports */
static struct hl_coll_qp *get_coll_qp_from_conn_id(struct hl_nic_port *nic_port, u32 conn_id)
{
	struct hl_nic_port_funcs *port_funcs;
	struct hl_coll_qp *coll_qp;
	struct hl_device *hdev;
	struct hl_nic *nic;
	u32 coll_conn_id, coll_conn_type;

	hdev = nic_port->hdev;
	nic = &hdev->nic;
	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;

	coll_conn_type = (conn_id >= nic_port->scale_out_coll_qp_idx_offset) ?
		HL_NIC_COLL_CONN_TYPE_SCALE_OUT : HL_NIC_COLL_CONN_TYPE_NON_SCALE_OUT;

	coll_conn_id = conn_id - port_funcs->get_coll_qps_offset(nic_port);

	coll_qp = idr_find(&nic->coll_props[coll_conn_type].coll_qp_ids, coll_conn_id);

	return coll_qp;
}

/* This function must be called after taking cfg_lock for all the ports */
struct hl_qp *hl_nic_get_qp_from_coll_conn_id(struct hl_nic_port *nic_port, u32 conn_id)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_coll_qp *coll_qp;
	u32 port = nic_port->port;

	coll_qp = get_coll_qp_from_conn_id(nic_port, conn_id);
	if (IS_ERR_OR_NULL(coll_qp)) {
		dev_dbg(hdev->dev,
			"Failed to find matching collective QP for conn_id %u, port %u\n",
			conn_id, port);
		return NULL;
	}

	return coll_qp->qps_array[port];
}

bool hl_nic_is_scale_out_coll_type(u32 coll_conn_type)
{
	return coll_conn_type == HL_NIC_COLL_CONN_TYPE_SCALE_OUT;
}

static void hl_nic_get_qp_id_range(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;

	nic_funcs->port_funcs->get_qp_id_range(nic_port, min_id, max_id);

	/* Take the minimum between the max id supported by the port and the max id supported by
	 * the WQs number the user asked to allocate.
	 */
	*max_id = min(nic_port->qp_idx_offset + nic_port->num_of_wqs - 1, *max_id);
}

static void hl_nic_get_coll_qp_id_range(struct hl_device *hdev, bool is_scale_out_conn, u32 *min_id,
					u32 *max_id)
{
	struct hl_nic *nic = &hdev->nic;
	u32 coll_conn_type;

	hdev->asic_funcs->nic_funcs->get_coll_qp_id_range(hdev, is_scale_out_conn, min_id, max_id);

	coll_conn_type = is_scale_out_conn ?
		HL_NIC_COLL_CONN_TYPE_SCALE_OUT : HL_NIC_COLL_CONN_TYPE_NON_SCALE_OUT;

	/* Take the minimum between the max id supported by the port and the max id supported by
	 * the WQs number the user asked to allocate.
	 */
	*max_id = min(*min_id + nic->coll_props[coll_conn_type].num_of_coll_wqs - 1, *max_id);
}

static void hl_nic_qp_do_release(struct hl_qp *qp)
{
	struct hl_nic_port *nic_port;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_qpc_drain_attr drain_attr = { .wait_for_idle = false, };

	if (IS_ERR_OR_NULL(qp))
		return;

	nic_port = qp->nic_port;
	port_funcs = nic_port->hdev->asic_funcs->nic_funcs->port_funcs;

	port_funcs->qp_pre_destroy(qp);

	if (qp->is_coll) {
		struct hl_coll_qp *coll_qp = get_coll_qp_from_conn_id(nic_port, qp->qp_id);

		coll_qp->qps_array[nic_port->port] = NULL;
	} else {
		idr_replace(&qp->nic_port->qp_ids, NULL, qp->qp_id);
	}

	/* drain the Req QP now in order to make sure that accesses to the WQ will not
	 * be performed from this point on.
	 * Waiting for the WQ to drain is performed in the reset work
	 */
	hl_nic_qp_modify(nic_port, qp, NIC_QP_STATE_SQD, &drain_attr);

	queue_work(nic_port->qp_wq, &qp->async_work);
}

static int alloc_qp(struct hl_device *hdev, struct hl_ctx *ctx, struct hl_nic_alloc_conn_in *in,
			struct hl_nic_alloc_conn_out *out)
{
	struct hl_wq_array_properties *swq_arr_props, *rwq_arr_props;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_port *nic_port;
	struct hl_qp *qp;
	u32 min_id, max_id, port;
	int id, rc;

	if (!in || !out) {
		dev_dbg(hdev->dev, "Missing parameters for allocating a NIC QP\n");
		return -EINVAL;
	}

	port = in->port;

	rc = hl_nic_ioctl_port_check(hdev, port, NIC_PORT_CHECK_OPEN | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return -ENOMEM;

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;

	nic_port = &hdev->nic.nic_ports[port];
	qp->nic_port = nic_port;
	qp->port = port;
	qp->ctx = ctx;
	qp->asid = ctx->asid;
	qp->curr_state = NIC_QP_STATE_RESET;
	INIT_WORK(&qp->async_work, qp_destroy_work);

	/* TODO: handle local/remote keys */

	hl_nic_get_qp_id_range(nic_port, &min_id, &max_id);

	port_funcs->cfg_lock(nic_port);

	if (!nic_port->set_app_params) {
		dev_dbg(hdev->dev,
			"Failed to allocate QP, set_app_params wasn't called yet, port %d\n",
			port);
		rc = -EPERM;
		goto error_exit;
	}

	swq_arr_props = &nic_port->wq_arr_props[HL_NIC_USER_WQ_SEND];
	rwq_arr_props = &nic_port->wq_arr_props[HL_NIC_USER_WQ_RECV];

	if (!swq_arr_props->enable || !rwq_arr_props->enable) {
		dev_dbg(hdev->dev, "Failed to allocate QP as WQs are not configured, port %d\n",
			port);
		rc = -EPERM;
		goto error_exit;
	}

	if (swq_arr_props->under_unset || rwq_arr_props->under_unset) {
		dev_dbg(hdev->dev, "Failed to allocate QP as WQs are under unset, port %d\n",
			port);
		rc = -EPERM;
		goto error_exit;
	}

	id = idr_alloc(&nic_port->qp_ids, qp, min_id, max_id + 1, GFP_KERNEL);
	if (id < 0) {
		dev_dbg(hdev->dev, "Failed allocate QP IDR entry, port %d", port);
		rc = id;
		goto error_exit;
	}

	qp->qp_id = id;

	rc = port_funcs->register_qp(nic_port, id, ctx->asid);
	if (rc) {
		dev_dbg(hdev->dev, "Failed to register QP %d, port %d\n", id, port);
		goto qp_register_error;
	}

	atomic_inc(&nic_port->num_of_allocated_qps);

	port_funcs->cfg_unlock(nic_port);

	out->conn_id = id;

#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "Allocated QP %d, port %d", id, port);
#endif

	return 0;

qp_register_error:
	idr_remove(&qp->nic_port->qp_ids, qp->qp_id);
error_exit:
	port_funcs->cfg_unlock(nic_port);
	kfree(qp);
	return rc;
}

static int alloc_coll_qp(struct hl_device *hdev, struct hl_ctx *ctx,
				struct hl_nic_alloc_coll_conn_in *in,
				struct hl_nic_alloc_coll_conn_out *out)
{
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_funcs *nic_funcs;
	struct hl_nic_port *nic_port;
	struct hl_coll_qp *coll_qp;
	struct hl_nic *nic;
	struct hl_qp *qp;
	u32 min_id, max_id, port, _port, coll_conn_type;
	u8 max_num_of_ports;
	bool is_scale_out_conn;
	int id, rc;

	if (!in || !out) {
		dev_dbg(hdev->dev, "Missing parameters for allocating a NIC collective QP\n");
		return -EINVAL;
	}

	max_num_of_ports = hdev->asic_prop.nic_props.max_num_of_ports;

	/* Return with failure in case not all ports are UP */
	for (port = 0 ; port < max_num_of_ports ; port++) {
		if (!(hdev->nic_ports_mask & BIT(port)))
			continue;

		rc = hl_nic_ioctl_port_check(hdev, port,
					NIC_PORT_CHECK_OPEN | NIC_PORT_PRINT_ON_ERR);
		if (rc)
			return rc;
	}

	coll_qp = kzalloc(sizeof(*coll_qp), GFP_KERNEL);
	if (!coll_qp)
		return -ENOMEM;

	coll_qp->qps_array = kcalloc(max_num_of_ports, sizeof(*coll_qp->qps_array), GFP_KERNEL);
	if (!coll_qp->qps_array) {
		kfree(coll_qp);
		return -ENOMEM;
	}

	nic_funcs = hdev->asic_funcs->nic_funcs;
	port_funcs = nic_funcs->port_funcs;
	nic = &hdev->nic;

	coll_qp->hdev = hdev;
	atomic_set(&coll_qp->num_of_initialized_qps, 0);

	is_scale_out_conn = in->is_scale_out;

	hl_nic_get_coll_qp_id_range(hdev, is_scale_out_conn, &min_id, &max_id);

	coll_conn_type = is_scale_out_conn ?
		HL_NIC_COLL_CONN_TYPE_SCALE_OUT : HL_NIC_COLL_CONN_TYPE_NON_SCALE_OUT;

	hl_nic_cfg_lock_all(hdev);

	id = idr_alloc(&nic->coll_props[coll_conn_type].coll_qp_ids, coll_qp, min_id,
			max_id + 1, GFP_KERNEL);
	if (id < 0) {
		dev_dbg(hdev->dev, "Failed to allocate coll QP\n");
		rc = id;
		goto cfg_unlock_all;
	}

	coll_qp->id = id;
	coll_qp->coll_conn_type = coll_conn_type;

	for (port = 0 ; port < max_num_of_ports ; port++) {
		if (!(hdev->nic_ports_mask & BIT(port)))
			continue;

		qp = kzalloc(sizeof(*qp), GFP_KERNEL);
		if (!qp) {
			rc = -ENOMEM;
			goto free_qps;
		}

		coll_qp->qps_array[port] = qp;
		nic_port = &hdev->nic.nic_ports[port];

		qp->is_coll = true;
		qp->coll_conn_type = coll_conn_type;
		qp->nic_port = nic_port;
		qp->port = port;
		qp->ctx = ctx;
		qp->asid = ctx->asid;
		qp->curr_state = NIC_QP_STATE_RESET;
		INIT_WORK(&qp->async_work, qp_destroy_work);

		/* TODO: handle local/remote keys */

		qp->qp_id = id + port_funcs->get_coll_qps_offset(nic_port);

		if (is_scale_out_conn)
			atomic_inc(&nic_port->num_of_allocated_scale_out_coll_qps);
		else
			atomic_inc(&nic_port->num_of_allocated_coll_qps);
	}

	hl_nic_cfg_unlock_all(hdev);

	out->conn_id = id;

#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "Allocated collective QP %d\n", id);
#endif

	return 0;

free_qps:
	for (_port = 0 ; _port < port ; _port++) {
		if (!(hdev->nic_ports_mask & BIT(_port)))
			continue;

		nic_port = &hdev->nic.nic_ports[_port];

		if (is_scale_out_conn)
			atomic_dec(&nic_port->num_of_allocated_scale_out_coll_qps);
		else
			atomic_dec(&nic_port->num_of_allocated_coll_qps);

		qp = coll_qp->qps_array[port];
		coll_qp->qps_array[port] = NULL;
		kfree(qp);
	}

	idr_remove(&nic->coll_props[coll_conn_type].coll_qp_ids, coll_qp->id);
cfg_unlock_all:
	hl_nic_cfg_unlock_all(hdev);
	kfree(coll_qp->qps_array);
	kfree(coll_qp);

	return rc;
}

u32 hl_nic_get_wq_array_type(bool is_send, bool is_coll, bool is_scale_out_conn)
{
	u32 type;

	if (is_send)
		if (is_coll)
			type = is_scale_out_conn ?
				HL_NIC_USER_COLL_SCALE_OUT_WQ_SEND : HL_NIC_USER_COLL_WQ_SEND;
		else
			type = HL_NIC_USER_WQ_SEND;
	else
		if (is_coll)
			type = is_scale_out_conn ?
				HL_NIC_USER_COLL_SCALE_OUT_WQ_RECV : HL_NIC_USER_COLL_WQ_RECV;
		else
			type = HL_NIC_USER_WQ_RECV;

	return type;
}

static int alloc_and_map_wq(struct hl_nic_port *nic_port, struct hl_qp *qp, u32 n_wq, bool is_swq)
{
	struct hl_wq_array_properties *wq_arr_props;
	struct hl_nic_properties *nic_props;
	struct hl_device *hdev;
	struct hl_nic_mem *mem;
	struct hl_nic_mem_data mem_data = {};
	struct hl_ctx *ctx = qp->ctx;
	struct hl_mem_mgr *mmg = &ctx->hpriv->mem_mgr;
	struct hl_mmap_mem_buf *buf;
	u64 wq_arr_size, wq_size;
	u32 wq_arr_type, wqe_size, qp_idx_offset, wq_idx;
	bool is_coll, is_scale_out_conn;
	int rc;

	hdev = nic_port->hdev;
	nic_props = &hdev->asic_prop.nic_props;

	is_coll = qp->is_coll;
	is_scale_out_conn = hl_nic_is_scale_out_coll_type(qp->coll_conn_type);

	if (is_coll) {
		qp_idx_offset = is_scale_out_conn ? nic_port->scale_out_coll_qp_idx_offset :
						nic_port->coll_qp_idx_offset;
	} else {
		qp_idx_offset = nic_port->qp_idx_offset;
	}

	wq_idx = qp->qp_id - qp_idx_offset;

	wq_arr_type = hl_nic_get_wq_array_type(is_swq, is_coll, is_scale_out_conn);
	wq_arr_props = &nic_port->wq_arr_props[wq_arr_type];
	wqe_size = is_swq ? nic_port->swqe_size : nic_props->rwqe_size;

	if (wq_arr_props->dva_base) {
		mem_data.mem_id = HL_NIC_DRV_MEM_HOST_VIRTUAL;
		mem_data.size = PAGE_ALIGN(n_wq * wqe_size);

		/* Get offset into device VA block pre-allocated for SWQ.
		 *
		 * Note: HW indexes into SWQ array using qp_id.
		 * In general, it's HW requirement to leave holes in a WQ array if corresponding QP
		 * indexes are allocated on another WQ array.
		 */
		mem_data.device_va = wq_arr_props->dva_base + wq_arr_props->wq_size * wq_idx;

		/* Check for out of range. */
		if (mem_data.device_va + mem_data.size >
			wq_arr_props->dva_base + wq_arr_props->dva_size) {
			dev_dbg(hdev->dev, "Out of range device VA. device_va 0x%llx, size 0x%llx\n",
				mem_data.device_va, mem_data.size);
			return -EINVAL;
		}
	} else {
		/*
		 * DMA coherent allocate case. Memory for WQ array is already allocated in
		 * user_wq_arr_set(). Here we use the allocated base addresses and QP id to
		 * calculate the CPU & bus addresses of the WQ for current QP and return that
		 * handle to the user. User may mmap() this handle returned by set_req_qp_ctx()
		 * to write WQEs.
		 */
		mem_data.mem_id = HL_NIC_DRV_MEM_HOST_MAP_ONLY;

		buf = hl_mmap_mem_buf_get(mmg, wq_arr_props->handle);
		if (!buf) {
			dev_err(hdev->dev,
				"Failed to retrieve WQ arr handle for port %d\n", nic_port->port);
			return -EINVAL;
		}

		mem = buf->private;

		/* Actual size to allocate. Page aligned since we mmap to user. */
		mem_data.size = PAGE_ALIGN(n_wq * wqe_size);
		wq_size = wq_arr_props->wq_size;
		wq_arr_size = buf->mappable_size;

		 /* Get offset into kernel buffer block pre-allocated for SWQ. */
		mem_data.in.host_map_data.kernel_address =
				(void *) (mem->kernel_address + wq_size * wq_idx);

		mem_data.in.host_map_data.bus_address = (mem->bus_address + wq_size * wq_idx);

		/* Check for out of range. */
		if ((u64) mem_data.in.host_map_data.kernel_address + mem_data.size >
			(u64) mem->kernel_address + wq_arr_size) {
			dev_dbg(hdev->dev, "Out of range kernel addr. kernel addr 0x%p, size 0x%llx\n",
				mem_data.in.host_map_data.kernel_address, mem_data.size);
			return -EINVAL;
		}
	}

	/* Allocate host vmalloc memory and map its physical pages to PMMU. */
	rc = hl_nic_mem_alloc(qp->ctx, &mem_data);
	if (rc) {
		dev_dbg(hdev->dev, "Failed to allocate %s. Port %d, QP %d\n",
			is_swq ? "SWQ" : "RWQ", nic_port->port, qp->qp_id);
		return rc;
	}

	/* Retrieve mmap handle. */
	if (is_swq)
		qp->swq_handle = mem_data.handle;
	else
		qp->rwq_handle = mem_data.handle;

	return 0;
}

static int set_req_qp_ctx(struct hl_device *hdev, struct hl_nic_req_conn_ctx_in *in,
				struct hl_nic_req_conn_ctx_out *out)
{
	struct hl_wq_array_properties *swq_arr_props, *rwq_arr_props;
	struct hl_nic_encap_idr_pdata *encap_data;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_funcs *nic_funcs;
	struct hl_nic_port *nic_port;
	struct hl_mem_mgr *mmg;
	struct hl_nic *nic;
	struct hl_qp *qp;
	u32 wq_size, port, max_wq_size;
	bool is_coll_conn;
	int rc, i;

	if (!in) {
		dev_dbg(hdev->dev, "Missing parameters for setting a requester QPC\n");
		return -EINVAL;
	}

	if (in->reserved) {
		dev_dbg(hdev->dev, "Reserved bytes must be 0\n");
		return -EINVAL;
	}

	for (i = 0 ; i < sizeof(in->pad) ; i++)
		if (in->pad[i]) {
			dev_dbg(hdev->dev, "Padding bytes must be 0\n");
			return -EINVAL;
		}

	port = in->port;

	rc = hl_nic_ioctl_port_check(hdev, port, NIC_PORT_CHECK_OPEN | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	nic_funcs = hdev->asic_funcs->nic_funcs;
	port_funcs = nic_funcs->port_funcs;
	nic = &hdev->nic;
	nic_port = &nic->nic_ports[port];

	is_coll_conn = nic_funcs->is_coll_conn_id(hdev, in->conn_id);

	if (is_coll_conn) {
		hl_nic_cfg_lock_all(hdev);

		/* For collective QPs we check that set_app_params was called for this port here
		 * and not in alloc_coll_qp.
		 * The reason is that alloc_coll_qp is being called for all the ports even though
		 * some of the ports are not necessarily part of the collective group.
		 */
		if (!nic_port->set_app_params) {
			dev_dbg(hdev->dev,
				"Failed to set requester for conn_id %u, set_app_params wasn't called yet, port %d\n",
				in->conn_id, port);
			rc = -EPERM;
			goto cfg_unlock;
		}

		qp = hl_nic_get_qp_from_coll_conn_id(nic_port, in->conn_id);
	} else {
		port_funcs->cfg_lock(nic_port);
		qp = idr_find(&nic_port->qp_ids, in->conn_id);
	}

	if (IS_ERR_OR_NULL(qp)) {
		dev_dbg(hdev->dev, "Failed to find matching QP for handle %d, port %d\n",
			in->conn_id, port);
		rc = -EINVAL;
		goto cfg_unlock;
	}

	/* sanity test the port IDs */
	if (qp->port != port) {
		dev_dbg(hdev->dev, "QP port %d does not match requested port %d\n", qp->port, port);
		rc = -EINVAL;
		goto cfg_unlock;
	}

	/* TODO: w/a SW-99462 remove when HCL stops using the prev ctx structure */
	if (in->encap_en) {
		encap_data = idr_find(&nic_port->encap_ids, in->encap_id);
		if (!encap_data) {
			dev_dbg_ratelimited(hdev->dev,
					"Encapsulation ID %d not found, ignoring\n", in->encap_id);
			in->encap_en = 0;
			in->encap_id = 0;
		}
	}

	 /* TODO: w/a SW-62591, remove when fixed in synapse */
	if (qp->curr_state == NIC_QP_STATE_RESET) {
		rc = hl_nic_qp_modify(nic_port, qp, NIC_QP_STATE_INIT, NULL);
		if (rc)
			goto cfg_unlock;
	}

	if (qp->is_req) {
		dev_dbg(hdev->dev, "Port %d, QP %d - Requester QP is already set\n", port,
			qp->qp_id);
		rc = -EINVAL;
		goto cfg_unlock;
	}

	/* For backward compatibility, use 'last_index' if 'wq_size' is not set. */
	if (in->wq_size) {
		wq_size = in->wq_size;
	} else if (check_add_overflow(in->last_index, (u32) 1, &wq_size)) {
		dev_dbg(hdev->dev,
			"Port %d, Requester QP %d - QP WQ last index (0x%x) is invalid\n",
				port, qp->qp_id, in->last_index);
		rc = -EINVAL;
		goto cfg_unlock;
	}

	/* verify that size does not exceed wq_array size */
	max_wq_size = qp->is_coll ?
			nic->coll_props[qp->coll_conn_type].num_of_coll_wq_entries :
			nic_port->num_of_wq_entries;

	if (wq_size > max_wq_size) {
		dev_dbg(hdev->dev,
			"Port %d, Requester QP %d - requested size (%d) > max size (%d)\n",
				port, qp->qp_id, wq_size, max_wq_size);
		rc = -EINVAL;
		goto cfg_unlock;
	}

	if (qp->is_coll) {
		struct hl_coll_properties *coll_props = &nic->coll_props[qp->coll_conn_type];

		swq_arr_props = &nic_port->wq_arr_props[coll_props->swq_type];
		rwq_arr_props = &nic_port->wq_arr_props[coll_props->rwq_type];
	} else {
		swq_arr_props = &nic_port->wq_arr_props[HL_NIC_USER_WQ_SEND];
		rwq_arr_props = &nic_port->wq_arr_props[HL_NIC_USER_WQ_RECV];
	}

	if (!swq_arr_props->on_device_mem) {
		rc = alloc_and_map_wq(nic_port, qp, wq_size, true);
		if (rc)
			goto cfg_unlock;

		out->swq_mem_handle = qp->swq_handle;
	}

	if (!rwq_arr_props->on_device_mem) {
		rc = alloc_and_map_wq(nic_port, qp, wq_size, false);
		if (rc)
			goto err_free_swq;

		out->rwq_mem_handle = qp->rwq_handle;
	}

	qp->remote_key = in->remote_key;

	rc = hl_nic_qp_modify(nic_port, qp, NIC_QP_STATE_RTS, in);
	if (rc)
		goto err_free_rwq;

	if (is_coll_conn)
		hl_nic_cfg_unlock_all(hdev);
	else
		port_funcs->cfg_unlock(nic_port);

	return 0;

err_free_rwq:
	if (qp->rwq_handle) {
		hl_nic_mem_destroy(qp->ctx, qp->rwq_handle);
		out->rwq_mem_handle = qp->rwq_handle = 0;
		if (!rwq_arr_props->dva_base) {
			int ret;

			mmg = &qp->ctx->hpriv->mem_mgr;
			ret = hl_mmap_mem_buf_put_handle(mmg, rwq_arr_props->handle);
			if (ret == 1)
				rwq_arr_props->handle = 0;
		}
	}
err_free_swq:
	if (qp->swq_handle) {
		hl_nic_mem_destroy(qp->ctx, qp->swq_handle);
		out->swq_mem_handle = qp->swq_handle = 0;
		if (!swq_arr_props->dva_base) {
			int ret;

			mmg = &qp->ctx->hpriv->mem_mgr;
			ret = hl_mmap_mem_buf_put_handle(mmg, swq_arr_props->handle);
			if (ret == 1)
				swq_arr_props->handle = 0;
		}
	}
cfg_unlock:
	if (is_coll_conn)
		hl_nic_cfg_unlock_all(hdev);
	else
		port_funcs->cfg_unlock(nic_port);

	return rc;
}

static int set_res_qp_ctx(struct hl_device *hdev, struct hl_ctx *ctx,
				struct hl_nic_res_conn_ctx_in *in)
{
	struct hl_nic_encap_idr_pdata *encap_data;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_funcs *nic_funcs;
	struct hl_nic_port *nic_port;
	struct hl_nic *nic;
	struct hl_qp *qp;
	u32 port;
	bool is_coll_conn;
	int rc, i;

	if (!in) {
		dev_dbg(hdev->dev, "Missing parameters for setting a responder QPC\n");
		return -EINVAL;
	}

	for (i = 0 ; i < sizeof(in->pad) ; i++)
		if (in->pad[i]) {
			dev_dbg(hdev->dev, "Padding bytes must be 0\n");
			return -EINVAL;
		}

	port = in->port;

	rc = hl_nic_ioctl_port_check(hdev, port, NIC_PORT_CHECK_OPEN | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	nic_funcs = hdev->asic_funcs->nic_funcs;
	port_funcs = nic_funcs->port_funcs;
	nic = &hdev->nic;
	nic_port = &nic->nic_ports[port];

	is_coll_conn = nic_funcs->is_coll_conn_id(hdev, in->conn_id);

	if (is_coll_conn) {
		hl_nic_cfg_lock_all(hdev);

		/* For collective QPs we check that set_app_params was called for this port here
		 * and not in alloc_coll_qp.
		 * The reason is that alloc_coll_qp is being called for all the ports even though
		 * some of the ports are not necessarily part of the collective group.
		 */
		if (!nic_port->set_app_params) {
			dev_dbg(hdev->dev,
				"Failed to set responder for conn_id %u, set_app_params wasn't called yet, port %d\n",
				in->conn_id, port);
			rc = -EPERM;
			goto unlock_cfg;
		}

		qp = hl_nic_get_qp_from_coll_conn_id(nic_port, in->conn_id);
	} else {
		port_funcs->cfg_lock(nic_port);
		qp = idr_find(&nic_port->qp_ids, in->conn_id);
	}

	if (IS_ERR_OR_NULL(qp)) {
		dev_dbg(hdev->dev, "Failed to find matching QP for handle %d, port %d\n",
			in->conn_id, port);
		rc = -EINVAL;
		goto unlock_cfg;
	}

	/* TODO: w/a SW-99462 remove when HCL stops using the prev ctx structure */
	if (in->encap_en) {
		encap_data = idr_find(&nic_port->encap_ids, in->encap_id);
		if (!encap_data) {
			dev_dbg_ratelimited(hdev->dev,
					"Encapsulation ID %d not found, ignoring\n", in->encap_id);
			in->encap_en = 0;
			in->encap_id = 0;
		}
	}

	if (qp->is_res) {
		dev_dbg(hdev->dev, "Port %d, QP %d - Responder QP is already set\n", port,
			qp->qp_id);
		rc = -EINVAL;
		goto unlock_cfg;
	}

	if (is_coll_conn) {
		struct hl_coll_qp *coll_qp = get_coll_qp_from_conn_id(nic_port, qp->qp_id);

		rc = port_funcs->register_qp(nic_port, qp->qp_id, ctx->asid);
		if (rc) {
			dev_dbg(hdev->dev,
				"Failed to register collective QP %u for port %u\n",
				qp->qp_id, port);
			goto unlock_cfg;
		}

		atomic_inc(&coll_qp->num_of_initialized_qps);
	}

	/* sanity test the port IDs */
	if (qp->port != port) {
		dev_dbg(hdev->dev, "QP port %d does not match requested port %d\n", qp->port, port);
		rc = -EINVAL;
		goto unregister_coll_qp;
	}

	qp->local_key = in->local_key;

	 /* TODO: w/a SW-62591, modify when fixed in synapse */
	if (qp->curr_state == NIC_QP_STATE_RESET) {
		rc = hl_nic_qp_modify(nic_port, qp, NIC_QP_STATE_INIT, NULL);
		if (rc)
			goto unregister_coll_qp;
	}

	/* all is well, we are ready to receive */
	rc = hl_nic_qp_modify(nic_port, qp, NIC_QP_STATE_RTR, in);

	if (is_coll_conn)
		hl_nic_cfg_unlock_all(hdev);
	else
		port_funcs->cfg_unlock(nic_port);

	return rc;

unregister_coll_qp:
	if (is_coll_conn) {
		struct hl_coll_qp *coll_qp = get_coll_qp_from_conn_id(nic_port, qp->qp_id);

		port_funcs->unregister_qp(nic_port, qp->qp_id);
		atomic_dec(&coll_qp->num_of_initialized_qps);
	}
unlock_cfg:
	if (is_coll_conn)
		hl_nic_cfg_unlock_all(hdev);
	else
		port_funcs->cfg_unlock(nic_port);

	return rc;
}

/* must be called under the port cfg lock */
u32 hl_nic_get_max_qp_id(struct hl_nic_port *nic_port)
{
	int max_qp_id = nic_port->qp_idx_offset;
	struct hl_qp *qp;
	int qp_id;

	idr_for_each_entry(&nic_port->qp_ids, qp, qp_id)
		if (qp->qp_id > max_qp_id)
			max_qp_id = qp->qp_id;

	return max_qp_id;
}

static void hl_nic_coll_qp_free(struct hl_coll_qp *coll_qp)
{
	struct hl_device *hdev = coll_qp->hdev;
	struct hl_nic *nic = &hdev->nic;
	u32 port;

	/* Go over all the ports and call the QP release function for the collective QP of each
	 * one. In that way, all the QPs will go through the same release flow.
	 */
	for (port = 0 ; port < hdev->asic_prop.nic_props.max_num_of_ports ; port++) {
		if (!(hdev->nic_ports_mask & BIT(port)))
			continue;

		hl_nic_qp_do_release(coll_qp->qps_array[port]);
	}

	idr_remove(&nic->coll_props[coll_qp->coll_conn_type].coll_qp_ids, coll_qp->id);
	kfree(coll_qp->qps_array);
	kfree(coll_qp);
}

static void qp_destroy_work(struct work_struct *work)
{
	struct hl_qp *qp = container_of(work, struct hl_qp, async_work);
	struct hl_wq_array_properties *swq_arr_props, *rwq_arr_props;
	struct hl_nic_port *nic_port = qp->nic_port;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_port_funcs *port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	struct hl_nic_qpc_drain_attr drain_attr;
	struct hl_nic_qpc_reset_attr rst_attr;
	struct hl_coll_properties *coll_props = NULL;
	struct hl_ctx *ctx = qp->ctx;
	struct hl_mem_mgr *mmg = &ctx->hpriv->mem_mgr;
	int rc;

	/* always perform orderly reset in simulator */
	if (hdev->reset_info.hard_reset_pending) {
		drain_attr.wait_for_idle = false;
		if (hdev->pdev)
			rst_attr.reset_mode = NIC_QP_RESET_MODE_HARD;
		else
			rst_attr.reset_mode = NIC_QP_RESET_MODE_FAST;
	} else {
		drain_attr.wait_for_idle = true;
		rst_attr.reset_mode = NIC_QP_RESET_MODE_GRACEFUL;
	}

	/* Complete the wait for SQ to drain. To allow parallel QPs destruction, don't take the cfg
	 * lock here. This is safe because SQD->SQD QP transition is a simple wait to drain the QP
	 * without any access to the HW.
	 */
	if (qp->curr_state == NIC_QP_STATE_SQD)
		hl_nic_qp_modify(nic_port, qp, NIC_QP_STATE_SQD, &drain_attr);

	if (qp->is_coll)
		hl_nic_cfg_lock_all(hdev);
	else
		port_funcs->cfg_lock(nic_port);

	if (qp->is_coll) {
		struct hl_coll_qp *coll_qp = get_coll_qp_from_conn_id(nic_port, qp->qp_id);

		/* If the coll_qp is NULL, meaning it was freed in previous run of this thread,
		 * no addtional action needs to be done here and we can continue with the common
		 * flow.
		 */
		if (coll_qp) {
			/* If this QP is not in reset (i.e., was set), we can decrement the number
			 * of initialized QPs under this collective QP.
			 */
			if (qp->curr_state != NIC_QP_STATE_RESET)
				atomic_dec(&coll_qp->num_of_initialized_qps);

			/* If there are no initialized QPs left, we can free the collective QP */
			if (atomic_read(&coll_qp->num_of_initialized_qps) == 0)
				hl_nic_coll_qp_free(coll_qp);
		}
	}

	hl_nic_qp_modify(nic_port, qp, NIC_QP_STATE_RESET, &rst_attr);

	port_funcs->unregister_qp(nic_port, qp->qp_id);

	if (qp->is_coll) {
		coll_props = &hdev->nic.coll_props[qp->coll_conn_type];

		swq_arr_props = &nic_port->wq_arr_props[coll_props->swq_type];
		rwq_arr_props = &nic_port->wq_arr_props[coll_props->rwq_type];
	} else {
		swq_arr_props = &nic_port->wq_arr_props[HL_NIC_USER_WQ_SEND];
		rwq_arr_props = &nic_port->wq_arr_props[HL_NIC_USER_WQ_RECV];
	}

	if (qp->swq_handle) {
		hl_nic_mem_destroy(qp->ctx, qp->swq_handle);
		qp->swq_handle = 0;
		if (!swq_arr_props->dva_base) {
			rc = hl_mmap_mem_buf_put_handle(mmg, swq_arr_props->handle);
			if (rc == 1)
				swq_arr_props->handle = 0;
		}
	}

	if (qp->rwq_handle) {
		hl_nic_mem_destroy(qp->ctx, qp->rwq_handle);
		qp->rwq_handle = 0;
		if (!rwq_arr_props->dva_base) {
			rc = hl_mmap_mem_buf_put_handle(mmg, rwq_arr_props->handle);
			if (rc == 1)
				rwq_arr_props->handle = 0;
		}
	}

	if (qp->is_coll) {
		atomic_t *num_of_allocated_coll_qps =
						hl_nic_is_scale_out_coll_type(qp->coll_conn_type) ?
						&nic_port->num_of_allocated_scale_out_coll_qps :
						&nic_port->num_of_allocated_coll_qps;

		if (atomic_dec_and_test(num_of_allocated_coll_qps)) {
			if (swq_arr_props->under_unset)
				__user_wq_arr_unset(nic_port, coll_props->swq_type, qp->ctx);

			if (rwq_arr_props->under_unset)
				__user_wq_arr_unset(nic_port, coll_props->rwq_type, qp->ctx);
		}
	} else {
		idr_remove(&nic_port->qp_ids, qp->qp_id);

		if (atomic_dec_and_test(&nic_port->num_of_allocated_qps)) {
			if (swq_arr_props->under_unset)
				__user_wq_arr_unset(nic_port, HL_NIC_USER_WQ_SEND, qp->ctx);

			if (rwq_arr_props->under_unset)
				__user_wq_arr_unset(nic_port, HL_NIC_USER_WQ_RECV, qp->ctx);
		}
	}

	if (qp->req_user_cq)
		hl_nic_user_cq_put(qp->req_user_cq);

	if (qp->res_user_cq)
		hl_nic_user_cq_put(qp->res_user_cq);

	port_funcs->qp_post_destroy(qp);

	/* hl_nic_mem_destroy should be included inside lock not due to protection.
	 * The handles (swq_handle and rwq_handle) are created based on QP id.
	 * Lock is to avoid concurrent memory access from a new handle created
	 * before freeing memory
	 */
	if (qp->is_coll)
		hl_nic_cfg_unlock_all(hdev);
	else
		port_funcs->cfg_unlock(nic_port);

	kfree(qp);
}

static void qps_drain_async_work(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic_port *nic_port;
	struct hl_nic *nic = &hdev->nic;
	int i, num_gen_qps, num_coll_qps, num_scale_out_coll_qps;

	/* wait for the workers to complete */
	for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];

		drain_workqueue(nic_port->qp_wq);

		num_gen_qps = atomic_read(&nic_port->num_of_allocated_qps);
		if (num_gen_qps)
			dev_warn(hdev->dev, "Port %d still has %d QPs alive\n", i, num_gen_qps);

		num_coll_qps = atomic_read(&nic_port->num_of_allocated_coll_qps);
		if (num_coll_qps)
			dev_warn(hdev->dev, "Port %d still has %d collective QPs alive\n",
				i, num_coll_qps);

		num_scale_out_coll_qps =
			atomic_read(&nic_port->num_of_allocated_scale_out_coll_qps);
		if (num_scale_out_coll_qps)
			dev_warn(hdev->dev, "Port %d still has %d scale-out collective QPs alive\n",
				i, num_scale_out_coll_qps);
	}
}

static inline int __must_check PTR_ERR_OR_EINVAL(__force const void *ptr)
{
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);
	else
		return -EINVAL;
}

static int destroy_qp(struct hl_device *hdev, struct hl_nic_destroy_conn_in *in)
{
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_funcs *nic_funcs;
	struct hl_nic_port *nic_port;
	struct hl_nic *nic;
	struct hl_qp *qp;
	u32 port, flags;
	bool is_coll_conn;
	int rc;

	if (!in) {
		dev_dbg(hdev->dev, "Missing parameters for destroying a QP\n");
		return -EINVAL;
	}

	nic = &hdev->nic;
	port = in->port;

	if (port >= hdev->asic_prop.nic_props.max_num_of_ports) {
		dev_dbg(hdev->dev, "Invalid port %d\n", port);
		return -EINVAL;
	}

	nic_port = &nic->nic_ports[port];

	/* in case of destroying QPs of external ports the port may be already closed
	 * by a user issuing "ip link set down" command so we only check if the port
	 * is enabled in these ports
	 */
	flags = nic_port->eth_enable ? NIC_PORT_CHECK_ENABLE : NIC_PORT_CHECK_OPEN;
	flags |= NIC_PORT_PRINT_ON_ERR;
	rc = hl_nic_ioctl_port_check(hdev, port, flags);
	if (rc)
		return rc;

	nic_funcs = hdev->asic_funcs->nic_funcs;
	port_funcs = nic_funcs->port_funcs;

	is_coll_conn = nic_funcs->is_coll_conn_id(hdev, in->conn_id);

	/* prevent reentrancy by locking the whole process of destroy_qp */
	if (is_coll_conn) {
		hl_nic_cfg_lock_all(hdev);
		qp = hl_nic_get_qp_from_coll_conn_id(nic_port, in->conn_id);
	} else {
		port_funcs->cfg_lock(nic_port);
		qp = idr_find(&nic_port->qp_ids, in->conn_id);
	}

	if (IS_ERR_OR_NULL(qp)) {
		rc = PTR_ERR_OR_EINVAL(qp);
		goto out_err;
	}

	hl_nic_qp_do_release(qp);

	if (is_coll_conn)
		hl_nic_cfg_unlock_all(hdev);
	else
		port_funcs->cfg_unlock(nic_port);

	return 0;

out_err:
	if (is_coll_conn)
		hl_nic_cfg_unlock_all(hdev);
	else
		port_funcs->cfg_unlock(nic_port);

	return rc;
}

void hl_nic_qps_stop(struct hl_nic_port *nic_port)
{
	struct hl_nic_port_funcs *port_funcs = nic_port->hdev->asic_funcs->nic_funcs->port_funcs;
	struct hl_nic_qpc_drain_attr drain = { .wait_for_idle = false, };
	struct hl_qp *qp;
	int qp_id;

	port_funcs->cfg_lock(nic_port);

	idr_for_each_entry(&nic_port->qp_ids, qp, qp_id) {
		if (IS_ERR_OR_NULL(qp))
			continue;

		hl_nic_qp_modify(nic_port, qp, NIC_QP_STATE_QPD, (void *) &drain);
	}

	port_funcs->cfg_unlock(nic_port);
}

static void qps_stop(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic_port *nic_port;
	struct hl_nic *nic = &hdev->nic;
	int i;

	/* stop the QPs */
	for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];

		hl_nic_qps_stop(nic_port);
	}
}

static void qps_destroy(struct hl_device *hdev)
{
	struct hl_nic_port_funcs *port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic_port *nic_port;
	struct hl_nic *nic = &hdev->nic;
	struct hl_coll_qp *coll_qp;
	struct hl_qp *qp;
	int qp_id, i, coll_conn_type;

	/* destroy the QPs */
	for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];

		/* protect against destroy_qp occurring in parallel */
		port_funcs->cfg_lock(nic_port);

		idr_for_each_entry(&nic_port->qp_ids, qp, qp_id) {
			if (IS_ERR_OR_NULL(qp))
				continue;

			hl_nic_qp_do_release(qp);
		}

		port_funcs->cfg_unlock(nic_port);
	}

	hl_nic_cfg_lock_all(hdev);

	for (coll_conn_type = 0 ; coll_conn_type < HL_NIC_COLL_CONN_TYPE_MAX ; coll_conn_type++) {
		idr_for_each_entry(&nic->coll_props[coll_conn_type].coll_qp_ids, coll_qp, qp_id) {
			if (IS_ERR_OR_NULL(coll_qp))
				continue;

			for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
				if (!(hdev->nic_ports_mask & BIT(i)))
					continue;

				qp = coll_qp->qps_array[i];
				hl_nic_qp_do_release(qp);
			}
		}
	}

	hl_nic_cfg_unlock_all(hdev);

	/* wait for the workers to complete */
	qps_drain_async_work(hdev);

	/* Verify the lists are empty */
	for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];

		port_funcs->cfg_lock(nic_port);

		idr_for_each_entry(&nic_port->qp_ids, qp, qp_id)
			dev_err_ratelimited(hdev->dev,
					"Port %d QP %d is still alive\n", nic_port->port, qp_id);

		port_funcs->cfg_unlock(nic_port);
	}

	hl_nic_cfg_lock_all(hdev);

	for (coll_conn_type = 0 ; coll_conn_type < HL_NIC_COLL_CONN_TYPE_MAX ; coll_conn_type++) {
		idr_for_each_entry(&nic->coll_props[coll_conn_type].coll_qp_ids, coll_qp, qp_id)
			dev_err_ratelimited(hdev->dev, "Collective QP %d is still alive\n", qp_id);
	}

	hl_nic_cfg_unlock_all(hdev);
}

static void wq_arrs_destroy(struct hl_device *hdev, struct hl_ctx *ctx)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_wq_array_properties *wq_arr_props;
	struct hl_nic_port *nic_port;
	struct hl_nic *nic = &hdev->nic;
	u32 type;
	int i;

	for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];

		wq_arr_props = nic_port->wq_arr_props;

		for (type = 0 ; type < HL_NIC_USER_WQ_TYPE_MAX ; type++) {
			if (wq_arr_props[type].enable)
				__user_wq_arr_unset(nic_port, type, ctx);
		}
	}

	/* After all the WQ arrays for all the ports have been destroyed, we can destroy also
	 * the WQ arrays pool.
	 */
	wq_arrays_pool_destroy(ctx);
}

static void encap_ids_destroy(struct hl_device *hdev, struct hl_ctx *ctx)
{
	struct hl_nic_port_funcs *port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic_encap_idr_pdata *idr_pdata;
	struct hl_nic_port *nic_port;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_funcs *nic_funcs;
	int encap_id, i;

	nic_funcs = hdev->asic_funcs->nic_funcs;

	for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];

		port_funcs->cfg_lock(nic_port);

		idr_for_each_entry(&nic_port->encap_ids, idr_pdata, encap_id) {
			nic_funcs->port_funcs->encap_unset(nic_port, encap_id, idr_pdata);

			if (idr_pdata->encap_type != HL_NIC_ENCAP_NONE)
				kfree(idr_pdata->encap_header);

			kfree(idr_pdata);
			idr_remove(&nic_port->encap_ids, encap_id);
		}

		port_funcs->cfg_unlock(nic_port);
	}
}

static int user_wq_arr_set(struct hl_device *hdev,
			struct hl_nic_user_wq_arr_set_in *in,
			struct hl_nic_user_wq_arr_set_out *out,
			struct hl_ctx *ctx)
{
	struct hl_wq_array_properties *wq_arr_props;
	struct hl_coll_properties *coll_props = NULL;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_properties *nic_props;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_port *nic_port;
	u32 port, type, num_of_wqs, num_of_wq_entries, min_wqs_per_port;
	char *type_str;
	int rc, i;

	if (!in || !out) {
		dev_dbg(hdev->dev, "missing parameters, can't set user WQ\n");
		return -EINVAL;
	}

	for (i = 0 ; i < sizeof(in->pad) ; i++)
		if (in->pad[i]) {
			dev_dbg(hdev->dev, "Padding bytes must be 0\n");
			return -EINVAL;
	}

	if (in->swq_granularity > HL_NIC_SWQE_GRAN_64B) {
		dev_dbg(hdev->dev, "Invalid send WQE granularity %d\n", in->swq_granularity);
		return -EINVAL;
	}

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	nic_props = &hdev->asic_prop.nic_props;

	type = in->type;

	if (type > nic_props->max_wq_arr_type) {
		dev_dbg(hdev->dev, "invalid type %d, can't set user WQ\n", type);
		return -EINVAL;
	}

	port = in->port;
	rc = hl_nic_ioctl_port_check(hdev, port, NIC_PORT_CHECK_OPEN | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	nic_port = &nic->nic_ports[port];

	wq_arr_props = &nic_port->wq_arr_props[type];
	type_str = wq_arr_props->type_str;

	if (wq_arr_props->is_coll)
		coll_props = &nic->coll_props[wq_arr_props->coll_wq_type];

	/* For generic WQs minimum number of wqs required is 2, one for raw eth and one for rdma */
	min_wqs_per_port = wq_arr_props->is_coll ? NIC_MIN_COLL_WQS_PER_PORT : NIC_MIN_WQS_PER_PORT;
	if (in->num_of_wqs < min_wqs_per_port) {
		dev_dbg(hdev->dev, "number of %s WQs must be minimum %d, port %d\n", type_str,
			min_wqs_per_port, port);
		return -EINVAL;
	}

	/* H/W limitation */
	if (in->num_of_wqs > nic_props->max_hw_qps_num) {
		dev_dbg(hdev->dev, "number of %s WQs (0x%x) can't be bigger than 0x%x, port %d\n",
			type_str, in->num_of_wqs, nic_props->max_hw_qps_num, port);
		return -EINVAL;
	}

	if (!is_power_of_2(in->num_of_wq_entries)) {
		dev_dbg(hdev->dev,
			"number of %s WQ entries (0x%x) must be a power of 2, port %d\n", type_str,
			in->num_of_wq_entries, port);
		return -EINVAL;
	}

	/* H/W limitation */
	if (in->num_of_wq_entries < nic_props->min_hw_user_wqs_num) {
		dev_dbg(hdev->dev,
			"number of %s WQ entries (0x%x) must be at least %d, port %d\n", type_str,
			in->num_of_wq_entries, nic_props->min_hw_user_wqs_num, port);
		return -EINVAL;
	}

	/* H/W limitation */
	if (in->num_of_wq_entries > nic_props->max_hw_user_wqs_num) {
		dev_dbg(hdev->dev,
			"number of %s WQ entries (0x%x) can't be bigger than 0x%x, port %d\n",
			type_str, in->num_of_wq_entries, nic_props->max_hw_user_wqs_num, port);
		return -EINVAL;
	}

	port_funcs->cfg_lock(nic_port);

	if (!nic_port->set_app_params) {
		dev_dbg(hdev->dev,
			"Failed to set %s WQ array, set_app_params wasn't called yet, port %d\n",
			type_str, port);
		rc = -EPERM;
		goto out;
	}

	/* we first check the wq_under_unset condition since a prev WQ unset (async) operation may
	 * still be in progress, and since in such cases we would like to return -EAGAIN to the
	 * caller and not -EINVAL
	 */
	if (wq_arr_props->enable && wq_arr_props->under_unset) {
		dev_dbg_ratelimited(hdev->dev,
			"Retry to set %s WQ array as it is under unset, port %d\n",
			type_str, port);
		rc = -EAGAIN;
		goto out;
	}

	if (wq_arr_props->enable) {
		dev_dbg(hdev->dev, "%s WQ array is already enabled, port %d\n", type_str, port);
		rc = -EINVAL;
		goto out;
	}

	if (wq_arr_props->under_unset) {
		dev_dbg(hdev->dev,
			"Failed to set %s WQ array as it is not enabled and under unset, port %d\n",
			type_str, port);
		rc = -EPERM;
		goto out;
	}

	if (wq_arr_props->is_coll) {
		num_of_wq_entries = coll_props->num_of_coll_wq_entries;
		num_of_wqs = coll_props->num_of_coll_wqs;

		if (!hl_nic_is_scale_out_coll_type(wq_arr_props->coll_wq_type) &&
				(in->num_of_wqs > NIC_MAX_NON_SCALE_OUT_COLL_CONNS)) {
			dev_dbg(hdev->dev, "Too many WQs (%u) for non scale-out collective WQ - should be max %u, port %d\n",
				in->num_of_wqs, NIC_MAX_NON_SCALE_OUT_COLL_CONNS, port);
			rc = -EINVAL;
			goto out;
		}
	} else {
		num_of_wq_entries = nic_port->num_of_wq_entries;
		num_of_wqs = nic_port->num_of_wqs;
	}

	if (num_of_wq_entries && (num_of_wq_entries != in->num_of_wq_entries)) {
		dev_dbg(hdev->dev, "%s WQ number of entries (0x%x) should be 0x%x, port %d\n",
			type_str, in->num_of_wq_entries, num_of_wq_entries, port);
		rc = -EINVAL;
		goto out;
	}

	if (num_of_wqs && (num_of_wqs != in->num_of_wqs)) {
		dev_dbg(hdev->dev, "%s WQs number (0x%x) should be 0x%x, port %d\n",
			type_str, in->num_of_wqs, num_of_wqs, port);
		rc = -EINVAL;
		goto out;
	}

	rc = hdev->asic_funcs->nic_funcs->user_wq_arr_set(hdev, in, out, ctx);
	if (rc) {
		dev_err(hdev->dev, "%s WQ array set failed, port %d, err %d\n", type_str, port, rc);
		goto out;
	}

	if (wq_arr_props->is_coll) {
		/* num_of_coll_wq_entries and num_of_coll_wqs are global hence will be set for the
		 * first requested WQ array.
		 */
		if (atomic_read(&coll_props->num_of_coll_wq_arrays) == 0) {
			coll_props->num_of_coll_wq_entries = in->num_of_wq_entries;
			coll_props->num_of_coll_wqs = in->num_of_wqs;
		}

		atomic_inc(&coll_props->num_of_coll_wq_arrays);
	} else {
		nic_port->num_of_wq_entries = in->num_of_wq_entries;
		nic_port->num_of_wqs = in->num_of_wqs;
	}

	wq_arr_props->enable = true;

out:
	port_funcs->cfg_unlock(nic_port);

	return rc;
}

static int __user_wq_arr_unset(struct hl_nic_port *nic_port, u32 type, struct hl_ctx *ctx)
{
	struct hl_wq_array_properties *wq_arr_props;
	struct hl_coll_properties *coll_props = NULL;
	struct hl_device *hdev;
	u32 port;
	char *type_str;
	int rc;

	hdev = nic_port->hdev;
	wq_arr_props = &nic_port->wq_arr_props[type];
	type_str = wq_arr_props->type_str;
	port = nic_port->port;

	rc = hdev->asic_funcs->nic_funcs->port_funcs->user_wq_arr_unset(nic_port, type, ctx);
	if (rc)
		dev_err(hdev->dev, "%s WQ array unset failed, port %d, err %d\n", type_str, port,
			rc);

	if (wq_arr_props->is_coll)
		coll_props = &hdev->nic.coll_props[wq_arr_props->coll_wq_type];

	wq_arr_props->enable = false;
	wq_arr_props->under_unset = false;

	if (!nic_port->wq_arr_props[HL_NIC_USER_WQ_SEND].enable &&
			!nic_port->wq_arr_props[HL_NIC_USER_WQ_RECV].enable) {
		nic_port->num_of_wq_entries = 0;
		nic_port->num_of_wqs = 0;
	}

	if (wq_arr_props->is_coll) {
		if (atomic_dec_and_test(&coll_props->num_of_coll_wq_arrays)) {
			coll_props->num_of_coll_wq_entries = 0;
			coll_props->num_of_coll_wqs = 0;
		}
	}

	return rc;
}

static int user_wq_arr_unset(struct hl_device *hdev, struct hl_nic_user_wq_arr_unset_in *in,
				struct hl_ctx *ctx)
{
	struct hl_wq_array_properties *wq_arr_props;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_properties *nic_props;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_port *nic_port;
	u32 port, type;
	char *type_str;
	int rc;

	if (!in) {
		dev_dbg(hdev->dev, "missing parameters, can't unset user WQ\n");
		return -EINVAL;
	}

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	nic_props = &hdev->asic_prop.nic_props;

	type = in->type;

	if (type > nic_props->max_wq_arr_type) {
		dev_dbg(hdev->dev, "invalid type %d, can't unset user WQ\n", type);
		return -EINVAL;
	}

	port = in->port;

	/* No need to check if the port is open because internal ports are always open and external
	 * ports might be closed by a user command e.g. "ip link set down" after a WQ was
	 * configured, but we still want to unset it.
	 */
	rc = hl_nic_ioctl_port_check(hdev, port, NIC_PORT_CHECK_ENABLE | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	nic_port = &nic->nic_ports[port];

	wq_arr_props = &nic_port->wq_arr_props[type];
	type_str = wq_arr_props->type_str;

	port_funcs->cfg_lock(nic_port);

	if (!wq_arr_props->enable) {
		dev_dbg(hdev->dev, "%s WQ array is disabled, port %d\n", type_str, port);
		rc = -EINVAL;
		goto out;
	}

	if (wq_arr_props->under_unset) {
		dev_dbg(hdev->dev, "%s WQ array is already under unset, port %d\n", type_str, port);
		rc = -EPERM;
		goto out;
	}

	/* Allocated QPs might still use the WQ, hence unset the WQ once they are destroyed */
	if (wq_arr_props->is_coll) {
		atomic_t *num_of_allocated_coll_qps =
					hl_nic_is_scale_out_coll_type(wq_arr_props->coll_wq_type) ?
					&nic_port->num_of_allocated_scale_out_coll_qps :
					&nic_port->num_of_allocated_coll_qps;

		if (atomic_read(num_of_allocated_coll_qps)) {
			wq_arr_props->under_unset = true;
			rc = 0;
			goto out;
		}
	} else {
		if (atomic_read(&nic_port->num_of_allocated_qps)) {
			wq_arr_props->under_unset = true;
			rc = 0;
			goto out;
		}
	}

	rc = __user_wq_arr_unset(nic_port, type, ctx);
out:
	port_funcs->cfg_unlock(nic_port);

	return rc;
}

static int alloc_user_cq_id(struct hl_device *hdev, struct hl_nic_alloc_user_cq_id_in *in,
				struct hl_nic_alloc_user_cq_id_out *out, struct hl_ctx *ctx)
{
	struct hl_nic_port_funcs *port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_user_cq *user_cq;
	struct hl_nic_port *nic_port;
	u32 min_id, max_id, port, flags;
	int id, rc;

	if (!in || !out) {
		dev_dbg(hdev->dev, "Missing parameters to allocate a NIC user CQ\n");
		return -EINVAL;
	}

	if (in->pad) {
		dev_dbg(hdev->dev, "Padding bytes must be 0\n");
		return -EINVAL;
	}

	port = in->port;
	flags = NIC_PORT_PRINT_ON_ERR;

	if (!nic_props->force_cq)
		flags |= NIC_PORT_CHECK_OPEN;

	rc = hl_nic_ioctl_port_check(hdev, port, flags);
	if (rc)
		return rc;

	nic_port = &nic->nic_ports[port];

	user_cq = kzalloc(sizeof(*user_cq), GFP_KERNEL);
	if (!user_cq)
		return -ENOMEM;

	user_cq->state = USER_CQ_STATE_ALLOC;
	user_cq->ctx = ctx;
	user_cq->nic_port = nic_port;
	kref_init(&user_cq->refcount);

	port_funcs->get_cq_id_range(nic_port, &min_id, &max_id);

	port_funcs->cfg_lock(nic_port);

	if (!nic_port->set_app_params) {
		dev_dbg(hdev->dev,
			"Failed to allocate a CQ ID, set_app_params wasn't called yet, port %d\n",
			port);
		rc = -EPERM;
		goto cfg_unlock;
	}

	id = idr_alloc(&nic_port->cq_ids, user_cq, min_id, max_id + 1, GFP_KERNEL);
	user_cq->id = id;

	port_funcs->cfg_unlock(nic_port);

	if (id < 0) {
		dev_err(hdev->dev, "No available user CQ, port %d\n", port);
		rc = id;
		goto idr_alloc_fail;
	}

	dev_dbg(hdev->dev, "Allocating CQ id %d in port %d", id, port);

	out->id = id;

	return 0;

cfg_unlock:
	port_funcs->cfg_unlock(nic_port);
idr_alloc_fail:
	kfree(user_cq);

	return rc;
}

static bool validate_cq_id_range(struct hl_nic_port *nic_port, u32 cq_id)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_port_funcs *port_funcs;
	u32 min_id, max_id;

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;

	port_funcs->get_cq_id_range(nic_port, &min_id, &max_id);

	return (cq_id >= min_id) && (cq_id <= max_id);
}

static int __user_cq_set(struct hl_device *hdev, struct hl_nic_user_cq_set_in_params *in,
			struct hl_nic_user_cq_set_out_params *out, struct hl_ctx *ctx)
{
	struct hl_nic_port_funcs *port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_user_cq *user_cq;
	struct hl_nic_port *nic_port;
	u32 port, flags, id;
	int rc;

	if (!in || !out) {
		dev_dbg(hdev->dev, "missing parameters, can't set user CQ ID\n");
		return -EINVAL;
	}

	id = in->id;
	port = in->port;

	flags = NIC_PORT_PRINT_ON_ERR;

	if (!nic_props->force_cq)
		flags |= NIC_PORT_CHECK_OPEN;

	rc = hl_nic_ioctl_port_check(hdev, port, flags);
	if (rc)
		return rc;

	nic_port = &nic->nic_ports[port];

	if (!validate_cq_id_range(nic_port, id)) {
		dev_dbg(hdev->dev, "NIC user CQ %d is invalid, port %d\n", id, port);
		return -EINVAL;
	}

	if (in->num_of_cqes < nic_props->user_cq_min_entries) {
		dev_dbg(hdev->dev,
			"NIC user CQ %d buffer length must be at least 0x%x entries, port %d\n",
			id, nic_props->user_cq_min_entries, port);
		return -EINVAL;
	}

	if (!is_power_of_2(in->num_of_cqes)) {
		dev_dbg(hdev->dev, "NIC user CQ %d buffer length must be at power of 2, port %d\n",
			id, port);
		return -EINVAL;
	}

	if (in->num_of_cqes > nic_props->user_cq_max_entries) {
		dev_dbg(hdev->dev,
			"NIC user CQ %d buffer length must not be more than 0x%x entries, port %d\n",
			id, nic_props->user_cq_max_entries, port);
		return -EINVAL;
	}

	port_funcs->cfg_lock(nic_port);

	/* Validate if user CQ is allocated. */
	user_cq = idr_find(&nic_port->cq_ids, id);
	if (!user_cq) {
		dev_dbg(hdev->dev, "NIC user CQ %d wasn't allocated, port %d\n", id, port);
		rc = -EINVAL;
		goto out;
	}

	/* Validate that user CQ is in ALLOC state. */
	if (user_cq->state != USER_CQ_STATE_ALLOC) {
		dev_dbg(hdev->dev, "NIC user CQ %d set failed, current state %d, port %d\n",
			id, user_cq->state, port);
		rc = -EINVAL;
		goto out;
	}

	rc = port_funcs->user_cq_set(user_cq, in, out);
	if (rc) {
		dev_dbg(hdev->dev, "NIC user CQ %d set failed, port %d\n", id, port);
		goto out;
	}

	user_cq->state = USER_CQ_STATE_SET;
out:
	port_funcs->cfg_unlock(nic_port);

	return rc;
}

/* used for backward compatibility, shouldn't be used by new ASICs */
static int user_cq_set(struct hl_device *hdev, struct hl_nic_user_cq_set_in *in, struct hl_ctx *ctx)
{
	struct hl_nic_alloc_user_cq_id_in alloc_in = {};
	struct hl_nic_alloc_user_cq_id_out alloc_out = {};
	struct hl_nic_user_cq_set_in_params set_in = {};
	struct hl_nic_user_cq_set_out_params set_out = {};
	u32 port;
	int rc;

	if (!in) {
		dev_dbg(hdev->dev, "missing parameters, can't set user CQ\n");
		return -EINVAL;
	}

	port = in->port;

	/* Legacy user CQ API had no allocation stage prior to the actual setting. Hence need to
	 * call it manually.
	 */
	alloc_in.port = port;
	rc = alloc_user_cq_id(hdev, &alloc_in, &alloc_out, ctx);
	if (rc) {
		dev_dbg(hdev->dev, "failed to allocate user CQ with ID 0, port %d\n", port);
		return -EINVAL;
	}

	/* Legacy user CQ has a single user CQ (ID 0) per port */
	if (alloc_out.id)
		dev_crit(hdev->dev, "user CQ with a non zero ID was allocated (%d), port %d\n",
				alloc_out.id, port);

	set_in.addr = in->addr;
	set_in.port = port;
	set_in.num_of_cqes = in->num_of_cqes;
	/* This function is used for Gaudi only which supports a single CQ per port */
	set_in.id = 0;

	return __user_cq_set(hdev, &set_in, &set_out, ctx);
}

static int user_cq_id_set(struct hl_device *hdev, struct hl_nic_user_cq_id_set_in *in,
			struct hl_nic_user_cq_id_set_out *out, struct hl_ctx *ctx)
{
	struct hl_nic_user_cq_set_in_params in2 = {};
	struct hl_nic_user_cq_set_out_params out2 = {};
	int rc;

	if (!in || !out) {
		dev_dbg(hdev->dev, "missing parameters, can't set user CQ ID\n");
		return -EINVAL;
	}

	if (in->pad) {
		dev_dbg(hdev->dev, "Padding bytes must be 0\n");
		return -EINVAL;
	}

	in2.port = in->port;
	in2.num_of_cqes = in->num_of_cqes;
	in2.id = in->id;

	rc = __user_cq_set(hdev, &in2, &out2, ctx);
	if (rc)
		return rc;

	out->mem_handle = out2.mem_handle;
	out->pi_handle = out2.pi_handle;
	out->regs_handle = out2.regs_handle;
	out->regs_offset = out2.regs_offset;

	return 0;
}

static void user_cq_destroy(struct kref *kref)
{
	struct hl_nic_user_cq *user_cq = container_of(kref, struct hl_nic_user_cq, refcount);
	struct hl_nic_port *nic_port = user_cq->nic_port;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_port_funcs *port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;

	/* Destroy the remaining resources allocated during SET state. The below callback needs to
	 * be called only if the CQ moved to unset from set state. This is because, this resource
	 * was created only during set state. If the CQ moved directly to unset from alloc then we
	 * shouldn't be trying to clear the resource.
	 */
	if (user_cq->state == USER_CQ_STATE_SET_TO_UNSET)
		port_funcs->user_cq_destroy(user_cq);

	idr_remove(&nic_port->cq_ids, user_cq->id);
	kfree(user_cq);
}

struct hl_nic_user_cq *hl_nic_user_cq_get(struct hl_nic_port *nic_port, u8 cq_id)
{
	struct hl_nic_user_cq *user_cq;

	user_cq = idr_find(&nic_port->cq_ids, cq_id);
	if (!user_cq || user_cq->state != USER_CQ_STATE_SET)
		return NULL;

	kref_get(&user_cq->refcount);

	return user_cq;
}

int hl_nic_user_cq_put(struct hl_nic_user_cq *user_cq)
{
	return kref_put(&user_cq->refcount, user_cq_destroy);
}

static int user_cq_unset_locked(struct hl_nic_user_cq *user_cq, struct hl_ctx *ctx,
				bool warn_if_alive)
{
	struct hl_nic_port *nic_port = user_cq->nic_port;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_port_funcs *port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	u32 port = nic_port->port, id = user_cq->id;
	int rc = 0, ret;

	/* Call unset only if the CQ has already been SET */
	if (user_cq->state == USER_CQ_STATE_SET) {
		rc = port_funcs->user_cq_unset(user_cq);
		if (rc)
			dev_dbg(hdev->dev, "NIC user CQ %d unset failed, port %d\n", id, port);

		user_cq->state = USER_CQ_STATE_SET_TO_UNSET;
	} else {
		user_cq->state = USER_CQ_STATE_ALLOC_TO_UNSET;
	}

	/* we'd like to destroy even if the unset callback returned error */
	ret = hl_nic_user_cq_put(user_cq);

	if (warn_if_alive && ret != 1)
		dev_warn(hdev->dev, "user CQ %d was not destroyed, port %d\n", id, port);

	return rc;
}

static int __user_cq_unset(struct hl_device *hdev, struct hl_nic_user_cq_unset_in_params *in,
				struct hl_ctx *ctx)
{
	struct hl_nic_port_funcs *port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_user_cq *user_cq;
	struct hl_nic_port *nic_port;
	u32 port, flags, id;
	int rc;

	if (!in) {
		dev_dbg(hdev->dev, "missing parameters, can't unset user CQ\n");
		return -EINVAL;
	}

	port = in->port;
	id = in->id;

	flags = NIC_PORT_PRINT_ON_ERR;
	if (!nic_props->force_cq)
		flags |= NIC_PORT_CHECK_OPEN;

	rc = hl_nic_ioctl_port_check(hdev, port, flags);
	if (rc)
		return rc;

	nic_port = &nic->nic_ports[port];

	if (!validate_cq_id_range(nic_port, id)) {
		dev_dbg(hdev->dev, "NIC user CQ %d is invalid, port %d\n", id, port);
		return -EINVAL;
	}

	port_funcs->cfg_lock(nic_port);

	/* Validate if user CQ is allocated. */
	user_cq = idr_find(&nic_port->cq_ids, id);
	if (!user_cq) {
		dev_dbg(hdev->dev, "NIC user CQ %d wasn't allocated, port %d\n", id, port);
		rc = -EINVAL;
		goto out;
	}

	rc = user_cq_unset_locked(user_cq, ctx, false);
out:
	port_funcs->cfg_unlock(nic_port);

	return rc;
}

/* used for backward compatibility, shouldn't be used by new ASICs */
static int user_cq_unset(struct hl_device *hdev, struct hl_nic_user_cq_unset_in *in,
				struct hl_ctx *ctx)
{
	struct hl_nic_user_cq_unset_in_params in2 = {};

	if (!in) {
		dev_dbg(hdev->dev, "missing parameters, can't unset user CQ\n");
		return -EINVAL;
	}

	if (in->pad) {
		dev_dbg(hdev->dev, "Padding bytes must be 0\n");
		return -EINVAL;
	}

	in2.port = in->port;
	/* This function is used for Gaudi only which supports a single CQ per port */
	in2.id = 0;

	return __user_cq_unset(hdev, &in2, ctx);
}

static int user_cq_id_unset(struct hl_device *hdev, struct hl_nic_user_cq_id_unset_in *in,
				struct hl_ctx *ctx)
{
	struct hl_nic_user_cq_unset_in_params in2 = {};

	if (!in) {
		dev_dbg(hdev->dev, "missing parameters, can't set user CQ ID\n");
		return -EINVAL;
	}

	in2.port = in->port;
	in2.id = in->id;

	return __user_cq_unset(hdev, &in2, ctx);
}

static void user_cqs_destroy(struct hl_device *hdev, struct hl_ctx *ctx)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_user_cq *user_cq;
	struct hl_nic_port *nic_port;
	u32 id;
	int i;

	for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
		if (!nic_props->force_cq && !(hdev->nic_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];

		idr_for_each_entry(&nic_port->cq_ids, user_cq, id) {
			if (user_cq->state == USER_CQ_STATE_ALLOC)
				hl_nic_user_cq_put(user_cq);
			else if (user_cq->state == USER_CQ_STATE_SET)
				user_cq_unset_locked(user_cq, ctx, true);
		}
	}
}

/* used for backward compatibility, shouldn't be used by new ASICs */
static int user_cq_update_ci(struct hl_device *hdev, struct hl_nic_user_cq_update_ci_in *in)
{
	struct hl_nic_port_funcs *port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_user_cq *user_cq;
	struct hl_nic_port *nic_port;
	u32 port, flags;
	int rc;

	if (!in) {
		dev_dbg(hdev->dev, "missing parameters, can't set user CQ\n");
		return -EINVAL;
	}

	flags = NIC_PORT_PRINT_ON_ERR;
	if (!nic_props->force_cq)
		flags |= NIC_PORT_CHECK_OPEN;

	port = in->port;
	rc = hl_nic_ioctl_port_check(hdev, port, flags);
	if (rc)
		return rc;

	nic_port = &nic->nic_ports[port];

	/*
	 * This lock prevents concurrent CI updates for different ports which is undesirable, but we
	 * need to protect here from user_cq_unset so this lock is essential. But the penalty is not
	 * so big as the CI updates should happen only once in half cycle and not after each packet.
	 */
	port_funcs->cfg_lock(nic_port);

	/* This function is used for Gaudi only which supports a single CQ per port */
	user_cq = idr_find(&nic_port->cq_ids, 0);
	if (!user_cq) {
		dev_dbg(hdev->dev, "NIC user CQ 0 wasn't allocated, can't update CI, port %d\n",
			port);
		rc = -EINVAL;
		goto out;
	}

	if (user_cq->state != USER_CQ_STATE_SET) {
		dev_dbg(hdev->dev, "NIC user CQ 0 is disabled, can't update CI, port %d\n", port);
		rc = -EINVAL;
		goto out;
	}

	port_funcs->user_cq_update_ci(nic_port, in->ci);

out:
	port_funcs->cfg_unlock(nic_port);

	return rc;
}

static int hl_nic_modify_wqe_checkers(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	struct cpucp_packet pkt;
	int rc;

	if (!(hdev->fw_components & FW_TYPE_BOOT_CPU)) {
		hdev->asic_funcs->set_priv_assertions(hdev, false);
		hdev->asic_funcs->nic_funcs->port_funcs->disable_wqe_index_checker(nic_port);
		hdev->asic_funcs->set_priv_assertions(hdev, true);
		return 0;
	}

	/* Disable the WQE index checker on the RX side */
	memset(&pkt, 0, sizeof(pkt));
	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_SET_CHECKERS << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.value = cpu_to_le64(RX_WQE_IDX_MISMATCH);
	pkt.port_index = cpu_to_le32(port);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to disable Rx WQE idx mismatch checker, port %d, rc %d\n",
			port, rc);
		return rc;
	}

	/* Disable the WQE index checker on the TX side */
	memset(&pkt, 0, sizeof(pkt));
	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_SET_CHECKERS << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.value = cpu_to_le64(TX_WQE_IDX_MISMATCH);
	pkt.port_index = cpu_to_le32(port);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc)
		dev_err(hdev->dev,
			"Failed to disable Tx WQE idx mismatch checker, port %d, rc %d\n",
			port, rc);

	return rc;
}


static int user_set_app_params(struct hl_device *hdev, struct hl_nic_set_user_app_params_in *in,
				struct hl_ctx *ctx)
{
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	struct hl_nic_port_funcs *port_funcs = nic_funcs->port_funcs;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_port *nic_port;
	bool modify_wqe_checkers;
	u32 port;
	int rc;

	if (!in) {
		dev_dbg(hdev->dev, "Missing [in] parameter for set_app_param\n");
		return -EINVAL;
	}

	if (in->pad1 || in->pad2) {
		dev_dbg(hdev->dev, "Padding bytes must be 0\n");
		return -EINVAL;
	}

	port = in->port;

	rc = hl_nic_ioctl_port_check(hdev, port, NIC_PORT_CHECK_OPEN | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	nic_port = &nic->nic_ports[port];

	port_funcs->cfg_lock(nic_port);

	if (nic_port->set_app_params) {
		dev_dbg(hdev->dev, "App params were already set, port %d\n", port);
		rc = -EPERM;
		goto out;
	}

	rc = nic_funcs->user_set_app_params(hdev, in, &modify_wqe_checkers, ctx);
	if (rc)
		goto out;

	if (modify_wqe_checkers) {
		rc = hl_nic_modify_wqe_checkers(nic_port);
		if (rc)
			goto out;
	}

	nic_port->set_app_params = true;

out:
	port_funcs->cfg_unlock(nic_port);

	return rc;
}

static int user_get_app_params(struct hl_device *hdev,
			      struct hl_nic_get_user_app_params_in *in,
			      struct hl_nic_get_user_app_params_out *out)
{
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	struct hl_nic_port_funcs *port_funcs = nic_funcs->port_funcs;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_port *nic_port;
	u32 port;
	int rc, i;

	if (!in || !out) {
		dev_dbg(hdev->dev, "Missing [in|out] parameters for get_app_param\n");
		return -EINVAL;
	}

	for (i = 0 ; i < sizeof(in->pad) ; i++)
		if (in->pad[i]) {
			dev_dbg(hdev->dev, "Padding bytes must be 0\n");
			return -EINVAL;
		}

	port = in->port;

	rc = hl_nic_ioctl_port_check(hdev, port, NIC_PORT_CHECK_OPEN | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	nic_port = &nic->nic_ports[port];

	port_funcs->cfg_lock(nic_port);
	nic_funcs->user_get_app_params(hdev, in, out);
	port_funcs->cfg_unlock(nic_port);

	return 0;
}

static int eq_poll(struct hl_device *hdev, struct hl_ctx *ctx, struct hl_nic_eq_poll_in *in,
			struct hl_nic_eq_poll_out *out)
{
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	struct hl_nic_port *nic_port;
	u32 port;
	int rc;

	if (!in || !out) {
		dev_dbg_ratelimited(hdev->dev, "Missing parameters to poll on EQ\n");
		return -EINVAL;
	}

	if (in->pad) {
		dev_dbg(hdev->dev, "Padding bytes must be 0\n");
		return -EINVAL;
	}

	port = in->port;
	rc = hl_nic_ioctl_port_check(hdev, port, NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	nic_port = &hdev->nic.nic_ports[port];
	rc = nic_funcs->port_funcs->eq_poll(nic_port, ctx->asid, out);
	switch (rc) {
	case 0:
		out->status = HL_NIC_EQ_POLL_STATUS_SUCCESS;
		break;
	case -EOPNOTSUPP:
		out->status = HL_NIC_EQ_POLL_STATUS_ERR_UNSUPPORTED_OP;
		break;
	case -EINVAL:
		out->status = HL_NIC_EQ_POLL_STATUS_ERR_NO_SUCH_PORT;
		break;
	case -ENXIO:
		out->status = HL_NIC_EQ_POLL_STATUS_ERR_PORT_DISABLED;
		break;
	case -ENODATA:
		out->status = HL_NIC_EQ_POLL_STATUS_EQ_EMPTY;
		break;
	case -ESRCH:
		out->status = HL_NIC_EQ_POLL_STATUS_ERR_NO_SUCH_EQ;
		break;
	default:
		out->status = HL_NIC_EQ_POLL_STATUS_ERR_UNDEF;
		break;
	}

	return 0;
}

static int alloc_user_db_fifo(struct hl_device *hdev, struct hl_ctx *ctx,
				struct hl_nic_alloc_user_db_fifo_in *in,
				struct hl_nic_alloc_user_db_fifo_out *out)
{
	int rc, id;
	u32 port;
	u32 min_id, max_id;
	struct hl_nic_port *nic_port;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_db_fifo_idr_pdata *idr_pdata;

	if (!in || !out) {
		dev_dbg(hdev->dev, "Missing in/out param for allocating db fifo ID\n");
		return -EINVAL;
	}

	if (in->pad) {
		dev_dbg(hdev->dev, "Padding bytes must be 0\n");
		return -EINVAL;
	}

	port = in->port;
	rc = hl_nic_ioctl_port_check(hdev, port, NIC_PORT_CHECK_OPEN | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	nic_port = &hdev->nic.nic_ports[port];
	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;

	port_funcs->get_db_fifo_id_range(nic_port, &min_id, &max_id);

	/* IDR private data. */
	idr_pdata = kzalloc(sizeof(*idr_pdata), GFP_KERNEL);
	if (!idr_pdata)
		return -ENOMEM;

	idr_pdata->asid = ctx->asid;
	idr_pdata->state = DB_FIFO_STATE_ALLOC;
	idr_pdata->port = port;

	port_funcs->cfg_lock(nic_port);

	if (!nic_port->set_app_params) {
		dev_dbg(hdev->dev,
			"Failed to allocate DB FIFO, set_app_params wasn't called yet, port %d\n",
			port);
		rc = -EPERM;
		goto cfg_unlock;
	}

	id = idr_alloc(&nic_port->db_fifo_ids, idr_pdata, min_id, max_id + 1, GFP_KERNEL);
	port_funcs->cfg_unlock(nic_port);

	if (id < 0) {
		dev_dbg_ratelimited(hdev->dev, "DB FIFO ID allocation failed, port %d\n", port);
		rc = id;
		goto free_idr_pdata;
	}

	out->id = id;

	return 0;

cfg_unlock:
	port_funcs->cfg_unlock(nic_port);
free_idr_pdata:
	kfree(idr_pdata);
	return rc;
}

static int validate_db_fifo_id_range(struct hl_nic_port *nic_port, u32 db_fifo_id)
{
	struct hl_device *hdev;
	u32 min_id, max_id;
	struct hl_nic_funcs *nic_funcs;

	hdev = nic_port->hdev;
	nic_funcs = hdev->asic_funcs->nic_funcs;

	nic_funcs->port_funcs->get_db_fifo_id_range(nic_port, &min_id, &max_id);

	if (db_fifo_id < min_id || db_fifo_id > max_id) {
		dev_dbg_ratelimited(hdev->dev, "Invalid db fifo ID, %d, port: %d\n", db_fifo_id,
										nic_port->port);
		return -EINVAL;
	}

	return 0;
}

static int validate_db_fifo_mode(struct hl_nic_port *nic_port, u8 fifo_mode)
{
	struct hl_nic_funcs *nic_funcs;
	struct hl_device *hdev;
	u32 modes_mask;

	hdev = nic_port->hdev;
	nic_funcs = hdev->asic_funcs->nic_funcs;

	nic_funcs->port_funcs->get_db_fifo_modes_mask(nic_port, &modes_mask);

	if (!(BIT(fifo_mode) & modes_mask)) {
		dev_dbg_ratelimited(hdev->dev, "Invalid db fifo mode, %d, port: %d\n", fifo_mode,
										nic_port->port);
		return -EINVAL;
	}

	return 0;
}

static int validate_db_fifo_ioctl(struct hl_nic_port *nic_port, u32 db_fifo_id)
{
	return validate_db_fifo_id_range(nic_port, db_fifo_id);
}

static int user_db_fifo_unset_and_free(struct hl_nic_port *nic_port, struct hl_ctx *ctx, u32 id,
					struct hl_nic_db_fifo_idr_pdata *idr_pdata)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	int rc = 0;

	nic_funcs->port_funcs->db_fifo_unset(nic_port, ctx, id, idr_pdata);

	/*
	 * Destroy CI buffer if we allocated one.
	 * Note: Not all DB fifo modes need CI memory buffer. e.g. Collective operations
	 * track CI via sync objects.
	 * If there is an issue in destroying the CI memory, then we might exit this function
	 * without freeing the db_fifo_pool. This would cause a kernel assertion when we try to do
	 * rmmod as the gen_alloc_destroy for db_fifo_pool would fail as there are allocations
	 * still left in the pool. So, the db_fifo_pool needs to be freed irrespective of the ci
	 * memory being destroyed or not.
	 */
	if (idr_pdata->ci_mmap_handle)
		rc = hl_nic_mem_destroy(ctx, idr_pdata->ci_mmap_handle);

	nic_funcs->port_funcs->db_fifo_free(nic_port, idr_pdata->db_pool_addr,
								idr_pdata->fifo_size);

	return rc;
}

static int user_db_fifo_set(struct hl_device *hdev, struct hl_ctx *ctx,
				struct hl_nic_user_db_fifo_set_in *in,
				struct hl_nic_user_db_fifo_set_out *out)
{
	struct hl_nic_port *nic_port;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_db_fifo_idr_pdata *idr_pdata;
	struct hl_nic_mem_data mem_data = {};
	u64 umr_block_addr, umr_mmap_handle, ci_mmap_handle = 0, ci_device_handle;
	u32 umr_db_offset, port, id, sob_payload;
	int rc, i;
	bool is_coll_ops;

	if (!in || !out) {
		dev_dbg(hdev->dev, "Missing in/out param for DB FIFO set\n");
		return -EINVAL;
	}

	for (i = 0 ; i < sizeof(in->pad) ; i++)
		if (in->pad[i]) {
			dev_dbg(hdev->dev, "Padding bytes must be 0\n");
			return -EINVAL;
		}

	port = in->port;
	rc = hl_nic_ioctl_port_check(hdev, port, NIC_PORT_CHECK_OPEN | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	nic_port = &hdev->nic.nic_ports[port];
	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	id = in->id;

	rc = validate_db_fifo_ioctl(nic_port, id);
	if (rc)
		return rc;

	/*
	 * Get allocated ID private data. Having meta data associated with IDR also helps validate
	 * that user do not trick kernel into configuring db fifo HW for an unallocated ID.
	 */
	port_funcs->cfg_lock(nic_port);
	idr_pdata = idr_find(&nic_port->db_fifo_ids, id);
	if (!idr_pdata) {
		dev_dbg_ratelimited(hdev->dev, "DB FIFO ID %d is not allocated, port: %d\n", id,
					port);
		rc = -EINVAL;
		goto cfg_unlock;
	}

	rc = validate_db_fifo_mode(nic_port, in->mode);
	if (rc)
		goto cfg_unlock;

	is_coll_ops = (in->mode == HL_NIC_DB_FIFO_TYPE_COLL_OPS_SHORT) ||
			(in->mode == HL_NIC_DB_FIFO_TYPE_COLL_OPS_LONG) ||
			(in->mode == HL_NIC_DB_FIFO_TYPE_COLL_DIR_OPS_SHORT) ||
			(in->mode == HL_NIC_DB_FIFO_TYPE_COLL_DIR_OPS_LONG);
	idr_pdata->fifo_mode = in->mode;

	/* User may call db_fifo_set multiple times post db_fifo_alloc. So, before doing any
	 * further register changes, make sure to unset the previous settings for this id
	 */
	if (idr_pdata->state == DB_FIFO_STATE_SET) {
		rc = user_db_fifo_unset_and_free(nic_port, ctx, id, idr_pdata);
		if (rc) {
			dev_dbg(hdev->dev, "Fail to unset DB FIFO %d before set, port %d\n", id,
				port);
			goto cfg_unlock;
		}
	}

	rc = port_funcs->db_fifo_allocate(nic_port, idr_pdata);
	if (rc) {
		dev_dbg(hdev->dev, "DB FIFO %d allocation failed, port %d, mode %d\n", id, port,
			in->mode);
		goto cfg_unlock;
	}

	/* TODO: SW-74501. In case the mode is collective-operations, driver needs to provide
	 * the corresponding DUP register to the user
	 */
	/*
	 * Get the user mapped register(UMR) block address and
	 * db fifo offset associated with the ID.
	 */
	port_funcs->get_db_fifo_umr(nic_port, id, &umr_block_addr, &umr_db_offset);

	/* Get mmap handle for UMR block. */
	rc = hl_get_hw_block_handle(hdev, umr_block_addr, &umr_mmap_handle, NULL);
	if (rc) {
		dev_dbg_ratelimited(hdev->dev,
			"Failed to get UMR mmap handle of DB FIFO %d, port %d\n", id, port);
		goto free_db_fifo;
	}

	if (is_coll_ops && in->num_sobs) {
		idr_pdata->base_sob_addr = in->base_sob_addr;
		idr_pdata->num_sobs = in->num_sobs;

		/* SOB operation increment with value 1. */
		sob_payload = FIELD_PREP(NIC_SOB_INC_MASK, 1) | FIELD_PREP(NIC_SOB_VAL_MASK, 1);

		/* Track DB fifo CI using sync objects.
		 * Lower 32 bits: SOB offset from LBW base.
		 * Upper 32 bits: LBW SOB payload.
		 */
		ci_device_handle = (((u64) sob_payload) << 32) | idr_pdata->base_sob_addr;
	} else {
		/*
		 * Allocate a consumer-index(CI) buffer in host kernel.
		 * HW updates CI when it pops a db fifo. User mmaps CI
		 * buffer and may poll to read current CI.
		 *
		 * Allocate page size, else we risk exposing kernel data
		 * to userspace inadvertently.
		 */
		mem_data.mem_id = HL_NIC_DRV_MEM_HOST_DMA_COHERENT;
		mem_data.size = PAGE_SIZE;
		rc = hl_nic_mem_alloc(ctx, &mem_data);
		if (rc) {
			dev_dbg_ratelimited(hdev->dev,
				"DB FIFO id %d, CI buffer allocation failed, port %d\n", id, port);
			goto free_db_fifo;
		}

		ci_mmap_handle = mem_data.handle;
		ci_device_handle = mem_data.addr;
	}

	idr_pdata->dir_dup_ports_mask = in->dir_dup_ports_mask;

	rc = port_funcs->db_fifo_set(nic_port, ctx, id, ci_device_handle, idr_pdata);
	if (rc) {
		dev_dbg_ratelimited(hdev->dev, "DB FIFO id %d, HW config failed, port %d\n", id,
					port);
		goto free_ci;
	}

	/* Cache IDR metadata and init IOCTL out. */
	idr_pdata->ci_mmap_handle = out->ci_handle = ci_mmap_handle;
	idr_pdata->umr_mmap_handle = out->regs_handle = umr_mmap_handle;
	idr_pdata->umr_db_offset = out->regs_offset = umr_db_offset;
	idr_pdata->state = DB_FIFO_STATE_SET;

	out->fifo_size = idr_pdata->fifo_size;
	out->fifo_bp_thresh = idr_pdata->fifo_size / 2;

	port_funcs->cfg_unlock(nic_port);

	return 0;

free_ci:
	if (ci_mmap_handle)
		hl_nic_mem_destroy(ctx, ci_mmap_handle);
free_db_fifo:
	port_funcs->db_fifo_free(nic_port, idr_pdata->db_pool_addr, idr_pdata->fifo_size);
cfg_unlock:
	port_funcs->cfg_unlock(nic_port);

	return rc;
}

static int __user_db_fifo_unset(struct hl_nic_port *nic_port, struct hl_ctx *ctx, u32 id,
				struct hl_nic_db_fifo_idr_pdata *idr_pdata)
{
	int rc = 0;

	/* User may call unset or the context may be destroyed while a db fifo is still in
	 * allocated state. When we call alloc_user_db_fifo next time, we would skip that
	 * particular id. This way, the id is blocked indefinitely until a full reset is done.
	 * So to fix this issue, we maintain the state of the idr. Perform unset only if set had
	 * been previously done for the idr.
	 */
	if (idr_pdata->state == DB_FIFO_STATE_SET)
		rc = user_db_fifo_unset_and_free(nic_port, ctx, id, idr_pdata);

	kfree(idr_pdata);
	idr_remove(&nic_port->db_fifo_ids, id);

	return rc;
}

static int user_db_fifo_unset(struct hl_device *hdev, struct hl_ctx *ctx,
				struct hl_nic_user_db_fifo_unset_in *in)
{
	int rc;
	struct hl_nic_port *nic_port;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_db_fifo_idr_pdata *idr_pdata;
	u32 id;

	if (!in) {
		dev_dbg(hdev->dev, "Missing in param for db fifo unset\n");
		return -EINVAL;
	}

	rc = hl_nic_ioctl_port_check(hdev, in->port, NIC_PORT_CHECK_OPEN | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	nic_port = &hdev->nic.nic_ports[in->port];
	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	id = in->id;

	rc = validate_db_fifo_ioctl(nic_port, id);
	if (rc)
		return rc;

	port_funcs->cfg_lock(nic_port);

	idr_pdata = idr_find(&nic_port->db_fifo_ids, id);
	if (!idr_pdata) {
		dev_dbg_ratelimited(hdev->dev, "DB fifo ID %d is not allocated, port: %d\n", id,
											in->port);
		rc = -EINVAL;
		goto out;
	}

	rc = __user_db_fifo_unset(nic_port, ctx, id, idr_pdata);
out:
	port_funcs->cfg_unlock(nic_port);

	return rc;
}

static void __user_db_fifo_ctx_destroy(struct hl_nic_port *nic_port, struct hl_ctx *ctx)
{
	struct hl_nic_port_funcs *port_funcs = nic_port->hdev->asic_funcs->nic_funcs->port_funcs;
	struct hl_nic_db_fifo_idr_pdata *idr_pdata;
	u32 id;

	port_funcs->cfg_lock(nic_port);

	idr_for_each_entry(&nic_port->db_fifo_ids, idr_pdata, id) {
		if (idr_pdata->asid == ctx->asid)
			__user_db_fifo_unset(nic_port, ctx, id, idr_pdata);
	}

	port_funcs->cfg_unlock(nic_port);
}

void hl_nic_user_db_fifo_ctx_destroy(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	int i;

	for (i = 0 ; i < nic_props->max_num_of_ports ; i++)
		if (hdev->nic_ports_mask & BIT(i))
			__user_db_fifo_ctx_destroy(&hdev->nic.nic_ports[i], ctx);
}

static int user_encap_alloc(struct hl_device *hdev,
				struct hl_nic_user_encap_alloc_in *in,
				struct hl_nic_user_encap_alloc_out *out)
{
	int rc, id;
	u32 port;
	u32 min_id, max_id;
	struct hl_nic_port *nic_port;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_encap_idr_pdata *idr_pdata;

	if (!in || !out) {
		dev_dbg_ratelimited(hdev->dev,
				"Missing in/out params for allocating encapsulation ID\n");
		return -EINVAL;
	}

	if (in->pad) {
		dev_dbg(hdev->dev, "Padding bytes must be 0\n");
		return -EINVAL;
	}

	port = in->port;
	rc = hl_nic_ioctl_port_check(hdev, port, NIC_PORT_CHECK_OPEN | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	nic_port = &hdev->nic.nic_ports[port];
	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;

	port_funcs->get_encap_id_range(nic_port, &min_id, &max_id);

	/* IDR private data. */
	idr_pdata = kzalloc(sizeof(*idr_pdata), GFP_KERNEL);
	if (!idr_pdata)
		return -ENOMEM;

	idr_pdata->port = port;

	port_funcs->cfg_lock(nic_port);

	if (!nic_port->set_app_params) {
		dev_dbg(hdev->dev,
			"Failed to allocate encapsulation ID, set_app_params wasn't called yet, port %d\n",
			port);
		rc = -EPERM;
		goto cfg_unlock;
	}

	id = idr_alloc(&nic_port->encap_ids, idr_pdata, min_id, max_id + 1, GFP_KERNEL);
	idr_pdata->id = id;

	port_funcs->cfg_unlock(nic_port);

	if (id < 0) {
		dev_dbg_ratelimited(hdev->dev, "Encapsulation ID allocation failed, port %d\n",
					port);
		rc = id;
		goto free_idr_pdata;
	}

	out->id = id;

	return 0;

cfg_unlock:
	port_funcs->cfg_unlock(nic_port);
free_idr_pdata:
	kfree(idr_pdata);

	return rc;
}

static int validate_encap_id_range(struct hl_nic_port *nic_port, u32 encap_id)
{
	struct hl_device *hdev;
	u32 min_id, max_id;
	struct hl_nic_funcs *nic_funcs;

	hdev = nic_port->hdev;
	nic_funcs = hdev->asic_funcs->nic_funcs;

	nic_funcs->port_funcs->get_encap_id_range(nic_port, &min_id, &max_id);

	if (encap_id < min_id || encap_id > max_id) {
		dev_dbg_ratelimited(hdev->dev, "Invalid encapsulation ID, %d\n", encap_id);
		return -EINVAL;
	}

	return 0;
}

static int validate_encap_ioctl(struct hl_nic_port *nic_port, u32 encap_id)
{
	return validate_encap_id_range(nic_port, encap_id);
}

static int user_encap_set(struct hl_device *hdev, struct hl_nic_user_encap_set_in *in)
{
	int rc, i;
	struct hl_nic_port *nic_port;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	void *encap_header = NULL;
	u32 id;
	struct hl_nic_encap_idr_pdata *idr_pdata;
	u32 encap_type_data = 0;

	if (!in) {
		dev_dbg_ratelimited(hdev->dev, "Missing in param for encapsulation set\n");
		return -EINVAL;
	}

	for (i = 0 ; i < sizeof(in->pad) ; i++)
		if (in->pad[i]) {
			dev_dbg(hdev->dev, "Padding bytes must be 0\n");
			return -EINVAL;
		}

	rc = hl_nic_ioctl_port_check(hdev, in->port, NIC_PORT_CHECK_OPEN | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	nic_port = &hdev->nic.nic_ports[in->port];
	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	id = in->id;

	rc = validate_encap_ioctl(nic_port, id);
	if (rc)
		return rc;

	switch (in->encap_type) {
	case HL_NIC_ENCAP_OVER_IPV4:
		encap_type_data = in->ip_proto;
		break;
	case HL_NIC_ENCAP_OVER_UDP:
		encap_type_data = in->udp_dst_port;
		break;
	case HL_NIC_ENCAP_NONE:
		/*
		 * No encapsulation/tunneling mode. Just set
		 * source IPv4 address and UDP protocol.
		 */
		encap_type_data = HL_NIC_IPv4_PROTOCOL_UDP;
		break;
	default:
		dev_dbg_ratelimited(hdev->dev, "Invalid encapsulation type, %d\n", in->encap_type);
		return -EINVAL;
	}

	port_funcs->cfg_lock(nic_port);

	idr_pdata = idr_find(&nic_port->encap_ids, id);
	if (!idr_pdata) {
		dev_dbg_ratelimited(hdev->dev, "Encapsulation ID %d is not allocated\n", id);
		rc = -EINVAL;
		goto cfg_unlock;
	}

	/* There could be a use case wherein the user allocates a encap ID and then calls encap_set
	 * with IPv4 encap. Now, without doing a unset, the user can call the encap_set with UDP
	 * encap or encap_none. In this case, we should be clearing the existing settings as well
	 * as freeing any allocated buffer. So, call unset API to clear the settings
	 */
	port_funcs->encap_unset(nic_port, id, idr_pdata);

	if (idr_pdata->encap_type != HL_NIC_ENCAP_NONE)
		kfree(idr_pdata->encap_header);

	if (in->encap_type != HL_NIC_ENCAP_NONE) {
		if (in->tnl_hdr_size > nic_props->max_tnl_hdr_size) {
			dev_dbg_ratelimited(hdev->dev, "Invalid tunnel header size, %d\n",
						in->tnl_hdr_size);
			rc = -EINVAL;
			goto cfg_unlock;
		}

		/* Align encapsulation header to 32bit register fields. */
		encap_header = kzalloc(ALIGN(in->tnl_hdr_size, 4), GFP_KERNEL);
		if (!encap_header) {
			rc = -ENOMEM;
			goto cfg_unlock;
		}

		rc = copy_from_user(encap_header, u64_to_user_ptr(in->tnl_hdr_ptr),
					in->tnl_hdr_size);
		if (rc) {
			dev_dbg_ratelimited(hdev->dev,
						"Copy encapsulation header data failed, %d\n", rc);
			rc = -EFAULT;
			goto free_header;
		}

		idr_pdata->encap_header = encap_header;
		idr_pdata->encap_header_size = in->tnl_hdr_size;
	}

	idr_pdata->encap_type = in->encap_type;
	idr_pdata->encap_type_data = encap_type_data;
	idr_pdata->src_ip = in->ipv4_addr;

	rc = port_funcs->encap_set(nic_port, id, idr_pdata);
	if (rc)
		goto free_header;

	port_funcs->cfg_unlock(nic_port);

	return 0;

free_header:
	if (in->encap_type != HL_NIC_ENCAP_NONE)
		kfree(encap_header);
cfg_unlock:
	port_funcs->cfg_unlock(nic_port);

	return rc;
}

static int user_encap_unset(struct hl_device *hdev, struct hl_nic_user_encap_unset_in *in)
{
	int rc;
	struct hl_nic_port *nic_port;
	struct hl_nic_port_funcs *port_funcs;
	u32 id;
	struct hl_nic_encap_idr_pdata *idr_pdata;

	if (!in) {
		dev_dbg_ratelimited(hdev->dev, "Missing in param for encapsulation unset\n");
		return -EINVAL;
	}

	rc = hl_nic_ioctl_port_check(hdev, in->port, NIC_PORT_CHECK_OPEN | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	nic_port = &hdev->nic.nic_ports[in->port];
	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	id = in->id;

	rc = validate_encap_ioctl(nic_port, id);
	if (rc)
		return rc;

	port_funcs->cfg_lock(nic_port);

	idr_pdata = idr_find(&nic_port->encap_ids, id);
	if (!idr_pdata) {
		dev_dbg_ratelimited(hdev->dev, "Encapsulation ID %d is not allocated\n", id);
		rc = -EINVAL;
		goto out;
	}

	port_funcs->encap_unset(nic_port, id, idr_pdata);

	if (idr_pdata->encap_type != HL_NIC_ENCAP_NONE)
		kfree(idr_pdata->encap_header);

	idr_remove(&nic_port->encap_ids, id);
	kfree(idr_pdata);

out:
	port_funcs->cfg_unlock(nic_port);

	return rc;
}

int hl_nic_get_link_state(struct hl_device *hdev, u32 port, struct hl_info_habana_link_state *out)
{
	int rc;

	rc = hl_nic_ioctl_port_check(hdev, port, NIC_PORT_CHECK_ENABLE | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	out->up = hdev->nic.nic_ports[port].pcs_link;

	return 0;
}

int hl_nic_get_statistics(struct hl_device *hdev, u32 port,
				struct hl_info_habana_link_counters *out)
{
	void __user *usr_str_buf, *usr_val_buf;
	struct hl_nic_port *nic_port;
	char *drv_str_buf;
	u64 *drv_val_buf;
	u32 num_of_stat;
	int rc;

	if (!out) {
		dev_dbg(hdev->dev, "Missing parameters to get NIC statistics\n");
		return -EINVAL;
	}

	usr_str_buf = (void __user *) (uintptr_t) out->str_buf_ptr;
	usr_val_buf = (void __user *) (uintptr_t) out->val_buf_ptr;

	if (!usr_str_buf || !usr_val_buf) {
		dev_dbg(hdev->dev, "Can't get NIC statistics, out buffer is NULL\n");
		return -EINVAL;
	}

	rc = hl_nic_ioctl_port_check(hdev, port, NIC_PORT_CHECK_ALL);
	if (rc)
		return rc;

	nic_port = &hdev->nic.nic_ports[port];

	num_of_stat = __hl_nic_get_cnts_num(nic_port);

	drv_str_buf = kcalloc(num_of_stat, HABANA_LINK_STR_LEN, GFP_KERNEL);
	if (!drv_str_buf)
		return -ENOMEM;

	drv_val_buf = kcalloc(num_of_stat, sizeof(u64), GFP_KERNEL);
	if (!drv_val_buf) {
		rc = -ENOMEM;
		goto out;
	}

	__hl_nic_get_cnts_names(nic_port, drv_str_buf);
	__hl_nic_get_cnts_values(nic_port, drv_val_buf);

	rc = copy_to_user(usr_str_buf, drv_str_buf, HABANA_LINK_STR_LEN * num_of_stat);
	if (rc) {
		dev_err(hdev->dev, "Can't get NIC statistics, failed to copy strings to user\n");
		rc = -EFAULT;
		goto out;
	}

	rc = copy_to_user(usr_val_buf, drv_val_buf, sizeof(u64) * num_of_stat);
	if (rc) {
		dev_err(hdev->dev, "Can't get NIC statistics, failed to copy values to user\n");
		rc = -EFAULT;
		goto out;
	}

	out->num_of_stat = num_of_stat;

out:
	kfree(drv_val_buf);
	kfree(drv_str_buf);

	return rc;
}

static int user_ccq_set(struct hl_device *hdev,
			struct hl_nic_user_ccq_set_in *in,
			struct hl_nic_user_ccq_set_out *out,
			struct hl_ctx *ctx)
{
	struct hl_nic_port_funcs *port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	u64 ccq_mmap_handle, ccq_device_addr, pi_mmap_handle, pi_device_addr;
	struct hl_nic_mem_data mem_data = {};
	struct hl_nic_port *nic_port;
	u32 port, ccqn;
	int rc;

	if (!out || !in) {
		dev_dbg(hdev->dev, "Missing parameters to CCQ set\n");
		return -EINVAL;
	}

	rc = hl_nic_ioctl_port_check(hdev, in->port, NIC_PORT_CHECK_OPEN | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	port = in->port;

	if (!hdev->nic.mmu_bypass) {
		dev_dbg(hdev->dev, "Allocation of non physical dma-mem is not supported, port %d\n",
			port);
		return -EOPNOTSUPP;
	}

	if (!is_power_of_2(in->num_of_entries)) {
		dev_dbg(hdev->dev, "NIC user CCQ buffer length must be at power of 2, port %d\n",
			port);
		return -EINVAL;
	}

	if (in->num_of_entries > USER_CCQ_MAX_ENTRIES ||
			in->num_of_entries < USER_CCQ_MIN_ENTRIES) {
		dev_dbg(hdev->dev, "CCQ buffer length invalid 0x%x, port %d\n", in->num_of_entries,
			port);
		return -EINVAL;
	}

	nic_port = &hdev->nic.nic_ports[port];

	port_funcs->cfg_lock(nic_port);

	if (!nic_port->set_app_params) {
		dev_dbg(hdev->dev,
			"Failed to set CCQ handler, set_app_params wasn't called yet, port %d\n",
			port);
		rc = -EPERM;
		goto cfg_unlock;
	}

	if (nic_port->ccq_enable) {
		dev_dbg(hdev->dev, "Failed setting CCQ handler - it is already set, port %d\n",
			port);
		rc = -EBUSY;
		goto cfg_unlock;
	}

	/* Allocate the queue memory buffer in host kernel */
	mem_data.mem_id = HL_NIC_DRV_MEM_HOST_DMA_COHERENT;
	mem_data.size = in->num_of_entries * CC_CQE_SIZE;
	rc = hl_nic_mem_alloc(ctx, &mem_data);
	if (rc) {
		dev_err(hdev->dev, "CCQ memory buffer allocation failed, port %d\n", port);
		goto cfg_unlock;
	}

	ccq_mmap_handle = mem_data.handle;
	ccq_device_addr = mem_data.addr;

	/* Allocate a producer-index (PI) buffer in host kernel */
	memset(&mem_data, 0, sizeof(mem_data));
	mem_data.mem_id = HL_NIC_DRV_MEM_HOST_DMA_COHERENT;
	mem_data.size = PAGE_SIZE;
	rc = hl_nic_mem_alloc(ctx, &mem_data);
	if (rc) {
		dev_err(hdev->dev, "CCQ PI buffer allocation failed, port %d\n", port);
		goto free_ccq;
	}

	pi_mmap_handle = mem_data.handle;
	pi_device_addr = mem_data.addr;

	port_funcs->user_ccq_set(nic_port, ccq_device_addr, pi_device_addr, in->num_of_entries,
					&ccqn);

	rc = hl_nic_eq_dispatcher_register_ccq(nic_port, ctx->asid, ccqn);
	if (rc) {
		dev_err(hdev->dev, "failed to register CCQ EQ handler, port %u, asid %u\n",
				port, ctx->asid);
		goto free_pi;
	}

	out->mem_handle = nic_port->ccq_handle = ccq_mmap_handle;
	out->pi_handle = nic_port->ccq_pi_handle = pi_mmap_handle;

	nic_port->ccq_enable = true;

	port_funcs->cfg_unlock(nic_port);

	return 0;

free_pi:
	hl_nic_mem_destroy(ctx, pi_mmap_handle);
free_ccq:
	hl_nic_mem_destroy(ctx, ccq_mmap_handle);
cfg_unlock:
	port_funcs->cfg_unlock(nic_port);

	return rc;
}

static int __user_ccq_unset(struct hl_device *hdev, struct hl_ctx *ctx, u32 port)
{
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_port *nic_port;
	bool has_errors = false;
	u32 ccqn;
	int rc;

	nic_port = &hdev->nic.nic_ports[port];

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	port_funcs->user_ccq_unset(nic_port, &ccqn);

	rc = hl_nic_mem_destroy(ctx, nic_port->ccq_pi_handle);
	if (rc) {
		dev_err(hdev->dev, "Failed to free CCQ PI memory, port %d\n", port);
		has_errors = true;
	}

	rc = hl_nic_mem_destroy(ctx, nic_port->ccq_handle);
	if (rc) {
		dev_err(hdev->dev, "Failed to free CCQ memory, port %d\n", port);
		has_errors = true;
	}

	rc = hl_nic_eq_dispatcher_unregister_ccq(nic_port, ctx->asid, ccqn);
	if (rc) {
		dev_err(hdev->dev, "Failed to unregister CCQ EQ handler, port %u, asid %u\n",
				port, ctx->asid);
		has_errors = true;
	}

	if (has_errors)
		return -EIO;

	nic_port->ccq_enable = false;

	return 0;
}

static int user_ccq_unset(struct hl_device *hdev, struct hl_nic_user_ccq_unset_in *in,
				struct hl_ctx *ctx)
{
	struct hl_nic_port_funcs *port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	struct hl_nic_port *nic_port;
	u32 port;
	int rc;

	if (!in) {
		dev_dbg(hdev->dev, "Missing parameters to CCQ unset\n");
		return -EINVAL;
	}

	if (in->pad) {
		dev_dbg(hdev->dev, "Padding bytes must be 0\n");
		return -EINVAL;
	}

	port = in->port;

	rc = hl_nic_ioctl_port_check(hdev, port, NIC_PORT_CHECK_ENABLE | NIC_PORT_PRINT_ON_ERR);
	if (rc)
		return rc;

	nic_port = &hdev->nic.nic_ports[port];

	port_funcs->cfg_lock(nic_port);

	if (!nic_port->ccq_enable) {
		dev_dbg(hdev->dev, "Failed unsetting CCQ handler - it is already unset, port %u\n",
			port);
		rc = -ENXIO;
		goto out;
	}

	rc = __user_ccq_unset(hdev, ctx, in->port);
out:
	port_funcs->cfg_unlock(nic_port);

	return rc;
}

static int __hl_nic_control(struct hl_device *hdev, u32 op, void *input, void *output,
				struct hl_ctx *ctx)
{
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	int rc;

	if (!nic_funcs->get_hw_cap(hdev)) {
		dev_dbg(hdev->dev, "NIC is not initialized, can't execute request %d\n", op);
		return -EFAULT;
	}

	if (!(hdev->nic.ctrl_op_mask & BIT(op))) {
		dev_dbg(hdev->dev, "NIC control request %d is not supported on this device\n", op);
		return -EOPNOTSUPP;
	}

	switch (op) {
	case HL_NIC_OP_ALLOC_CONN:
		rc = alloc_qp(hdev, ctx, input, output);
		break;
	case HL_NIC_OP_SET_REQ_CONN_CTX:
		rc = set_req_qp_ctx(hdev, input, output);
		break;
	case HL_NIC_OP_SET_RES_CONN_CTX:
		rc = set_res_qp_ctx(hdev, ctx, input);
		break;
	case HL_NIC_OP_DESTROY_CONN:
		rc = destroy_qp(hdev, input);
		break;
	case HL_NIC_OP_USER_WQ_SET:
		rc = user_wq_arr_set(hdev, input, output, ctx);
		break;
	case HL_NIC_OP_USER_WQ_UNSET:
		rc = user_wq_arr_unset(hdev, input, ctx);
		break;
	case HL_NIC_OP_USER_CQ_SET:
		rc = user_cq_set(hdev, input, ctx);
		break;
	case HL_NIC_OP_USER_CQ_UNSET:
		rc = user_cq_unset(hdev, input, ctx);
		break;
	case HL_NIC_OP_USER_CQ_UPDATE_CI:
		rc = user_cq_update_ci(hdev, input);
		break;
	case HL_NIC_OP_ALLOC_USER_CQ_ID:
		rc = alloc_user_cq_id(hdev, input, output, ctx);
		break;
	case HL_NIC_OP_SET_USER_APP_PARAMS:
		rc = user_set_app_params(hdev, input, ctx);
		break;
	case HL_NIC_OP_GET_USER_APP_PARAMS:
		rc = user_get_app_params(hdev, input, output);
		break;
	case HL_NIC_OP_EQ_POLL:
		rc = eq_poll(hdev, ctx, input, output);
		break;
	case HL_NIC_OP_ALLOC_USER_DB_FIFO:
		rc = alloc_user_db_fifo(hdev, ctx, input, output);
		break;
	case HL_NIC_OP_USER_DB_FIFO_SET:
		rc = user_db_fifo_set(hdev, ctx, input, output);
		break;
	case HL_NIC_OP_USER_DB_FIFO_UNSET:
		rc = user_db_fifo_unset(hdev, ctx, input);
		break;
	case HL_NIC_OP_USER_ENCAP_ALLOC:
		rc = user_encap_alloc(hdev, input, output);
		break;
	case HL_NIC_OP_USER_ENCAP_SET:
		rc = user_encap_set(hdev, input);
		break;
	case HL_NIC_OP_USER_ENCAP_UNSET:
		rc = user_encap_unset(hdev, input);
		break;
	case HL_NIC_OP_USER_CCQ_SET:
		rc = user_ccq_set(hdev, input, output, ctx);
		break;
	case HL_NIC_OP_USER_CCQ_UNSET:
		rc = user_ccq_unset(hdev, input, ctx);
		break;
	case HL_NIC_OP_USER_CQ_ID_SET:
		rc = user_cq_id_set(hdev, input, output, ctx);
		break;
	case HL_NIC_OP_USER_CQ_ID_UNSET:
		rc = user_cq_id_unset(hdev, input, ctx);
		break;
	case HL_NIC_OP_ALLOC_COLL_CONN:
		rc = alloc_coll_qp(hdev, ctx, input, output);
		break;
	default:
		/* we shouldn't get here as we check the opcode mask before */
		dev_dbg(hdev->dev, "Invalid NIC control request %d\n", op);
		return -EINVAL;
	}

	return rc;
}

static int hl_nic_ib_cmd_ctrl(struct hl_aux_dev *aux_dev, void *core_ctx, u32 op, void *input,
				void *output)
{
	struct hl_nic *nic = HL_AUX2NIC(aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	int rc;

	do
		rc = __hl_nic_control(hdev, op, input, output, core_ctx);
	while (rc == -EAGAIN);

	return rc;
}

static int hl_nic_ib_mmap(struct hl_aux_dev *aux_dev, void *core_ctx,
				struct vm_area_struct *vma)
{
	return __hl_mmap(((struct hl_ctx *) core_ctx)->hpriv, vma);
}

int hl_nic_control(struct hl_device *hdev, u32 op, void *input,	void *output, struct hl_ctx *ctx)
{
	return __hl_nic_control(hdev, op, input, output, ctx);
}

static void hl_nic_randomize_status_cnts(struct hl_nic_port *nic_port,
						struct cpucp_nic_status *nic_status)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	RAND_STAT_CNT(nic_status->high_ber_reinit);
	RAND_STAT_CNT(nic_status->correctable_err_cnt);
	RAND_STAT_CNT(nic_status->uncorrectable_err_cnt);
	RAND_STAT_CNT(nic_status->bad_format_cnt);
	RAND_STAT_CNT(nic_status->responder_out_of_sequence_psn_cnt);
}

static void hl_nic_get_status(struct hl_nic_port *nic_port, struct cpucp_nic_status *nic_status)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic *nic = &hdev->nic;
	u32 port = nic_port->port;

	nic_status->port = cpu_to_le32(port);
	nic_status->up = hl_nic_is_port_open(nic_port);

	if (!nic_status->up)
		return;

	nic_status->pcs_link = nic_port->pcs_link;
	nic_status->phy_ready = nic_port->phy_fw_tuned;
	nic_status->auto_neg = nic_port->auto_neg_enable;

	if (nic->rand_status) {
		hl_nic_randomize_status_cnts(nic_port, nic_status);
		return;
	}

	nic_status->high_ber_reinit = cpu_to_le32(nic_port->pcs_remote_fault_reconfig_cnt);

	/* Each ASIC will fill the rest of the statistics */
	hdev->asic_funcs->nic_funcs->port_funcs->fill_nic_status(nic_port, nic_status);
}

static void nic_status_work(struct work_struct *work)
{
	struct hl_nic_port *nic_port = container_of(work, struct hl_nic_port, nic_status_work);
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct cpucp_nic_status_packet *pkt;
	struct cpucp_nic_status nic_status = {0};
	size_t total_pkt_size, data_size;
	u64 result;
	u32 port = nic_port->port;
	int rc;

	hl_nic_get_status(nic_port, &nic_status);

	data_size = nic_props->status_packet_size;

	total_pkt_size = sizeof(struct cpucp_nic_status_packet) + data_size;

	/* data should be aligned to 8 bytes in order to CPU-CP to copy it */
	total_pkt_size = (total_pkt_size + 0x7) & ~0x7;

	/* total_pkt_size is casted to u16 later on */
	if (total_pkt_size > USHRT_MAX) {
		dev_err(hdev->dev, "NIC status data is too big\n");
		goto out;
	}

	pkt = kzalloc(total_pkt_size, GFP_KERNEL);
	if (!pkt)
		goto out;

	pkt->length = cpu_to_le32(data_size / sizeof(u32));
	memcpy(&pkt->data, &nic_status, data_size);

	pkt->cpucp_pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_STATUS << CPUCP_PKT_CTL_OPCODE_SHIFT);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) pkt,
						total_pkt_size, 0, &result);

	if (rc)
		dev_err(hdev->dev, "failed to send NIC status\n");

	kfree(pkt);
out:
	hl_fw_unmask_irq(hdev, nic_props->base_status_event_idx + port);
}

void hl_nic_send_status(struct hl_device *hdev, int port)
{
	struct hl_nic_port *nic_port = &hdev->nic.nic_ports[port];

	queue_work(nic_port->wq, &nic_port->nic_status_work);
}

static void nic_port_sw_fini(struct hl_nic_port *nic_port)
{
	struct hl_nic_funcs *nic_funcs =
			nic_port->hdev->asic_funcs->nic_funcs;

	if (!nic_port->sw_initialized)
		return;

	nic_port->sw_initialized = false;

	nic_funcs->port_funcs->port_sw_fini(nic_port);

	idr_destroy(&nic_port->cq_ids);
	idr_destroy(&nic_port->encap_ids);
	idr_destroy(&nic_port->db_fifo_ids);
	idr_destroy(&nic_port->qp_ids);

	mutex_destroy(&nic_port->cnt_lock);
	mutex_destroy(&nic_port->control_lock);

	destroy_workqueue(nic_port->qp_wq);
	destroy_workqueue(nic_port->wq);
}

static void nic_wq_arr_props_init(struct hl_wq_array_properties *wq_arr_props)
{
	wq_arr_props[HL_NIC_USER_WQ_SEND].type_str = "send";
	wq_arr_props[HL_NIC_USER_WQ_SEND].is_send = true;
	wq_arr_props[HL_NIC_USER_WQ_SEND].is_coll = false;

	wq_arr_props[HL_NIC_USER_WQ_RECV].type_str = "recv";
	wq_arr_props[HL_NIC_USER_WQ_RECV].is_send = false;
	wq_arr_props[HL_NIC_USER_WQ_RECV].is_coll = false;

	wq_arr_props[HL_NIC_USER_COLL_WQ_SEND].type_str = "collective send";
	wq_arr_props[HL_NIC_USER_COLL_WQ_SEND].is_send = true;
	wq_arr_props[HL_NIC_USER_COLL_WQ_SEND].is_coll = true;
	wq_arr_props[HL_NIC_USER_COLL_WQ_SEND].coll_wq_type = HL_NIC_COLL_CONN_TYPE_NON_SCALE_OUT;

	wq_arr_props[HL_NIC_USER_COLL_WQ_RECV].type_str = "collective recv";
	wq_arr_props[HL_NIC_USER_COLL_WQ_RECV].is_send = false;
	wq_arr_props[HL_NIC_USER_COLL_WQ_RECV].is_coll = true;
	wq_arr_props[HL_NIC_USER_COLL_WQ_RECV].coll_wq_type = HL_NIC_COLL_CONN_TYPE_NON_SCALE_OUT;

	wq_arr_props[HL_NIC_USER_COLL_SCALE_OUT_WQ_SEND].type_str = "collective scale-out send";
	wq_arr_props[HL_NIC_USER_COLL_SCALE_OUT_WQ_SEND].is_send = true;
	wq_arr_props[HL_NIC_USER_COLL_SCALE_OUT_WQ_SEND].is_coll = true;
	wq_arr_props[HL_NIC_USER_COLL_SCALE_OUT_WQ_SEND].coll_wq_type =
						HL_NIC_COLL_CONN_TYPE_SCALE_OUT;

	wq_arr_props[HL_NIC_USER_COLL_SCALE_OUT_WQ_RECV].type_str = "collective scale-out recv";
	wq_arr_props[HL_NIC_USER_COLL_SCALE_OUT_WQ_RECV].is_send = false;
	wq_arr_props[HL_NIC_USER_COLL_SCALE_OUT_WQ_RECV].is_coll = true;
	wq_arr_props[HL_NIC_USER_COLL_SCALE_OUT_WQ_RECV].coll_wq_type =
						HL_NIC_COLL_CONN_TYPE_SCALE_OUT;
}

static int nic_port_sw_init(struct hl_nic_port *nic_port)
{
	struct hl_wq_array_properties *wq_arr_props;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_funcs *nic_funcs;
	u32 port = nic_port->port;
	char wq_name[32] = {0};
	int rc;

	nic_funcs = hdev->asic_funcs->nic_funcs;
	port_funcs = nic_funcs->port_funcs;
	wq_arr_props = nic_port->wq_arr_props;

	snprintf(wq_name, sizeof(wq_name) - 1, "nic%d-wq", port);
	nic_port->wq = alloc_workqueue(wq_name, 0, 0);
	if (!nic_port->wq) {
		dev_err(hdev->dev, "Failed to create NIC WQ, port: %d\n", port);
		return -ENOMEM;
	}

	snprintf(wq_name, sizeof(wq_name) - 1, "nic%d-qp_wq", port);
	nic_port->qp_wq = alloc_workqueue(wq_name, WQ_UNBOUND, 0);
	if (!nic_port->qp_wq) {
		dev_err(hdev->dev, "Failed to create NIC QP WQ, port: %d\n", port);
		destroy_workqueue(nic_port->wq);
		return -ENOMEM;
	}

	mutex_init(&nic_port->control_lock);
	mutex_init(&nic_port->cnt_lock);

	idr_init(&nic_port->qp_ids);
	idr_init(&nic_port->db_fifo_ids);
	idr_init(&nic_port->encap_ids);
	idr_init(&nic_port->cq_ids);

	INIT_WORK(&nic_port->nic_status_work, nic_status_work);
	INIT_DELAYED_WORK(&nic_port->link_status_work, port_funcs->phy_link_status_work);

	nic_port->speed = nic_funcs->get_default_port_speed(hdev);
	nic_port->pfc_enable = true;
	nic_port->pflags = PFLAGS_PCS_LINK_CHECK | PFLAGS_PHY_AUTO_NEG_LPBK;

	nic_wq_arr_props_init(wq_arr_props);

	rc = port_funcs->port_sw_init(nic_port);
	if (rc)
		goto err;

	nic_port->sw_initialized = true;

	return 0;

err:
	idr_destroy(&nic_port->cq_ids);
	idr_destroy(&nic_port->encap_ids);
	idr_destroy(&nic_port->db_fifo_ids);
	idr_destroy(&nic_port->qp_ids);

	mutex_destroy(&nic_port->cnt_lock);
	mutex_destroy(&nic_port->control_lock);

	destroy_workqueue(nic_port->qp_wq);
	destroy_workqueue(nic_port->wq);

	return rc;
}

static void nic_coll_props_init(struct hl_coll_properties *coll_props)
{
	idr_init(&coll_props[HL_NIC_COLL_CONN_TYPE_NON_SCALE_OUT].coll_qp_ids);
	atomic_set(&coll_props[HL_NIC_COLL_CONN_TYPE_NON_SCALE_OUT].num_of_coll_wq_arrays, 0);
	coll_props[HL_NIC_COLL_CONN_TYPE_NON_SCALE_OUT].swq_type = HL_NIC_USER_COLL_WQ_SEND;
	coll_props[HL_NIC_COLL_CONN_TYPE_NON_SCALE_OUT].rwq_type = HL_NIC_USER_COLL_WQ_RECV;

	idr_init(&coll_props[HL_NIC_COLL_CONN_TYPE_SCALE_OUT].coll_qp_ids);
	atomic_set(&coll_props[HL_NIC_COLL_CONN_TYPE_SCALE_OUT].num_of_coll_wq_arrays, 0);
	coll_props[HL_NIC_COLL_CONN_TYPE_SCALE_OUT].swq_type = HL_NIC_USER_COLL_SCALE_OUT_WQ_SEND;
	coll_props[HL_NIC_COLL_CONN_TYPE_SCALE_OUT].rwq_type = HL_NIC_USER_COLL_SCALE_OUT_WQ_RECV;
}

static void nic_coll_props_fini(struct hl_coll_properties *coll_props)
{
	idr_destroy(&coll_props[HL_NIC_COLL_CONN_TYPE_NON_SCALE_OUT].coll_qp_ids);
	idr_destroy(&coll_props[HL_NIC_COLL_CONN_TYPE_SCALE_OUT].coll_qp_ids);
}

static int nic_macro_sw_init(struct hl_nic_macro *nic_macro)
{
	struct hl_nic_funcs *nic_funcs;

	nic_funcs = nic_macro->hdev->asic_funcs->nic_funcs;

	return nic_funcs->macro_sw_init(nic_macro);
}

static void nic_macro_sw_fini(struct hl_nic_macro *nic_macro)
{
	struct hl_nic_funcs *nic_funcs;

	nic_funcs = nic_macro->hdev->asic_funcs->nic_funcs;

	nic_funcs->macro_sw_fini(nic_macro);
}

void hl_nic_sw_fini(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	struct hl_nic *nic = &hdev->nic;
	int i;

	for (i = 0 ; i < nic_props->max_num_of_ports ; i++)
		nic_port_sw_fini(&nic->nic_ports[i]);

	nic_coll_props_fini(nic->coll_props);

	for (i = 0 ; i < nic_props->num_of_macros ; i++)
		nic_macro_sw_fini(&nic->nic_macros[i]);

	nic_funcs->sw_fini(hdev);

	kfree(nic->ib_aux_dev.core_info);
	kfree(nic->ib_aux_dev.aux_ops);
	kfree(nic->en_aux_dev.core_info);
	kfree(nic->en_aux_dev.aux_ops);
	kfree(nic->mac_lane_remap);
	kfree(nic->phy_tx_taps);
	kfree(nic->nic_macros);
	kfree(nic->nic_ports);
}

int hl_nic_sw_init(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	struct hl_nic_macro *nic_macro, *nic_macros;
	struct hl_nic_port *nic_port, *nic_ports;
	int rc, i, macro_cnt = 0, port_cnt = 0;
	struct hl_en_core_info *en_core_info;
	struct hl_ib_core_info *ib_core_info;
	struct hl_en_aux_ops *en_aux_ops;
	struct hl_ib_aux_ops *ib_aux_ops;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_tx_taps *tx_taps;
	u32 *mac_lane_remap;

	/* Allocate per port common structure array */
	nic_ports = kcalloc(nic_props->max_num_of_ports, sizeof(*nic_ports), GFP_KERNEL);
	if (!nic_ports)
		return -ENOMEM;

	/* Allocate per macro common structure array */
	nic_macros = kcalloc(nic_props->num_of_macros, sizeof(*nic_macros), GFP_KERNEL);
	if (!nic_macros) {
		rc = -ENOMEM;
		goto macro_alloc_fail;
	}

	tx_taps = kcalloc(nic_props->max_num_of_lanes, sizeof(*tx_taps), GFP_KERNEL);
	if (!tx_taps) {
		rc = -ENOMEM;
		goto taps_alloc_fail;
	}

	mac_lane_remap = kcalloc(nic_props->num_of_macros, sizeof(*mac_lane_remap), GFP_KERNEL);
	if (!mac_lane_remap) {
		rc = -ENOMEM;
		goto mac_remap_alloc_fail;
	}

	en_core_info = kzalloc(sizeof(*en_core_info), GFP_KERNEL);
	if (!en_core_info) {
		rc = -ENOMEM;
		goto en_core_info_alloc_fail;
	}

	en_aux_ops = kzalloc(sizeof(*en_aux_ops), GFP_KERNEL);
	if (!en_aux_ops) {
		rc = -ENOMEM;
		goto en_aux_ops_alloc_fail;
	}

	ib_core_info = kzalloc(sizeof(*ib_core_info), GFP_KERNEL);
	if (!ib_core_info) {
		rc = -ENOMEM;
		goto ib_core_info_alloc_fail;
	}

	ib_aux_ops = kzalloc(sizeof(*ib_aux_ops), GFP_KERNEL);
	if (!ib_aux_ops) {
		rc = -ENOMEM;
		goto ib_aux_ops_alloc_fail;
	}

	nic->en_aux_dev.core_info = en_core_info;
	nic->en_aux_dev.aux_ops = en_aux_ops;
	nic->ib_aux_dev.core_info = ib_core_info;
	nic->ib_aux_dev.aux_ops = ib_aux_ops;

	rc = nic_funcs->sw_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "NIC ASIC SW init failed, rc: %d\n", rc);
		goto sw_init_fail;
	}

	nic->nic_ports = nic_ports;
	nic->nic_macros = nic_macros;
	for (i = 0 ; i < nic_props->num_of_macros ; i++, macro_cnt++) {
		nic_macro = &nic->nic_macros[i];

		nic_macro->hdev = hdev;
		nic_macro->idx = i;
		nic_macro->phy_macro_needs_reset = true;

		rc = nic_macro_sw_init(nic_macro);
		if (rc) {
			dev_err(hdev->dev, "Macro %d SW init failed, rc: %d\n", i, rc);
			goto macro_init_fail;
		}
	}

	nic->phy_tx_taps = tx_taps;
	nic->mac_lane_remap = mac_lane_remap;
	nic->pcs_fail_time_frame = NIC_PCS_FAIL_TIME_FRAME_SEC;
	nic->pcs_fail_threshold = NIC_PCS_FAIL_THRESHOLD;
	nic->phy_config_fw = !hdev->pldm && !hdev->skip_nic_phy_init;
	nic->mmu_bypass = 1;

	/* Boot CPU loads the PHY F/W at boot */
	nic->phy_load_fw = (!(hdev->fw_components & FW_TYPE_BOOT_CPU) && !hdev->pldm) ||
				(hdev->nic_load_fw);
	nic->debugfs_reset = true;

	nic_coll_props_init(nic->coll_props);

	/* At this stage, we don't know how many ports we have, so we must
	 * allocate for the maximum number of ports (and also free all of them
	 * in sw_fini)
	 */
	for (i = 0 ; i < nic_props->max_num_of_ports ; i++, port_cnt++) {
		nic_port = &nic->nic_ports[i];
		nic_port->hdev = hdev;
		nic_port->port = i;
		atomic_set(&nic_port->num_of_allocated_qps, 0);
		atomic_set(&nic_port->num_of_allocated_coll_qps, 0);
		atomic_set(&nic_port->num_of_allocated_scale_out_coll_qps, 0);
		rc = nic_port_sw_init(nic_port);
		if (rc) {
			dev_err(hdev->dev, "NIC S/W init failed, port: %d, rc: %d\n", i, rc);
			goto port_init_fail;
		}
	}

	return 0;

port_init_fail:
	for (i = 0 ; i < port_cnt ; i++)
		nic_port_sw_fini(&nic->nic_ports[i]);

	nic_coll_props_fini(nic->coll_props);
macro_init_fail:
	for (i = 0 ; i < macro_cnt ; i++)
		nic_macro_sw_fini(&nic->nic_macros[i]);

	nic_funcs->sw_fini(hdev);
sw_init_fail:
	kfree(ib_aux_ops);
ib_aux_ops_alloc_fail:
	kfree(ib_core_info);
ib_core_info_alloc_fail:
	kfree(en_aux_ops);
en_aux_ops_alloc_fail:
	kfree(en_core_info);
en_core_info_alloc_fail:
	kfree(mac_lane_remap);
mac_remap_alloc_fail:
	kfree(tx_taps);
taps_alloc_fail:
	kfree(nic_macros);
macro_alloc_fail:
	kfree(nic_ports);

	return rc;
}

static void wq_arrays_pool_destroy(struct hl_ctx *ctx)
{
	struct hl_nic_ctx *nic_ctx = &ctx->nic_ctx;
	struct hl_device *hdev = ctx->hdev;
	struct hl_nic *nic = &hdev->nic;

	if (nic->skip_wq_arrays_pool)
		return;

	gen_pool_destroy(nic_ctx->wq_arrays_pool);
}

static int wq_arrays_pool_alloc(struct hl_ctx *ctx)
{
	struct hl_nic_ctx *nic_ctx = &ctx->nic_ctx;
	struct asic_fixed_properties *asic_props;
	struct hl_nic_properties *nic_props;
	struct hl_device *hdev = ctx->hdev;
	struct hl_nic *nic = &hdev->nic;
	int rc;

	if (nic->skip_wq_arrays_pool)
		return 0;

	asic_props = &hdev->asic_prop;
	nic_props = &asic_props->nic_props;

	nic_ctx->wq_arrays_pool = gen_pool_create(ilog2(asic_props->cache_line_size), -1);
	if (!nic_ctx->wq_arrays_pool) {
		dev_err(hdev->dev, "Failed to create a pool to manage WQ arrays on HBM\n");
		rc = -ENOMEM;
		goto gen_pool_create_fail;
	}

	gen_pool_set_algo(nic_ctx->wq_arrays_pool, gen_pool_best_fit, NULL);

	rc = gen_pool_add(nic_ctx->wq_arrays_pool, nic_props->wq_base_addr,
				nic_props->wq_base_size, -1);
	if (rc) {
		dev_err(hdev->dev, "Failed to add memory to the WQ arrays pool\n");
		goto gen_pool_add_fail;
	}

	return 0;

gen_pool_add_fail:
	gen_pool_destroy(nic_ctx->wq_arrays_pool);
gen_pool_create_fail:
	return rc;
}

int hl_nic_ctx_init(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	int rc;

	if (ctx->asid == HL_KERNEL_ASID_ID)
		return hdev->asic_funcs->nic_funcs->kernel_ctx_init(ctx);

	rc = wq_arrays_pool_alloc(ctx);
	if (rc)
		goto wq_arrays_pool_alloc_fail;

	rc = hdev->asic_funcs->nic_funcs->ctx_init(ctx);
	if (rc)
		goto ctx_init_fail;

	return 0;

ctx_init_fail:
	wq_arrays_pool_destroy(ctx);
wq_arrays_pool_alloc_fail:
	return rc;
}

static void ccqs_destroy(struct hl_device *hdev, struct hl_ctx *ctx)
{
	struct hl_nic_port *nic_port;
	int port;

	for (port = 0 ; port < hdev->asic_prop.nic_props.max_num_of_ports ; port++) {
		if (!(hdev->nic_ports_mask & BIT(port)))
			continue;

		nic_port = &hdev->nic.nic_ports[port];
		if (nic_port->ccq_enable)
			__user_ccq_unset(hdev, ctx, port);
	}
}

static void set_app_params_clear(struct hl_device *hdev)
{
	struct hl_nic_port *nic_port;
	int port;

	for (port = 0 ; port < hdev->asic_prop.nic_props.max_num_of_ports ; port++) {
		if (!(hdev->nic_ports_mask & BIT(port)))
			continue;

		nic_port = &hdev->nic.nic_ports[port];
		nic_port->set_app_params = false;
	}
}

void hl_nic_ctx_fini(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct hl_nic_funcs *nic_funcs;

	nic_funcs = hdev->asic_funcs->nic_funcs;

	if (ctx->asid == HL_KERNEL_ASID_ID) {
		nic_funcs->kernel_ctx_fini(ctx);
	} else if (nic_funcs->get_hw_cap(hdev)) {
		qps_destroy(hdev);
		user_cqs_destroy(hdev, ctx);
		wq_arrs_destroy(hdev, ctx);
		ccqs_destroy(hdev, ctx);
		nic_funcs->ctx_fini(ctx);
		encap_ids_destroy(hdev, ctx);
		set_app_params_clear(hdev);
	}
}

int hl_nic_request_irqs(struct hl_device *hdev)
{
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;

	if (!hdev->nic_ports_mask)
		return 0;

	return nic_funcs->request_irqs(hdev);
}

void hl_nic_free_irqs(struct hl_device *hdev)
{
	struct hl_nic_funcs *nic_funcs =  hdev->asic_funcs->nic_funcs;

	if (hdev->nic_ports_mask)
		nic_funcs->free_irqs(hdev);
}

void hl_nic_synchronize_irqs(struct hl_device *hdev)
{
	struct hl_nic_funcs *nic_funcs =  hdev->asic_funcs->nic_funcs;

	if (hdev->nic_ports_mask)
		nic_funcs->synchronize_irqs(hdev);
}

bool hl_nic_is_port_open(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_aux_dev *aux_dev = &hdev->nic.en_aux_dev;
	struct hl_en_aux_ops *aux_ops = aux_dev->aux_ops;
	u32 port = nic_port->port;

	if (aux_ops->is_port_open && nic_port->eth_enable)
		return aux_ops->is_port_open(aux_dev, port);

	return nic_port->port_open;
}

u32 hl_nic_get_pflags(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_aux_dev *aux_dev = &hdev->nic.en_aux_dev;
	struct hl_en_aux_ops *aux_ops = aux_dev->aux_ops;
	u32 port = nic_port->port;

	if (nic_port->eth_enable)
		return aux_ops->get_pflags(aux_dev, port);

	return nic_port->pflags;
}

u8 hl_nic_get_num_of_digits(u64 num)
{
	u8 n_digits = 0;

	while (num) {
		n_digits++;
		num /= 10;
	}

	return n_digits;
}

void hl_nic_spmu_init(struct hl_device *hdev, int port, bool full)
{
	u32 spmu_events[NIC_SPMU_STATS_LEN_MAX], num_event_types;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_en_stat *event_types;
	struct hl_nic_port *nic_port;
	int rc, i;

	if (!hdev->supports_coresight)
		return;

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	nic_port = &hdev->nic.nic_ports[port];

	port_funcs->spmu_get_stats_info(nic_port, &event_types, &num_event_types);
	num_event_types = min(num_event_types, (u32) NIC_SPMU_STATS_LEN_MAX);

	for (i = 0 ; i < num_event_types ; i++)
		spmu_events[i] = event_types[i].lo_offset;

	if (full) {
		rc = port_funcs->spmu_config(nic_port, num_event_types, spmu_events, false);
		if (rc)
			dev_err(hdev->dev, "Failed to disable spmu for NIC port %d\n", port);
	}

	rc = port_funcs->spmu_config(nic_port, num_event_types, spmu_events, true);
	if (rc)
		dev_err(hdev->dev, "Failed to enable spmu for NIC port %d\n", port);
}

void hl_nic_reset_ethtool_cnt(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic *nic = &hdev->nic;
	struct hl_en_aux_ops *aux_ops;
	struct hl_nic_port *nic_port;
	struct hl_aux_dev *aux_dev;
	u32 port;
	int i;

	aux_dev = &nic->en_aux_dev;
	aux_ops = aux_dev->aux_ops;
	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;

	for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];

		if (!hl_nic_is_port_open(nic_port))
			continue;

		port = nic_port->port;

		/* Ethernet */
		if (nic_port->eth_enable)
			aux_ops->reset_stats(aux_dev, port);

		/* MAC */
		port_funcs->reset_mac_stats(nic_port);

		/* SPMU */
		hl_nic_spmu_init(hdev, i, true);

		/* XPCS91 */
		nic_port->correctable_errors_cnt = 0;
		nic_port->uncorrectable_errors_cnt = 0;

		/* PCS */
		nic_port->pcs_local_fault_cnt = 0;
		nic_port->pcs_remote_fault_cnt = 0;
		nic_port->pcs_remote_fault_reconfig_cnt = 0;
		nic_port->pcs_link_restore_cnt = 0;
	}
}

/*
 * The following implements the events dispatcher
 * Each application registering with the device is assigned a unique ASID
 * by the driver, it is also being associated with a SW-EQ by the dispatcher
 * (The Eth driver is handled by the kernel associated with ASID 0).
 * during the lifetime of the app/ASID, each resource allocated to it
 * that can generate events (such as QP and CQ) is being associated by the
 * dispatcher the appropriate ASID.
 * During the course of work of the NIC, the HW EQ is accessed
 * (by poling or interrupt), and for each event found in it
 * - The resource ID which generated the event is retrieved from it (CQ# or QP#)
 * - The ASID it retrieved from the ASID-resource association lists,
 * - The event is inserted to the ASID-specific SW-EQ to be retrieved later on
 *   by the app. An exception is the Eth driver which as for today is tightly
 *   coupled with the EQ so the dispatcher calls the Eth event handling routine
 *   (if registered) immediately after dispatching the events to the SW-EQs.
 * Note: The Link events which are always handled by the Eth driver (ASID 0).
 */

struct hl_nic_ev_dq *hl_nic_cqn_to_dq(struct hl_nic_ev_dqs *ev_dqs,
					u32 cqn, struct hl_device *hdev)
{
	struct hl_nic_properties *nic_prop = &hdev->asic_prop.nic_props;
	struct hl_nic_ev_dq *dq;

	if (cqn >= nic_prop->max_cqs)
		return NULL;

	dq = ev_dqs->cq_dq[cqn];
	if ((dq == NULL) || !dq->associated)
		return NULL;

	return dq;
}

struct hl_nic_ev_dq *hl_nic_ccqn_to_dq(struct hl_nic_ev_dqs *ev_dqs, u32 ccqn,
					struct hl_device *hdev)
{
	struct hl_nic_properties *nic_prop = &hdev->asic_prop.nic_props;
	struct hl_nic_ev_dq *dq;

	if (ccqn >= nic_prop->max_ccqs)
		return NULL;

	dq = ev_dqs->ccq_dq[ccqn];
	if ((dq == NULL) || !dq->associated)
		return NULL;

	return dq;
}

struct hl_nic_dq_qp_info *hl_nic_get_qp_info(struct hl_nic_ev_dqs *ev_dqs, u32 qpn)
{
	struct hl_nic_dq_qp_info *qp_info = NULL;

	hash_for_each_possible(ev_dqs->qps, qp_info, node, qpn)
		if (qpn == qp_info->qpn)
			return qp_info;

	return NULL;
}

struct hl_nic_ev_dq *hl_nic_qpn_to_dq(struct hl_nic_ev_dqs *ev_dqs, u32 qpn)
{
	struct hl_nic_dq_qp_info *qp_info = hl_nic_get_qp_info(ev_dqs, qpn);

	if (qp_info)
		return qp_info->dq;

	return NULL;
}

struct hl_nic_ev_dq *hl_nic_dbn_to_dq(struct hl_nic_ev_dqs *ev_dqs, u32 dbn,
					struct hl_device *hdev)
{
	struct hl_nic_properties *nic_prop = &hdev->asic_prop.nic_props;
	struct hl_nic_ev_dq *dq;

	if (dbn >= nic_prop->max_db_fifos)
		return NULL;

	dq = ev_dqs->db_dq[dbn];
	if ((dq == NULL) || !dq->associated)
		return NULL;

	return dq;
}

struct hl_nic_ev_dq *hl_nic_asid_to_dq(struct hl_nic_ev_dqs *ev_dqs, u32 asid)
{
	struct hl_nic_ev_dq *dq;
	int i;

	for (i = 0 ; i < NIC_NUM_CONCUR_ASIDS ; i++) {
		dq = &ev_dqs->edq[i];
		if (dq->associated && (dq->asid == asid))
			return dq;
	}

	return NULL;
}

void hl_nic_dq_reset(struct hl_nic_ev_dq *dq)
{
	struct hl_nic_eq_raw_buf *buf = &dq->buf;

	dq->overflow = 0;
	buf->head = buf->tail = 0;
	buf->events_count = 0;
	memset(buf->events, 0, sizeof(buf->events));
}

bool hl_nic_eq_dispatcher_is_empty(struct hl_nic_ev_dq *dq)
{
	return (dq->buf.events_count == 0);
}

bool hl_nic_eq_dispatcher_is_full(struct hl_nic_ev_dq *dq)
{
	return (dq->buf.events_count == (NIC_EQ_INFO_BUF_SIZE - 1));
}

void hl_nic_eq_dispatcher_init(struct hl_nic_port *nic_port)
{
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
#if HL_NIC_DEBUG
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
#endif
	int i;

	hash_init(ev_dqs->qps);
	mutex_init(&ev_dqs->lock);

	hl_nic_dq_reset(&ev_dqs->default_edq);

	for (i = 0 ; i < NIC_NUM_CONCUR_ASIDS ; i++)
		hl_nic_dq_reset(&ev_dqs->edq[i]);

	for (i = 0 ; i < NIC_DRV_MAX_CQS_NUM ; i++)
		ev_dqs->cq_dq[i] = NULL;

	for (i = 0 ; i < NIC_DRV_NUM_DB_FIFOS ; i++)
		ev_dqs->db_dq[i] = NULL;

#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "%s port %d\n", __func__, port);
#endif
}

void hl_nic_eq_dispatcher_fini(struct hl_nic_port *nic_port)
{
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_ev_dqs *edqs = ev_dqs;
	struct hl_nic_dq_qp_info *qp_info;
	struct hlist_node *tmp;
	u32 port = nic_port->port;
	int i;

	if (!hash_empty(edqs->qps))
		dev_err(hdev->dev,
			"port %d dispatcher is closed while there are QPs in use\n",
			port);

	hash_for_each_safe(edqs->qps, i, tmp, qp_info, node) {
		dev_err_ratelimited(hdev->dev,
				"port %d QP %d was not destroyed\n",
				port, qp_info->qpn);
		hash_del(&qp_info->node);
		kfree(qp_info);
	}

	mutex_destroy(&ev_dqs->lock);

#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "%s port %d\n", __func__, port);
#endif
}

void hl_nic_eq_dispatcher_reset(struct hl_nic_port *nic_port)
{
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
	struct hl_nic_ev_dqs *edqs = ev_dqs;
#if HL_NIC_DEBUG
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
#endif
	int i;

	mutex_lock(&edqs->lock);

	hl_nic_dq_reset(&edqs->default_edq);

	for (i = 0; i < NIC_NUM_CONCUR_ASIDS ; i++)
		hl_nic_dq_reset(&edqs->edq[i]);

	mutex_unlock(&edqs->lock);

#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "%s port %d\n", __func__, port);
#endif
}

int hl_nic_eq_dispatcher_associate_dq(struct hl_nic_port *nic_port, u32 asid)
{
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
#if HL_NIC_DEBUG
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
#endif
	struct hl_nic_ev_dq *dq;
	int i, rc = -ENOSPC;

	mutex_lock(&ev_dqs->lock);

	dq = hl_nic_asid_to_dq(ev_dqs, asid);
	if (dq) {
		rc = 0;
		goto exit;
	}

	for (i = 0 ; i < NIC_NUM_CONCUR_ASIDS ; i++) {
		dq = &(ev_dqs->edq[i]);
		if (!dq->associated) {
			dq->associated = true;
			dq->asid = asid;
			rc = 0;
			break;
		}
	}

exit:
	mutex_unlock(&ev_dqs->lock);
#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "%s port %d, asid %d\n", __func__, port, asid);
#endif

	return rc;
}

int hl_nic_eq_dispatcher_dissociate_dq(struct hl_nic_port *nic_port, u32 asid)
{
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
#if HL_NIC_DEBUG
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
#endif
	struct hl_nic_ev_dq *dq;

	mutex_lock(&ev_dqs->lock);

	dq = hl_nic_asid_to_dq(ev_dqs, asid);
	if (dq == NULL)
		goto exit;

	hl_nic_dq_reset(dq);
	dq->associated = false;
	dq->asid = U32_MAX;

exit:
	mutex_unlock(&ev_dqs->lock);
#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "%s port %d, asid %d\n", __func__, port, asid);
#endif

	return 0;
}

int hl_nic_eq_dispatcher_register_qp(struct hl_nic_port *nic_port, u32 asid, u32 qp_id)
{
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
#if HL_NIC_DEBUG
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
#endif
	struct hl_nic_dq_qp_info *qp_info;
	struct hl_nic_ev_dq *dq;
	int rc = 0;

	mutex_lock(&ev_dqs->lock);

	/* check if such qp is already registered and if with the same
	 * asid
	 */
	dq = hl_nic_qpn_to_dq(ev_dqs, qp_id);
	if (dq) {
		if (dq->asid != asid)
			rc = -EINVAL;

		goto exit;
	}

	/* find the dq associated with the given asid */
	dq = hl_nic_asid_to_dq(ev_dqs, asid);
	if (dq == NULL) {
		rc = -ENODATA;
		goto exit;
	}

	/* register the QP */
	qp_info = kmalloc(sizeof(*qp_info), GFP_KERNEL);
	if (qp_info == NULL) {
		rc = -ENOMEM;
		goto exit;
	}

	qp_info->dq = dq;
	qp_info->qpn = qp_id;
	hash_add(ev_dqs->qps, &qp_info->node, qp_id);

exit:
	mutex_unlock(&ev_dqs->lock);
#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "%s port %d, asid %d, qp %d\n",
			__func__, port, asid, qp_id);
#endif

	return rc;
}

int hl_nic_eq_dispatcher_unregister_qp(struct hl_nic_port *nic_port, u32 qp_id)
{
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
	struct hl_nic_dq_qp_info *qp_info;
#if HL_NIC_DEBUG
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
#endif

	mutex_lock(&ev_dqs->lock);

	qp_info = hl_nic_get_qp_info(ev_dqs, qp_id);
	if (qp_info) {
		hash_del(&qp_info->node);
		kfree(qp_info);
	}

	mutex_unlock(&ev_dqs->lock);
#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "%s port %d qp %d\n", __func__, port, qp_id);
#endif

	return 0;
}

int hl_nic_eq_dispatcher_register_cq(struct hl_nic_port *nic_port, u32 asid, u32 cqn)
{
	struct hl_nic_properties *nic_prop = &nic_port->hdev->asic_prop.nic_props;
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
	struct hl_nic_ev_dq *dq;
#if HL_NIC_DEBUG
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
#endif
	int rc = 0;

	if (cqn >= nic_prop->max_cqs)
		return -EINVAL;

	mutex_lock(&ev_dqs->lock);

	/* check if such qp is already registered and if with the same
	 * asid
	 */
	dq = ev_dqs->cq_dq[cqn];
	if (dq) {
		if (dq->asid != asid)
			rc = -EINVAL;

		goto exit;
	}

	/* find the dq associated with the given asid */
	dq = hl_nic_asid_to_dq(ev_dqs, asid);
	if (dq == NULL) {
		rc = -ENODATA;
		goto exit;
	}

	ev_dqs->cq_dq[cqn] = dq;

exit:
	mutex_unlock(&ev_dqs->lock);
#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "%s port %d, asid %d, cqn %d\n",
			__func__, port, asid, cqn);
#endif

	return rc;
}

int hl_nic_eq_dispatcher_unregister_cq(struct hl_nic_port *nic_port, u32 cqn)
{
	struct hl_nic_properties *nic_prop = &nic_port->hdev->asic_prop.nic_props;
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
#if HL_NIC_DEBUG
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
#endif

	if (cqn >= nic_prop->max_cqs)
		return -EINVAL;

	mutex_lock(&ev_dqs->lock);

	ev_dqs->cq_dq[cqn] = NULL;

	mutex_unlock(&ev_dqs->lock);
#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "%s port %d cq %d\n", __func__, port, cqn);
#endif

	return 0;
}

int hl_nic_eq_dispatcher_register_ccq(struct hl_nic_port *nic_port, u32 asid, u32 ccqn)
{
	struct hl_nic_properties *nic_prop = &nic_port->hdev->asic_prop.nic_props;
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
#if HL_NIC_DEBUG
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
#endif
	struct hl_nic_ev_dq *dq;
	int rc = 0;

	if (ccqn >= nic_prop->max_ccqs)
		return -EINVAL;

	mutex_lock(&ev_dqs->lock);

	/* check if such qp is already registered and if with the same asid */
	dq = ev_dqs->ccq_dq[ccqn];
	if (dq) {
		rc = -EINVAL;
		goto exit;
	}

	/* find the dq associated with the given asid */
	dq = hl_nic_asid_to_dq(ev_dqs, asid);
	if (dq == NULL) {
		rc = -ENODATA;
		goto exit;
	}

	ev_dqs->ccq_dq[ccqn] = dq;

exit:
	mutex_unlock(&ev_dqs->lock);
#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "%s port %d, asid %d, ccqn %d\n",
			__func__, port, asid, ccqn);
#endif
	return rc;
}

int hl_nic_eq_dispatcher_unregister_ccq(struct hl_nic_port *nic_port, u32 asid, u32 ccqn)
{
	struct hl_nic_properties *nic_prop = &nic_port->hdev->asic_prop.nic_props;
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
#if HL_NIC_DEBUG
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
#endif

	if (ccqn >= nic_prop->max_ccqs)
		return -EINVAL;

	if (!hl_nic_asid_to_dq(ev_dqs, asid))
		return -ENODATA;

	mutex_lock(&ev_dqs->lock);

	ev_dqs->ccq_dq[ccqn] = NULL;

	mutex_unlock(&ev_dqs->lock);
#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "%s port %d ccq %d\n", __func__, port, ccqn);
#endif

	return 0;
}

int hl_nic_eq_dispatcher_register_db(struct hl_nic_port *nic_port, u32 asid, u32 dbn)
{
	struct hl_nic_properties *nic_prop = &nic_port->hdev->asic_prop.nic_props;
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
	struct hl_nic_ev_dq *dq;
#if HL_NIC_DEBUG
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
#endif
	int rc = 0;

	if (dbn >= nic_prop->max_db_fifos)
		return -EINVAL;

	mutex_lock(&ev_dqs->lock);

	/* check if doorbell is already registered and if so is it with the same
	 * asid
	 */
	dq = ev_dqs->db_dq[dbn];
	if (dq) {
		if (dq->asid != asid)
			rc = -EINVAL;

		goto exit;
	}

	/* find the dq associated with the given asid and transport */
	dq = hl_nic_asid_to_dq(ev_dqs, asid);
	if (dq == NULL) {
		rc = -ENODATA;
		goto exit;
	}

	ev_dqs->db_dq[dbn] = dq;

exit:
	mutex_unlock(&ev_dqs->lock);
#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "%s port %d, asid %d, dbn %d\n",
			__func__, port, asid, dbn);
#endif

	return rc;
}

int hl_nic_eq_dispatcher_unregister_db(struct hl_nic_port *nic_port, u32 dbn)
{
	struct hl_nic_properties *nic_prop = &nic_port->hdev->asic_prop.nic_props;
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
#if HL_NIC_DEBUG
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
#endif

	if (dbn >= nic_prop->max_db_fifos)
		return -EINVAL;

	mutex_lock(&ev_dqs->lock);

	ev_dqs->db_dq[dbn] = NULL;

	mutex_unlock(&ev_dqs->lock);
#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "%s port %d db %d\n", __func__, port, dbn);
#endif

	return 0;
}

int hl_nic_eq_dispatcher_enqueue(struct hl_nic_port *nic_port, const struct hl_nic_eqe *eqe)
{
	struct hl_aux_dev *aux_dev =  &nic_port->hdev->nic.ib_aux_dev;
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
	struct hl_ib_aux_ops *aux_ops = aux_dev->aux_ops;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_ev_dq *dq;
	int rc;

	if (!hl_nic_is_port_open(nic_port))
		return 0;

	port_funcs = nic_port->hdev->asic_funcs->nic_funcs->port_funcs;

	mutex_lock(&ev_dqs->lock);

	dq = port_funcs->eq_dispatcher_select_dq(nic_port, eqe);
	if (dq == NULL) {
		rc = -ENODATA;
		goto exit;
	}

	if (hl_nic_eq_dispatcher_is_full(dq)) {
		dq->overflow++;
		rc = -ENOSPC;
		goto exit;
	}

	memcpy(&dq->buf.events[dq->buf.head], eqe,
		min(sizeof(*eqe), sizeof(dq->buf.events[0])));
	dq->buf.head = (dq->buf.head + 1) & (NIC_EQ_INFO_BUF_SIZE - 1);
	dq->buf.events_count++;

	/* If IB device exist, call work scheduler for hlib to poll eq */
	if (aux_ops->eqe_work_schd)
		aux_ops->eqe_work_schd(aux_dev, nic_port->port);

	rc = 0;

exit:
	mutex_unlock(&ev_dqs->lock);
	return rc;
}

int hl_nic_eq_dispatcher_dequeue(struct hl_nic_port *nic_port, u32 asid,
					struct hl_nic_eqe *eqe, bool is_default)
{
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
	struct hl_nic_ev_dq *dq;
	int rc;

	mutex_lock(&ev_dqs->lock);

	if (is_default)
		dq = &ev_dqs->default_edq;
	else
		dq = hl_nic_asid_to_dq(ev_dqs, asid);

	if (dq == NULL) {
		rc = -ESRCH;
		goto exit;
	}

	if (hl_nic_eq_dispatcher_is_empty(dq)) {
		rc = -ENODATA;
		goto exit;
	}

	/* We do a copy here instead of returning a pointer since a reset or
	 * destroy operation may occur after we return from the routine
	 */
	memcpy(eqe, &dq->buf.events[dq->buf.tail],
		min(sizeof(*eqe), sizeof(dq->buf.events[0])));

	dq->buf.tail = (dq->buf.tail + 1) & (NIC_EQ_INFO_BUF_SIZE - 1);
	dq->buf.events_count--;
	rc = 0;

exit:
	mutex_unlock(&ev_dqs->lock);
	return rc;
}

u8 hl_nic_dram_readb(struct hl_device *hdev, u64 addr)
{
	u64 val = 0;
	int rc;

	rc = hdev->asic_funcs->access_dev_mem(hdev, PCI_REGION_DRAM, addr, &val, DEBUGFS_READ8);
	if (rc)
		dev_crit(hdev->dev, "Failed to readb from dev_mem addr 0x%llx\n", addr);

	return val;
}

u32 hl_nic_dram_readl(struct hl_device *hdev, u64 addr)
{
	u64 val = 0;
	int rc;

	rc = hdev->asic_funcs->access_dev_mem(hdev, PCI_REGION_DRAM, addr, &val, DEBUGFS_READ32);
	if (rc)
		dev_crit(hdev->dev, "Failed to readl from dev_mem addr 0x%llx\n", addr);

	return val;
}

u64 hl_nic_dram_readq(struct hl_device *hdev, u64 addr)
{
	u64 val = 0;
	int rc;

	rc = hdev->asic_funcs->access_dev_mem(hdev, PCI_REGION_DRAM, addr, &val, DEBUGFS_READ64);
	if (rc)
		dev_crit(hdev->dev, "Failed to readq from dev_mem addr 0x%llx\n", addr);

	return val;
}

void hl_nic_dram_writeb(struct hl_device *hdev, u8 val, u64 addr)
{
	u64 data = val;
	int rc;

	rc = hdev->asic_funcs->access_dev_mem(hdev, PCI_REGION_DRAM, addr, &data, DEBUGFS_WRITE8);
	if (rc)
		dev_crit(hdev->dev, "Failed to writeb to dev_mem addr 0x%llx\n", addr);
}

void hl_nic_dram_writel(struct hl_device *hdev, u32 val, u64 addr)
{
	u64 data = val;
	int rc;

	rc = hdev->asic_funcs->access_dev_mem(hdev, PCI_REGION_DRAM, addr, &data, DEBUGFS_WRITE32);
	if (rc)
		dev_crit(hdev->dev, "Failed to writel to dev_mem addr 0x%llx\n", addr);
}

u64 hl_nic_reserve_wq_dva(struct hl_device *hdev, struct hl_ctx *ctx, struct hl_nic_port *nic_port,
				u64 wq_arr_size, u32 type)
{
	struct hl_wq_array_properties *wq_arr_props;
	u64 rc;

	/* The Device VA block for WQ array is just reserved here. It will be backed by host
	 * physical pages once the MMU mapping is done via hl_map_vmalloc_range inside the
	 * alloc_and_map_wq. Using host page alignment ensures we start with offset 0, both
	 * on host and device side.
	 */
	rc = hl_reserve_va_block(hdev, ctx, HL_VA_RANGE_TYPE_HOST,
						wq_arr_size, PAGE_SIZE);
	if (!rc)
		return rc;

	wq_arr_props = &nic_port->wq_arr_props[type];

	wq_arr_props->dva_base = rc;
	wq_arr_props->dva_size = wq_arr_size;

	return rc;
}

int hl_nic_unreserve_wq_dva(struct hl_device *hdev, struct hl_ctx *ctx,
				struct hl_nic_port *nic_port, u32 type)
{
	struct hl_wq_array_properties *wq_arr_props;
	int rc;

	wq_arr_props = &nic_port->wq_arr_props[type];

	rc = hl_unreserve_va_block(hdev, ctx, wq_arr_props->dva_base, wq_arr_props->dva_size);
	wq_arr_props->dva_base = 0;

	return rc;
}
