// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "nic.h"
#include "../common/habanalabs.h"

#define OP_RETRY_COUNT		4
#define OPC_SETTLE_RETRY_COUNT	20

/* The following table represents the (valid) operations that can be performed on
 * a QP in order to move it from one state to another
 * For example: a QP in RTR state can be moved to RTS state using the NIC_QP_OP_RTR_2RTS
 * operation.
 */
static const enum hl_nic_qp_state_op qp_valid_state_op[NIC_QP_NUM_STATE][NIC_QP_NUM_STATE] = {
	[NIC_QP_STATE_RESET] = {
		[NIC_QP_STATE_RESET]	= NIC_QP_OP_2RESET,
		[NIC_QP_STATE_INIT]	= NIC_QP_OP_RST_2INIT,
		[NIC_QP_STATE_SQD]	= NIC_QP_OP_NOP,
		[NIC_QP_STATE_QPD]	= NIC_QP_OP_NOP,
	},
	[NIC_QP_STATE_INIT] = {
		[NIC_QP_STATE_RESET]	= NIC_QP_OP_2RESET,
		[NIC_QP_STATE_ERR]	= NIC_QP_OP_2ERR,
		[NIC_QP_STATE_INIT]	= NIC_QP_OP_NOP,
		[NIC_QP_STATE_RTR]	= NIC_QP_OP_INIT_2RTR,
		[NIC_QP_STATE_RTS]	= NIC_QP_OP_INIT_2RTS,
		[NIC_QP_STATE_SQD]	= NIC_QP_OP_NOP,
		[NIC_QP_STATE_QPD]	= NIC_QP_OP_NOP,
	},
	[NIC_QP_STATE_RTR] = {
		[NIC_QP_STATE_RESET]	= NIC_QP_OP_2RESET,
		[NIC_QP_STATE_ERR]	= NIC_QP_OP_2ERR,
		[NIC_QP_STATE_RTR]	= NIC_QP_OP_RTR_2RTR,
		[NIC_QP_STATE_RTS]	= NIC_QP_OP_RTR_2RTS,
		[NIC_QP_STATE_SQD]	= NIC_QP_OP_NOP,
		[NIC_QP_STATE_QPD]	= NIC_QP_OP_RTR_2QPD,
	},
	[NIC_QP_STATE_RTS] = {
		[NIC_QP_STATE_RESET]	= NIC_QP_OP_2RESET,
		[NIC_QP_STATE_ERR]	= NIC_QP_OP_2ERR,
		[NIC_QP_STATE_RTS]	= NIC_QP_OP_RTS_2RTS,
		[NIC_QP_STATE_SQD]	= NIC_QP_OP_RTS_2SQD,
		[NIC_QP_STATE_QPD]	= NIC_QP_OP_RTS_2QPD,
		[NIC_QP_STATE_SQERR]	= NIC_QP_OP_RTS_2SQERR,
		[NIC_QP_STATE_RTR]	= NIC_QP_OP_RTS_2RTR,
	},
	[NIC_QP_STATE_SQD] = {
		[NIC_QP_STATE_RESET]	= NIC_QP_OP_2RESET,
		[NIC_QP_STATE_ERR]	= NIC_QP_OP_2ERR,
		[NIC_QP_STATE_SQD]	= NIC_QP_OP_SQD_2SQD,
		[NIC_QP_STATE_RTS]	= NIC_QP_OP_SQD_2RTS,
		[NIC_QP_STATE_QPD]	= NIC_QP_OP_SQD_2QPD,
		[NIC_QP_STATE_SQERR]	= NIC_QP_OP_SQD_2SQ_ERR,
	},
	[NIC_QP_STATE_QPD] = {
		[NIC_QP_STATE_RESET]	= NIC_QP_OP_2RESET,
		[NIC_QP_STATE_ERR]	= NIC_QP_OP_2ERR,
		[NIC_QP_STATE_SQD]	= NIC_QP_OP_NOP,
		[NIC_QP_STATE_QPD]	= NIC_QP_OP_NOP,
		[NIC_QP_STATE_RTR]	= NIC_QP_OP_QPD_2RTR,
	},
	[NIC_QP_STATE_SQERR] = {
		[NIC_QP_STATE_RESET]	= NIC_QP_OP_2RESET,
		[NIC_QP_STATE_ERR]	= NIC_QP_OP_2ERR,
		[NIC_QP_STATE_SQD]	= NIC_QP_OP_SQ_ERR_2SQD,
		[NIC_QP_STATE_SQERR]	= NIC_QP_OP_NOP,
	},
	[NIC_QP_STATE_ERR] = {
		[NIC_QP_STATE_RESET]	= NIC_QP_OP_2RESET,
		[NIC_QP_STATE_ERR]	= NIC_QP_OP_2ERR,
	}
};

