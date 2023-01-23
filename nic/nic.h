/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2021-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef NIC_H_
#define NIC_H_

#include <uapi/drm/habanalabs_accel.h>
#include <linux/habanalabs/nic.h>

#include <linux/kfifo.h>
#include <linux/hashtable.h>
#include <linux/ctype.h>

#include "../common/habanalabs_compat.h"
#include "../include/common/cpucp_if.h"

#define CQ_USER_MAX_ENTRIES		(U32_MAX / sizeof(struct hl_nic_cqe))
#define HL_NIC_DEBUG			0
#define NIC_PHY_TX_TAPS_NUM		5
#define PORT_LANES_2			2
#define PORT_LANES_4			4
#define NIC_EQ_INFO_BUF_SIZE		256
#define NIC_NUM_CONCUR_ASIDS		4

/* driver specific value, should always be >= asic specific h/w resource */
#define NIC_DRV_MAX_CQS_NUM		32
#define NIC_DRV_MAX_CCQS_NUM		4
#define NIC_DRV_NUM_DB_FIFOS		32

#define NIC_QPC_INV_USEC		1000000 /* 1s */
#define NIC_SIM_QPC_INV_USEC		(NIC_QPC_INV_USEC * 5)
#define NIC_PLDM_QPC_INV_USEC		(NIC_QPC_INV_USEC * 10)

#define NIC_SPMU_STATS_LEN_MAX		6

#define PARSE_FIELD(data, shift, size)	(((data) >> (shift)) & (BIT(size) - 1))
#define MERGE_FIELDS(data_hi, data_lo, shift)	\
					((data_hi) << (shift) | (data_lo))

#define NIC_MACRO_CFG_SIZE		hdev->asic_prop.nic_props.macro_cfg_size
#define NIC_MACRO_CFG_BASE(port)	(NIC_MACRO_CFG_SIZE * ((port) >> 1))

#define NIC_MACRO_RREG32(reg) RREG32(NIC_MACRO_CFG_BASE(port) + (reg))
#define NIC_MACRO_WREG32(reg, val) \
				WREG32(NIC_MACRO_CFG_BASE(port) + (reg), (val))
#define NIC_MACRO_RMWREG32(reg, val, mask) \
			RMWREG32(NIC_MACRO_CFG_BASE(port) + (reg), val, mask)

#define NIC_PORT_CHECK_ENABLE	BIT(0)
#define NIC_PORT_CHECK_OPEN	BIT(1)
#define NIC_PORT_PRINT_ON_ERR	BIT(2)
#define NIC_PORT_CHECK_INTERNAL	BIT(3)
#define NIC_PORT_CHECK_ALL	GENMASK(3, 0)

#define QPC_REQ_BURST_SIZE	16
#define QPC_REQ_SCHED_Q		3
#define QPC_RES_SCHED_Q		2
#define QPC_RAW_SCHED_Q		1

#define REGMASK(V, F)			(((V) << F##_SHIFT) & F##_MASK)
#define REGMASK2(V, F)			(((V) << F##_S) & F##_M)

#define NIC_DR_10		1031250
#define NIC_DR_25		2578125
#define NIC_DR_26		2656250
#define NIC_DR_50		5312500

#define NIC_MAC_LANE_0		0U
#define NIC_MAC_LANE_1		1U
#define NIC_MAC_LANE_2		2U
#define NIC_MAC_LANE_3		3U
#define NIC_MAC_LANES		4U

#define CC_CQE_SIZE		16
#define USER_CCQ_MAX_ENTRIES	((1 << 21) / CC_CQE_SIZE) /* dma_alloc can only allocate 2M */
#define USER_CCQ_MIN_ENTRIES	16

/* QP drain time in seconds */
#define NIC_QP_DRAIN_TIME	2

#define DMA_COHERENT_MAX_SIZE	SZ_4M

#define NIC_MAX_NON_SCALE_OUT_COLL_CONNS	128

extern struct hl_en_stat hl_nic_mac_fec_stats[];
extern struct hl_en_stat hl_nic_mac_stats_rx[];
extern struct hl_en_stat hl_nic_mac_stats_tx[];
extern size_t hl_nic_mac_fec_stats_len;
extern size_t hl_nic_mac_stats_rx_len;
extern size_t hl_nic_mac_stats_tx_len;

/**
 * struct hl_nic_dq_qp_info - structure to hold qp info for dispatch queue.
 * @node: reference to a QP within the list
 * @dq: dq per asid event dispatcher queue
 * @qpn: QP id
 */
struct hl_nic_dq_qp_info {
	struct hlist_node	node;
	struct hl_nic_ev_dq	*dq;
	u32			qpn;
};

/**
 * struct hl_nic_eq_raw_buf - a buffer holding unparsed NIC raw eq events
 * @events: an array which stores the events
 * @head: the queue head
 * @tail: the queue tail
 * @events_count: number of available events in the queue
 */
struct hl_nic_eq_raw_buf {
	struct hl_nic_eqe events[NIC_EQ_INFO_BUF_SIZE];
	u32 head;
	u32 tail;
	u32 events_count;
};

/**
 * struct hl_nic_ev_dq - per-asid/app event dispatch queue
 * @buf: the dispatched events queue.
 * @asid: the asid registered on this dq
 * @overflow: buffer overflow counter
 * @associated: if this dq associate with a user/asid or not.
 */
struct hl_nic_ev_dq {
	struct hl_nic_eq_raw_buf buf;
	u32 asid;
	u32 overflow;
	u8 associated;
};

/**
 * struct hl_nic_ev_dqs - software managed events dispatch queues
 *                            used for dispatching events to their applications
 * @qps: a hash table to convert QP-numbers to their owner (ASID).
 * @cq_dq: array to associate/convert cq-numbers to the dispatch queues.
 * @ccq_dq: array to associate/convert ccq-numbers to the dispatch queues.
 * @db_dq: array to associate/convert doorbell-numbers to the dispatch queues.
 * @edq: the events dispatch queues (as many queues as the number of possible
 *       same-time ASIDs).
 * @default_edq: default events dispatch queue for unknown resources and events.
 * @lock: mutex to protect from simultaneous operations.
 */
struct hl_nic_ev_dqs {
	DECLARE_HASHTABLE(qps, 11);
	struct hl_nic_ev_dq *cq_dq[NIC_DRV_MAX_CQS_NUM];
	struct hl_nic_ev_dq *ccq_dq[NIC_DRV_MAX_CCQS_NUM];
	struct hl_nic_ev_dq *db_dq[NIC_DRV_NUM_DB_FIFOS];
	struct hl_nic_ev_dq edq[NIC_NUM_CONCUR_ASIDS];
	struct hl_nic_ev_dq default_edq;
	struct mutex lock;
};

/**
 * struct hl_nic_properties - ASIC specific NIC properties.
 * @phy_base_addr: base address of the PHY.
 * @nic_drv_addr: the base address of NIC memory in the device
 * @nic_drv_size: the size of NIC memory in the device
 * @nic_drv_base_addr: the aligned base address of NIC memory in the device
 * @nic_drv_end_addr: the aligned end address of NIC memory in the device
 * @sb_base_addr: the base address of a Tx eth pkt cyclic buffer
 * @sb_base_size: the size of a Tx eth pkt cyclic buffer
 * @swq_base_addr: the base address of a Tx workqueue cyclic buffer
 * @swq_base_size: the size of a Tx workqueue cyclic buffer (relevant only for Gaudi1).
 * @txs_base_addr: base address of the ports timer cfg
 * @txs_base_size: size of the ports timer cfg
 * @wq_base_addr: base address of send and receive work-q
 * @wq_base_size: base address of send and receive work-q
 * @tmr_base_addr: base address of the macros timer cfg
 * @tmr_base_size: size of the macros timer cfg
 * @req_qpc_base_addr: the base address of a requester (sender) QP context buffer
 * @req_qpc_base_size: the size of a requester (sender) QP context buffer
 * @res_qpc_base_addr: the base address of a responder (receiver) QP context buffer
 * @res_qpc_base_size: the size of a requester (receiver) QP context buffer
 * @req_qpc_swl_base_addr: the base address of the Selective WQE List (SWL) portion of the
 *                         requester QPC (gaudi3)
 * @req_qpc_swl_base_size: the size of a requester SWL QP context buffer
 * @max_hw_qps_num: maximum number of QPs supported by HW.
 * @max_qps_num: maximum number of QPs to allocate.
 * @max_hw_user_wqs_num: maximum number of WQ entries supported by HW.
 * @min_hw_user_wqs_num: minimum number of WQ entries supported by HW.
 * @nic_qpc_cache_inv_timeout: timeout for NIC QPC cache invalidation.
 * @macro_cfg_size: the size of the NIC macro configuration space.
 * @base_status_event_idx: the base index of the status packet that is sent to
 *                         the FW.
 * @rwqe_size: receive WQE size.
 * @max_tnl_hdr_size: maximum size of the encapsulation header supported, in bytes.
 * @cq_min_entries: minimum number of CQ entries supported by ASIC.
 * @user_cq_min_entries: minimum number of supported user CQ entries.
 * @user_cq_max_entries: max number of supported user CQ entries.
 * @max_frm_len: maximum allowed frame length.
 * @raw_elem_size: size of element in raw buffers.
 * @status_packet_size: size of the NIC status packet we are going to send to F/W.
 * @cqe_size: Size of the Completion queue Entry.
 * @max_raw_mtu: maximum MTU size for raw packets.
 * @min_raw_mtu: minimum MTU size for raw packets.
 * @clk: NIC clock frequency in MHz.
 * @force_cq: all CQs should be enabled regardless of the ports link mask.
 * @max_num_of_lanes: maximum number of lanes supported by ASIC.
 * @max_num_of_ports: maximum number of ports supported by ASIC.
 * @num_of_macros: number of NIC macros supported by ASIC.
 * @max_cqs: maximum number of completion queues.
 * @max_ccqs: maximum number of congestion control completion queues.
 * @max_db_fifos: maximum number of DB fifos.
 * @advanced_hw_support: indicate whether advanced NIC H/W features supported by ASIC.
 * @max_wq_arr_type: maximum WQ array type number.
 */
