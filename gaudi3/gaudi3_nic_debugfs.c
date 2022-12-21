// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi3_nic.h"

#define __snprintf(buf, bsize, fmt, ...)							\
		do {										\
			size_t _blen = strlen(buf);						\
												\
			if (_blen < (bsize))							\
				snprintf((buf) + _blen, (bsize) - _blen, fmt, ##__VA_ARGS__);	\
		} while (0)

#define _fsnprintf(buf, size, fmt, ...)							\
		do {									\
			if (full_print)							\
				__snprintf(buf, size, fmt, ##__VA_ARGS__);		\
											\
		} while (0)


static int gaudi3_nic_debugfs_qpc_req_parse(struct hl_device *hdev,
					struct hl_nic_qp_info *qp_info,
					struct gaudi3_qpc_requester *req, char *buf, size_t bsize)
{
	bool full_print, force_read;

	force_read = qp_info->force_read;
	full_print = qp_info->full_print;

	__snprintf(buf, bsize, "Valid: %lld\n", REQ_QPC_GET_VALID(*req));
	if (strlen(buf) >= bsize)
		return -EFBIG;

	if (!force_read && !REQ_QPC_GET_VALID(*req))
		return 0;

	__snprintf(buf, bsize, "Error: %lld\n", REQ_QPC_GET_ERROR(*req));
	if (strlen(buf) >= bsize)
		return -EFBIG;

	if (!force_read && REQ_QPC_GET_ERROR(*req))
		return 0;

	__snprintf(buf, bsize, "in work: 0x%llx\n", REQ_QPC_GET_IN_WORK(*req));
	_fsnprintf(buf, bsize, "trusted: 0x%llx\n", REQ_QPC_GET_TRUST_LEVEL(*req));
	_fsnprintf(buf, bsize, "WQ base addr: 0x%llx\n", REQ_QPC_GET_WQ_BASE_ADDR(*req));
	_fsnprintf(buf, bsize, "MTU: 0x%llx\n", REQ_QPC_GET_MTU(*req));
	_fsnprintf(buf, bsize, "cong mode: 0x%llx\n", REQ_QPC_GET_CONGESTION_MODE(*req));
	_fsnprintf(buf, bsize, "priority: 0x%llx\n", REQ_QPC_GET_PRIORITY(*req));
	_fsnprintf(buf, bsize, "transport service: 0x%llx\n", REQ_QPC_GET_TRANSPORT_SERVICE(*req));
	_fsnprintf(buf, bsize, "SWQ gran: 0x%llx\n", REQ_QPC_GET_SWQ_GRANULARITY(*req));
	_fsnprintf(buf, bsize, "loopback: 0x%llx\n", REQ_QPC_GET_LOOPBACK(*req));
	_fsnprintf(buf, bsize, "EQ number: 0x%llx\n", REQ_QPC_GET_EQ_NUM(*req));
	_fsnprintf(buf, bsize, "WQ type: 0x%llx\n", REQ_QPC_GET_WQ_TYPE(*req));
	_fsnprintf(buf, bsize, "port/lane: 0x%llx\n", REQ_QPC_GET_PORT(*req));
	_fsnprintf(buf, bsize, "data MMU BP: 0x%llx\n", REQ_QPC_GET_DATA_MMU_BYPASS(*req));
	_fsnprintf(buf, bsize, "plain RDMA: 0x%llx\n", REQ_QPC_GET_PLAIN_RDMA(*req));
	_fsnprintf(buf, bsize, "compression EN: 0x%llx\n", REQ_QPC_GET_COMPRESSION_EN(*req));
	_fsnprintf(buf, bsize, "FOL seg 0 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_0_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "FOL seg 1 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_1_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "FOL seg 2 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_2_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "FOL seg 3 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_3_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "FOL seg 4 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_4_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "FOL seg 5 RSN: 0x%llx\n",
			MERGE_FIELDS(REQ_QPC_GET_FOL_SEG_5_RSN_VAL_HI(*req),
						REQ_QPC_GET_FOL_SEG_5_RSN_VAL_LO(*req), 4));
	_fsnprintf(buf, bsize, "FOL seg 6 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_6_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "FOL seg 7 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_7_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "FOL seg 8 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_8_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "FOL seg 9 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_9_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "FOL seg 10 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_10_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "FOL seg 11 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_11_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "FOL seg 12 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_12_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "FOL seg 13 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_13_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "FOL seg 14 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_14_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "FOL seg 15 RSN: 0x%llx\n", REQ_QPC_GET_FOL_SEG_15_RSN_VAL(*req));
	_fsnprintf(buf, bsize, "ONA RSN: 0x%llx\n", REQ_QPC_GET_ONA_RSN(*req));
	_fsnprintf(buf, bsize, "NTS PSN retrans: 0x%llx\n", REQ_QPC_GET_NTS_PSN_RETRANS(*req));
	_fsnprintf(buf, bsize, "SACK EN: 0x%llx\n", REQ_QPC_GET_SACK_EN(*req));
	_fsnprintf(buf, bsize, "IN retrans: 0x%llx\n", REQ_QPC_GET_IN_RETRANS(*req));
	_fsnprintf(buf, bsize, "NTS RSN: 0x%llx\n", REQ_QPC_GET_NTS_RSN(*req));
	_fsnprintf(buf, bsize, "QP timeout: 0x%llx\n", REQ_QPC_GET_QP_TIMEOUT(*req));
	_fsnprintf(buf, bsize, "remain burst valid: 0x%llx\n", REQ_QPC_GET_REMAIN_BURST_VLD(*req));
	_fsnprintf(buf, bsize, "remain burst: 0x%llx\n", REQ_QPC_GET_REMAIN_BURST(*req));
	_fsnprintf(buf, bsize, "PSN delivered: 0x%llx\n", REQ_QPC_GET_PSN_DELIVERED(*req));
	_fsnprintf(buf, bsize, "pacing time: 0x%llx\n", REQ_QPC_GET_PACING_TIME(*req));
	_fsnprintf(buf, bsize, "ackreq freq: 0x%llx\n", REQ_QPC_GET_ACKREQ_FREQ(*req));
	_fsnprintf(buf, bsize, "PSN since ackreq: 0x%llx\n", REQ_QPC_GET_PSN_SINCE_ACKREQ(*req));
	_fsnprintf(buf, bsize, "patcher remote PI: 0x%llx\n", REQ_QPC_GET_PATCHER_REM_PI(*req));
	_fsnprintf(buf, bsize, "latest acked bit: 0x%llx\n", REQ_QPC_GET_LASTEST_ACKED_BIT(*req));
	__snprintf(buf, bsize, "remote CI: 0x%llx\n", REQ_QPC_GET_REM_CI(*req));
	__snprintf(buf, bsize, "remote PI count: 0x%llx\n", REQ_QPC_GET_REM_PI_CNT(*req));
	_fsnprintf(buf, bsize, "remote PI highest: 0x%llx\n", REQ_QPC_GET_REM_PI_HIGHEST(*req));
	__snprintf(buf, bsize, "remote PI: 0x%llx\n", REQ_QPC_GET_REM_PI(*req));
	__snprintf(buf, bsize, "PI: 0x%llx\n", REQ_QPC_GET_PI(*req));
	__snprintf(buf, bsize, "CI: 0x%llx\n", REQ_QPC_GET_CI(*req));
	__snprintf(buf, bsize, "EI: 0x%llx\n", REQ_QPC_GET_EI(*req));
	_fsnprintf(buf, bsize, "RDV local class: 0x%llx\n", REQ_QPC_GET_RDV_LCL_CLASS(*req));
	_fsnprintf(buf, bsize, "RDV remote class: 0x%llx\n", REQ_QPC_GET_RDV_REM_CLASS(*req));
	_fsnprintf(buf, bsize, "RDV local MCID: 0x%llx\n", REQ_QPC_GET_RDV_LCL_MCID(*req));
	_fsnprintf(buf, bsize, "RDV remote MCID: 0x%llx\n", REQ_QPC_GET_RDV_REM_MCID(*req));
	_fsnprintf(buf, bsize, "WQ log size: 0x%llx\n", REQ_QPC_GET_LOCAL_WQ_LOG_SZ(*req));
	_fsnprintf(buf, bsize, "ASID: 0x%llx\n", REQ_QPC_GET_ASID(*req));
	_fsnprintf(buf, bsize, "burst size: 0x%llx\n", REQ_QPC_GET_BURST_SIZE(*req));
	_fsnprintf(buf, bsize, "MAX MDF: 0x%llx\n", REQ_QPC_GET_MAX_MDF(*req));
	_fsnprintf(buf, bsize, "RTT marked PSN: 0x%llx\n", REQ_QPC_GET_RTT_MARKED_PSN(*req));
	_fsnprintf(buf, bsize, "msg coalesce cntr:0x%llx\n", REQ_QPC_GET_MSG_COALESCE_CNT(*req));
	_fsnprintf(buf, bsize, "RTT timestamp: 0x%llx\n", REQ_QPC_GET_RTT_TIMESTAMP(*req));
	_fsnprintf(buf, bsize, "BETA nominator: 0x%llx\n", REQ_QPC_GET_BETA_NOMINATOR(*req));
	_fsnprintf(buf, bsize, "target delay: 0x%llx\n", REQ_QPC_GET_TARGET_DELAY(*req));
	_fsnprintf(buf, bsize, "BETA de-nominator: 0x%llx\n", REQ_QPC_GET_BETA_DE_NOMINATOR(*req));
	_fsnprintf(buf, bsize, "congestion window: 0x%llx\n", REQ_QPC_GET_CONGESTION_WIN(*req));
	_fsnprintf(buf, bsize, "AI: 0x%llx\n", REQ_QPC_GET_AI(*req));
	_fsnprintf(buf, bsize, "cngstn non-marked ack: 0x%llx\n",
			REQ_QPC_GET_CONG_NON_MRKD_ACK(*req));
	_fsnprintf(buf, bsize, "back pressure: 0x%llx\n", REQ_QPC_GET_WQ_BACK_PRESSURE(*req));
	_fsnprintf(buf, bsize, "Timeout gran: 0x%llx\n", REQ_QPC_GET_TM_GRANULARITY(*req));
	_fsnprintf(buf, bsize, "cngstn marked ack: 0x%llx\n",
			REQ_QPC_GET_CONGESTION_MARKED_ACK(*req));
	_fsnprintf(buf, bsize, "encap enable: 0x%llx\n", REQ_QPC_GET_ENCAP_ENABLE(*req));
	_fsnprintf(buf, bsize, "RTT state: 0x%llx\n", REQ_QPC_GET_RTT_STATE(*req));
	_fsnprintf(buf, bsize, "remote WQ log sz: 0x%llx\n", REQ_QPC_GET_REMOTE_WQ_LOG_SZ(*req));
	__snprintf(buf, bsize, "BCC PSN: 0x%llx\n", REQ_QPC_GET_BCC_PSN(*req));
	_fsnprintf(buf, bsize, "encap type: 0x%llx\n", REQ_QPC_GET_ENCAP_TYPE(*req));
	_fsnprintf(buf, bsize, "CQ number: 0x%llx\n", REQ_QPC_GET_CQ_NUM(*req));
	__snprintf(buf, bsize, "ONA PSN: 0x%llx\n", REQ_QPC_GET_ONA_PSN(*req));
	_fsnprintf(buf, bsize, "sched queue num: 0x%llx\n", REQ_QPC_GET_SCHD_Q_NUM(*req));
	__snprintf(buf, bsize, "BCS PSN: 0x%llx\n", REQ_QPC_GET_BCS_PSN(*req));
	__snprintf(buf, bsize, "NTS PSN: 0x%llx\n", REQ_QPC_GET_NTS_PSN(*req));
	__snprintf(buf, bsize, "timeout retry cnt: 0x%llx\n", REQ_QPC_GET_TMOUT_RTRY_CNT(*req));
	__snprintf(buf, bsize, "SEQ err retry cnt: 0x%llx\n", REQ_QPC_GET_SEQ_ERR_RTRY_CNT(*req));
	__snprintf(buf, bsize, "dst MAC: %04llx%08llx\n", REQ_QPC_GET_DST_MAC_MSB(*req),
			REQ_QPC_GET_DST_MAC_LSB(*req));
	_fsnprintf(buf, bsize, "dst ipv4: 0x%llx\n", REQ_QPC_GET_DST_IP(*req));
	_fsnprintf(buf, bsize, "remote key: 0x%llx\n", REQ_QPC_GET_RKEY(*req));
	_fsnprintf(buf, bsize, "dst QP: 0x%llx\n", REQ_QPC_GET_DST_QP(*req));
	_fsnprintf(buf, bsize, "dst RANK: 0x%llx\n", REQ_QPC_GET_DST_RANK(*req));
	_fsnprintf(buf, bsize, "is last RANK: 0x%llx\n", REQ_QPC_GET_LAST_RANK(*req));
	_fsnprintf(buf, bsize, "Last NIC in Lag: 0x%llx\n", REQ_QPC_GET_LAST_NIC_IN_LAG(*req));
	_fsnprintf(buf, bsize, "WQ Index: 0x%llx\n", REQ_QPC_GET_WQ_IDX(*req));

	/* make sure the caller is aware that the buffer it is using is not long enough */
	return (strlen(buf) >= bsize) ? -EFBIG : 0;
}

static int gaudi3_nic_debugfs_qpc_req_coll_parse(struct hl_device *hdev,
					struct hl_nic_qp_info *qp_info,
					struct gaudi3_qpc_requester *req, char *buf, size_t bsize)
{
	bool full_print;

	__snprintf(buf, bsize, "\nCollective desc:\n");

	__snprintf(buf, bsize, "dest rank: 0x%llx\n", REQ_QPC_GET_COLL_DEST_RANK(*req));
	__snprintf(buf, bsize, "pipeline steps: 0x%llx\n", REQ_QPC_GET_COLL_PIPELINE_STEPS(*req));
	__snprintf(buf, bsize, "last rank: 0x%llx\n", REQ_QPC_GET_COLL_LAST_RANK(*req));
	__snprintf(buf, bsize, "reduction opcode: 0x%llx\n", REQ_QPC_GET_COLL_REDUCTION(*req));
	__snprintf(buf, bsize, "compression: 0x%llx\n", REQ_QPC_GET_COLL_COMPRESS(*req));
	__snprintf(buf, bsize, "data type: 0x%llx\n", REQ_QPC_GET_COLL_DATA_TYPE(*req));
	__snprintf(buf, bsize, "split residue strategy: 0x%llx\n",
			REQ_QPC_GET_COLL_SPLIT_RESIDUE(*req));
	__snprintf(buf, bsize, "opcode: 0x%llx\n", REQ_QPC_GET_COLL_OPCODE(*req));
	__snprintf(buf, bsize, "read clear: 0x%llx\n", REQ_QPC_GET_COLL_READ_CLR(*req));
	__snprintf(buf, bsize, "force AckReq: 0x%llx\n", REQ_QPC_GET_COLL_A(*req));
	__snprintf(buf, bsize, "rank residue size: 0x%llx\n",
			REQ_QPC_GET_COLL_RANK_RESIDUE_SZ(*req));
	__snprintf(buf, bsize, "stride between ranks: 0x%llx\n", REQ_QPC_GET_COLL_STRIDE(*req));
	__snprintf(buf, bsize, "NIC size: 0x%llx\n", REQ_QPC_GET_COLL_NIC_SIZE(*req));
	__snprintf(buf, bsize, "NIC residue: 0x%llx\n", REQ_QPC_GET_COLL_NIC_RESIDUE(*req));
	__snprintf(buf, bsize, "local base address: 0x%0llx%0llx\n",
			REQ_QPC_GET_COLL_LOCAL_BASE_ADDR_H(*req),
			REQ_QPC_GET_COLL_LOCAL_BASE_ADDR_L(*req));
	__snprintf(buf, bsize, "local tag: 0x%llx\n", REQ_QPC_GET_COLL_LOCAL_TAG(*req));
	__snprintf(buf, bsize, "local SOB: 0x%llx\n", REQ_QPC_GET_COLL_LOCAL_SOB(*req));
	__snprintf(buf, bsize, "local SM: 0x%llx\n", REQ_QPC_GET_COLL_LOCAL_SM(*req));
	__snprintf(buf, bsize, "local MCID: 0x%llx\n", REQ_QPC_GET_COLL_LOCAL_MCID(*req));
	__snprintf(buf, bsize, "local cache class: 0x%llx\n", REQ_QPC_GET_COLL_LOCAL_CLASS(*req));
	__snprintf(buf, bsize, "long SOB: 0x%llx\n", REQ_QPC_GET_COLL_LSO(*req));
	__snprintf(buf, bsize, "SOB CMD: 0x%llx\n", REQ_QPC_GET_COLL_SO_CMD(*req));
	__snprintf(buf, bsize, "completion type: 0x%llx\n", REQ_QPC_GET_COLL_CT(*req));

	/* Use the full-print to filter out the remote params when the opcode does not supp them */
	switch (REQ_QPC_GET_COLL_OPCODE(*req)) {
	case WQE_NOP:
	case WQE_SEND:
	case WQE_RENDEZVOUS_WR:
		full_print = false;
		break;
	default:
		full_print = true;
		break;
	}

	_fsnprintf(buf, bsize, "remote base address: 0x%0llx%0llx\n",
			REQ_QPC_GET_COLL_WR_REM_BASE_ADDR_H(*req),
			REQ_QPC_GET_COLL_WR_REM_BASE_ADDR_L(*req));
	_fsnprintf(buf, bsize, "remote tag: 0x%llx\n", REQ_QPC_GET_COLL_WR_REM_TAG(*req));
	_fsnprintf(buf, bsize, "remote SOB: 0x%llx\n", REQ_QPC_GET_COLL_WR_REM_SOB(*req));
	_fsnprintf(buf, bsize, "remote SM: 0x%llx\n", REQ_QPC_GET_COLL_WR_REM_SM(*req));
	_fsnprintf(buf, bsize, "remote MCID: 0x%llx\n", REQ_QPC_GET_COLL_WR_REM_MCID(*req));
	_fsnprintf(buf, bsize, "remote cache class: 0x%llx\n",
			REQ_QPC_GET_COLL_WR_REM_CLASS(*req));
	_fsnprintf(buf, bsize, "remote long SOB: 0x%llx\n", REQ_QPC_GET_COLL_WR_REM_LSO(*req));
	_fsnprintf(buf, bsize, "remote COB CMD: 0x%llx\n", REQ_QPC_GET_COLL_WR_REM_SO_CMD(*req));
	_fsnprintf(buf, bsize, "remote completion type: 0x%llx\n",
			REQ_QPC_GET_COLL_WR_REM_CT(*req));

	/* make sure the caller is aware that the buffer it is using is not long enough */
	return (strlen(buf) >= bsize) ? -EFBIG : 0;
}

static int gaudi3_nic_debugfs_qpc_res_parse(struct hl_device *hdev,
					struct hl_nic_qp_info *qp_info,
					struct gaudi3_qpc_responder *res, char *buf, size_t bsize)
{
	bool full_print, force_read;

	force_read = qp_info->force_read;
	full_print = qp_info->full_print;

	__snprintf(buf, bsize, "Valid: %lld\n", RES_QPC_GET_VALID(*res));
	if (strlen(buf) >= bsize)
		return -EFBIG;

	if (!force_read && !RES_QPC_GET_VALID(*res))
		return 0;

	_fsnprintf(buf, bsize, "atomic F&A val: 0x%llx\n", RES_QPC_GET_ATOMIC_FA_VAL(*res));
	_fsnprintf(buf, bsize, "atomic F&A EN: 0x%llx\n", RES_QPC_GET_ATOMIC_FA_EN(*res));
	_fsnprintf(buf, bsize, "enable SACK: 0x%llx\n", RES_QPC_GET_SACK_EN(*res));
	__snprintf(buf, bsize, "RSN highest val: 0x%llx\n", RES_QPC_GET_RSN_HIGHEST_VALUE(*res));
	_fsnprintf(buf, bsize, "plain RDMA: 0x%llx\n", RES_QPC_GET_PLAIN_RDMA(*res));
	_fsnprintf(buf, bsize, "loopback: 0x%llx\n", RES_QPC_GET_LOOPBACK(*res));
	__snprintf(buf, bsize, "pkts since ackreq: 0x%llx\n", RES_QPC_GET_PKTS_SINCE_ACKREQ(*res));
	_fsnprintf(buf, bsize, "ackreq freq: 0x%llx\n", RES_QPC_GET_RES_ACKREQ_FREQ(*res));
	__snprintf(buf, bsize, "in_work: 0x%llx\n", RES_QPC_GET_IN_WORK(*res));
	_fsnprintf(buf, bsize, "CQ num: 0x%llx\n", RES_QPC_GET_CQ_NUM(*res));
	_fsnprintf(buf, bsize, "data MMU BP: 0x%llx\n", RES_QPC_GET_DATA_MMU_BYPASS(*res));
	__snprintf(buf, bsize, "cyc_idx: 0x%llx\n", RES_QPC_GET_CYCLIC_INDEX(*res));
	_fsnprintf(buf, bsize, "encap EN: 0x%llx\n", RES_QPC_GET_ENCAP_ENABLE(*res));
	_fsnprintf(buf, bsize, "encap type: 0x%llx\n", RES_QPC_GET_ENCAP_TYPE(*res));
	_fsnprintf(buf, bsize, "EQ num: 0x%llx\n", RES_QPC_GET_EQ_NUM(*res));
	_fsnprintf(buf, bsize, "trust level: 0x%llx\n", RES_QPC_GET_TRUST_LEVEL(*res));
	__snprintf(buf, bsize, "expected PSN: 0x%llx\n", RES_QPC_GET_EXPECTED_PSN(*res));
	_fsnprintf(buf, bsize, "sched Q: 0x%llx\n", RES_QPC_GET_SCHD_Q_NUM(*res));
	_fsnprintf(buf, bsize, "ASID: 0x%llx\n", RES_QPC_GET_ASID(*res));
	_fsnprintf(buf, bsize, "transport service: 0x%llx\n", RES_QPC_GET_TRANSPORT_SERVICE(*res));
	__snprintf(buf, bsize, "ECN count: 0x%llx\n", RES_QPC_GET_ECN_COUNT(*res));
	_fsnprintf(buf, bsize, "dst MAC: %04llx%08llx\n", RES_QPC_GET_DST_MAC_MSB(*res),
			RES_QPC_GET_DST_MAC_LSB(*res));
	_fsnprintf(buf, bsize, "dst ipv4: 0x%llx\n", RES_QPC_GET_DST_IP(*res));
	_fsnprintf(buf, bsize, "local key: 0x%llx\n", RES_QPC_GET_LKEY(*res));
	__snprintf(buf, bsize, "NACK syndrome: 0x%llx\n", RES_QPC_GET_NACK_SYNDROME_(*res));
	__snprintf(buf, bsize, "conn state: 0x%llx\n", RES_QPC_GET_CONN_STATE(*res));
	_fsnprintf(buf, bsize, "Priority:0x%llx\n", RES_QPC_GET_PRIORITY(*res));
	_fsnprintf(buf, bsize, "port/lane: 0x%llx\n", RES_QPC_GET_PORT(*res));
	_fsnprintf(buf, bsize, "dest QP: 0x%llx\n", RES_QPC_GET_DST_QP(*res));
	_fsnprintf(buf, bsize, "peer WQ log size: 0x%llx\n", RES_QPC_GET_PEER_WQ_LOG_SIZE(*res));
	__snprintf(buf, bsize, "peer CI: 0x%llx\n", RES_QPC_GET_PEER_REMOTE_CI(*res));
	_fsnprintf(buf, bsize, "peer WQ base addr: 0x%llx\n", RES_QPC_GET_PEER_WQ_BASE_ADDR(*res));
	_fsnprintf(buf, bsize, "peer WQ gran: 0x%llx\n", RES_QPC_GET_PEER_WQ_GRAN(*res));
	_fsnprintf(buf, bsize, "peer QP: 0x%llx\n", RES_QPC_GET_PEER_QP(*res));
	_fsnprintf(buf, bsize, "Peer WQ Index: 0x%llx\n", RES_QPC_GET_PEER_WQ_IDX(*res));

	/* make sure the caller is aware that the buffer it is using is not long enough */
	if (strlen(buf) >= bsize)
		return -EFBIG;

	return 0;
}

static int gaudi3_nic_debugfs_qpc_req_sack_parse(struct hl_device *hdev,
						struct hl_nic_qp_info *qp_info,
						struct gaudi3_qpc_requester *req,
						char *buf, size_t bsize)
{
	int i;

	for (i = 0 ; i < (sizeof(req->sal) / sizeof(u64)) ; i++)
		__snprintf(buf, bsize, "SAL (seg%d): 0x%016llx\n", i, req->sal[i]);

	for (i = 0 ; i < (sizeof(req->swl.data) / sizeof(u64)) ; i++)
		__snprintf(buf, bsize, "SWL (seg%d): 0x%016llx\n", i, req->swl.data[i]);

	/* make sure the caller is aware that the buffer it is using is not long enough */
	return (strlen(buf) >= bsize) ? -EFBIG : 0;
}

static int gaudi3_nic_debugfs_qpc_res_sack_parse(struct hl_device *hdev,
						struct hl_nic_qp_info *qp_info,
						struct gaudi3_qpc_responder *res,
						char *buf, size_t bsize)
{
	int i;

	for (i = 0 ; i < (sizeof(res->sal) / sizeof(u64)) ; i++)
		__snprintf(buf, bsize, "SAL (seg%d): 0x%016llx\n", i, res->sal[i]);

	/* make sure the caller is aware that the buffer it is using is not long enough */
	return (strlen(buf) >= bsize) ? -EFBIG : 0;
}

int gaudi3_nic_debugfs_qp_read(struct hl_device *hdev, char *buf, size_t bsize)
{
	struct gaudi3_qpc_requester qpc_req = {};
	struct gaudi3_qpc_responder qpc_res = {};
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_port *nic_port;
	struct hl_nic_qp_info *qp_info;
	void *qpc;
	u32 port, qpn;
	int rc;
	bool req;

	/* get the details of the QP to read as written by the user via debugfs */
	qp_info = &nic->qp_info;
	req = qp_info->req;
	port = qp_info->port;
	qpn = qp_info->qpn;

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	nic_port = &hdev->nic.nic_ports[port];
	qpc = req ? (void *) &qpc_req : (void *) &qpc_res;

	if (!hl_nic_is_port_open(nic_port)) {
		dev_err(hdev->dev, "Cannot read port %d QP %d, port is not initialized\n", port,
			qpn);
		return -EPERM;
	}

	port_funcs->cfg_lock(nic_port);
	rc = gaudi3_nic_qpc_read(nic_port, qpc, qpn, req);
	port_funcs->cfg_unlock(nic_port);
	if (rc)
		return rc;

	__snprintf(buf, bsize, "port %d, qpn %d, req %d:\n", port, qpn, req);
	if (strlen(buf) >= bsize)
		return -EFBIG;

	if (req) {
		rc = gaudi3_nic_debugfs_qpc_req_parse(hdev, qp_info, &qpc_req, buf, bsize);
		if (!rc && qp_info->exts_print) {
			if (REQ_QPC_GET_SACK_EN(qpc_req))
				rc = gaudi3_nic_debugfs_qpc_req_sack_parse(hdev, qp_info, &qpc_req,
										buf, bsize);
			else
				rc = gaudi3_nic_debugfs_qpc_req_coll_parse(hdev, qp_info, &qpc_req,
										buf, bsize);
		}
	} else {
		rc = gaudi3_nic_debugfs_qpc_res_parse(hdev, qp_info, &qpc_res, buf, bsize);
		if (!rc && qp_info->exts_print && RES_QPC_GET_SACK_EN(qpc_res))
			rc = gaudi3_nic_debugfs_qpc_res_sack_parse(hdev, qp_info, &qpc_res, buf,
									bsize);
	}

	return rc;
}

static int gaudi3_nic_debugfs_wqe_parse(struct hl_device *hdev, struct hl_nic_wqe_info *wqe_info,
					void *wqe, char *buf, size_t bsize)
{
	struct gaudi3_sq_wqe sq_wqe;
	struct gaudi3_rq_wqe rq_wqe;

	if (wqe_info->tx) {
		sq_wqe = *((struct gaudi3_sq_wqe *) wqe);
		__snprintf(buf, bsize, "opcode: 0x%llx\n",
				TX_WQE_GET_OPCODE(sq_wqe));
		__snprintf(buf, bsize, "local class: 0x%llx\n",
				TX_WQE_GET_LOCAL_CLASS(sq_wqe));
		__snprintf(buf, bsize, "SOB ctrl: 0x%llx\n",
				TX_WQE_GET_SOB_CTRL(sq_wqe));
		__snprintf(buf, bsize, "local MCID: 0x%llx\n",
				TX_WQE_GET_LOCAL_MCID(sq_wqe));
		__snprintf(buf, bsize, "local allocH: 0x%llx\n",
				TX_WQE_GET_LOCAL_ALLOCH(sq_wqe));
		__snprintf(buf, bsize, "reduction opcode: 0x%llx\n",
				TX_WQE_GET_REDUCTION_OPCODE(sq_wqe));
		__snprintf(buf, bsize, "RC: 0x%llx\n",
				TX_WQE_GET_RC(sq_wqe));
		__snprintf(buf, bsize, "SE or compress: 0x%llx\n",
				TX_WQE_GET_SE_OR_COMPRESS(sq_wqe));
		__snprintf(buf, bsize, "inline: 0x%llx\n",
				TX_WQE_GET_INLINE(sq_wqe));
		__snprintf(buf, bsize, "ackreq: 0x%llx\n",
				TX_WQE_GET_ACKREQ(sq_wqe));
		__snprintf(buf, bsize, "size: 0x%llx\n",
				TX_WQE_GET_SIZE(sq_wqe));
		__snprintf(buf, bsize, "local address LSB: 0x%llx\n",
				TX_WQE_GET_LOCAL_ADDR_LSB(sq_wqe));
		__snprintf(buf, bsize, "local address MSB: 0x%llx\n",
				TX_WQE_GET_LOCAL_ADDR_MSB(sq_wqe));
		__snprintf(buf, bsize, "remote address LSB: 0x%llx\n",
				TX_WQE_GET_REMOTE_ADDR_LSB(sq_wqe));
		__snprintf(buf, bsize, "remote address MSB: 0x%llx\n",
				TX_WQE_GET_REMOTE_ADDR_MSB(sq_wqe));
		__snprintf(buf, bsize, "tag: 0x%llx\n",
				TX_WQE_GET_TAG(sq_wqe));
		__snprintf(buf, bsize, "SOB id: 0x%llx\n",
				TX_WQE_GET_REMOTE_SOB_ID(sq_wqe));
		__snprintf(buf, bsize, "remote sub SM: 0x%llx\n",
				TX_WQE_GET_REMOTE_SUB_SM(sq_wqe));
		__snprintf(buf, bsize, "remote SM id: 0x%llx\n",
				TX_WQE_GET_REMOTE_SM_ID(sq_wqe));
		__snprintf(buf, bsize, "remote MCID: 0x%llx\n",
				TX_WQE_GET_REMOTE_MCID(sq_wqe));
		__snprintf(buf, bsize, "remote allocH: 0x%llx\n",
				TX_WQE_GET_REMOTE_ALLOCH(sq_wqe));
		__snprintf(buf, bsize, "remote class: 0x%llx\n",
				TX_WQE_GET_REMOTE_CLASS(sq_wqe));
		__snprintf(buf, bsize, "long SOB: 0x%llx\n",
				TX_WQE_GET_LONG_SOB(sq_wqe));
		__snprintf(buf, bsize, "SOB commnad: 0x%llx\n",
				TX_WQE_GET_SOB_CMD(sq_wqe));
		__snprintf(buf, bsize, "completion type: 0x%llx\n",
				TX_WQE_GET_COMPLETION_TYPE(sq_wqe));
	} else {
		rq_wqe = *((struct gaudi3_rq_wqe *) wqe);
		__snprintf(buf, bsize, "opcode: 0x%llx\n",
				RX_WQE_GET_OPCODE(rq_wqe));
		__snprintf(buf, bsize, "WQE index: 0x%llx\n",
				RX_WQE_GET_WQE_INDEX(rq_wqe));
		__snprintf(buf, bsize, "local SOB id: 0x%llx\n",
				RX_WQE_GET_LOCAL_SOB_ID(rq_wqe));
		__snprintf(buf, bsize, "local sub SM: 0x%llx\n",
				RX_WQE_GET_LOCAL_SUB_SM(rq_wqe));
		__snprintf(buf, bsize, "local SM id: 0x%llx\n",
				RX_WQE_GET_LOCAL_SM_ID(rq_wqe));
		__snprintf(buf, bsize, "SOB fifo: 0x%llx\n",
				RX_WQE_GET_SOB_FIFO(rq_wqe));
		__snprintf(buf, bsize, "long SOB: 0x%llx\n",
				RX_WQE_GET_LONG_SOB(rq_wqe));
		__snprintf(buf, bsize, "SOB command: 0x%llx\n",
				RX_WQE_GET_SOB_CMD(rq_wqe));
		__snprintf(buf, bsize, "completion type: 0x%llx\n",
				RX_WQE_GET_COMPLETION_TYPE(rq_wqe));
		__snprintf(buf, bsize, "size: 0x%llx\n",
				RX_WQE_GET_SIZE(rq_wqe));
		__snprintf(buf, bsize, "tag: 0x%llx\n",
				RX_WQE_GET_TAG(rq_wqe));
	}

	/* Make sure the caller is aware that the buffer used isn't big enough */
	if (strlen(buf) >= bsize)
		return -EFBIG;

	return 0;
}

static int gaudi3_nic_debugfs_get_wqe_from_dram(struct hl_nic_port *nic_port, u32 type,
						u64 wqe_offset, u8 wqe_size, void *wqe)
{
	struct hl_wq_array_properties *wq_arr_props;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_mmap_mem_buf *buf;
	struct hl_nic_mem *mem;
	struct hl_ctx *ctx;
	u64 wq_base_addr, wqe_addr;
	u32 *wqe_p;
	char *type_str;
	int i, rc = 0;

	ctx = hl_get_compute_ctx(hdev);
	if (!ctx) {
		dev_err(hdev->dev, "no ctx available\n");
		return -EINVAL;
	}

	wq_arr_props = &nic_port->wq_arr_props[type];
	type_str = wq_arr_props->type_str;

	buf = hl_mmap_mem_buf_get(&ctx->hpriv->mem_mgr, wq_arr_props->handle);
	if (!buf) {
		dev_err(hdev->dev, "Failed to retrieve port %d %s WQ memory\n",
			nic_port->port, type_str);
		rc = -EINVAL;
		goto out;
	}

	mem = buf->private;
	wq_base_addr = mem->device_addr;
	wqe_addr = wq_base_addr + wqe_offset;
	wqe_p = (u32 *) wqe;

	for (i = 0 ; i < wqe_size / sizeof(u32) ; i++)
		wqe_p[i] = hl_nic_dram_readl(hdev, wqe_addr + (i * 4));

	hl_mmap_mem_buf_put(buf);

out:
	hl_ctx_put(ctx);
	return rc;
}

static int gaudi3_nic_debugfs_get_wqe_from_host(struct hl_nic_port *nic_port, u32 type,
						u64 wqe_offset, u8 wqe_size, void *wqe)
{
	struct hl_wq_array_properties *wq_arr_props;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_mmap_mem_buf *buf;
	void *wq_base_addr, *wqe_addr;
	struct hl_nic_mem *mem;
	struct hl_ctx *ctx;
	char *type_str;
	int rc = 0;

	ctx = hl_get_compute_ctx(hdev);
	if (!ctx) {
		dev_err(hdev->dev, "no ctx available\n");
		return -EINVAL;
	}

	wq_arr_props = &nic_port->wq_arr_props[type];
	type_str = wq_arr_props->type_str;

	buf = hl_mmap_mem_buf_get(&ctx->hpriv->mem_mgr, wq_arr_props->handle);
	if (!buf) {
		dev_err(hdev->dev, "Failed to retrieve port %d %s WQ memory\n",
			nic_port->port, type_str);
		rc = -EINVAL;
		goto out;
	}

	mem = buf->private;
	wq_base_addr = mem->kernel_address;
	wqe_addr = (u8 *) wq_base_addr + wqe_offset;
	memcpy(wqe, wqe_addr, wqe_size);

	hl_mmap_mem_buf_put(buf);

out:
	hl_ctx_put(ctx);
	return rc;
}

static int gaudi3_nic_debugfs_get_wqe(struct hl_nic_port *nic_port, void *wqe, u32 qpn,
					u32 wqe_idx, bool is_tx)
{
	struct hl_wq_array_properties *wq_arr_props;
	struct hl_nic_port_funcs *port_funcs;
	struct hl_nic_funcs *nic_funcs;
	struct hl_device *hdev;
	u64 wq_size, wqe_offset, wq_size_cline_log;
	u32 port, offset, type, wq_idx, min_id, max_id;
	u8 wqe_size;
	bool is_coll, is_scale_out_conn = false;
	int rc = 0;

	hdev = nic_port->hdev;
	nic_funcs = hdev->asic_funcs->nic_funcs;
	port_funcs = nic_funcs->port_funcs;
	port = nic_port->port;

	is_coll = nic_funcs->is_coll_conn_id(hdev, qpn);

	/* Validate QPN input and calculate the WQ index in the WQ array */
	if (is_coll) {
		u32 coll_qp_offset = ELEMENT_OFFSET(port, NIC_MAX_COLL_QP_NUM);

		is_scale_out_conn = qpn >= nic_port->scale_out_coll_qp_idx_offset;
		nic_funcs->get_coll_qp_id_range(hdev, is_scale_out_conn, &min_id, &max_id);

		min_id += coll_qp_offset;
		max_id += coll_qp_offset;

		if ((qpn < min_id) || (qpn > max_id)) {
			dev_err(hdev->dev, "Invalid collective QP id for port %u (should be between %u-%u)\n",
				port, min_id, max_id);
			return -EINVAL;
		}

		if (is_scale_out_conn)
			wq_idx = qpn - nic_port->scale_out_coll_qp_idx_offset;
		else
			wq_idx = qpn - nic_port->coll_qp_idx_offset;
	} else {
		port_funcs->get_qp_id_range(nic_port, &min_id, &max_id);

		if ((qpn < min_id) || (qpn > max_id)) {
			dev_err(hdev->dev, "Invalid QP id for port %u (should be %u-%u)\n",
				port, min_id, max_id);
			return -EINVAL;
		}

		wq_idx = qpn - nic_port->qp_idx_offset;
	}

	type = hl_nic_get_wq_array_type(is_tx, is_coll, is_scale_out_conn);
	wq_arr_props = &nic_port->wq_arr_props[type];
	offset = wq_arr_props->idx;

	if (is_tx) {
		wqe_size = nic_port->swqe_size;
		wq_size_cline_log = NIC_OFFSET_RREG32(mmD0_NIC0_TXE_LOG_MAX_WQ_SIZE_0);
	} else {
		wqe_size = NIC_RECV_WQE_SIZE;
		wq_size_cline_log = NIC_OFFSET_RREG32(mmD0_NIC0_RXE_WIN_WQ_MISC_0);
	}

	/* Calculate the WQE offset. */
	wq_size = (1ULL << wq_size_cline_log) * NIC_CACHE_LINE_SIZE;
	wqe_offset = wq_size * wq_idx + wqe_size * wqe_idx;

	if (wq_arr_props->on_device_mem)
		rc = gaudi3_nic_debugfs_get_wqe_from_dram(nic_port, type, wqe_offset, wqe_size,
								wqe);
	else
		rc = gaudi3_nic_debugfs_get_wqe_from_host(nic_port, type, wqe_offset, wqe_size,
								wqe);

	return rc;
}

int gaudi3_nic_debugfs_wqe_read(struct hl_device *hdev, char *buf, size_t bsize)
{
	struct gaudi3_sq_wqe sq_wqe = {};
	struct gaudi3_rq_wqe rq_wqe = {};
	struct hl_nic_wqe_info *wqe_info;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_port *nic_port;
	u32 port, qpn, wqe_idx;
	void *wqe;
	bool tx;
	int rc;

	/* Get the details of the WQE to read as written by the user via debugfs */
	wqe_info = &nic->wqe_info;
	tx = wqe_info->tx;
	port = wqe_info->port;
	qpn = wqe_info->qpn;
	wqe_idx = wqe_info->wqe_idx;
	nic_port = &hdev->nic.nic_ports[port];
	wqe = tx ? (void *) &sq_wqe : (void *) &rq_wqe;

	if (!hl_nic_is_port_open(nic_port)) {
		dev_err(hdev->dev,
			"Cannot read port %d QP %d, port is not initialized\n", port, qpn);
		return -EPERM;
	}

	rc = gaudi3_nic_debugfs_get_wqe(nic_port, wqe, qpn, wqe_idx, tx);
	if (rc)
		goto exit;

	__snprintf(buf, bsize, "port %d, qpn %d, wqe_idx %d, tx %d:\n", port, qpn, wqe_idx, tx);

	rc = gaudi3_nic_debugfs_wqe_parse(hdev, wqe_info, wqe, buf, bsize);

exit:
	return rc;
}

void gaudi3_nic_debugfs_print_fec_stats(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 card_location, port;
	u64 data[FEC_STAT_LAST];

	card_location = hdev->nic.card_location;
	port = nic_port->port;

	gaudi3_nic_read_mac_fec_stats(nic_port, data);

	dev_info(hdev->dev,
		"Card %u Port %u: corrected_accumulated %llu uncorrected_accumulated %llu pre_fec_SER: %llu post_fec_SER: %llu\n",
		card_location, port, data[FEC_CW_CORRECT], data[FEC_CW_UNCORRECTABLE],
		data[FEC_PRE_FEC_SER], data[FEC_POST_FEC_SER]);
}

int gaudi3_nic_debugfs_write_coll_lag_size(struct hl_device *hdev, u32 coll_lag_size)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct hl_nic_macro *nic_macro;
	enum hl_device_status status;
	u32 port;
	int i;

	if (!hl_device_operational(hdev, &status)) {
		dev_dbg(hdev->dev, "Device is %s. Can't write coll_lag_size\n",
			hdev->status[status]);
		return -EBUSY;
	}

	if (coll_lag_size == gaudi3->coll_lag_size)
		return 0;

	gaudi3->coll_lag_size = coll_lag_size;

	/* Since coll_lag_size is a macro parameter, program the register per macro.*/
	for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++) {
		nic_macro = &hdev->nic.nic_macros[i];

		if (!gaudi3_nic_is_macro_enabled(hdev, nic_macro))
			continue;

		port = gaudi3_nic_get_first_port(nic_macro);

		NIC_RMWREG32(mmD0_NIC0_QPC_PATCHER_CFG,
			(coll_lag_size << NIC_QPC_PATCHER_CFG_LAG_SIZE_S),
			NIC_QPC_PATCHER_CFG_LAG_SIZE_M);
	}

	return 0;
}

int gaudi3_nic_debugfs_read_coll_lag_size(struct hl_device *hdev, u32 *coll_lag_size)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	enum hl_device_status status;

	if (!hl_device_operational(hdev, &status)) {
		dev_dbg(hdev->dev, "Device is %s. Can't read coll_lag_size\n",
			hdev->status[status]);
		return -EBUSY;
	}

	*coll_lag_size = gaudi3->coll_lag_size;

	return 0;
}
