// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "nic.h"
#include "../common/habanalabs.h"

#ifdef CONFIG_DEBUG_FS

#include <linux/debugfs.h>
#ifdef _HAS_NO_SPEC
#include <linux/nospec.h>
#endif

#define POLARITY_KBUF_SIZE		8
#define TX_TAPS_KBUF_SIZE		25
#define KBUF_IN_SIZE			18
#define KBUF_OUT_SIZE			BIT(12)
#define MAC_LANE_REMAP_READ_SIZE	10
#define MAX_INT_PORT_STS_KBUF_SIZE	20

static int hl_device_hard_reset_sync(struct hl_device *hdev)
{
	ktime_t timeout;
	u64 reset_sec;

	if (hdev->pldm)
		reset_sec = HL_PLDM_HARD_RESET_MAX_TIMEOUT;
	else
		reset_sec = HL_HARD_RESET_MAX_TIMEOUT;

	hl_device_reset(hdev, HL_DRV_RESET_HARD);

	timeout = ktime_add_ms(ktime_get(), reset_sec * 1000);
	while (hdev->reset_info.in_reset) {
		ssleep(1);
		if (ktime_compare(ktime_get(), timeout) > 0) {
			dev_crit(hdev->dev, "Timed out waiting for hard reset to finish\n");
			return -ETIMEDOUT;
		}
	}

	return 0;
}

static ssize_t debugfs_pam4_tx_taps_write(struct file *f,
						const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic *nic = &hdev->nic;
	char kbuf[TX_TAPS_KBUF_SIZE];
	char *c1, *c2;
	ssize_t rc;
	u32 lane, max_num_of_lanes = hdev->asic_prop.nic_props.max_num_of_lanes;
	s32 tx_pre2, tx_pre1, tx_main, tx_post1, tx_post2;
	s32 *taps;

	if (count > sizeof(kbuf) - 1)
		goto err;
	if (copy_from_user(kbuf, buf, count))
		goto err;
	kbuf[count] = '\0';

	c1 = kbuf;
	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtou32(c1, 10, &lane);
	if (rc)
		goto err;

	if (lane >= max_num_of_lanes) {
		dev_err(hdev->dev, "lane max value is %d\n", max_num_of_lanes - 1);
		return -EINVAL;
	}

#ifdef _HAS_NO_SPEC
	/* Turn off speculation due to Spectre vulnerability */
	lane = array_index_nospec(lane, max_num_of_lanes);
#endif

	c1 = c2 + 1;

	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtos32(c1, 10, &tx_pre2);
	if (rc)
		goto err;

	c1 = c2 + 1;

	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtos32(c1, 10, &tx_pre1);
	if (rc)
		goto err;

	c1 = c2 + 1;

	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtos32(c1, 10, &tx_main);
	if (rc)
		goto err;

	c1 = c2 + 1;

	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtos32(c1, 10, &tx_post1);
	if (rc)
		goto err;

	c1 = c2 + 1;

	rc = kstrtos32(c1, 10, &tx_post2);
	if (rc)
		goto err;

	taps = nic->phy_tx_taps[lane].pam4_taps;
	taps[0] = tx_pre2;
	taps[1] = tx_pre1;
	taps[2] = tx_main;
	taps[3] = tx_post1;
	taps[4] = tx_post2;

	return count;
err:
	dev_err(hdev->dev,
		"usage: echo <lane> <tx_pre2> <tx_pre1> <tx_main> <tx_post1> <tx_post2> > nic_pam4_tx_taps\n");

	return -EINVAL;
}