struct hl_nic_properties {
	u64			phy_base_addr;
	u64			nic_drv_addr;
	u64			nic_drv_size;
	u64			nic_drv_base_addr;
	u64			nic_drv_end_addr;
	u64			sb_base_addr;
	u64			sb_base_size;
	u64			swq_base_addr;
	u64			swq_base_size;
	u64			txs_base_addr;
	u64			txs_base_size;
	u64			wq_base_addr;
	u64			wq_base_size;
	u64			tmr_base_addr;
	u64			tmr_base_size;
	u64			req_qpc_base_addr;
	u64			req_qpc_base_size;
	u64			res_qpc_base_addr;
	u64			res_qpc_base_size;
	u64			req_qpc_swl_base_addr;
	u64			req_qpc_swl_base_size;
	u32			max_hw_qps_num;
	u32			max_qps_num;
	u32			max_hw_user_wqs_num;
	u32			min_hw_user_wqs_num;
	u32			nic_qpc_cache_inv_timeout;
	u32			macro_cfg_size;
	u32			base_status_event_idx;
	u32			rwqe_size;
	u32			max_tnl_hdr_size;
	u32			cq_min_entries;
	u32			user_cq_min_entries;
	u32			user_cq_max_entries;
	u32			max_frm_len;
	u32			raw_elem_size;
	u32			status_packet_size;
	u32			cqe_size;
	u16			max_raw_mtu;
	u16			min_raw_mtu;
	u16			clk;
	u8			force_cq;
	u8			max_num_of_lanes;
	u8			max_num_of_ports;
	u8			num_of_macros;
	u8			max_cqs;
	u8			max_ccqs;
	u8			max_db_fifos;
	u8			advanced_hw_support;
	u8			max_wq_arr_type;
};

/**
 * struct hl_nic_macro - manage specific NIC macro that holds two NIC
 *                       engines.
 * @hdev: habanalabs device structure.
 * @asic_priv: ASIC specific data.
 * @rec_link_sts: link status bits as received from the MAC_REC_STS0 register.
 * @phy_macro_needs_reset: true if the PHY macro needs to be reset.
 * @idx: index of the NIC macro.
 */
struct hl_nic_macro {
	struct hl_device	*hdev;
	void			*asic_priv;
	u32			rec_link_sts;
	u8			phy_macro_needs_reset;
	u8			idx;
};

/**
 * struct hl_nic_qp_info - holds information of a QP to read via debugfs.
 * @port: the port the QP belongs to.
 * @qpn: QP number.
 * @req: true for requester QP, otherwise responder.
 * @full_print: print full QP information.
 * @force_read: force reading a QP in invalid/error state.
 * @exts_print: print QPC extensions like SAL or collective-descriptor.
 */
struct hl_nic_qp_info {
	u32			port;
	u32			qpn;
	u8			req;
	u8			full_print;
	u8			force_read;
	u8			exts_print;
};

/**
 * struct hl_nic_wqe_info - holds information of a WQE to read via debugfs.
 * @port: the port the WQE belongs to.
 * @qpn: QP number.
 * @wqe_idx: WQE index.
 * @tx: true for tx WQE, otherwise rx WQE.
 */
struct hl_nic_wqe_info {
	u32			port;
	u32			qpn;
	u32			wqe_idx;
	u8			tx;
};

/**
 * struct hl_nic_tx_taps - holds the NIC Tx taps values for a specific lane (tx_pre2, tx_pre1,
 *                         tx_main, tx_post1 and tx_post2).
 * @pam4_taps: taps for PAM4 mode.
 * @nrz_taps: taps for NRZ mode.
 */
struct hl_nic_tx_taps {
	s32			pam4_taps[NIC_PHY_TX_TAPS_NUM];
	s32			nrz_taps[NIC_PHY_TX_TAPS_NUM];
};

/**
 * enum db_fifo_state - Describes db fifo's current state. Starting it from 1 because by default
 *                      on reset when the state is 0, it shouldn't be confused as Allocated state.
 * @DB_FIFO_STATE_ALLOC: db fifo id has been allocated.
 * @DB_FIFO_STATE_SET: db fifo set is done for the corresponding id.
 */
enum db_fifo_state {
	DB_FIFO_STATE_ALLOC = 1,
	DB_FIFO_STATE_SET,
};

/**
 * struct hl_nic_db_fifo_idr_pdata - Holds private data of
 *                                   userspace doorbell idr
 * @asid: Associated user context
 * @port: NIC specific port
 * @ci_mmap_handle: Consumer index mmap handle
 * @umr_mmap_handle: UMR block mmap handle
 * @umr_db_offset: db fifo offset in UMR block
 * @state: db fifo's state
 * @db_pool_addr: offset of the allocated address in gen pool
 * @fifo_offset: actual fifo offset allocated for that id
 * @fifo_size: size of the fifo allocated
 * @base_sob_addr: Sync object base address for collective operations.
 * @num_sobs: Number of sync objects for collective operations.
 * @fifo_mode: mode of the fifo as received in the IOCTL
 */
struct hl_nic_db_fifo_idr_pdata {
	u32			asid;
	u32			port;
	u64			ci_mmap_handle;
	u64			umr_mmap_handle;
	u32			umr_db_offset;
	enum db_fifo_state	state;
	u32			db_pool_addr;
	u32			fifo_offset;
	u32			fifo_size;
	u32			base_sob_addr;
	u32			num_sobs;
	u8			fifo_mode;
	u8			dir_dup_ports_mask;
};

/**
 * struct hl_nic_encap_idr_pdata - Holds private data of
 *                                 userspace encapsulation idr
 * @port: NIC specific port
 * @id: Encapsulation ID
 * @src_ip: Source port IPv4 address.
 * @encap_type: L3/L4 encapsulation
 * @encap_type_data: IPv4 protocol or UDP port
 * @encap_header: Encapsulation header
 * @encap_header_size: Encapsulation header size
 */
struct hl_nic_encap_idr_pdata {
	u32			port;
	u32			id;
	u32			src_ip;
	enum hl_nic_encap_type	encap_type;
	u32			encap_type_data;
	void			*encap_header;
	u32			encap_header_size;
};

/**
 * enum hl_nic_user_cq_state - User CQ states.
 * @USER_CQ_STATE_ALLOC: ID allocated.
 * @USER_CQ_STATE_SET: HW configured. Resources allocated.
 * @USER_CQ_STATE_ALLOC_TO_UNSET: CQ moved to unset state from alloc state directly.
 * @USER_CQ_STATE_SET_TO_UNSET: CQ moved to unset from set state. HW config cleared.
 *                              Resources ready to be reclaimed.
 */
enum hl_nic_user_cq_state {
	USER_CQ_STATE_ALLOC = 1,
	USER_CQ_STATE_SET,
	USER_CQ_STATE_ALLOC_TO_UNSET,
	USER_CQ_STATE_SET_TO_UNSET,
};

/**
 * struct hl_nic_user_cq - user CQ data.
 * @nic_port: associated port.
 * @refcount: number of QPs that use this CQ.
 * @ctx: Associated user context.
 * @state: User CQ state.
 * @mem_handle: mmap handle of buffer memory.
 * @pi_handle: mmap handle of PI memory.
 * @id: CQ ID.
 */
struct hl_nic_user_cq {
	struct hl_nic_port		*nic_port;
	struct kref			refcount;
	struct hl_ctx			*ctx;
	enum hl_nic_user_cq_state	state;
	u64				mem_handle;
	u64				pi_handle;
	u32				id;
};

