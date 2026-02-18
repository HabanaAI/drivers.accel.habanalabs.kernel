// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2024 HabanaLabs, Ltd.
 * Copyright (C) 2024-2025, Intel Corporation.
 * All Rights Reserved.
 */

#include "habanalabs.h"
#include "habanalabs_compat_accel.h"

#include <linux/capability.h>
#include <linux/pci.h>
#include <linux/types.h>

static ssize_t clk_max_freq_mhz_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	value = hl_fw_get_frequency(hdev, hdev->asic_prop.clk_pll_index, false);
	if (value < 0)
		return value;

	hdev->asic_prop.max_freq_value = value;

	return sprintf(buf, "%lu\n", (value / 1000 / 1000));
}

static ssize_t clk_max_freq_mhz_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	int rc;
	u64 value;

	if (!hl_device_operational(hdev, NULL)) {
		count = -ENODEV;
		goto fail;
	}

	rc = kstrtoull(buf, 0, &value);
	if (rc) {
		count = -EINVAL;
		goto fail;
	}

	hdev->asic_prop.max_freq_value = value * 1000 * 1000;

	hl_fw_set_frequency(hdev, hdev->asic_prop.clk_pll_index, hdev->asic_prop.max_freq_value);

fail:
	return count;
}

static ssize_t clk_cur_freq_mhz_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	value = hl_fw_get_frequency(hdev, hdev->asic_prop.clk_pll_index, true);
	if (value < 0)
		return value;

	return sprintf(buf, "%lu\n", (value / 1000 / 1000));
}

static DEVICE_ATTR_RW(clk_max_freq_mhz);
static DEVICE_ATTR_RO(clk_cur_freq_mhz);

static struct attribute *hl_dev_clk_attrs[] = {
	&dev_attr_clk_max_freq_mhz.attr,
	&dev_attr_clk_cur_freq_mhz.attr,
	NULL,
};

static ssize_t vrm_ver_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	struct cpucp_info *cpucp_info;
	u32 infineon_second_stage_version;
	u32 infineon_second_stage_first_instance;
	u32 infineon_second_stage_second_instance;
	u32 infineon_second_stage_third_instance;
	u32 mask = 0xff;

	cpucp_info = &hdev->asic_prop.cpucp_info;

	infineon_second_stage_version = le32_to_cpu(cpucp_info->infineon_second_stage_version);
	infineon_second_stage_first_instance = infineon_second_stage_version & mask;
	infineon_second_stage_second_instance =
					(infineon_second_stage_version >> 8) & mask;
	infineon_second_stage_third_instance =
					(infineon_second_stage_version >> 16) & mask;

	if (cpucp_info->infineon_version && cpucp_info->infineon_second_stage_version)
		return sprintf(buf, "%#04x %#04x:%#04x:%#04x\n",
				le32_to_cpu(cpucp_info->infineon_version),
				infineon_second_stage_first_instance,
				infineon_second_stage_second_instance,
				infineon_second_stage_third_instance);
	else if (cpucp_info->infineon_second_stage_version)
		return sprintf(buf, "%#04x:%#04x:%#04x\n",
				infineon_second_stage_first_instance,
				infineon_second_stage_second_instance,
				infineon_second_stage_third_instance);
	else if (cpucp_info->infineon_version)
		return sprintf(buf, "%#04x\n", le32_to_cpu(cpucp_info->infineon_version));

	return 0;
}

static DEVICE_ATTR_RO(vrm_ver);

static struct attribute *hl_dev_vrm_attrs[] = {
	&dev_attr_vrm_ver.attr,
	NULL,
};

static ssize_t uboot_ver_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", hdev->asic_prop.uboot_ver);
}

static ssize_t armcp_kernel_ver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s", hdev->asic_prop.cpucp_info.kernel_version);
}

static ssize_t armcp_ver_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", hdev->asic_prop.cpucp_info.cpucp_version);
}