static char *nic_qp_state_2name(enum hl_nic_qp_state state)
{
	static char *arr[NIC_QP_NUM_STATE] = {
						"Reset",
						"Init",
						"RTR",
						"RTS",
						"SQD",
						"QPD",
						"SQERR",
						"ERR",
	};

	return arr[state];
}

static int nic_qp_op_reset(struct hl_nic_port *nic_port, struct hl_qp *qp)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	int rc, rc1;

	/* clear the QPCs */
	rc = nic_funcs->port_funcs->qpc_clear(nic_port, qp, true);
	if (rc && hl_device_operational(hdev, NULL))
		/* Device might not respond during reset if the reset was due to error */
		dev_err(hdev->dev, "Port %d QP %d: Failed to clear requester QPC\n",
			qp->port, qp->qp_id);
	else
		qp->is_req = false;

	rc1 = nic_funcs->port_funcs->qpc_clear(nic_port, qp, false);
	if (rc1) {
		rc = rc1;
		if (hl_device_operational(hdev, NULL))
			/* Device might not respond during reset if the reset was due to error */
			dev_err(hdev->dev, "Port %d QP %d: Failed to clear responder QPC\n",
				qp->port, qp->qp_id);
	} else {
		qp->is_res = false;
	}

#if HL_NIC_DEBUG
	dev_dbg(hdev->dev,
		"Port %d QP %d: moved to RESET state (rc %d)\n",
		qp->port, qp->qp_id, rc);
#endif

	qp->curr_state = NIC_QP_STATE_RESET;

	return rc;
}

static int nic_qp_op_reset_2init(struct hl_nic_port *nic_port, struct hl_qp *qp)
{
	if (ZERO_OR_NULL_PTR(qp))
		return -EINVAL;

	qp->curr_state = NIC_QP_STATE_INIT;

	return 0;
}

static int nic_qp_op_2rts(struct hl_nic_port *nic_port, struct hl_qp *qp,
			struct hl_nic_req_conn_ctx_in *in)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	int rc;

	rc = nic_funcs->set_req_qp_ctx(hdev, in, qp);
	if (rc)
		return rc;

	qp->curr_state = NIC_QP_STATE_RTS;
	qp->is_req = true;

	return 0;
}

static int nic_qp_op_2rtr(struct hl_nic_port *nic_port, struct hl_qp *qp,
			struct hl_nic_res_conn_ctx_in *in)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	int rc;

	rc = nic_funcs->set_res_qp_ctx(hdev, in, qp);
	if (rc)
		return rc;

	qp->curr_state = NIC_QP_STATE_RTR;
	qp->is_res = true;

	return 0;
}

static inline int nic_qp_invalidate_qpc(struct hl_nic_port *nic_port,
					struct hl_qp *qp, bool is_req)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	int i, rc;

	for (i = 0 ; i < OP_RETRY_COUNT ; i++) {
		rc = nic_funcs->port_funcs->qpc_invalidate(nic_port, qp, is_req);
		if (!rc)
			break;

		usleep_range(100, 200);
	}

	return rc;
}

static inline int wait_for_qpc_idle(struct hl_nic_port *nic_port,
					struct hl_qp *qp, bool is_req)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_qpc_attr qpc_attr;
	struct hl_nic_port_funcs *port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;
	int i, rc;

	for (i = 0 ; i < OPC_SETTLE_RETRY_COUNT ; i++) {
		rc = port_funcs->qpc_query(nic_port, qp->qp_id,
						is_req, &qpc_attr);
		if (!(rc || qpc_attr.in_work))
			break;

		/* Release lock while we wait before retry.
		 * Note, we can assert that we are already locked.
		 */
		port_funcs->cfg_unlock(nic_port);

		msleep(20);

		port_funcs->cfg_lock(nic_port);
	}

	if (!rc && qpc_attr.in_work)
		rc = -ETIMEDOUT;

	return rc;
}

static int nic_qp_invalidate(struct hl_nic_port *nic_port, struct hl_qp *qp,
			bool is_req, bool wait_for_idle)
{
	struct hl_device *hdev = nic_port->hdev;
	int rc;

	rc = nic_qp_invalidate_qpc(nic_port, qp, is_req);
	if (rc) {
		if (hl_device_operational(hdev, NULL))
			dev_err(hdev->dev, "Port %d QP %d, failed to invalidate %s QPC (rc %d)\n",
				nic_port->port, qp->qp_id, is_req ? "Requester" : "Responder", rc);
		return rc;
	}

	/* TODO: SW-63650 do not wait for idle in simulator as qpc reads are very expensive */
	if (!wait_for_idle || !hdev->pdev)
		return rc;

	rc = wait_for_qpc_idle(nic_port, qp, is_req);
	if (rc) {
		dev_err(hdev->dev, "Port %d QP %d, %s QPC is not idle (rc %d)\n",
			nic_port->port, qp->qp_id, is_req ? "Requester" : "Responder", rc);
		return rc;
	}

	return rc;
}