/**
 * enum hl_nic_coll_conn_type - Collective connection type.
 * HL_NIC_COLL_CONN_TYPE_NON_SCALE_OUT - non scale out connection.
 * HL_NIC_COLL_CONN_TYPE_SCALE_OUT - scale out connection.
 * HL_NIC_COLL_CONN_TYPE_MAX - number of values in enum.
 */
enum hl_nic_coll_conn_type {
	HL_NIC_COLL_CONN_TYPE_NON_SCALE_OUT,
	HL_NIC_COLL_CONN_TYPE_SCALE_OUT,
	HL_NIC_COLL_CONN_TYPE_MAX
};

/**
 * struct hl_wq_array_properties - WQ array properties.
 * @type_str: string of this WQ array type.
 * @coll_wq_type: type of this collective WQ array (scale-out or not).
 * @handle: handle for this WQ array.
 * @dva_base: reserved device VA for this WQ array.
 * @dva_size: size in bytes of device VA block of this WQ array.
 * @wq_size: size in bytes of each WQ in this WQ array.
 * @idx: index of this WQ array.
 * @enable: true if this WQ array is enabled, false otherwise.
 * @under_unset: true if this WQ array is waiting for unset (will be done when all QPs are
 *               destroyed), false otherwise.
 * @on_device_mem: true if this WQ array resides on HBM, false if on host.
 * @is_send: true if this WQ array should contain send WQEs, false if recv WQEs.
 * @is_coll: true if this WQ array is for collective connections.
 */
struct hl_wq_array_properties {
	char				*type_str;
	enum hl_nic_coll_conn_type	coll_wq_type;
	u64				handle;
	u64				dva_base;
	u64				dva_size;
	u64				wq_size;
	u32				idx;
	u8				enable;
	u8				under_unset;
	u8				on_device_mem;
	u8				is_send;
	u8				is_coll;
};

/**
 * struct hl_nic_port - manage specific NIC port common structure.
 * @hdev: habanalabs device structure.
 * @nic_specific: pointer to an ASIC specific NIC port structure.
 * @nic_macro: pointer to the manage structure of the containing NIC macro.
 * @wq: general purpose WQ for low/medium priority jobs like link status
 *     detection or NIC status fetch.
 * @qp_wq: QP work queue for handling the reset or destruction of QPs.
 * @cq_wq: CQ work queue for handling CQEs outside interrupt context.
 * @wq_arr_props: array per type of WQ array properties.
 * @ev_dqs: per ASID/App events dispatch queues managed by the driver.
 * @num_of_allocated_qps: the currently number of allocated qps for this nic.
 * @num_of_allocated_coll_qps: the currently number of allocated collective qps for this nic.
 * @num_of_allocated_scale_out_coll_qps: the currently number of allocated scale-out collective qps
 *                                       for this nic.
 * @link_status_work: work for checking NIC link status.
 * @nic_status_work: work for sending port status to the FW.
 * @control_lock: protects from a race between port open/close and other stuff that might run in
 *                parallel (such as event handling).
 * @cnt_lock: protects the counters from concurrent reading. Needed for SPMU and
 *            XPCS91 counters.
 * @qp_ids: IDR to hold all QP IDs.
 * @db_fifo_ids: Allocated doorbell fifo IDs.
 * @cq_ids: IDR to hold all CQ IDs.
 * @encap_ids: Allocated encapsulation IDs.
 * @pcs_fail_fifo: queue for keeping the PCS link failures time stamps in order
 *                 to reconfigure F/W if needed.
 * @last_fw_tuning_ts: time stamp of last F/W tuning.
 * @last_pcs_link_drop_ts: time stamp of last PCS link drop.
 * @fw_tuning_limit_ts: time stamp of FW tuning time limit.
 * @port: NIC specific port.
 * @speed: the bandwidth of the port in Mb/s.
 * @pflags: private flags bit mask.
 * @retry_cnt: counts the number of retries during link establishment.
 * @pcs_fail_cnt: counter of PCS link failures since last F/W configuration.
 * @pcs_local_fault_cnt: counter of PCS link local errors since last F/W
 *                       configuration. These errors can appear even when link
 *                       is up.
 * @pcs_remote_fault_cnt: counter of PCS link remote errors since last F/W
 *                        configuration. These errors can appear even when link
 *                        is up.
 * @pcs_remote_fault_seq_cnt: counter for number of PCS remote faults in a row,
 *                            or in other words the length of their sequence.
 * @pcs_remote_fault_reconfig_cnt: counter of PHY reconfigurations due to remote
 *                                 fault errors since last port open.
 * @pcs_link_restore_cnt: counter of PCS link momentary loss (glitch) since last
 *                        F/W configuration.
 * @correctable_errors_cnt: count the correctable FEC blocks.
 * @uncorrectable_errors_cnt: count the uncorrectable FEC blocks.
 * @data_rate: NIC data rate according to speed and number of lanes.
 * @num_of_wq_entries: number of entries configured in the port WQ.
 * @num_of_wqs: number of WQs configured for this port.
 * @qp_idx_offset: offset to the base QP index of this port for generic QPs.
 * @coll_qp_idx_offset: offset to the base QP index of this port for collective QPs.
 * @scale_out_coll_qp_idx_offset: offset to the base QP index of this port for scale-out
 *                                collective QPs.
 * @port_open: true if the port H/W is initialized, false otherwise.
 * @mac_loopback: true if port in MAC loopback mode, false otherwise.
 * @pfc_enable: true if this port supports Priority Flow Control, false
 *              otherwise.
 * @sw_initialized: true if the basic SW initialization was completed
 *                  successfully for this port, false otherwise.
 * @phy_fw_tuned: true if F/W is tuned, false otherwise.
 * @phy_func_mode_en: true if PHY is set to functional mode, false otherwise.
 * @pcs_link: true if the NIC has PCS link, false otherwise.
 * @prev_pcs_link: previous state of the PCS link.
 * @auto_neg_enable: true if this port supports Autonegotiation, false
 *                   otherwise.
 * @auto_neg_resolved: true if Autonegotiation was completed for this port,
 *                     false otherwise.
 * @auto_neg_skipped: true if Autonegotiation was skipped for this port, false
 *                    otherwise.
 * @power_up_mask: represents which MAC channels should be configured during PHY
 *                 power up.
 * @fw_tuning_mask: represents which MAC channels should be configured during
 *                  F/W tuning.
 * @auto_neg_mask: represents which MAC channels should be configured during
 *                 Autonegotiation.
 * @eth_enable: is Ethernet traffic enabled in addition to RDMA.
 * @ccq_enable: true if the CCQ was initialized successfully for this port, false otherwise.
 * @set_app_params: set_app_params operation was executed by the user. This is mandatory step for
 *                  Gaudi2 and above in order to initialize the NIC uAPI.
 */
struct hl_nic_port {
	struct hl_device		*hdev;
	void				*nic_specific;

	struct hl_nic_macro		*nic_macro;
	struct workqueue_struct		*wq;
	struct workqueue_struct		*qp_wq;
	struct workqueue_struct		*cq_wq;

	struct hl_wq_array_properties	wq_arr_props[HL_NIC_USER_WQ_TYPE_MAX];
	struct hl_nic_ev_dqs		ev_dqs;

	atomic_t			num_of_allocated_qps;
	atomic_t			num_of_allocated_coll_qps;
	atomic_t			num_of_allocated_scale_out_coll_qps;
	struct delayed_work		link_status_work;
	struct work_struct		nic_status_work;
	struct mutex			control_lock;
	struct mutex			cnt_lock;
	struct idr			qp_ids;
	struct idr			db_fifo_ids;
	struct idr			cq_ids;
	struct idr			encap_ids;
	struct kfifo			pcs_fail_fifo;
	ktime_t				last_fw_tuning_ts;
	ktime_t				last_pcs_link_drop_ts;
	ktime_t				fw_tuning_limit_ts;

	u64				ccq_handle;
	u64				ccq_pi_handle;
	u32				port;
	u32				speed;
	u32				pflags;
	u32				retry_cnt;
	u32				pcs_fail_cnt;
	u32				pcs_local_fault_cnt;
	u32				pcs_remote_fault_seq_cnt;
	u32				pcs_remote_fault_reconfig_cnt;
	u32				pcs_remote_fault_cnt;
	u32				pcs_link_restore_cnt;
	u32				correctable_errors_cnt;
	u32				uncorrectable_errors_cnt;
	u32				data_rate;
	u32				num_of_wq_entries;
	u32				num_of_wqs;
	u32				qp_idx_offset;
	u32				coll_qp_idx_offset;
	u32				scale_out_coll_qp_idx_offset;
	u32				swqe_size;
	u8				port_open;
	u8				mac_loopback;
	u8				pfc_enable;
	u8				sw_initialized;
	u8				phy_fw_tuned;
	u8				phy_func_mode_en;
	u8				pcs_link;
	u8				prev_pcs_link;
	u8				auto_neg_enable;
	u8				auto_neg_resolved;
	u8				auto_neg_skipped;
	u8				power_up_mask;
	u8				fw_tuning_mask;
	u8				auto_neg_mask;
	u8				eth_enable;
	u8				ccq_enable;
	u8				set_app_params;
};