static ssize_t cpld_ver_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "0x%08x%08x\n",
			le32_to_cpu(hdev->asic_prop.cpucp_info.cpld_timestamp),
			le32_to_cpu(hdev->asic_prop.cpucp_info.cpld_version));
}

static ssize_t cpucp_kernel_ver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s", hdev->asic_prop.cpucp_info.kernel_version);
}

static ssize_t cpucp_ver_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", hdev->asic_prop.cpucp_info.cpucp_version);
}

static ssize_t fuse_ver_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", hdev->asic_prop.cpucp_info.fuse_version);
}

static ssize_t thermal_ver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s", hdev->asic_prop.cpucp_info.thermal_version);
}

static ssize_t fw_os_ver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s", hdev->asic_prop.cpucp_info.fw_os_version);
}

static ssize_t preboot_btl_ver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", hdev->asic_prop.preboot_ver);
}

static ssize_t soft_reset_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;
	int rc;

	rc = kstrtoul(buf, 0, &value);

	if (rc) {
		count = -EINVAL;
		goto out;
	}

	if (!hdev->asic_prop.allow_inference_soft_reset) {
		hl_err_ratelimited(hdev, "Device does not support inference soft-reset\n");
		goto out;
	}

	hl_warn(hdev, "Inference Soft-Reset requested through sysfs\n");

	hl_device_reset(hdev, 0);

out:
	return count;
}

static ssize_t hard_reset_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long value;
	int rc;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	rc = kstrtoul(buf, 0, &value);

	if (rc) {
		count = -EINVAL;
		goto out;
	}

	hl_warn(hdev, "Hard-Reset requested through sysfs\n");

	hl_device_reset(hdev, HL_DRV_RESET_HARD);

out:
	return count;
}

static ssize_t driver_ver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", hdev->driver_ver);
}