static ssize_t debugfs_pam4_tx_taps_read(struct file *f, char __user *buf, size_t count,
					loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	u32 lane, max_num_of_lanes = hdev->asic_prop.nic_props.max_num_of_lanes;
	s32 *taps;
	char *kbuf;
	ssize_t rc;

	if (*ppos)
		return 0;

	kbuf = kzalloc(KBUF_OUT_SIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	sprintf(kbuf + strlen(kbuf), "PAM4 tx taps:\n");

	for (lane = 0 ; lane < max_num_of_lanes ; lane++) {
		taps = hdev->nic.phy_tx_taps[lane].pam4_taps;
		sprintf(kbuf + strlen(kbuf), "lane %u: %d %d %d %d %d\n",
			lane, taps[0], taps[1], taps[2], taps[3], taps[4]);
	}

	rc = simple_read_from_buffer(buf, count, ppos, kbuf, strlen(kbuf) + 1);

	kfree(kbuf);

	return rc;
}

static const struct file_operations debugfs_pam4_tx_taps_fops = {
	.owner = THIS_MODULE,
	.write = debugfs_pam4_tx_taps_write,
	.read = debugfs_pam4_tx_taps_read,
};

static ssize_t debugfs_nrz_tx_taps_write(struct file *f,
						const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic *nic = &hdev->nic;
	char kbuf[TX_TAPS_KBUF_SIZE];
	char *c1, *c2;
	ssize_t rc;
	u32 lane, max_num_of_lanes = hdev->asic_prop.nic_props.max_num_of_lanes;
	s32 tx_pre2, tx_pre1, tx_main, tx_post1, tx_post2;
	s32 *taps;

	if (count > sizeof(kbuf) - 1)
		goto err;
	if (copy_from_user(kbuf, buf, count))
		goto err;
	kbuf[count] = '\0';

	c1 = kbuf;
	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtou32(c1, 10, &lane);
	if (rc)
		goto err;

	if (lane >= max_num_of_lanes) {
		dev_err(hdev->dev, "lane max value is %d\n", max_num_of_lanes - 1);
		return -EINVAL;
	}

#ifdef _HAS_NO_SPEC
	/* Turn off speculation due to Spectre vulnerability */
	lane = array_index_nospec(lane, max_num_of_lanes);
#endif

	c1 = c2 + 1;

	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtos32(c1, 10, &tx_pre2);
	if (rc)
		goto err;

	c1 = c2 + 1;

	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtos32(c1, 10, &tx_pre1);
	if (rc)
		goto err;

	c1 = c2 + 1;

	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtos32(c1, 10, &tx_main);
	if (rc)
		goto err;

	c1 = c2 + 1;

	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtos32(c1, 10, &tx_post1);
	if (rc)
		goto err;

	c1 = c2 + 1;

	rc = kstrtos32(c1, 10, &tx_post2);
	if (rc)
		goto err;

	taps = nic->phy_tx_taps[lane].nrz_taps;
	taps[0] = tx_pre2;
	taps[1] = tx_pre1;
	taps[2] = tx_main;
	taps[3] = tx_post1;
	taps[4] = tx_post2;

	return count;
err:
	dev_err(hdev->dev,
		"usage: echo <lane> <tx_pre2> <tx_pre1> <tx_main> <tx_post1> <tx_post2> > nic_nrz_tx_taps\n");

	return -EINVAL;
}

static ssize_t debugfs_nrz_tx_taps_read(struct file *f, char __user *buf, size_t count,
					loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	u32 lane, max_num_of_lanes = hdev->asic_prop.nic_props.max_num_of_lanes;
	s32 *taps;
	char *kbuf;
	ssize_t rc;

	if (*ppos)
		return 0;

	kbuf = kzalloc(KBUF_OUT_SIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	sprintf(kbuf + strlen(kbuf), "NRZ tx taps:\n");

	for (lane = 0 ; lane < max_num_of_lanes ; lane++) {
		taps = hdev->nic.phy_tx_taps[lane].nrz_taps;
		sprintf(kbuf + strlen(kbuf), "lane %u: %d %d %d %d %d\n",
			lane, taps[0], taps[1], taps[2], taps[3], taps[4]);
	}

	rc = simple_read_from_buffer(buf, count, ppos, kbuf, strlen(kbuf) + 1);

	kfree(kbuf);

	return rc;
}

static const struct file_operations debugfs_nrz_tx_taps_fops = {
	.owner = THIS_MODULE,
	.write = debugfs_nrz_tx_taps_write,
	.read = debugfs_nrz_tx_taps_read,
};

static ssize_t debugfs_polarity_write(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct cpucp_nic_info *nic_info = &hdev->asic_prop.cpucp_nic_info;
	char kbuf[POLARITY_KBUF_SIZE];
	char *c1, *c2;
	ssize_t rc;
	u64 val;
	u32 lane, max_num_of_lanes = hdev->asic_prop.nic_props.max_num_of_lanes;
	u8 pol_tx, pol_rx;

	if (count > sizeof(kbuf) - 1)
		goto err;
	if (copy_from_user(kbuf, buf, count))
		goto err;
	kbuf[count] = '\0';

	c1 = kbuf;
	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtou32(c1, 10, &lane);
	if (rc)
		goto err;

	if (lane >= max_num_of_lanes) {
		dev_err(hdev->dev, "lane max value is %d\n", max_num_of_lanes - 1);
		return -EINVAL;
	}

	c1 = c2 + 1;

	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtou8(c1, 10, &pol_tx);
	if (rc)
		goto err;

	c1 = c2 + 1;

	rc = kstrtou8(c1, 10, &pol_rx);
	if (rc)
		goto err;

	if ((pol_tx & ~1) || (pol_rx & ~1)) {
		dev_err(hdev->dev, "pol_tx and pol_rx should be 0 or 1\n");
		goto err;
	}

	val = le64_to_cpu(nic_info->pol_tx_mask[0]);
	val &= ~BIT_ULL(lane);
	val |= ((u64) pol_tx) << lane;
	nic_info->pol_tx_mask[0] = cpu_to_le64(val);

	val = le64_to_cpu(nic_info->pol_rx_mask[0]);
	val &= ~BIT_ULL(lane);
	val |= ((u64) pol_rx) << lane;
	nic_info->pol_rx_mask[0] = cpu_to_le64(val);

	return count;
err:
	dev_err(hdev->dev, "usage: echo <lane> <pol_tx> <pol_rx> > nic_polarity\n");

	return -EINVAL;
}

static const struct file_operations debugfs_polarity_fops = {
	.owner = THIS_MODULE,
	.write = debugfs_polarity_write,
};

static ssize_t debugfs_qp_read(struct file *f, char __user *buf, size_t count,
				loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	char *kbuf;
	ssize_t rc;

	if (*ppos)
		return 0;

	kbuf = kzalloc(KBUF_OUT_SIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	rc = nic_funcs->qp_read(hdev, kbuf, KBUF_OUT_SIZE);
	if (rc)
		goto out;

	rc = simple_read_from_buffer(buf, count, ppos, kbuf, strlen(kbuf) + 1);

out:
	kfree(kbuf);

	return rc;
}

static ssize_t debugfs_qp_write(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic_qp_info *qp_info = &hdev->nic.qp_info;
	char kbuf[KBUF_IN_SIZE];
	char *c1, *c2;
	ssize_t rc;
	u32 port, qpn, max_num_of_ports = hdev->asic_prop.nic_props.max_num_of_ports;
	u8 req, full_print, force_read, exts_print = 0;

	if (count > sizeof(kbuf) - 1)
		goto err;
	if (copy_from_user(kbuf, buf, count))
		goto err;
	kbuf[count] = '\0';

	c1 = kbuf;
	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtou32(c1, 10, &port);
	if (rc)
		goto err;

	if (port >= max_num_of_ports) {
		dev_err(hdev->dev, "port max value is %d\n", max_num_of_ports - 1);
		return -EINVAL;
	}

	c1 = c2 + 1;

	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtou32(c1, 10, &qpn);
	if (rc)
		goto err;

	c1 = c2 + 1;

	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtou8(c1, 10, &req);
	if (rc)
		goto err;

	if (req & ~1) {
		dev_err(hdev->dev, "req should be 0 or 1\n");
		goto err;
	}

	c1 = c2 + 1;

	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtou8(c1, 10, &full_print);
	if (rc)
		goto err;

	if (full_print & ~1) {
		dev_err(hdev->dev, "full_print should be 0 or 1\n");
		goto err;
	}

	c1 = c2 + 1;

	/* may not be the last element due to the optional params */
	c2 = strchr(c1, ' ');
	if (c2)
		*c2 = '\0';

	rc = kstrtou8(c1, 10, &force_read);
	if (rc)
		goto err;

	if (force_read & ~1) {
		dev_err(hdev->dev, "force_read should be 0 or 1\n");
		goto err;
	}

	/* handle the optional extensions print (if exists) */
	if (!c2)
		goto done;

	c1 = c2 + 1;

	rc = kstrtou8(c1, 10, &exts_print);
	if (rc)
		goto err;

	if (exts_print & ~1) {
		dev_err(hdev->dev, "exts_print should be 0 or 1\n");
		goto err;
	}

done:
	qp_info->port = port;
	qp_info->qpn = qpn;
	qp_info->req = req;
	qp_info->full_print = full_print;
	qp_info->force_read = force_read;
	qp_info->exts_print = exts_print;

	return count;
err:
	dev_err(hdev->dev,
		"usage: echo <port> <qpn> <is_req> <is_full_print> <force_read> [<exts_print>] > nic_qp\n");

	return -EINVAL;
}

static const struct file_operations debugfs_qp_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_qp_read,
	.write = debugfs_qp_write
};

static ssize_t debugfs_wqe_read(struct file *f, char __user *buf, size_t count,
				loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	char *kbuf;
	ssize_t rc;

	if (*ppos)
		return 0;

	kbuf = kzalloc(KBUF_OUT_SIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	rc = nic_funcs->wqe_read(hdev, kbuf, KBUF_OUT_SIZE);
	if (rc)
		goto out;

	rc = simple_read_from_buffer(buf, count, ppos, kbuf, strlen(kbuf) + 1);

out:
	kfree(kbuf);

	return rc;
}

static ssize_t debugfs_wqe_write(struct file *f, const char __user *buf,
					size_t count, loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic_wqe_info *wqe_info = &hdev->nic.wqe_info;
	char kbuf[KBUF_IN_SIZE];
	char *c1, *c2;
	ssize_t rc;
	u32 port, qpn, wqe_idx,
		max_num_of_lanes = hdev->asic_prop.nic_props.max_num_of_lanes;
	u8 tx;

	if (count > sizeof(kbuf) - 1)
		goto err;
	if (copy_from_user(kbuf, buf, count))
		goto err;
	kbuf[count] = '\0';

	c1 = kbuf;
	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtou32(c1, 10, &port);
	if (rc)
		goto err;

	if (port >= max_num_of_lanes) {
		dev_err(hdev->dev, "port max value is %d\n", max_num_of_lanes - 1);
		return -EINVAL;
	}

	c1 = c2 + 1;

	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtou32(c1, 10, &qpn);
	if (rc)
		goto err;

	c1 = c2 + 1;

	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtou32(c1, 10, &wqe_idx);
	if (rc)
		goto err;

	c1 = c2 + 1;

	rc = kstrtou8(c1, 10, &tx);
	if (rc)
		goto err;

	if (tx & ~1) {
		dev_err(hdev->dev, "tx should be 0 or 1\n");
		goto err;
	}

	wqe_info->port = port;
	wqe_info->qpn = qpn;
	wqe_info->wqe_idx = wqe_idx;
	wqe_info->tx = tx;

	return count;
err:
	dev_err(hdev->dev, "usage: echo <port> <qpn> <wqe_idx> <is_tx> > nic_wqe\n");

	return -EINVAL;
}

static const struct file_operations debugfs_wqe_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_wqe_read,
	.write = debugfs_wqe_write
};

static ssize_t debugfs_reset_ethtool_cnt_write(struct file *f,
					const char __user *buf, size_t count,
					loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	ssize_t rc;
	u32 val;

	rc = kstrtou32_from_user(buf, count, 10, &val);
	if (rc)
		return rc;

	hl_nic_reset_ethtool_counters(hdev);

	return count;
}

static const struct file_operations debugfs_reset_ethtool_cnt_fops = {
	.owner = THIS_MODULE,
	.write = debugfs_reset_ethtool_cnt_write
};

static int parse_user_mac_lane_remap_data(u32 *dest_arr, int *dest_arr_cnt, char *buf, int count)
{
	int i = 0, j = 0, rc;
	int offset;
	u32 val;

	while (i < count) {
		offset = strcspn(&buf[i], " ");
		buf[i + offset] = '\0';

		rc = kstrtou32(&buf[i], 16, &val);
		if (rc)
			return rc;

		dest_arr[j++] = val;
		i += (offset + 1);
	}

	*dest_arr_cnt = j;

	return 0;
}

static ssize_t debugfs_mac_lane_remap_write(struct file *f,
					const char __user *buf, size_t count,
					loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	int rc;
	char *kbuf;
	u32 *mac_lane_remap_buf;
	int n_parsed = 0;

	kbuf = kcalloc(count + 1, sizeof(*buf), GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	mac_lane_remap_buf = kcalloc(nic_props->num_of_macros,
					sizeof(*mac_lane_remap_buf), GFP_KERNEL);
	if (!mac_lane_remap_buf) {
		rc = -ENOMEM;
		goto err_free_kbuf;
	}

	rc = copy_from_user(kbuf, buf, count);
	if (rc)
		goto err_free_mac_lane_remap_buf;

	/* Add trailing space to simplify parsing user data. */
	kbuf[count] = ' ';

	rc = parse_user_mac_lane_remap_data(mac_lane_remap_buf, &n_parsed, kbuf, count + 1);
	if (rc || n_parsed != nic_props->num_of_macros) {
		rc = -EINVAL;
		goto err_parse;
	}

	memcpy(nic->mac_lane_remap, mac_lane_remap_buf,
		sizeof(*mac_lane_remap_buf) * nic_props->num_of_macros);

	rc = hl_device_hard_reset_sync(hdev);
	if (rc)
		goto err_free_mac_lane_remap_buf;

	kfree(mac_lane_remap_buf);
	kfree(kbuf);

	return count;
err_parse:
	dev_err_ratelimited(hdev->dev,
		"usage: echo macro0 macr1 macro2 ... macroX > mac_lane_remap\n");
err_free_mac_lane_remap_buf:
	kfree(mac_lane_remap_buf);
err_free_kbuf:
	kfree(kbuf);
	return -EINVAL;
}

static ssize_t debugfs_mac_lane_remap_read(struct file *f,
					char __user *buf, size_t count,
					loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic_properties *nic_props;
	char kbuf[MAC_LANE_REMAP_READ_SIZE];
	struct hl_nic *nic = &hdev->nic;
	int i, j;

	nic_props = &hdev->asic_prop.nic_props;

	if (*ppos)
		return 0;

	for (i = 0, j = 0 ; i < nic_props->num_of_macros ; i++, j += MAC_LANE_REMAP_READ_SIZE) {
		memset(kbuf, 0, MAC_LANE_REMAP_READ_SIZE);
		sprintf(kbuf, "0x%x ", nic->mac_lane_remap[i]);

		if (copy_to_user(&buf[j], kbuf, MAC_LANE_REMAP_READ_SIZE)) {
			dev_err(hdev->dev, "error in copying lane info to user\n");
			return -EFAULT;
		}

		*ppos += MAC_LANE_REMAP_READ_SIZE;
	}

	return j + 1;
}

static ssize_t debugfs_eth_loopback_write(struct file *f,
					const char __user *buf, size_t count,
					loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic *nic = &hdev->nic;
	u32 val = 0;
	int rc;

	rc = kstrtou32_from_user(buf, count, 10, &val);
	if (rc)
		return rc;

	nic->eth_loopback = !!val;

	dev_info(hdev->dev, "%s eth_loopback\n", nic->eth_loopback ? "enable" : "disable");

	return count;
}

static ssize_t debugfs_eth_loopback_read(struct file *f,
					char __user *buf, size_t count,
					loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic *nic = &hdev->nic;
	u32 val = 0;
	ssize_t rc;

	if (*ppos)
		return 0;

	snprintf((char *) &val, sizeof(val), "%u", nic->eth_loopback);

	rc = simple_read_from_buffer(buf, count, ppos, &val, sizeof(val));

	return rc;
}

static const struct file_operations debugfs_mac_lane_remap_fops = {
	.owner = THIS_MODULE,
	.write = debugfs_mac_lane_remap_write,
	.read = debugfs_mac_lane_remap_read,
};

static const struct file_operations debugfs_eth_loopback_fops = {
	.owner = THIS_MODULE,
	.write = debugfs_eth_loopback_write,
	.read = debugfs_eth_loopback_read,
};

static ssize_t debugfs_phy_regs_print_write(struct file *f,
					const char __user *buf, size_t count,
					loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic *nic = &hdev->nic;
	u32 val = 0;
	int rc;

	rc = kstrtou32_from_user(buf, count, 10, &val);
	if (rc)
		return rc;

	nic->phy_regs_print = !!val;

	dev_info(hdev->dev,
		"%s printing PHY registers\n", nic->phy_regs_print ? "enable" : "disable");

	return count;
}

static ssize_t debugfs_phy_regs_print_read(struct file *f,
					char __user *buf, size_t count,
					loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic *nic = &hdev->nic;
	u32 val = 0;
	ssize_t rc;

	if (*ppos)
		return 0;

	snprintf((char *) &val, sizeof(val), "%u", nic->phy_regs_print);

	rc = simple_read_from_buffer(buf, count, ppos, &val, sizeof(val));

	return rc;
}

static const struct file_operations debugfs_phy_regs_print_fops = {
	.owner = THIS_MODULE,
	.write = debugfs_phy_regs_print_write,
	.read = debugfs_phy_regs_print_read,
};

static ssize_t debugfs_show_internal_ports_status_read(struct file *f,
							char __user *buf, size_t count,
							loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic_properties *nic_props;
	struct hl_nic_port *nic_port;
	struct hl_nic *nic;
	char kbuf[MAX_INT_PORT_STS_KBUF_SIZE];
	int i, cnt, total_cnt;

	nic_props = &hdev->asic_prop.nic_props;
	nic = &hdev->nic;

	if (*ppos)
		return 0;

	total_cnt = 0;

	for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)) || (nic->eth_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];

		memset(kbuf, 0, MAX_INT_PORT_STS_KBUF_SIZE);
		cnt = sprintf(kbuf, "Port %-2u: %s\n",
				nic_port->port, nic_port->pcs_link ? "UP" : "DOWN");

		if (copy_to_user(&buf[total_cnt], kbuf, cnt)) {
			dev_err(hdev->dev, "error in copying info to user\n");
			return -EFAULT;
		}

		total_cnt += cnt;
		*ppos += cnt;
	}

	return total_cnt + 1;
}