/**
 * struct hl_nic_mem - describes a NIC memory allocation.
 * @hdev: pointer to device this memory belongs to.
 * @ctx: pointer to the memory owner's context.
 * @bus_address: Holds the memory's DMA address.
 * @kernel_address: Holds the memory's kernel virtual address.
 * @device_addr: Holds the HBM address.
 * @device_va: Device virtual address. Valid only for MMU mapped allocations.
 * @mem_id: specify host/device memory allocation.
 * @is_destroyed: Indicates whether or not the memory was destroyed.
 */
struct hl_nic_mem {
	struct hl_device	*hdev;
	struct hl_ctx		*ctx;
	dma_addr_t		bus_address;
	void			*kernel_address;
	u64			device_addr;
	u64			device_va;
	u32			mem_id;
	atomic_t		is_destroyed;
};

/**
 * struct hl_nic_ctx - habanalabs NIC context common structure.
 * @wq_arrays_pool: memory pool for WQ arrays on HBM.
 */
struct hl_nic_ctx {
	struct gen_pool		*wq_arrays_pool;
};

/**
 * struct hl_coll_properties - collective properties.
 * @coll_qp_ids: IDR to hold all collective QP IDs.
 * @num_of_coll_wq_arrays: number of allocated collective WQ arrays (each port will have two).
 * @num_of_coll_wqs: number of configured collective WQs.
 * @num_of_coll_wq_entries: number of entries configured per collective WQ.
 * @swq_type: the type of send work-queue array.
 * @rwq_type: the type of receive work-queue array.
 */
struct hl_coll_properties {
	struct idr		coll_qp_ids;
	atomic_t		num_of_coll_wq_arrays;
	u32			num_of_coll_wqs;
	u32			num_of_coll_wq_entries;
	u32			swq_type;
	u32			rwq_type;
};

/**
 * struct hl_nic - habanalabs NIC common structure.
 * @nic_ports: pointer to an array that holds all NIC ports manage common structures.
 * @nic_macros: pointer to an array that holds all NIC macros manage structures.
 * @phy_tx_taps: array that holds all PAM4 Tx taps of all NIC lanes.
 * @en_aux_dev: pointer to eth auxiliary device structure.
 * @ib_aux_dev: pointer to IB auxiliary device structure.
 * @qp_info: details of a QP to read via debugfs.
 * @wqe_info: details of a WQE to read via debugfs.
 * @coll_props: array of collective properties (to distinguish between scale-up/out ports).
 * @mac_lane_remap: MAC to PHY lane mapping.
 * @eth_ports_mask: Ethernet ports enable mask.
 * @mac_loopback: enable MAC loopback on specific NIC ports.
 * @auto_neg_mask: currently enabled Autoneg on specific NIC ports.
 * @ctrl_op_mask: mask of supported control operations.
 * @cq_arm_timeout: timeout for CQ interrupt (ticks for ASIC, usec for simulator).
 * @pcs_fail_time_frame: time frame is seconds to count PCS link failure.
 * @pcs_fail_threshold: PCS link failures threshold to reset link.
 * @card_location: the OAM number in the HLS (relevant for PMC card type).
 * @phy_port_to_dump: the port which its serdes params will be dump.
 * @phy_load_fw: true if the NIC PHY F/W should be loaded, false otherwise.
 *               The NIC PHY F/W should be loaded on ASIC only, in contrary to
 *               simulator/Palladium.
 * @phy_config_fw: true if the NIC PHY F/W should be configured, false
 *                 otherwise. The NIC PHY F/W should be configured on ASIC only,
 *                 in contrary to simulator/Palladium.
 * @use_fw_serdes_info: true if NIC should use serdes values from F/W, false if
 *                      NIC should use hard coded values.
 * @debugfs_reset: true if a device reset can be done from NIC debugfs.
 * @rand_status: randomize the NIC status counters (used for testing).
 * @mmu_bypass: true if the NIC use MMU bypass for its allocated data structures
 *              (false only for testing).
 * @wq_mmu_bypass: true if WQs has MMU-BP access, false otherwise.
 * @has_eq: true if event queue is supported.
 * @eth_loopback: enable hack in hl_en_handle_tx to test eth traffic.
 * @phy_regs_print: print all PHY registers reads/writes.
 * @phy_show_ber: show PHY BER statistics during power-up.
 * @is_eth_aux_dev_initialized: true if the eth auxiliary device is initialized.
 * @is_ib_aux_dev_initialized: true if the IB auxiliary device is initialized.
 * @skip_mac_reset: skip MAC reset.
 * @phy_set_nrz: Set the PHY to NRZ mode (25Gbps speed).
 * @skip_odd_ports_cfg_lock: do not lock the odd ports when acquiring the cfg lock for all ports.
 * @skip_mac_cnts: Used to skip MAC counters when running simulator.
 * @ib_support: InfiniBand support.
 * @skip_cq_arm_timeout: Used to skip CQ arm timeout settings when running on simulator.
 * @skip_wq_arrays_pool: Used to skip allocation and destruction of WQ arrays pool.
 */
struct hl_nic {
	struct hl_nic_port		*nic_ports;
	struct hl_nic_macro		*nic_macros;
	struct hl_nic_tx_taps		*phy_tx_taps;
	struct hl_aux_dev		en_aux_dev;
	struct hl_aux_dev		ib_aux_dev;
	struct hl_nic_qp_info		qp_info;
	struct hl_nic_wqe_info		wqe_info;
	struct hl_coll_properties	coll_props[HL_NIC_COLL_CONN_TYPE_MAX];
	u32				*mac_lane_remap;
	u64				eth_ports_mask;
	u64				mac_loopback;
	u64				auto_neg_mask;
	u64				ctrl_op_mask;
	u32				cq_arm_timeout;
	u32				pcs_fail_time_frame;
	u32				pcs_fail_threshold;
	u32				card_location;
	u32				phy_port_to_dump;
	u8				phy_load_fw;
	u8				phy_config_fw;
	u8				use_fw_serdes_info;
	u8				debugfs_reset;
	u8				rand_status;
	u8				mmu_bypass;
	u8				wq_mmu_bypass;
	u8				has_eq;
	u8				eth_loopback;
	u8				phy_regs_print;
	u8				phy_show_ber;
	u8				is_eth_aux_dev_initialized;
	u8				is_ib_aux_dev_initialized;
	u8				is_decap_disabled;
	u8				skip_mac_reset;
	u8				phy_set_nrz;
	u8				skip_odd_ports_cfg_lock;
	u8				skip_mac_cnts;
	u8				ib_support;
	u8				skip_cq_arm_timeout;
	u8				skip_wq_arrays_pool;
};

/**
 * enum mtu_type - Describes QP's MTU value source.
 * @MTU_INVALID: MTU is not configured yet.
 * @MTU_FROM_USER: MTU gotten from user call to set requester context.
 * @MTU_FROM_NETDEV: MTU gotten from netdev.
 * @MTU_DEFAULT: Use default MTU value.
 */
enum mtu_type {
	MTU_INVALID,
	MTU_FROM_USER,
	MTU_FROM_NETDEV,
	MTU_DEFAULT
};

/**
 * enum hl_nic_qp_state - The states the NIC QPs can be.
 *                        follows the spirit of "10.3.1 QUEUE PAIR AND EE CONTEXT STATES"
 *                        section in InfiniBand(TM) Architecture Release.
 * NIC_QP_STATE_RESET - QP is in reset state
 * NIC_QP_STATE_INIT - Initialized state
 *                     all QP resources are allocated
 *                     QP can post Recv WRs.
 * NIC_QP_STATE_RTR - Ready to receive state.
 *                    QP: can post & process Rcv WRs & send ACKs,
 * NIC_QP_STATE_RTS - Ready to send state.
 *                    QP: can post and process recv & send WRs
 * NIC_QP_STATE_SQD - SQ is in drained state.
 *                    (a sub-state of the QP draining process).
 * NIC_QP_STATE_QPD - QP is in drained state.
 *                    both RQ and SQ are drained.
 * NIC_QP_STATE_SQERR - Send queue error state.
 *                     QP: Can Post & process Receive WRs,
 *                     Send WRs are completed in error
 * NIC_QP_STATE_ERR - Error state.
 * NIC_QP_NUM_STATE - the number of states the QP can be in.
 */