static ssize_t device_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	char *str;

	switch (hdev->asic_type) {
#ifdef HL_DOWNSTREAM
	case ASIC_GOYA_SIM:
		str = "GOYA Simulator";
		break;
	case ASIC_GAUDI_SIM:
		str = "GAUDI Simulator";
		break;
	case ASIC_GAUDI_HL2000M_SIM:
		str = "GAUDI HL2000M Simulator";
		break;
	case ASIC_GAUDI2_SIM:
	case ASIC_GAUDI2_SIM_ARC:
		str = "GAUDI2 Simulator";
		break;
	case ASIC_GAUDI2B_SIM:
	case ASIC_GAUDI2B_SIM_ARC:
		str = "GAUDI2B Simulator";
		break;
	case ASIC_GAUDI2C_SIM:
	case ASIC_GAUDI2C_SIM_ARC:
		str = "GAUDI2C Simulator";
		break;
	case ASIC_GAUDI2D_SIM:
	case ASIC_GAUDI2D_SIM_ARC:
		str = "GAUDI2D Simulator";
		break;
	case ASIC_GAUDI2E_SIM:
	case ASIC_GAUDI2E_SIM_ARC:
			str = "GAUDI2E Simulator";
			break;
	case ASIC_GAUDI2_HL_288_SIM:
	case ASIC_GAUDI2_HL_288_SIM_ARC:
		str = "GAUDI2 HL-288 Simulator";
		break;
	case ASIC_GAUDI2D_HL_288_SIM:
	case ASIC_GAUDI2D_HL_288_SIM_ARC:
		str = "GAUDI2D HL-288 Simulator";
		break;
	case ASIC_GAUDI2E_HL_288_SIM:
	case ASIC_GAUDI2E_HL_288_SIM_ARC:
		str = "GAUDI2E HL-288 Simulator";
		break;
	case ASIC_GAUDI3_SIM:
	case ASIC_GAUDI3_SIM_ARC:
		str = "GAUDI3 Simulator";
		break;
	case ASIC_GAUDI3D_SIM:
	case ASIC_GAUDI3D_SIM_ARC:
		str = "GAUDI3D Simulator";
		break;
	case ASIC_GAUDI3E_SIM:
	case ASIC_GAUDI3E_SIM_ARC:
		str = "GAUDI3E Simulator";
		break;
	case ASIC_GAUDI3_HL_338_SIM:
	case ASIC_GAUDI3_HL_338_SIM_ARC:
		str = "GAUDI3 HL-338 Simulator";
		break;
	case ASIC_GAUDI3D_HL_338_SIM:
	case ASIC_GAUDI3D_HL_338_SIM_ARC:
		str = "GAUDI3D HL-338 Simulator";
		break;
	case ASIC_GAUDI3E_HL_338_SIM:
	case ASIC_GAUDI3E_HL_338_SIM_ARC:
		str = "GAUDI3E HL-338 Simulator";
		break;
	case ASIC_GAUDI3_FPGA:
		str = "GAUDI3 FPGA";
		break;
#endif /* HL_DOWNSTREAM */
	case ASIC_GOYA:
		str = "GOYA";
		break;
	case ASIC_GAUDI:
		str = "GAUDI";
		break;
	case ASIC_GAUDI_SEC:
		str = "GAUDI SEC";
		break;
	case ASIC_GAUDI_HL2000M:
		str = "GAUDI HL2000M";
		break;
	case ASIC_GAUDI_HL2000M_SEC:
		str = "GAUDI HL2000M SEC";
		break;
	case ASIC_GAUDI2:
		str = "GAUDI2";
		break;
	case ASIC_GAUDI2B:
		str = "GAUDI2B";
		break;
	case ASIC_GAUDI2C:
		str = "GAUDI2C";
		break;
	case ASIC_GAUDI2D:
		str = "GAUDI2D";
		break;
	case ASIC_GAUDI2E:
		str = "GAUDI2E";
		break;
	case ASIC_GAUDI2_HL_288:
		str = "GAUDI2 HL-288";
		break;
	case ASIC_GAUDI2D_HL_288:
		str = "GAUDI2D HL-288";
		break;
	case ASIC_GAUDI2E_HL_288:
		str = "GAUDI2E HL-288";
		break;
	case ASIC_GAUDI3:
		str = "GAUDI3";
		break;
	case ASIC_GAUDI3D:
		str = "GAUDI3D";
		break;
	case ASIC_GAUDI3E:
		str = "GAUDI3E";
		break;
	case ASIC_GAUDI3_HL_338:
		str = "GAUDI3 HL-338";
		break;
	case ASIC_GAUDI3D_HL_338:
		str = "GAUDI3D HL-338";
		break;
	case ASIC_GAUDI3E_HL_338:
		str = "GAUDI3E HL-338";
		break;
	default:
		hl_err(hdev, "Unrecognized ASIC type %d\n",
				hdev->asic_type);
		return -EINVAL;
	}

	return sprintf(buf, "%s\n", str);
}

static ssize_t pci_addr_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	/* Use dummy, fixed address for simulator */
	if (!hdev->pdev)
		return sprintf(buf, "0000:00:%02d.0\n", hdev->id);

	return sprintf(buf, "%04x:%02x:%02x.%x\n",
			pci_domain_nr(hdev->pdev->bus),
			hdev->pdev->bus->number,
			PCI_SLOT(hdev->pdev->devfn),
			PCI_FUNC(hdev->pdev->devfn));
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	char str[HL_STR_MAX];

	strscpy(str, hdev->status[hl_device_status(hdev)], HL_STR_MAX);

	/* use uppercase for backward compatibility */
	str[0] = 'A' + (str[0] - 'a');

	return sprintf(buf, "%s\n", str);
}

static ssize_t soft_reset_cnt_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", hdev->reset_info.compute_reset_cnt);
}

static ssize_t hard_reset_cnt_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", hdev->reset_info.hard_reset_cnt);
}

static ssize_t max_power_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	long val;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	val = hl_fw_get_max_power(hdev);
	if (val < 0)
		return val;

	return sprintf(buf, "%lu\n", val);
}