static const struct file_operations debugfs_show_internal_ports_status_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_show_internal_ports_status_read,
};

static ssize_t debugfs_print_fec_stats_read(struct file *f,
					char __user *buf, size_t count,
					loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic_properties *nic_props;
	struct hl_nic_funcs *nic_funcs;
	struct hl_nic_port *nic_port;
	struct hl_nic *nic;
	char *kbuf;
	int i, rc;

	nic_props = &hdev->asic_prop.nic_props;
	nic_funcs = hdev->asic_funcs->nic_funcs;
	nic = &hdev->nic;

	if (*ppos)
		return 0;

	kbuf = kzalloc(KBUF_OUT_SIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	sprintf(kbuf + strlen(kbuf), "Card %u FEC stats:\n", hdev->nic.card_location);

	for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];

		nic_funcs->port_funcs->collect_fec_stats(nic_port, kbuf, KBUF_OUT_SIZE);
	}

	rc = simple_read_from_buffer(buf, count, ppos, kbuf, strlen(kbuf) + 1);

	kfree(kbuf);

	return rc;
}

static const struct file_operations debugfs_print_fec_stats_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_print_fec_stats_read,
};

static ssize_t debugfs_phy_set_nrz_write(struct file *f,
					const char __user *buf, size_t count,
					loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic *nic = &hdev->nic;
	bool val;
	int rc;

	rc = kstrtobool_from_user(buf, count, &val);
	if (rc)
		return rc;

	if (val == nic->phy_set_nrz)
		return count;

	nic->phy_set_nrz = val;

	dev_info(hdev->dev, "%s NRZ mode\n", nic->phy_set_nrz ? "Enable" : "Disable");

	rc = hl_device_hard_reset_sync(hdev);
	if (rc)
		return -EINVAL;

	return count;
}