enum hl_nic_qp_state {
	NIC_QP_STATE_RESET = 0,
	NIC_QP_STATE_INIT,
	NIC_QP_STATE_RTR,
	NIC_QP_STATE_RTS,
	NIC_QP_STATE_SQD,
	NIC_QP_STATE_QPD,
	NIC_QP_STATE_SQERR,
	NIC_QP_STATE_ERR,
	NIC_QP_NUM_STATE,		/* must be last */
};

/**
 * enum hl_nic_qp_state_op - The Valid state transition operations on QP.
 * NIC_QP_OP_INVAL - invalid OP, indicates invalid state transition path, not to be used.
 * NIC_QP_OP_RST_2INIT - Move the QP from the Reset state to the Init state.
 * NIC_QP_QP_INIT_2RTR - Move the QP from the Init state to the Ready-to-receive state.
 * NIC_QP_OP_RTR_2RTR - Reconfig Responder.
 * NIC_QP_OP_RTR_2QPD - drain the Responder.
 * NIC_QP_OP_RTR_2RTS - Move the QP from RTR state to the Ready-to-send state.
 * NIC_QP_OP_RTR_2SQD - Move the QP from RTR state to the drained state.
 * NIC_QP_OP_RTS_2RTS - Reconfigure the requester.
 * NIC_QP_OP_RTS_2SQERR - move the QP from RTS to the SQER state due to HW errors.
 * NIC_QP_OP_RTS_2SQD - Drain the SQ and move to the SQ-Drained state.
 * NIC_QP_OP_RTS_2QPD - drain the QP (requester and responder).
 * NIC_QP_OP_SQD_2SQD - Re-drain the SQ.
 * NIC_QP_OP_SQD_2QPD - Drain the QP (Responder too).
 * NIC_QP_OP_SQD_2RTS - Enable the requester after draining is done.
 * NIC_QP_OP_SQD_2SQ_ERR - move the QP to SQ_Err due to HW error.
 * NIC_QP_OP_QPD_2RTR - restart the Responder.
 * NIC_QP_OP_QPD_2QPD - drain the QP (again).
 * NIC_QP_OP_SQ_ERR_2SQD - recover from SQ err and return to work
 * NIC_QP_OP_2ERR - Place the QP in error state (due to HW errors),
 *                  An error can be forced from any state, except Reset.
 * NIC_QP_OP_2RESET - Move to reset state.
 *                    it is possible to transition from any state to the Reset
 *                    state.
 * NIC_QP_OP_NOP - Do nothing.
 */
enum hl_nic_qp_state_op {
	NIC_QP_OP_INVAL = 0,	/* Invalid operation, must be 0 */
	NIC_QP_OP_RST_2INIT,
	NIC_QP_OP_INIT_2RTR,
	NIC_QP_OP_RTR_2RTR,
	NIC_QP_OP_RTR_2QPD,
	NIC_QP_OP_RTR_2RTS,
	NIC_QP_OP_RTR_2SQD,
	NIC_QP_OP_RTS_2RTS,
	NIC_QP_OP_RTS_2SQERR,
	NIC_QP_OP_RTS_2SQD,
	NIC_QP_OP_RTS_2QPD,
	NIC_QP_OP_SQD_2SQD,
	NIC_QP_OP_SQD_2QPD,
	NIC_QP_OP_SQD_2RTS,
	NIC_QP_OP_SQD_2SQ_ERR,
	NIC_QP_OP_QPD_2RTR,
	NIC_QP_OP_QPD_2QPD,
	NIC_QP_OP_SQ_ERR_2SQD,
	NIC_QP_OP_2ERR,
	NIC_QP_OP_2RESET,
	NIC_QP_OP_NOP,
	/* TODO: w/a for SW-62591, Requester initialized before Responder,
	 * remove when fixed
	 */
	NIC_QP_OP_INIT_2RTS,
	NIC_QP_OP_RTS_2RTR,
};

/**
 * struct hl_qp - Describes a NIC Queue Pair.
 * @nic_port: Pointer to the NIC port this QP belongs to.
 * @async_work: async work performed on QP, when destroying the QP.
 * @ctx: Associated user context.
 * @req_user_cq: CQ ID used by the requester context.
 * @res_user_cq: CQ ID used by the responder context.
 * @curr_state: The current state of the QP.
 * @mtu_type: Source of MTU value from user, from netdev or default.
 * @coll_conn_type: type of collective connection (scale-out or not).
 * @swq_handle: Send WQ mmap handle.
 * @rwq_handle: Receive WQ mmap handle.
 * @port: The port number this QP belongs to.
 * @qp_id: The QP number within its port.
 * @local_key: Key for local access.
 * @remote_key: Key for remote access.
 * @asid: address space id that is configured inside the device. This is the first index into the
 *        device's MMU page tables.
 * @mtu: Current MTU value.
 * @is_req: is requester context was set for the QP.
 * @is_res: is responder context was set for the QP.
 * @is_coll: is collective QP.
 */
struct hl_qp {
	struct hl_nic_port		*nic_port;
	struct work_struct		async_work;
	struct hl_ctx			*ctx;
	struct hl_nic_user_cq		*req_user_cq;
	struct hl_nic_user_cq		*res_user_cq;
	enum hl_nic_qp_state		curr_state;
	enum mtu_type			mtu_type;
	enum hl_nic_coll_conn_type	coll_conn_type;
	u64				swq_handle;
	u64				rwq_handle;
	u32				port;
	u32				qp_id;
	u32				local_key;
	u32				remote_key;
	u32				asid;
	u32				mtu;
	u8				is_req;
	u8				is_res;
	u8				is_coll;
};

/**
 * struct hl_coll_qp - Describes a NIC collective Queue Pair.
 * @hdev: habanalabs device structure.
 * @qps_array: per port array of QPs which have the same id like this collective QP.
 * @num_of_initialized_qps: number of initialized QPs which have the same id like this
 *                          collective QP.
 * @coll_conn_type: type of collective connection (scale-out or not).
 * @id: id of this collective QP.
 */
struct hl_coll_qp {
	struct hl_device		*hdev;
	struct hl_qp			**qps_array;
	atomic_t			num_of_initialized_qps;
	enum hl_nic_coll_conn_type	coll_conn_type;
	u32				id;
};

/**
 * enum qp_conn_state - State of retransmission flow.
 * @QP_CONN_STATE_OPEN: connection is open.
 * @QP_CONN_STATE_CLOSED: connection is closed.
 * @QP_CONN_STATE_RESYNC: Connection is re-synchronizing.
 * @QP_CONN_STATE_ERROR: Connection is in error state.
 */
enum qp_conn_state {
	QP_CONN_STATE_OPEN = 0,
	QP_CONN_STATE_CLOSED = 1,
	QP_CONN_STATE_RESYNC = 2,
	QP_CONN_STATE_ERROR = 3,
};

/**
 * struct hl_nic_qpc_attr - QPC attributes as read from the HW.
 * @valid: QPC is valid.
 * @in_work: qp was scheduled to work.
 * @error: QPC is in error state (relevant in Req QPC only).
 * @conn_state - state of retransmission flow (relevant in Res QPC only).
 */
struct hl_nic_qpc_attr {
	u8 valid;
	u8 in_work;
	u8 error;
	enum qp_conn_state conn_state;
};

/**
 * struct hl_nic_qp_reset_mode - QP reset method.
 * @NIC_QP_RESET_MODE_GRACEFUL: Graceful reset, reset the QP components in an
 *                              orderly manner and wait on each component to settle
 *                              before moving to the next step..
 * @NIC_QP_RESET_MODE_FAST: Fast reset, reset the QP components in an orderly
 *                          manner, without waiting for the components to settle .
 * @NIC_QP_RESET_MODE_HARD: Clear the QP contexts immediately.
 */
enum hl_nic_qp_reset_mode {
	NIC_QP_RESET_MODE_GRACEFUL = 0,
	NIC_QP_RESET_MODE_FAST = 1,
	NIC_QP_RESET_MODE_HARD = 2,
};

/**
 * struct hl_nic_qpc_reset_attr - attributes used when setting QP state to reset.
 * @reset_mode: the type/mode of reset to be used.
 */
struct hl_nic_qpc_reset_attr {
	enum hl_nic_qp_reset_mode reset_mode;
};

/**
 * struct hl_nic_qpc_drain_attr - QPC attributes used for draining operation.
 * @wait_for_idle: wait for QPC to become idle.
 */
struct hl_nic_qpc_drain_attr {
	bool wait_for_idle;
};

/**
 * enum hl_nic_drv_mem_id - Memory allocation methods:
 * HL_NIC_DRV_MEM_INVALID           - N/A option.
 * HL_NIC_DRV_MEM_HOST_DMA_COHERENT - Host DMA coherent memory.
 * HL_NIC_DRV_MEM_HOST_VIRTUAL      - Host virtual memory.
 * HL_NIC_DRV_MEM_DEVICE            - Device HBM memory.
 * HL_NIC_DRV_MEM_HOST_MAP_ONLY     - Host mapping only.
 */