/* Drain the Requester */
static int nic_qp_op_rts_2sqd(struct hl_nic_port *nic_port, struct hl_qp *qp, void *attr)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_qpc_drain_attr *drain = attr;
	int rc = 0;

	switch (qp->curr_state) {
	case NIC_QP_STATE_RTS:
		nic_qp_invalidate(nic_port, qp, true, drain->wait_for_idle);
		if (drain->wait_for_idle)
			ssleep(hdev->nic_qp_drain_time);

		break;
	default:
		rc = -EOPNOTSUPP;
		break;
	}

	if (!rc)
		qp->curr_state = NIC_QP_STATE_SQD;

	return rc;
}

/* Re-drain the Requester. This function is called without holding the cfg lock so it must not
 * access the HW or do anything other than just sleeping.
 */
static int nic_qp_op_sqd_2sqd(struct hl_nic_port *nic_port, struct hl_qp *qp, void *attr)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_qpc_drain_attr *drain = attr;

	/* no need to invalidate the QP as it was already invalidated just extend the wait time */
	if (drain->wait_for_idle)
		ssleep(hdev->nic_qp_drain_time);

	return 0;
}

/* Drain the QP (Requester and Responder) */
static int nic_qp_op_2qpd(struct hl_nic_port *nic_port, struct hl_qp *qp, void *attr)
{
	struct hl_nic_qpc_drain_attr *drain = attr;
	int rc = 0;

	switch (qp->curr_state) {
	case NIC_QP_STATE_RTR:
		/* In RTR only the Resp is working */
		nic_qp_invalidate(nic_port, qp, false, drain->wait_for_idle);
		break;
	case NIC_QP_STATE_RTS:
		/* In RTS both the Resp and Req are working */
		nic_qp_op_rts_2sqd(nic_port, qp, attr);
		nic_qp_invalidate(nic_port, qp, false, drain->wait_for_idle);
		break;
	case NIC_QP_STATE_SQD:
		/* In SQD only the Resp is working */
		nic_qp_invalidate(nic_port, qp, false, drain->wait_for_idle);
		break;
	case NIC_QP_STATE_QPD:
		break;
	default:
		rc = -EOPNOTSUPP;
		break;
	}

	if (!rc)
		qp->curr_state = NIC_QP_STATE_QPD;

	return rc;
}

static int nic_qp_op_2reset(struct hl_nic_port *nic_port,
			struct hl_qp *qp, const struct hl_nic_qpc_reset_attr *attr)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	struct hl_nic_qpc_drain_attr drain;

	/* brute-force reset when reset mode is Hard */
	if ((attr->reset_mode == NIC_QP_RESET_MODE_HARD) &&
			(qp->curr_state != NIC_QP_STATE_RESET)) {
		/* invalidate */
		nic_funcs->port_funcs->qpc_invalidate(nic_port, qp, true);
		nic_funcs->port_funcs->qpc_invalidate(nic_port, qp, false);

		/* wait for HW digest the invalidation */
		usleep_range(100, 150);

		nic_qp_op_reset(nic_port, qp);
		return 0;
	}

	if (attr->reset_mode == NIC_QP_RESET_MODE_GRACEFUL)
		drain.wait_for_idle = true;
	else
		drain.wait_for_idle = false;

	switch (qp->curr_state) {
	case NIC_QP_STATE_RESET:
		break;
	case NIC_QP_STATE_INIT:
		nic_qp_op_reset(nic_port, qp);
		break;
	case NIC_QP_STATE_RTR:
	case NIC_QP_STATE_RTS:
	case NIC_QP_STATE_SQD:
		nic_qp_op_2qpd(nic_port, qp, &drain);
		nic_qp_op_reset(nic_port, qp);
		break;
	case NIC_QP_STATE_QPD:
		nic_qp_op_reset(nic_port, qp);
		break;
	case NIC_QP_STATE_SQERR:
	case NIC_QP_STATE_ERR:
		nic_qp_op_reset(nic_port, qp);
		break;
	default:
		dev_err(hdev->dev,
			"Port %d QP %d: Unknown state %d, moving to RESET state\n",
			qp->port, qp->qp_id, qp->curr_state);
		nic_qp_op_reset(nic_port, qp);
		break;
	}

	return 0;
}