static ssize_t max_power_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hl_device *hdev = dev_get_drvdata(dev);
	unsigned long value;
	int rc;

	if (!hl_device_operational(hdev, NULL)) {
		count = -ENODEV;
		goto out;
	}

	rc = kstrtoul(buf, 0, &value);

	if (rc) {
		count = -EINVAL;
		goto out;
	}

	hdev->max_power = value;
	hl_fw_set_max_power(hdev);

out:
	return count;
}

static ssize_t eeprom_read_handler(struct file *filp, struct kobject *kobj,
			struct bin_attribute *attr, char *buf, loff_t offset,
			size_t max_size)
{
	struct device *dev = kobj_to_dev(kobj);
	struct hl_device *hdev = dev_get_drvdata(dev);
	char *data;
	int rc;

	if (!hl_device_operational(hdev, NULL))
		return -ENODEV;

	if (!max_size)
		return -EINVAL;

	data = kzalloc(max_size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	rc = hdev->asic_funcs->get_eeprom_data(hdev, data, max_size);
	if (rc)
		goto out;

	memcpy(buf, data, max_size);

out:
	kfree(data);

	return max_size;
}

static ssize_t security_enabled_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", hdev->asic_prop.fw_security_enabled);
}

static ssize_t module_id_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", hl_get_module_id(hdev));
}

static ssize_t parent_device_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", HL_PARENT_DEV_NAME(hdev));
}

static DEVICE_ATTR_RO(armcp_kernel_ver);
static DEVICE_ATTR_RO(armcp_ver);
static DEVICE_ATTR_RO(cpld_ver);
static DEVICE_ATTR_RO(cpucp_kernel_ver);
static DEVICE_ATTR_RO(cpucp_ver);
static DEVICE_ATTR_RO(device_type);
static DEVICE_ATTR_RO(driver_ver);
static DEVICE_ATTR_RO(fuse_ver);
static DEVICE_ATTR_WO(hard_reset);
static DEVICE_ATTR_RO(hard_reset_cnt);
static DEVICE_ATTR_RW(max_power);
static DEVICE_ATTR_RO(pci_addr);
static DEVICE_ATTR_RO(preboot_btl_ver);
static DEVICE_ATTR_WO(soft_reset);
static DEVICE_ATTR_RO(soft_reset_cnt);
static DEVICE_ATTR_RO(status);
static DEVICE_ATTR_RO(thermal_ver);
static DEVICE_ATTR_RO(uboot_ver);
static DEVICE_ATTR_RO(fw_os_ver);
static DEVICE_ATTR_RO(security_enabled);
static DEVICE_ATTR_RO(module_id);
static DEVICE_ATTR_RO(parent_device);

static struct bin_attribute bin_attr_eeprom = {
	.attr = {.name = "eeprom", .mode = (0444)},
	.size = PAGE_SIZE,
	.read = eeprom_read_handler
};

static struct attribute *hl_dev_attrs[] = {
	&dev_attr_armcp_kernel_ver.attr,
	&dev_attr_armcp_ver.attr,
	&dev_attr_cpld_ver.attr,
	&dev_attr_cpucp_kernel_ver.attr,
	&dev_attr_cpucp_ver.attr,
	&dev_attr_device_type.attr,
	&dev_attr_driver_ver.attr,
	&dev_attr_fuse_ver.attr,
	&dev_attr_hard_reset.attr,
	&dev_attr_hard_reset_cnt.attr,
	&dev_attr_max_power.attr,
	&dev_attr_pci_addr.attr,
	&dev_attr_preboot_btl_ver.attr,
	&dev_attr_status.attr,
	&dev_attr_thermal_ver.attr,
	&dev_attr_uboot_ver.attr,
	&dev_attr_fw_os_ver.attr,
	&dev_attr_security_enabled.attr,
	&dev_attr_module_id.attr,
	&dev_attr_parent_device.attr,
	NULL,
};

static struct bin_attribute *hl_dev_bin_attrs[] = {
	&bin_attr_eeprom,
	NULL
};

static struct attribute_group hl_dev_attr_group = {
	.attrs = hl_dev_attrs,
	.bin_attrs = hl_dev_bin_attrs,
};