enum hl_nic_drv_mem_id {
	HL_NIC_DRV_MEM_INVALID,
	HL_NIC_DRV_MEM_HOST_DMA_COHERENT,
	HL_NIC_DRV_MEM_HOST_VIRTUAL,
	HL_NIC_DRV_MEM_DEVICE,
	HL_NIC_DRV_MEM_HOST_MAP_ONLY,
};

/**
 * struct hl_nic_mem_data - NIC memory allocation metadata
 * @in: mem_id specific allocation parameters.
 * @in.device_mem_data: mem_id HL_NIC_DRV_MEM_DEVICE specific allocation parameters.
 * @in.device_mem_data.port: Associated NIC port.
 * @in.device_mem_data.type: enum hl_nic_mem_type to be allocated.
 * @in.host_map_data: mem_id HL_NIC_DRV_MEM_HOST_MAP_ONLY specific mapping parameters.
 * @in.host_map_data.bus_address: Memory DMA address.
 * @in.host_map_data.kernel_address: Memory kernel virtual address.
 * @mem_id: Allocation type, enum hl_nic_mem_id.
 * @size: Allocation size.
 * @device_va: Device virtual address. Valid only for MMU mapped allocation.
 * @handle: Returned mmap handle.
 * @addr: Returned allocation address.
 */
struct hl_nic_mem_data {
	union {
		/* HL_NIC_DRV_MEM_DEVICE */
		struct {
			u32 port;
			enum hl_nic_mem_type type;
		} device_mem_data;

		/* HL_NIC_DRV_MEM_HOST_MAP_ONLY */
		struct {
			dma_addr_t bus_address;
			void *kernel_address;
		} host_map_data;
	} in;

	/* Common in params */
	enum hl_nic_drv_mem_id mem_id;
	u64 size;
	u64 device_va;

	/* Common out params */
	u64 handle;
	u64 addr;
};

/**
 * struct hl_nic_user_cq_set_in_params - user CQ configuration in params.
 * @addr: CQ buffer address, relevant for Gaudi only.
 * @port: NIC port ID.
 * @num_of_cqes: Number of CQ entries in the buffer.
 * @id: CQ ID, relevant for Gaudi2 or higher.
 */
struct hl_nic_user_cq_set_in_params {
	u64 addr;
	u32 port;
	u32 num_of_cqes;
	u32 id;
};

/**
 * struct hl_nic_user_cq_set_in_params - user CQ configuration out params, relevant for Gaudi2 or
 *                                       higher.
 * @mem_handle: Handle of CQ memory buffer.
 * @pi_handle: Handle of CQ producer-inder memory buffer.
 * @regs_handle: Handle of CQ Registers base-address.
 * @regs_offset: CQ Registers sub-offset.
 */
struct hl_nic_user_cq_set_out_params {
	u64 mem_handle;
	u64 pi_handle;
	u64 regs_handle;
	u32 regs_offset;
};

/**
 * struct hl_nic_user_cq_set_in_params - user CQ unconfiguration in params.
 * @addr: CQ buffer address, relevant for Gaudi only.
 * @port: NIC port ID.
 * @num_of_cqes: Number of CQ entries in the buffer.
 * @id: CQ ID, relevant for Gaudi2 or higher.
 */
struct hl_nic_user_cq_unset_in_params {
	u32 port;
	u32 id;
};

/**
 * struct hl_nic_port_funcs - ASIC specific NIC functions that are can be called from common code
 * for a specific NIC port.
 * @port_hw_init: initialize the port HW.
 * @port_hw_fini: cleanup the port HW.
 * @phy_port_init: port PHY init.
 * @phy_port_start_stop: port PHY start/stop.
 * @phy_port_power_up: port PHY power-up.
 * @phy_port_reconfig: port PHY reconfigure.
 * @phy_port_fini: port PHY cleanup.
 * @phy_link_status_work: link status handler.
 * @update_mtu: updates MTU inside a requestor QP context.
 * @user_wq_arr_unset: unset user WQ array (check whether user_wq_lock should be taken).
 * @get_cq_id_range: get user CQ ID range.
 * @user_cq_set: set user CQ.
 * @user_cq_unset: unset user CQ.
 * @user_cq_destroy: destroy user CQ.
 * @user_cq_update_ci: update the user CQ consumer index.
 * @get_cnts_num: get the number of available counters.
 * @get_cnts_names: get the names of the available counters.
 * @get_cnts_values: get the values of the available counters.
 * @port_sw_init: initialize per port software components.
 * @port_sw_fini: finalize per port software components.
 * @spmu_get_stats_info: get NIC SPMU statistics information.
 * @spmu_config: config the NIC SPMU.
 * @spmu_sample: read the SPMU NIC counters.
 * @register_qp: register a new qp-id with the NIC.
 * @unregister_qp: unregister a qp.
 * @get_qp_id_range: Get unsecure QP ID range.
 * @eq_poll: poll the EQ for asid/app-specific events.
 * @get_db_fifo_id_range: Get unsecure userspace doorbell ID range.
 * @db_fifo_set: Config unsecure userspace doorbell fifo.
 * @db_fifo_unset: Destroy unsecure userspace doorbell fifo.
 * @get_db_fifo_umr: Get UMR block address and db fifo offset.
 * @get_db_fifo_modes_mask: Get the supported db fifo modes
 * @db_fifo_allocate: Allocate fifo for the specific mode
 * @db_fifo_free: Free the fifo for the specific id
 * @set_pfc: enable/disable PFC.
 * @get_encap_id_range: Get user encapsulation ID range
 * @encap_set: Start encapsulation
 * @encap_unset: Stop encapsulation
 * @set_ip_addr_encap: Setup IP address encapsulation.
 * @qpc_write: write a QP context to the HW.
 * @qpc_invalidate: invalidate a QP context.
 * @qpc_query: read a QP context.
 * @qpc_clear: clear a QP context.
 * @user_ccq_set: set user congestion completion queue.
 * @user_ccq_unset: unset user congestion completion queue.
 * @reset_mac_stats: reset MAC statistics.
 * @print_fec_stats: print FEC statistics.
 * @disable_wqe_index_checker: Disable WQE index checker for both Rx and Tx.
 * @fill_nic_status: fill information in NIC status packet for F/W.
 * @cfg_lock: acquire the port configuration lock.
 * @cfg_unlock: release the port configuration lock.
 * @cfg_is_locked: check if the port configuration lock is locked.
 * @override_phy_readiness: indicate if port's phy is ready or not, used for pldm and simulator.
 * @qp_pre_destroy: prepare for a QP destroy. Called under the cfg lock.
 * @qp_post_destroy: cleanup after a QP destroy. Called under the cfg lock.
 * @get_coll_qps_offset: Get collective QPs offset.
 */