/* QP state handling routines  */
int hl_nic_qp_modify(struct hl_nic_port *nic_port, struct hl_qp *qp,
			enum hl_nic_qp_state new_state, void *params)
{
	enum hl_nic_qp_state prev_state = qp->curr_state;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_port_funcs *port_funcs;
	enum hl_nic_qp_state_op op;
	int rc;

	port_funcs = hdev->asic_funcs->nic_funcs->port_funcs;

	/* only SQD->SQD transition can be executed without holding the configuration lock */
	if (prev_state != NIC_QP_STATE_SQD || new_state != NIC_QP_STATE_SQD) {
		if (!port_funcs->cfg_is_locked(nic_port)) {
			dev_err(hdev->dev,
				"Configuration lock must be held while moving Port %u QP %u from state %s to %s\n",
				qp->port, qp->qp_id, nic_qp_state_2name(prev_state),
				nic_qp_state_2name(new_state));
			return -EACCES;
		}
	}

	if ((qp->curr_state >= NIC_QP_NUM_STATE) || (new_state >= NIC_QP_NUM_STATE) ||
			(qp_valid_state_op[qp->curr_state][new_state] == NIC_QP_OP_INVAL)) {
		dev_err(hdev->dev,
			"Invalid QP state transition, Port %u QP %u from state %s to %s\n",
			qp->port, qp->qp_id, nic_qp_state_2name(prev_state),
			nic_qp_state_2name(new_state));
		return -EINVAL;
	}

	if (new_state >= NIC_QP_NUM_STATE) {
		dev_err(hdev->dev, "Invalid QP state %d\n", new_state);
		return -EINVAL;
	}

#if HL_NIC_DEBUG
	dev_dbg(hdev->dev,
		"Port %d QP %d: moving from state %s to %s\n",
		qp->port, qp->qp_id, nic_qp_state_2name(qp->curr_state),
		nic_qp_state_2name(new_state));
#endif

	/* get the operation needed for this state transition */
	op = qp_valid_state_op[qp->curr_state][new_state];

	switch (op) {
	case NIC_QP_OP_2RESET:
		rc = nic_qp_op_2reset(nic_port, qp, params);
		break;
	case NIC_QP_OP_RST_2INIT:
		rc = nic_qp_op_reset_2init(nic_port, qp);
		break;
	case NIC_QP_OP_INIT_2RTR:
		rc = nic_qp_op_2rtr(nic_port, qp, params);
		break;
	case NIC_QP_OP_RTR_2RTR:
		/* TODO: support modification mask as suggested in VERBS */
		rc = nic_qp_op_2rtr(nic_port, qp, params);
		break;
	case NIC_QP_OP_RTR_2QPD:
		rc = nic_qp_op_2qpd(nic_port, qp, params);
		break;
	case NIC_QP_OP_RTR_2RTS:
		rc = nic_qp_op_2rts(nic_port, qp, params);
		break;
	case NIC_QP_OP_RTS_2RTS:
		/* TODO: support modification mask as suggested in VERBS */
		rc = nic_qp_op_2rts(nic_port, qp, params);
		break;
	case NIC_QP_OP_RTS_2SQD:
		rc = nic_qp_op_rts_2sqd(nic_port, qp, params);
		break;
	case NIC_QP_OP_SQD_2SQD:
		rc = nic_qp_op_sqd_2sqd(nic_port, qp, params);
		break;
	case NIC_QP_OP_RTS_2QPD:
		rc = nic_qp_op_2qpd(nic_port, qp, params);
		break;
	case NIC_QP_OP_SQD_2QPD:
		rc = nic_qp_op_2qpd(nic_port, qp, params);
		break;
	case NIC_QP_OP_INVAL:
		rc = -EINVAL;
		break;
	/* TODO: w/a SW-62591, remove when fixed */
	case NIC_QP_OP_INIT_2RTS:
		rc = nic_qp_op_2rts(nic_port, qp, params);
		break;
	case NIC_QP_OP_RTS_2RTR:
		rc = nic_qp_op_2rtr(nic_port, qp, params);
		/* Remain in RTS state */
		qp->curr_state = NIC_QP_STATE_RTS;
		break;
	case NIC_QP_OP_NOP:
		rc = 0;
		break;
	default:
		rc = -EOPNOTSUPP;
		break;
	}

	if (rc)
		dev_err(hdev->dev,
			"Errors detected while moving Port %u QP %u from state %s to %s, (rc %d)\n",
			qp->port, qp->qp_id, nic_qp_state_2name(prev_state),
			nic_qp_state_2name(new_state), rc);

	return rc;
}