static const struct file_operations debugfs_phy_set_nrz_fops = {
	.owner = THIS_MODULE,
	.write = debugfs_phy_set_nrz_write,
};

static ssize_t debugfs_write_coll_lag_size(struct file *f,
					const char __user *buf, size_t count,
					loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	u32 val;
	int rc;

	/* For ASICs that don't support this feature, return an error */
	if (!nic_funcs->write_coll_lag_size)
		return -EINVAL;

	rc = kstrtou32_from_user(buf, count, 10, &val);
	if (rc)
		return rc;

	rc = nic_funcs->write_coll_lag_size(hdev, val);
	if (rc)
		return rc;

	return count;
}

static ssize_t debugfs_read_coll_lag_size(struct file *f,
					char __user *buf, size_t count,
					loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	u32 coll_lag_size;
	ssize_t rc;

	if (*ppos)
		return 0;

	/* For ASICs that don't support this feature, return an error */
	if (!nic_funcs->read_coll_lag_size)
		return -EINVAL;

	rc = nic_funcs->read_coll_lag_size(hdev, &coll_lag_size);
	if (rc)
		return rc;

	rc = simple_read_from_buffer(buf, count, ppos, &coll_lag_size, sizeof(coll_lag_size));

	return rc;
}

static const struct file_operations debugfs_coll_lag_size_fops = {
	.owner = THIS_MODULE,
	.write = debugfs_write_coll_lag_size,
	.read = debugfs_read_coll_lag_size,
};