struct hl_nic_port_funcs {
	int (*port_hw_init)(struct hl_nic_port *nic_port);
	void (*port_hw_fini)(struct hl_nic_port *nic_port);
	int (*phy_port_init)(struct hl_nic_port *nic_port);
	void (*phy_port_start_stop)(struct hl_nic_port *nic_port, bool is_start);
	int (*phy_port_power_up)(struct hl_nic_port *nic_port);
	void (*phy_port_reconfig)(struct hl_nic_port *nic_port);
	void (*phy_port_fini)(struct hl_nic_port *nic_port);
	void (*phy_link_status_work)(struct work_struct *work);
	int (*update_qp_mtu)(struct hl_nic_port *nic_port, struct hl_qp *qp, u32 mtu);
	int (*user_wq_arr_unset)(struct hl_nic_port *nic_port, u32 type, struct hl_ctx *ctx);
	void (*get_cq_id_range)(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id);
	int (*user_cq_set)(struct hl_nic_user_cq *user_cq, struct hl_nic_user_cq_set_in_params *in,
				struct hl_nic_user_cq_set_out_params *out);
	int (*user_cq_unset)(struct hl_nic_user_cq *user_cq);
	void (*user_cq_destroy)(struct hl_nic_user_cq *user_cq);
	void (*user_cq_update_ci)(struct hl_nic_port *nic_port, u32 ci);
	int (*get_cnts_num)(struct hl_nic_port *nic_port);
	void (*get_cnts_names)(struct hl_nic_port *nic_port, u8 *data);
	void (*get_cnts_values)(struct hl_nic_port *nic_port, u64 *data);
	int (*port_sw_init)(struct hl_nic_port *nic_port);
	void (*port_sw_fini)(struct hl_nic_port *nic_port);
	void (*spmu_get_stats_info)(struct hl_nic_port *nic_port, struct hl_en_stat **stats,
					u32 *n_stats);
	int (*spmu_config)(struct hl_nic_port *nic_port, u32 num_event_types, u32 event_types[],
				bool enable);
	int (*spmu_sample)(struct hl_nic_port *nic_port, u32 num_out_data, u64 out_data[]);
	int (*register_qp)(struct hl_nic_port *nic_port, u32 qp_id, u32 asid);
	void (*unregister_qp)(struct hl_nic_port *nic_port, u32 qp_id);
	void (*get_qp_id_range)(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id);
	int (*eq_poll)(struct hl_nic_port *nic_port, u32 asid,
			struct hl_nic_eq_poll_out *event);
	struct hl_nic_ev_dq * (*eq_dispatcher_select_dq)(struct hl_nic_port *nic_port,
			const struct hl_nic_eqe *eqe);
	void (*get_db_fifo_id_range)(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id);
	int (*db_fifo_set)(struct hl_nic_port *nic_port, struct hl_ctx *ctx, u32 id,
				u64 ci_device_handle, struct hl_nic_db_fifo_idr_pdata *idr_pdata);
	void (*db_fifo_unset)(struct hl_nic_port *nic_port, struct hl_ctx *ctx, u32 id,
				struct hl_nic_db_fifo_idr_pdata *idr_pdata);
	void (*get_db_fifo_umr)(struct hl_nic_port *nic_port, u32 id,
				u64 *umr_block_addr, u32 *umr_db_offset);
	void (*get_db_fifo_modes_mask)(struct hl_nic_port *nic_port, u32 *mode_mask);
	int (*db_fifo_allocate)(struct hl_nic_port *nic_port,
				struct hl_nic_db_fifo_idr_pdata *idr_pdata);
	void (*db_fifo_free)(struct hl_nic_port *nic_port, u32 db_pool_addr, u32 fifo_size);
	int (*set_pfc)(struct hl_nic_port *nic_port);
	void (*get_encap_id_range)(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id);
	int (*encap_set)(struct hl_nic_port *nic_port, u32 encap_id,
				struct hl_nic_encap_idr_pdata *idr_pdata);
	void (*encap_unset)(struct hl_nic_port *nic_port, u32 encap_id,
				struct hl_nic_encap_idr_pdata *idr_pdata);
	void (*set_ip_addr_encap)(struct hl_nic_port *nic_port, u32 *encap_id, u32 src_ip);
	int (*qpc_write)(struct hl_nic_port *nic_port, void *qpc, struct qpc_mask *qpc_mask,
				u32 qpn, bool is_req);
	int (*qpc_invalidate)(struct hl_nic_port *nic_port, u32 qpn, bool is_req);
	int (*qpc_query)(struct hl_nic_port *nic_port, u32 qpn, bool is_req,
				struct hl_nic_qpc_attr *attr);
	int (*qpc_clear)(struct hl_nic_port *nic_port, u32 qpn, bool is_req);
	void (*user_ccq_set)(struct hl_nic_port *nic_port, u64 ccq_device_addr, u64 pi_device_addr,
				u32 num_of_entries, u32 *ccqn);
	void (*user_ccq_unset)(struct hl_nic_port *nic_port, u32 *ccqn);
	void (*reset_mac_stats)(struct hl_nic_port *nic_port);
	void (*print_fec_stats)(struct hl_nic_port *nic_port);
	int (*disable_wqe_index_checker)(struct hl_nic_port *nic_port);
	void (*fill_nic_status)(struct hl_nic_port *nic_port, struct cpucp_nic_status *nic_status);
	void (*cfg_lock)(struct hl_nic_port *nic_port);
	void (*cfg_unlock)(struct hl_nic_port *nic_port);
	bool (*cfg_is_locked)(struct hl_nic_port *nic_port);
	void (*override_phy_readiness)(struct hl_nic_port *nic_port, bool set_ready);
	void (*qp_pre_destroy)(struct hl_qp *qp);
	void (*qp_post_destroy)(struct hl_qp *qp);
	u32 (*get_coll_qps_offset)(struct hl_nic_port *nic_port);
};

/**
 * struct hl_nic_funcs - ASIC specific NIC functions that are can be called from common code.
 * @pre_core_init: NIC initializations to be done only once on device probe.
 * @core_init: core infrastructure init.
 * @core_fini: core infrastructure cleanup.
 * @get_hw_cap: check rather HW capability bitmap is set for NIC.
 * @set_hw_cap: set HW capability (on/off).
 * @get_default_port_speed: get the default port BW in MB/s.
 * @phy_reset_macro: macro PHY reset.
 * @phy_get_crc: get PHY CRC.
 * @set_req_qp_ctx: set up a requester QP context.
 * @set_res_qp_ctx: set up a responder QP context.
 * @user_wq_arr_set: set user WQ array (check whether user_wq_lock should be taken).
 * @user_set_app_params: update user params to be later retrieved by user_get_app_params.
 * @user_get_app_params: retrieve user params, previously configured or saved by
 *                       user_set_app_params.
 * @mac_addr_convert: convert given address to specific MAC address.
 * @get_phy_fw_name: returns the NIC PHY FW file name.
 * @macro_sw_init: initialize per macro software components.
 * @macro_sw_fini: finalize per macro software components.
 * @kernel_ctx_init: initialize kernel context.
 * @kernel_ctx_fini: de-initialize kernel context.
 * @ctx_init: initialize user context.
 * @ctx_fini: de-initialize user context.
 * @qp_read: read a QP content.
 * @wqe_read: read a WQE content.
 * @phy_fw_load_all: load PHY fw on all the ports.
 * @set_en_core_data: core data to be used by the Ethernet driver.
 * @request_irqs: Add handlers to NICs interrupt lines.
 * @free_irqs: Free NIC interrupt allocated with request_irqs.
 * @synchronize_irqs: Wait for NICs pending IRQ handlers (on other CPUs).
 * @write_coll_lag_size: Write the collective operation lag size on to register
 * @read_coll_lag_size: Read the collective operation lag size
 * @get_coll_qp_id_range: Get collective QP id range.
 * @is_coll_conn_id: true if the provided conn_id is collective, false otherwise.
 * @phy_dump_serdes_params: dump the serdes parameters.
 * @get_max_msg_sz: get maximum message size.
 * @app_params_clear: clear app params.
 * @port_funcs: functions called from common code on a specific NIC port.
 */
struct hl_nic_funcs {
	int (*pre_core_init)(struct hl_device *hdev);
	int (*core_init)(struct hl_device *hdev);
	void (*core_fini)(struct hl_device *hdev);
	bool (*get_hw_cap)(struct hl_device *hdev);
	void (*set_hw_cap)(struct hl_device *hdev, bool enable);
	u32 (*get_default_port_speed)(struct hl_device *hdev);
	int (*phy_reset_macro)(struct hl_nic_macro *nic_macro);
	u16 (*phy_get_crc)(struct hl_device *hdev);
	int (*set_req_qp_ctx)(struct hl_device *hdev, struct hl_nic_req_conn_ctx_in *in,
				struct hl_qp *qp);
	int (*set_res_qp_ctx)(struct hl_device *hdev, struct hl_nic_res_conn_ctx_in *in,
				struct hl_qp *qp);
	int (*user_wq_arr_set)(struct hl_device *hdev, struct hl_nic_user_wq_arr_set_in *in,
				struct hl_nic_user_wq_arr_set_out *out, struct hl_ctx *ctx);
	int (*user_set_app_params)(struct hl_device *hdev,
					struct hl_nic_set_user_app_params_in *in,
					bool *modify_wqe_checkers, struct hl_ctx *ctx);
	void (*user_get_app_params)(struct hl_device *hdev,
					struct hl_nic_get_user_app_params_in *in,
					struct hl_nic_get_user_app_params_out *out);
	u32 (*mac_addr_convert)(int mac, char *cfg_type, u32 addr);
	const char* (*get_phy_fw_name)(void);
	int (*sw_init)(struct hl_device *hdev);
	void (*sw_fini)(struct hl_device *hdev);
	int (*macro_sw_init)(struct hl_nic_macro *nic_macro);
	void (*macro_sw_fini)(struct hl_nic_macro *nic_macro);
	int (*kernel_ctx_init)(struct hl_ctx *ctx);
	void (*kernel_ctx_fini)(struct hl_ctx *ctx);
	int (*ctx_init)(struct hl_ctx *ctx);
	void (*ctx_fini)(struct hl_ctx *ctx);
	int (*qp_read)(struct hl_device *hdev, char *buf, size_t bsize);
	int (*wqe_read)(struct hl_device *hdev, char *buf, size_t bsize);
	int (*phy_fw_load_all)(struct hl_device *hdev);
	void (*set_en_core_data)(struct hl_device *hdev);
	int (*request_irqs)(struct hl_device *hdev);
	void (*free_irqs)(struct hl_device *hdev);
	void (*synchronize_irqs)(struct hl_device *hdev);
	int (*write_coll_lag_size)(struct hl_device *hdev, u32 coll_lag_size);
	int (*read_coll_lag_size)(struct hl_device *hdev, u32 *coll_lag_size);
	void (*get_coll_qp_id_range)(struct hl_device *hdev, bool is_scale_out, u32 *min_id,
					u32 *max_id);
	bool (*is_coll_conn_id)(struct hl_device *hdev, u32 conn_id);
	void (*phy_dump_serdes_params)(struct hl_device *hdev, char *buf, size_t size);
	u32 (*get_max_msg_sz)(struct hl_device *hdev);
	char *(*qp_syndrome_to_str)(u32 syndrome);
	void (*app_params_clear)(struct hl_device *hdev);
	struct hl_nic_port_funcs *port_funcs;
};

