/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2021-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef SNI_H_
#define SNI_H_

#include <uapi/drm/habanalabs_accel.h>
#include <linux/habanalabs/sni.h>

#include <linux/kfifo.h>
#include <linux/hashtable.h>
#include <linux/ctype.h>

#include "../common/habanalabs_compat.h"
#include "../include/common/cpucp_if.h"

struct hl_device;
struct hl_ctx;

#define NIC_MAC_LANE_0			0U
#define NIC_MAC_LANE_1			1U
#define NIC_MAC_LANE_2			2U
#define NIC_MAC_LANE_3			3U
#define NIC_MAC_LANES			4U

#define QPC_REQ_BURST_SIZE		16

#define NIC_QPC_INV_USEC		1000000 /* 1s */
#define NIC_SIM_QPC_INV_USEC		(NIC_QPC_INV_USEC * 5)
#define NIC_PLDM_QPC_INV_USEC		(NIC_QPC_INV_USEC * 10)

#define NIC_MACRO_CFG_SIZE		hdev->asic_prop.sni_props.macro_cfg_size
#define NIC_MACRO_CFG_BASE(port)	(NIC_MACRO_CFG_SIZE * ((port) >> 1))
#define NIC_MACRO_WREG32(reg, val)	WREG32(NIC_MACRO_CFG_BASE(port) + (reg), (val))

/**
 * struct hl_sni - habanalabs SNI common structure.
 * @sni_aux_dev: pointer to SNI auxiliary device structure.
 * @ctx: compute user context.
 * @eth_ports_mask: Ethernet ports enable mask.
 * @card_location: the OAM number in the HLS (relevant for PMC card type).
 * @use_fw_serdes_info: true if NIC should use serdes values from F/W, false if SNI should use hard
 *                      coded values.
 * @is_sni_aux_dev_initialized: true if the SNI auxiliary device is initialized.
 * @is_initialized: is device initialized.
 */
struct hl_sni {
	struct hl_aux_dev	sni_aux_dev;
	struct hl_ctx		*ctx;
	u64			eth_ports_mask;
	u32			card_location;
	u8			use_fw_serdes_info;
	u8			is_sni_aux_dev_initialized;
	u8			is_initialized;
};

/**
 * struct hl_sni_port_funcs - ASIC specific SNI functions that are called from common code for a
 *                            specific port.
 * @spmu_get_stats_info: get SPMU statistics information.
 * @spmu_config: config the SPMU.
 * @spmu_sample: read the SPMU counters.
 * @send_cpucp_packet: Send cpucp packet to FW.
 * @post_send_status: ASIC-specific handler for post sending status packet to FW.
 */
struct hl_sni_port_funcs {
	void (*spmu_get_stats_info)(struct hl_device *hdev, u32 port, struct hl_sni_stat **stats,
					u32 *n_stats);
	int (*spmu_config)(struct hl_device *hdev, u32 port, u32 num_event_types, u32 event_types[],
				bool enable);
	int (*spmu_sample)(struct hl_device *hdev, u32 port, u32 num_out_data, u64 out_data[]);
	int (*send_cpucp_packet)(struct hl_device *hdev, u32 port, enum cpucp_packet_id packet_id,
					int val);
	void (*post_send_status)(struct hl_device *hdev, u32 port);
};

/**
 * struct hl_sni_funcs - ASIC specific SNI functions that are called from common code.
 * @pre_core_init: NIC initializations to be done only once on device probe.
 * @get_hw_cap: check rather HW capability bitmap is set for NIC.
 * @set_hw_cap: set HW capability (on/off).
 * @set_sni_data: ASIC data to be used by the SNI driver.
 * @port_funcs: functions called from common code for a specific NIC port.
 */
struct hl_sni_funcs {
	int (*pre_core_init)(struct hl_device *hdev);
	bool (*get_hw_cap)(struct hl_device *hdev);
	void (*set_hw_cap)(struct hl_device *hdev, bool enable);
	void (*set_sni_data)(struct hl_device *hdev);
	struct hl_sni_port_funcs *port_funcs;
};

int hl_sni_init(struct hl_device *hdev);
void hl_sni_fini(struct hl_device *hdev);
void hl_sni_stop(struct hl_device *hdev);
int hl_sni_reopen(struct hl_device *hdev);
int hl_sni_send_status(struct hl_device *hdev, int port, u8 cmd, u8 period);
void hl_sni_hard_reset_prepare(struct hl_device *hdev);
int hl_sni_control(struct hl_device *hdev, u32 op, void *input,	void *output, struct hl_ctx *ctx);
int hl_sni_ctx_init(struct hl_ctx *ctx);
void hl_sni_ctx_fini(struct hl_ctx *ctx);
void hl_sni_synchronize_irqs(struct hl_device *hdev);
int hl_sni_mmap(struct hl_device *hdev, u32 asid, struct vm_area_struct *vma);
int hl_sni_get_port_state(struct hl_device *hdev, u32 port, bool *up);
int hl_sni_get_port_statistics(struct hl_device *hdev, u32 port,
				struct hl_sni_port_statistics *out);
int hl_sni_check_ib_driver(struct hl_device *hdev);

#ifndef _HAS_AUX_BUS_H
extern int hl_sni_probe(struct hl_aux_dev *aux_dev);
extern void hl_sni_remove(struct hl_aux_dev *aux_dev);
#endif

#endif /* SNI_H_ */