static ssize_t debugfs_phy_dump_serdes_params_read(struct file *f, char __user *buf, size_t count,
							loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	char *kbuf;
	ssize_t rc;

	if (*ppos)
		return 0;

	/* For ASICs that don't support this feature, return an error */
	if (!nic_funcs->phy_dump_serdes_params)
		return -EINVAL;

	kbuf = kzalloc(KBUF_OUT_SIZE, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	nic_funcs->phy_dump_serdes_params(hdev, kbuf, KBUF_OUT_SIZE);

	rc = simple_read_from_buffer(buf, count, ppos, kbuf, strlen(kbuf) + 1);

	kfree(kbuf);

	return rc;
}

static ssize_t debugfs_phy_dump_serdes_params_write(struct file *f, const char __user *buf,
							size_t count, loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	u32 port;
	int rc;

	/* For ASICs that don't support this feature, return an error */
	if (!nic_funcs->phy_dump_serdes_params)
		return -EINVAL;

	rc = kstrtou32_from_user(buf, count, 10, &port);
	if (rc)
		return rc;

	if (port >= hdev->asic_prop.nic_props.max_num_of_ports) {
		dev_err(hdev->dev, "Invalid port number %u\n", port);
		return -EINVAL;
	}

	hdev->nic.phy_port_to_dump = port;

	return count;
}

static const struct file_operations debugfs_phy_dump_serdes_params_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_phy_dump_serdes_params_read,
	.write = debugfs_phy_dump_serdes_params_write,
};

