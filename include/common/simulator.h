/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef SIMULATOR_H
#define SIMULATOR_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define HLV_SIM_ID_OFFSET	200

/* These masks are used for @hlv_sim_devtype_minor_args.dram_mask.
 * The structure is:
 * bits[0..7]   : DRAM_SIZE_IN_GB - Ram size in GB
 * bits[8..23]  : DRAM_SIZE_IN_MB - Additional ram size in MB
 * bits[24..31] : SIM_ARGS_SIZE   - Size of args struct, for struct extension
 */
#define DRAM_SIZE_IN_GB_MASK	0x000000FF
#define DRAM_SIZE_IN_MB_MASK	0x00FFFF00
#define SIM_ARGS_SIZE_MASK	0xFF000000

/* User will allocate DRAM buffer and will supply a pointer to driver
 * @hlv_sim_devtype_minor_args.dram_user_pointer
 */
#define HLV_FEATURE_DRAM_USER_POINTER_BIT	0

/* User will allocate SRAM buffer and will supply a pointer to driver
 * @hlv_sim_devtype_minor_args.sram_user_pointer
 */
#define HLV_FEATURE_SRAM_USER_POINTER_BIT	1

/* User will allocate SRAM/DRAM buffer and will supply a single pointer
 * to driver @hlv_sim_devtype_minor_args.sram_dram_user_pointer
 */
#define HLV_FEATURE_SINGLE_USER_POINTER_BIT	2

enum hlv_sim_dev_types {
	HLV_SIM_GOYA = 1,
	HLV_SIM_GAUDI = 2,
	HLV_SIM_GRECO = 3,
	HLV_SIM_GAUDI2 = 4,
	HLV_SIM_GAUDI_HL2000M = 5,
	HLV_SIM_GAUDI3 = 6,
	HLV_SIM_GAUDI3_SINGLE_DIE = 7,
	HLV_SIM_GAUDI2_ARC = 8,
	HLV_SIM_GAUDI3_ARC = 9,
	HLV_SIM_GAUDI3_SINGLE_DIE_ARC = 10,
	HLV_SIM_GAUDI2B = 11,
	HLV_SIM_GAUDI2B_ARC = 12,
};

struct hlv_sim_devtype_minor_args {
	__u32 devtype_or_minor;
	__u32 dram_mask;
	__u64 dram_user_pointer;
	__u64 sram_user_pointer;
	__u64 sram_dram_user_pointer;
	__u32 sram_size_mb;
	__u32 reserved[7];
};

struct hlv_sim_supported_features {
	__u64 features_mask[8];
};

enum simulator_msg_cmd {
	SIM_CMD_READ = 1,
	SIM_CMD_WRITE,
	SIM_CMD_RESET,
	SIM_CMD_PRIV_ASSERTION_ENABLE,
	SIM_CMD_PRIV_ASSERTION_DISABLE
};

struct simulator_msg {
	__u64 addr;
	__u32 id;
	__u32 cmd;
	__u32 val;
	__u32 pad;
};

struct simulator_gen_int_args {
	__u32 id;
	__u32 pad;
};

struct simulator_pci_access_args {
	__u64 host_address;
	__u64 device_address;
	__u32 length;
	__u8 is_write;
	__u8 pad[3];
};

struct simulator_reset_device_args {
	__u64 pad;
};

int hl_sim_alloc_irq_vectors(struct hl_simulator_device *edev, unsigned int min_vecs,
			unsigned int max_vecs, unsigned int flags);
void hl_sim_free_irq_vectors(struct hl_simulator_device *edev);
int hl_sim_irq_vector(struct hl_simulator_device *edev, unsigned int nr);
int hl_sim_send_irq(struct hl_simulator_device *edev, unsigned int irq);

/* Gather information about the device and pass it to user-space */
#define SIMULATOR_IOCTL_GEN_INT \
			_IOW('O', 0x01, struct simulator_gen_int_args)

#define SIMULATOR_IOCTL_PCI_ACCESS_FROM_DEVICE \
			_IOW('O', 0x02, struct simulator_pci_access_args)

#define SIMULATOR_IOCTL_RESET_DEVICE \
			_IOW('O', 0x03, struct simulator_reset_device_args)

#define SIMULATOR_COMMAND_START		0x01
#define SIMULATOR_COMMAND_END		0x04

/* Get the device type from user-space and pass it its allocated minor number */
#define HLV_SIMULATOR_IOCTL_SET_DEVTYPE_GET_MINOR \
			_IOWR('O', 0x01, struct hlv_sim_devtype_minor_args)

#define HLV_SIMULATOR_IOCTL_GET_SUPPORTED_FEATURES \
			_IOR('O', 0x02, struct hlv_sim_supported_features)

#define HLV_SIMULATOR_COMMAND_START	0x01
#define HLV_SIMULATOR_COMMAND_END	0x03

#endif /* SIMULATOR_H */