static struct attribute_group hl_dev_clks_attr_group;
static struct attribute_group hl_dev_vrm_attr_group;

static const struct attribute_group *hl_dev_attr_groups[] = {
	&hl_dev_attr_group,
	&hl_dev_clks_attr_group,
	&hl_dev_vrm_attr_group,
	NULL,
};

static struct attribute *hl_dev_inference_attrs[] = {
	&dev_attr_soft_reset.attr,
	&dev_attr_soft_reset_cnt.attr,
	NULL,
};

static struct attribute_group hl_dev_inference_attr_group = {
	.attrs = hl_dev_inference_attrs,
};

static const struct attribute_group *hl_dev_inference_attr_groups[] = {
	&hl_dev_inference_attr_group,
	NULL,
};

void hl_sysfs_add_dev_clk_attr(struct hl_device *hdev, struct attribute_group *dev_clk_attr_grp)
{
	dev_clk_attr_grp->attrs = hl_dev_clk_attrs;
}

void hl_sysfs_add_dev_vrm_attr(struct hl_device *hdev, struct attribute_group *dev_vrm_attr_grp)
{
	dev_vrm_attr_grp->attrs = hl_dev_vrm_attrs;
}

#if IS_ENABLED(CONFIG_DRM_ACCEL)
int hl_sysfs_init(struct hl_device *hdev)
{
	int rc;

	hdev->max_power = hdev->asic_prop.max_power_default;

	hdev->asic_funcs->add_device_attr(hdev, &hl_dev_clks_attr_group, &hl_dev_vrm_attr_group);

	rc = device_add_groups(hdev->dev, hl_dev_attr_groups);
	if (rc) {
		hl_err(hdev,
			"Failed to add groups to device, error %d\n", rc);
		return rc;
	}

	if (!hdev->asic_prop.allow_inference_soft_reset)
		return 0;

	rc = device_add_groups(hdev->dev, hl_dev_inference_attr_groups);
	if (rc) {
		hl_err(hdev,
			"Failed to add groups to device, error %d\n", rc);
		goto remove_groups;
	}

	return 0;

remove_groups:
	device_remove_groups(hdev->dev, hl_dev_attr_groups);
	return rc;
}
#else
int hl_sysfs_init(struct hl_device *hdev)
{
	int rc;

	hdev->max_power = hdev->asic_prop.max_power_default;

	hdev->asic_funcs->add_device_attr(hdev, &hl_dev_clks_attr_group, &hl_dev_vrm_attr_group);

	rc = hl_accel_device_add_groups(hdev->dev, hl_dev_attr_groups);
	if (rc) {
		hl_err(hdev, "Failed to add groups to device, error %d\n", rc);
		return rc;
	}

	if (!hdev->asic_prop.allow_inference_soft_reset)
		return 0;

	rc = hl_accel_device_add_groups(hdev->dev, hl_dev_inference_attr_groups);
	if (rc) {
		hl_err(hdev, "Failed to add groups to device, error %d\n", rc);
		goto remove_groups;
	}

	return 0;

remove_groups:
	hl_accel_device_remove_groups(hdev->dev, hl_dev_attr_groups);
	return rc;
}
#endif /* IS_ENABLED(CONFIG_DRM_ACCEL) */

#if IS_ENABLED(CONFIG_DRM_ACCEL)
void hl_sysfs_fini(struct hl_device *hdev)
{
	device_remove_groups(hdev->dev, hl_dev_attr_groups);

	if (!hdev->asic_prop.allow_inference_soft_reset)
		return;

	device_remove_groups(hdev->dev, hl_dev_inference_attr_groups);
}
#else
void hl_sysfs_fini(struct hl_device *hdev)
{
	hl_accel_device_remove_groups(hdev->dev, hl_dev_attr_groups);

	if (!hdev->asic_prop.allow_inference_soft_reset)
		return;

	hl_accel_device_remove_groups(hdev->dev, hl_dev_inference_attr_groups);
}
#endif /* IS_ENABLED(CONFIG_DRM_ACCEL) */