static ssize_t debugfs_inject_rx_err_read(struct file *f, char __user *buf, size_t count,
						loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic *nic = &hdev->nic;
	u32 val;

	if (*ppos)
		return 0;

	snprintf((char *) &val, sizeof(val), "%u", nic->rx_drop_percent);

	return simple_read_from_buffer(buf, count, ppos, &val, sizeof(val));
}

static ssize_t debugfs_inject_rx_err_write(struct file *f, const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	struct hl_nic_funcs *nic_funcs = hdev->asic_funcs->nic_funcs;
	int rc;
	u32 val;

	if (*ppos)
		return 0;

	rc = kstrtou32_from_user(buf, count, 10, &val);
	if (rc)
		return rc;

	if (val > 100) {
		dev_dbg_ratelimited(hdev->dev, "Invalid drop percentage %d\n", val);
		return -EINVAL;
	}

	nic_funcs->inject_rx_err(hdev, val);

	return count;
}

static const struct file_operations debugfs_inject_rx_err_fops = {
	.owner = THIS_MODULE,
	.read = debugfs_inject_rx_err_read,
	.write = debugfs_inject_rx_err_write,
};

static ssize_t debugfs_override_port_status_write(struct file *f, const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct hl_device *hdev = file_inode(f)->i_private;
	char kbuf[KBUF_IN_SIZE];
	struct hl_nic *nic;
	struct hl_nic_port *nic_port;
	char *c1, *c2;
	ssize_t rc;
	u32 port, max_num_of_ports = hdev->asic_prop.nic_props.max_num_of_ports;
	u8 up;

	if (count > sizeof(kbuf) - 1)
		goto err;
	if (copy_from_user(kbuf, buf, count))
		goto err;
	kbuf[count] = '\0';

	c1 = kbuf;
	c2 = strchr(c1, ' ');
	if (!c2)
		goto err;
	*c2 = '\0';

	rc = kstrtou32(c1, 10, &port);
	if (rc)
		goto err;

	if (port >= max_num_of_ports) {
		dev_err(hdev->dev, "port max value is %d\n", max_num_of_ports - 1);
		return -EINVAL;
	}

#ifdef _HAS_NO_SPEC
	/* Turn off speculation due to Spectre vulnerability */
	port = array_index_nospec(port, max_num_of_ports);
#endif

	c1 = c2 + 1;

	rc = kstrtou8(c1, 10, &up);
	if (rc)
		goto err;

	nic = &hdev->nic;

	if (hdev->nic_ports_mask & BIT(port)) {
		nic_port = &nic->nic_ports[port];

		nic_port->pcs_link = !!up;
		hl_nic_phy_set_port_status(nic_port, !!up);
	}

	return count;
err:
	dev_err(hdev->dev, "usage: echo <port> <status> > nic_override_port_status\n");

	return -EINVAL;
}

