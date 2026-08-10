/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI3_PACKETS_H
#define GAUDI3_PACKETS_H

#include <linux/types.h>

#define PACKET_HEADER_PACKET_ID_SHIFT		56
#define PACKET_HEADER_PACKET_ID_MASK		0x1F00000000000000ull

enum packet_id {
	PACKET_WREG_32 = 0x1,
	PACKET_WREG_BULK = 0x2,
	PACKET_MSG_LONG = 0x3,
	PACKET_MSG_SHORT = 0x4,
	PACKET_MSG_PROT = 0x7,
	PACKET_FENCE = 0x8,
	PACKET_LIN_DMA = 0x9,
	PACKET_NOP = 0xA,
	PACKET_STOP = 0xB,
	PACKET_WAIT = 0xD,
	PACKET_LOAD_AND_EXE = 0xF,
	PACKET_WRITE_ARC_STREAM = 0x10,
	PACKET_WREG_64_SHORT = 0x12,
	PACKET_WREG_64_LONG = 0x13,
	MAX_PACKET_ID = (PACKET_HEADER_PACKET_ID_MASK >>
				PACKET_HEADER_PACKET_ID_SHIFT) + 1
};

#define GAUDI3_PKT_CTL_OPCODE_SHIFT			24
#define GAUDI3_PKT_CTL_OPCODE_MASK			0x1F000000

#define GAUDI3_PKT_CTL_EB_SHIFT				29
#define GAUDI3_PKT_CTL_EB_MASK				0x20000000

#define GAUDI3_PKT_CTL_SWITCH_SHIFT			30
#define GAUDI3_PKT_CTL_SWITCH_MASK			0x40000000

#define GAUDI3_PKT_CTL_MB_SHIFT				31
#define GAUDI3_PKT_CTL_MB_MASK				0x80000000

#define GAUDI3_PKT_MSG_SHORT_CTL_ADDR_OFFSET_SHIFT	0
#define GAUDI3_PKT_MSG_SHORT_CTL_ADDR_OFFSET_MASK	0x0000FFFF

#define GAUDI3_PKT_MSG_SHORT_CTL_BASE_MSB_SHIFT		19
#define GAUDI3_PKT_MSG_SHORT_CTL_BASE_MSB_MASK		0x00080000

#define GAUDI3_PKT_MSG_SHORT_CTL_BASE_LSB_SHIFT		22
#define GAUDI3_PKT_MSG_SHORT_CTL_BASE_LSB_MASK		0x00C00000

struct packet_nop {
	__le32 reserved;
	__le32 ctl;
};

struct packet_stop {
	__le32 reserved;
	__le32 ctl;
};

struct packet_wreg32 {
	__le32 value;
	__le32 ctl;
};

struct packet_wreg_bulk {
	__le32 size64;
	__le32 ctl;
	__le64 values[0]; /* data starts here */
};

struct packet_msg_long {
	__le32 value;
	__le32 ctl;
	__le64 addr;
};

struct packet_msg_short {
	__le32 value;
	__le32 ctl;
};

struct packet_msg_prot {
	__le32 value;
	__le32 ctl;
	__le64 addr;
};

struct packet_fence {
	__le32 cfg;
	__le32 ctl;
};

struct packet_lin_dma {
	__le32 tsize;
	__le32 ctl;
	__le64 src_addr;
	__le64 dst_addr;
};

struct packet_wait {
	__le32 cfg;
	__le32 ctl;
};

struct packet_load_and_exe {
	__le32 cfg;
	__le32 ctl;
	__le64 src_addr;
};

struct packet_cp_dma {
	__le32 tsize;
	__le32 ctl;
	__le64 src_addr;
};

struct packet_write_arc_stream {
	__le32 size64;
	__le32 ctl;
	__le64 values[0]; /* data starts here */
};

#endif /* GAUDI3_PACKETS_H */
