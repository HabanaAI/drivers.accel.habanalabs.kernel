// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2018-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi_nic.h"
#include "../include/gaudi/asic_reg/gaudi_regs.h"

int gaudi_nic_debugfs_qp_read(struct hl_device *hdev, char *buf, size_t bsize)
{
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	u32 data[16] = {0}, status, port, qpn;
	bool req, full_print, force_read;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_qp_info *qp_info;
	ssize_t rc;
	int i;

	qp_info = &nic->qp_info;
	req = qp_info->req;
	full_print = qp_info->full_print;
	force_read = qp_info->force_read;
	port = qp_info->port;
	qpn = qp_info->qpn;

	for (i = 0 ; i < GW_MASK_REG_NUM ; i++)
		NIC_WREG32(mmNIC0_QPC0_GW_MASK_0 + i * 4, 0);

	NIC_WREG32(mmNIC0_QPC0_GW_CTRL, (qpn << NIC0_QPC0_GW_CTRL_QPN_SHIFT) |
				(req << NIC0_QPC0_GW_CTRL_REQUESTER_SHIFT));

	NIC_WREG32(mmNIC0_QPC0_GW_BUSY, 1);

	rc = hl_poll_timeout(
		hdev,
		mmNIC0_QPC0_GW_BUSY + NIC_CFG_BASE(port),
		status,
		!status,
		1000,
		nic_props->nic_qpc_cache_inv_timeout);

	if (rc) {
		dev_warn(hdev->dev, "%s QPC GW timeout, port %d\n",
				req ? "requester" : "responder",
				port);
		return -ETIMEDOUT;
	}

	for (i = 0 ; i < GW_MASK_REG_NUM ; i++)
		data[i] = NIC_RREG32(mmNIC0_QPC0_GW_DATA_0 + i * 4);

	sprintf(buf + strlen(buf), "port %d, qpn %d, req %d:\n",
			port, qpn, req);

	if (req) {
		if (!force_read && !PARSE_FIELD(data[15], 31, 1)) {
			sprintf(buf + strlen(buf), "qpn %d is invalid\n", qpn);
			goto out;
		}

		if (!force_read && PARSE_FIELD(data[15], 30, 1)) {
			sprintf(buf + strlen(buf), "qpn %d is in error state\n",
				qpn);
			goto out;
		}

		if (full_print) {
			sprintf(buf + strlen(buf), "valid: 0x%lx\n",
				PARSE_FIELD(data[15], 31, 1));
			sprintf(buf + strlen(buf), "error: 0x%lx\n",
				PARSE_FIELD(data[15], 30, 1));
			sprintf(buf + strlen(buf), "in_work: 0x%lx\n",
				PARSE_FIELD(data[15], 29, 1));
			sprintf(buf + strlen(buf), "trusted: 0x%lx\n",
				PARSE_FIELD(data[15], 27, 2));
			sprintf(buf + strlen(buf), "WQ gran: 0x%lx\n",
				PARSE_FIELD(data[15], 26, 1));
			sprintf(buf + strlen(buf), "cong st: 0x%lx\n",
				PARSE_FIELD(data[15], 24, 2));
			sprintf(buf + strlen(buf), "WQ addr: 0x%lx\n",
				PARSE_FIELD(data[15], 0, 24));
		}

		sprintf(buf + strlen(buf), "PI: 0x%lx\n",
			PARSE_FIELD(data[14], 10, 22));
		sprintf(buf + strlen(buf), "CI: 0x%lx\n",
			MERGE_FIELDS(PARSE_FIELD(data[14], 0, 10),
					PARSE_FIELD(data[13], 20, 12), 12));
		sprintf(buf + strlen(buf), "EI: 0x%lx\n",
			MERGE_FIELDS(PARSE_FIELD(data[13], 0, 20),
					PARSE_FIELD(data[12], 30, 2), 2));
		sprintf(buf + strlen(buf), "last_idx: 0x%lx\n",
			PARSE_FIELD(data[12], 8, 22));

		if (full_print) {
			sprintf(buf + strlen(buf),
				"burst_size: 0x%lx\n",
				MERGE_FIELDS(PARSE_FIELD(data[12], 0, 8),
					PARSE_FIELD(data[11], 18, 14), 14));
			sprintf(buf + strlen(buf), "tran_type: 0x%lx\n",
				PARSE_FIELD(data[11], 17, 1));
			sprintf(buf + strlen(buf), "sob_en: 0x%lx\n",
				PARSE_FIELD(data[7], 31, 1));
			sprintf(buf + strlen(buf), "tmr_gran: 0x%lx\n",
				PARSE_FIELD(data[7], 24, 7));
			sprintf(buf + strlen(buf), "sched_q: 0x%lx\n",
				PARSE_FIELD(data[6], 24, 8));
		}

		sprintf(buf + strlen(buf), "ONA psn: 0x%lx\n",
			PARSE_FIELD(data[7], 0, 24));
		sprintf(buf + strlen(buf), "NTS psn: 0x%lx\n",
			PARSE_FIELD(data[6], 0, 24));

		if (full_print) {
			sprintf(buf + strlen(buf), "tmr retry: 0x%lx\n",
				PARSE_FIELD(data[5], 24, 8));
			sprintf(buf + strlen(buf), "err retry: 0x%lx\n",
				PARSE_FIELD(data[5], 16, 8));
			sprintf(buf + strlen(buf),
				"dst MAC: %04lx%08lx\n",
				PARSE_FIELD(data[5], 0, 16),
				PARSE_FIELD(data[4], 0, 32));
			sprintf(buf + strlen(buf), "src ipv4: 0x%lx\n",
				PARSE_FIELD(data[3], 0, 32));
			sprintf(buf + strlen(buf), "dst ipv4: 0x%lx\n",
				PARSE_FIELD(data[2], 0, 32));
			sprintf(buf + strlen(buf), "rkey: 0x%lx\n",
				PARSE_FIELD(data[1], 0, 32));
			sprintf(buf + strlen(buf), "cong_en: 0x%lx\n",
				PARSE_FIELD(data[0], 30, 2));
			sprintf(buf + strlen(buf), "prio: 0x%lx\n",
				PARSE_FIELD(data[0], 28, 2));
			sprintf(buf + strlen(buf), "port: 0x%lx\n",
				PARSE_FIELD(data[0], 24, 4));
			sprintf(buf + strlen(buf), "qpn: 0x%lx\n",
				PARSE_FIELD(data[0], 0, 24));
		}
	} else {
		if (!force_read && !PARSE_FIELD(data[7], 31, 1)) {
			sprintf(buf + strlen(buf), "qpn %d is invalid\n", qpn);
			goto out;
		}

		if (full_print) {
			sprintf(buf + strlen(buf), "valid: 0x%lx\n",
				PARSE_FIELD(data[7], 31, 1));
			sprintf(buf + strlen(buf), "in_work: 0x%lx\n",
				PARSE_FIELD(data[7], 30, 1));
			sprintf(buf + strlen(buf), "sob_en: 0x%lx\n",
				PARSE_FIELD(data[7], 27, 1));
			sprintf(buf + strlen(buf), "cyc_idx: 0x%lx\n",
				MERGE_FIELDS(PARSE_FIELD(data[7], 0, 27),
					PARSE_FIELD(data[6], 29, 3), 3));
			sprintf(buf + strlen(buf), "log_buf_size: 0x%lx\n",
				PARSE_FIELD(data[6], 24, 5));
		}

		sprintf(buf + strlen(buf), "EXP psn: 0x%lx\n",
			PARSE_FIELD(data[6], 0, 24));

		if (full_print) {
			sprintf(buf + strlen(buf), "trans_type: 0x%lx\n",
				PARSE_FIELD(data[6], 0, 1));
			sprintf(buf + strlen(buf), "nack_syn: 0x%lx\n",
				PARSE_FIELD(data[5], 30, 2));
			sprintf(buf + strlen(buf), "sched_q: 0x%lx\n",
				PARSE_FIELD(data[5], 16, 8));
			sprintf(buf + strlen(buf), "dst MAC: %04lx%08lx\n",
				PARSE_FIELD(data[5], 0, 16),
				PARSE_FIELD(data[4], 0, 32));
			sprintf(buf + strlen(buf), "src ipv4: 0x%lx\n",
				PARSE_FIELD(data[3], 0, 32));
			sprintf(buf + strlen(buf), "dst ipv4: 0x%lx\n",
				PARSE_FIELD(data[2], 0, 32));
			sprintf(buf + strlen(buf), "lkey: 0x%lx\n",
				PARSE_FIELD(data[1], 0, 32));
			sprintf(buf + strlen(buf), "con_state: 0x%lx\n",
				PARSE_FIELD(data[0], 30, 2));
			sprintf(buf + strlen(buf), "prio: 0x%lx\n",
				PARSE_FIELD(data[0], 28, 2));
			sprintf(buf + strlen(buf), "port: 0x%lx\n",
				PARSE_FIELD(data[0], 24, 4));
			sprintf(buf + strlen(buf), "qpn: 0x%lx\n",
				PARSE_FIELD(data[0], 0, 24));
		}
	}

out:
	for (i = 0 ; i < GW_MASK_REG_NUM ; i++)
		NIC_WREG32(mmNIC0_QPC0_GW_MASK_0 + i * 4,
				NIC0_QPC0_GW_MASK_R_MASK);

	return 0;
}

int gaudi_nic_debugfs_wqe_read(struct hl_device *hdev, char *buf, size_t bsize)
{
	/* SW-84300: implement that function */
	return 0;
}

void gaudi_nic_debugfs_print_fec_stats(struct hl_nic_port *nic_port)
{

}