static const struct file_operations debugfs_override_port_status_fops = {
	.owner = THIS_MODULE,
	.write = debugfs_override_port_status_write,
};

#define NIC_DEBUGFS(X, fmt, do_reset) \
static ssize_t debugfs_##X##_read(struct file *f, \
					char __user *buf, \
					size_t count, \
					loff_t *ppos) \
{ \
	struct hl_device *hdev = file_inode(f)->i_private; \
	struct hl_nic *nic = &hdev->nic; \
	char tmp_buf[32]; \
	ssize_t rc; \
\
	if (*ppos) \
		return 0; \
\
	sprintf(tmp_buf, fmt "\n", nic->X); \
	rc = simple_read_from_buffer(buf, strlen(tmp_buf) + 1, ppos, tmp_buf, \
			strlen(tmp_buf) + 1); \
\
	return rc; \
} \
\
static ssize_t debugfs_##X##_write(struct file *f, \
					const char __user *buf, \
					size_t count, \
					loff_t *ppos) \
{ \
	struct hl_device *hdev = file_inode(f)->i_private; \
	struct hl_nic *nic = &hdev->nic; \
	u64 val, base; \
	ssize_t ret; \
	int rc; \
\
	if (!strcmp(fmt, "%d")) \
		base = 10; \
	else \
		base = 16; \
\
	ret = kstrtoull_from_user(buf, count, base, &val); \
	if (ret) \
		return ret; \
\
	if (val == nic->X) \
		return count; \
\
	if (do_reset && nic->debugfs_reset) { \
		nic->X = val; \
		rc = hl_device_hard_reset_sync(hdev); \
		if (rc) \
			return rc; \
\
		return count; \
	} \
\
	dev_info(hdev->dev, "NIC reset for %s started\n", __stringify(X)); \
\
	hl_nic_hard_reset_prepare(hdev); \
\
	hl_nic_stop(hdev); \
\
	nic->X = val; \
\
	rc = hl_nic_reopen(hdev); \
	if (rc) \
		dev_err(hdev->dev, "Failed to init NIC, %d\n", rc); \
\
	dev_info(hdev->dev, "NIC reset for %s finished\n", __stringify(X)); \
\
	return count; \
} \
\
static const struct file_operations debugfs_##X##_fops = { \
	.owner = THIS_MODULE, \
	.read = debugfs_##X##_read, \
	.write = debugfs_##X##_write, \
}