static inline void hl_nic_strtolower(char *str)
{
	while (*str) {
		*str = tolower(*str);
		str++;
	}
}

int hl_nic_init(struct hl_device *hdev);
void hl_nic_fini(struct hl_device *hdev);
void hl_nic_stop(struct hl_device *hdev);
int hl_nic_reopen(struct hl_device *hdev);
int hl_nic_core_init(struct hl_device *hdev);
int hl_nic_internal_ports_init(struct hl_device *hdev);
int hl_nic_internal_port_init_locked(struct hl_nic_port *nic_port);
void hl_nic_internal_port_fini_locked(struct hl_nic_port *nic_port);
int hl_nic_phy_init(struct hl_nic_port *nic_port);
void hl_nic_phy_fini(struct hl_nic_port *nic_port);
void hl_nic_phy_port_reconfig(struct hl_nic_port *nic_port);
int hl_nic_phy_has_fw(struct hl_device *hdev);
void hl_nic_phy_set_port_status(struct hl_nic_port *nic_port, bool up);
int hl_nic_read_spmu_counters(struct hl_nic_port *nic_port, u64 out_data[], u32 *num_out_data);
void hl_nic_send_status(struct hl_device *hdev, int port);
bool hl_nic_disabled_or_in_reset(struct hl_nic_port *nic_port);
void hl_nic_hard_reset_prepare(struct hl_device *hdev);
int hl_nic_control(struct hl_device *hdev, u32 op, void *input,	void *output, struct hl_ctx *ctx);
int hl_nic_ioctl_port_check(struct hl_device *hdev, u32 port, u32 flags);
void hl_nic_cfg_lock_all(struct hl_device *hdev);
void hl_nic_cfg_unlock_all(struct hl_device *hdev);
struct hl_qp *hl_nic_get_qp_from_coll_conn_id(struct hl_nic_port *nic_port, u32 conn_id);
bool hl_nic_is_scale_out_coll_type(u32 coll_conn_type);
int hl_nic_qp_modify(struct hl_nic_port *nic_port, struct hl_qp *qp,
			enum hl_nic_qp_state new_state, void *params);
int hl_nic_sw_init(struct hl_device *hdev);
void hl_nic_sw_fini(struct hl_device *hdev);
int hl_nic_get_link_state(struct hl_device *hdev, u32 port, struct hl_info_habana_link_state *out);
int hl_nic_get_statistics(struct hl_device *hdev, u32 port,
				struct hl_info_habana_link_counters *out);
int hl_nic_ctx_init(struct hl_ctx *ctx);
void hl_nic_ctx_fini(struct hl_ctx *ctx);
void hl_nic_debugfs_init(struct hl_device *hdev);
u32 hl_nic_get_max_qp_id(struct hl_nic_port *nic_port);
void hl_nic_qps_stop(struct hl_nic_port *nic_port);
int hl_nic_mem_alloc(struct hl_ctx *ctx, struct hl_nic_mem_data *mem_data);
int hl_nic_mem_destroy(struct hl_ctx *ctx, u64 handle);
void hl_nic_user_db_fifo_ctx_destroy(struct hl_ctx *ctx);
int hl_nic_request_irqs(struct hl_device *hdev);
void hl_nic_free_irqs(struct hl_device *hdev);
void hl_nic_synchronize_irqs(struct hl_device *hdev);
bool hl_nic_is_port_open(struct hl_nic_port *nic_port);
u32 hl_nic_get_pflags(struct hl_nic_port *nic_port);
u8 hl_nic_get_num_of_digits(u64 num);
void hl_nic_spmu_init(struct hl_device *hdev, int port, bool full);
void hl_nic_reset_ethtool_cnt(struct hl_device *hdev);

void hl_nic_dq_reset(struct hl_nic_ev_dq *dq);
struct hl_nic_ev_dq *hl_nic_asid_to_dq(struct hl_nic_ev_dqs *ev_dqs, u32 asid);
struct hl_nic_ev_dq *hl_nic_dbn_to_dq(struct hl_nic_ev_dqs *ev_dqs, u32 dbn,
					struct hl_device *hdev);
struct hl_nic_ev_dq *hl_nic_qpn_to_dq(struct hl_nic_ev_dqs *ev_dqs, u32 qpn);
struct hl_nic_dq_qp_info *hl_nic_get_qp_info(struct hl_nic_ev_dqs *ev_dqs, u32 qpn);
struct hl_nic_ev_dq *hl_nic_cqn_to_dq(struct hl_nic_ev_dqs *ev_dqs,
					u32 cqn, struct hl_device *hdev);
struct hl_nic_ev_dq *hl_nic_ccqn_to_dq(struct hl_nic_ev_dqs *ev_dqs, u32 ccqn,
					struct hl_device *hdev);

bool hl_nic_eq_dispatcher_is_empty(struct hl_nic_ev_dq *dq);
bool hl_nic_eq_dispatcher_is_full(struct hl_nic_ev_dq *dq);
void hl_nic_eq_dispatcher_init(struct hl_nic_port *nic_port);
void hl_nic_eq_dispatcher_fini(struct hl_nic_port *nic_port);
void hl_nic_eq_dispatcher_reset(struct hl_nic_port *nic_port);
int hl_nic_eq_dispatcher_associate_dq(struct hl_nic_port *nic_port, u32 asid);
int hl_nic_eq_dispatcher_dissociate_dq(struct hl_nic_port *nic_port, u32 asid);
int hl_nic_eq_dispatcher_register_qp(struct hl_nic_port *nic_port, u32 asid, u32 qp_id);
int hl_nic_eq_dispatcher_unregister_qp(struct hl_nic_port *nic_port, u32 qp_id);
int hl_nic_eq_dispatcher_register_cq(struct hl_nic_port *nic_port, u32 asid, u32 cqn);
int hl_nic_eq_dispatcher_unregister_cq(struct hl_nic_port *nic_port, u32 cqn);
int hl_nic_eq_dispatcher_register_db(struct hl_nic_port *nic_port, u32 asid, u32 dbn);
int hl_nic_eq_dispatcher_unregister_db(struct hl_nic_port *nic_port, u32 dbn);
int hl_nic_eq_dispatcher_dequeue(struct hl_nic_port *nic_port, u32 asid,
					struct hl_nic_eqe *eqe, bool is_default);
int hl_nic_eq_dispatcher_register_ccq(struct hl_nic_port *nic_port, u32 asid, u32 ccqn);
int hl_nic_eq_dispatcher_unregister_ccq(struct hl_nic_port *nic_port, u32 asid, u32 ccqn);
int hl_nic_eq_dispatcher_enqueue(struct hl_nic_port *nic_port, const struct hl_nic_eqe *eqe);
struct hl_nic_user_cq *hl_nic_user_cq_get(struct hl_nic_port *nic_port, u8 cq_id);
int hl_nic_user_cq_put(struct hl_nic_user_cq *user_cq);

u8 hl_nic_dram_readb(struct hl_device *hdev, u64 addr);
u32 hl_nic_dram_readl(struct hl_device *hdev, u64 addr);
u64 hl_nic_dram_readq(struct hl_device *hdev, u64 addr);
void hl_nic_dram_writeb(struct hl_device *hdev, u8 val, u64 addr);
void hl_nic_dram_writel(struct hl_device *hdev, u32 val, u64 addr);

u64 hl_nic_reserve_wq_dva(struct hl_device *hdev, struct hl_ctx *ctx, struct hl_nic_port *nic_port,
				u64 wq_arr_size, u32 type);
int hl_nic_unreserve_wq_dva(struct hl_device *hdev, struct hl_ctx *ctx,
				struct hl_nic_port *nic_port, u32 type);
u32 hl_nic_get_wq_array_type(bool is_send, bool is_coll, bool is_scale_out);

#ifndef _HAS_AUX_BUS_H
extern int hl_en_probe(struct hl_aux_dev *aux_dev);
extern void hl_en_remove(struct hl_aux_dev *aux_dev);
#ifdef HL_LOAD_IB
extern int hl_ib_probe(struct hl_aux_dev *aux_dev);
extern void hl_ib_remove(struct hl_aux_dev *aux_dev);
#endif
#endif

#endif /* NIC_H_ */