NIC_DEBUGFS(mac_loopback, "0x%llx", true);
NIC_DEBUGFS(pcs_fail_time_frame, "%d", false);
NIC_DEBUGFS(pcs_fail_threshold, "%d", false);

void hl_nic_debugfs_init(struct hl_device *hdev, struct dentry *root_dir)
{
	struct hl_nic *nic = &hdev->nic;

	debugfs_create_file("nic_mac_loopback",
				0644,
				root_dir,
				hdev,
				&debugfs_mac_loopback_fops);

	debugfs_create_file("nic_pcs_fail_time_frame",
				0644,
				root_dir,
				hdev,
				&debugfs_pcs_fail_time_frame_fops);

	debugfs_create_file("nic_pcs_fail_threshold",
				0644,
				root_dir,
				hdev,
				&debugfs_pcs_fail_threshold_fops);

	debugfs_create_file("nic_pam4_tx_taps",
				0444,
				root_dir,
				hdev,
				&debugfs_pam4_tx_taps_fops);

	debugfs_create_file("nic_nrz_tx_taps",
				0444,
				root_dir,
				hdev,
				&debugfs_nrz_tx_taps_fops);

	debugfs_create_file("nic_polarity",
				0444,
				root_dir,
				hdev,
				&debugfs_polarity_fops);

	debugfs_create_file("nic_qp",
				0444,
				root_dir,
				hdev,
				&debugfs_qp_fops);

	debugfs_create_file("nic_wqe",
				0444,
				root_dir,
				hdev,
				&debugfs_wqe_fops);

	debugfs_create_file("nic_reset_cnt",
				0444,
				root_dir,
				hdev,
				&debugfs_reset_ethtool_cnt_fops);

	debugfs_create_file("nic_mac_lane_remap",
				0644,
				root_dir,
				hdev,
				&debugfs_mac_lane_remap_fops);

	debugfs_create_u8("nic_rand_status",
				0644,
				root_dir,
				&nic->rand_status);

	debugfs_create_u8("nic_mmu_bypass",
				0644,
				root_dir,
				&nic->mmu_bypass);

	debugfs_create_file("nic_eth_loopback",
				0644,
				root_dir,
				hdev,
				&debugfs_eth_loopback_fops);

	debugfs_create_file("nic_phy_regs_print",
				0444,
				root_dir,
				hdev,
				&debugfs_phy_regs_print_fops);

	debugfs_create_file("nic_show_internal_ports_status",
				0444,
				root_dir,
				hdev,
				&debugfs_show_internal_ports_status_fops);

	debugfs_create_file("nic_print_fec_stats",
				0444,
				root_dir,
				hdev,
				&debugfs_print_fec_stats_fops);

	debugfs_create_u8("nic_disable_decap",
				0644,
				root_dir,
				&nic->is_decap_disabled);

	debugfs_create_file("nic_phy_set_nrz",
				0444,
				root_dir,
				hdev,
				&debugfs_phy_set_nrz_fops);

	debugfs_create_file("nic_coll_lag_size",
				0644,
				root_dir,
				hdev,
				&debugfs_coll_lag_size_fops);

	debugfs_create_file("nic_phy_dump_serdes_params",
				0444,
				root_dir,
				hdev,
				&debugfs_phy_dump_serdes_params_fops);

	debugfs_create_file("nic_inject_rx_err",
				0444,
				root_dir,
				hdev,
				&debugfs_inject_rx_err_fops);

	debugfs_create_u8("nic_phy_calc_ber",
				0644,
				root_dir,
				&nic->phy_calc_ber);

	debugfs_create_u16("nic_phy_calc_ber_wait_sec",
				0644,
				root_dir,
				&nic->phy_calc_ber_wait_sec);

	debugfs_create_file("nic_override_port_status",
				0200,
				root_dir,
				hdev,
				&debugfs_override_port_status_fops);
}

#else

void hl_nic_debugfs_init(struct hl_device *hdev, struct dentry *root_dir)
{
}

#endif /* CONFIG_DEBUG_FS */
