// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2023 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#include <linux/debugfs.h>
#include <linux/slab.h>

#include "habanalabs_compat.h"
#include "habanalabs_compat_accel.h"

#define ACCEL_MAX_MINORS	256
#define ACCEL_NAME		"accel"

static struct dentry *accel_debugfs_root;
static struct class *accel_class;
static int accel_major;

#ifdef _HAS_DEVNODE_WITH_CONST_DEVICE
static char *accel_devnode(const struct device *dev, umode_t *mode)
#else
static char *accel_devnode(struct device *dev, umode_t *mode)
#endif
{
	return kasprintf(GFP_KERNEL, "accel/%s", dev_name(dev));
}

static int accel_sysfs_init(void)
{
	accel_class = class_create(THIS_MODULE, "accel");
	if (IS_ERR(accel_class))
		return PTR_ERR(accel_class);

	accel_class->devnode = accel_devnode;

	return 0;
}

static void accel_sysfs_destroy(void)
{
	if (IS_ERR_OR_NULL(accel_class))
		return;
	class_destroy(accel_class);
	accel_class = NULL;
}

int hl_accel_get_major(void)
{
	return accel_major;
}

struct class *hl_accel_get_class(void)
{
	return accel_class;
}

struct dentry *hl_accel_get_debugfs_root(void)
{
	return accel_debugfs_root;
}

void hl_accel_exit(void)
{
	debugfs_remove(accel_debugfs_root);
	accel_sysfs_destroy();
	unregister_chrdev_region(MKDEV(accel_major, 0), ACCEL_MAX_MINORS);
}

int __init hl_accel_init(void)
{
	dev_t dev;
	int rc;

	rc = alloc_chrdev_region(&dev, 0, ACCEL_MAX_MINORS, ACCEL_NAME);
	if (rc < 0) {
		pr_err("Unable to get accel major\n");
		return rc;
	}

	accel_major = MAJOR(dev);

	rc = accel_sysfs_init();
	if (rc < 0) {
		pr_err("Cannot create accel class: %d\n", rc);
		unregister_chrdev_region(MKDEV(accel_major, 0), ACCEL_MAX_MINORS);
		return rc;
	}

	accel_debugfs_root = debugfs_create_dir("accel", NULL);

	return 0;
}

static int accel_device_create_combined_group(struct device *dev,
						struct attribute_group *combined_group,
						const struct attribute_group **groups)
{
	u32 i, j, num_attrs = 0, num_bin_attrs = 0, attrs_idx = 0, bin_attrs_idx = 0;
	struct bin_attribute **bin_attrs;
	struct attribute **attrs;

	for (i = 0 ; groups[i] ; ++i) {
		for (j = 0 ; groups[i]->attrs && groups[i]->attrs[j] ; ++j)
			++num_attrs;
		for (j = 0 ; groups[i]->bin_attrs && groups[i]->bin_attrs[j] ; ++j)
			++num_bin_attrs;
	}

	/* add 1 for the NULL at the end of the combined attribute arrays */
	++num_attrs;
	++num_bin_attrs;

	attrs = kmalloc_array(num_attrs, sizeof(*combined_group->attrs), GFP_KERNEL | __GFP_ZERO);
	if (!attrs)
		return -ENOMEM;

	bin_attrs = kmalloc_array(num_bin_attrs, sizeof(*combined_group->bin_attrs),
					GFP_KERNEL | __GFP_ZERO);
	if (!bin_attrs) {
		kfree(attrs);
		return -ENOMEM;
	}

	for (i = 0 ; groups[i] ; ++i) {
		for (j = 0 ; groups[i]->attrs && groups[i]->attrs[j] ; ++j)
			attrs[attrs_idx++] = groups[i]->attrs[j];
		for (j = 0 ; groups[i]->bin_attrs && groups[i]->bin_attrs[j] ; ++j)
			bin_attrs[bin_attrs_idx++] = groups[i]->bin_attrs[j];
	}

	combined_group->attrs = attrs;
	combined_group->bin_attrs = bin_attrs;

	return 0;
}

static void accel_device_destroy_combined_group(struct device *dev,
						struct attribute_group *combined_group)
{
	kfree(combined_group->attrs);
	kfree(combined_group->bin_attrs);
}

int hl_accel_device_add_groups(struct device *dev, const struct attribute_group **groups)
{
	struct attribute_group combined_group = {.name = "device"};
	int rc;

	rc = accel_device_create_combined_group(dev, &combined_group, groups);
	if (rc)
		return rc;

	rc = device_add_group(dev, &combined_group);

	accel_device_destroy_combined_group(dev, &combined_group);

	return rc;
}

void hl_accel_device_remove_groups(struct device *dev, const struct attribute_group **groups)
{
	struct attribute_group combined_group = {.name = "device"};
	int rc;

	rc = accel_device_create_combined_group(dev, &combined_group, groups);
	if (rc)
		return;

	device_remove_group(dev, &combined_group);

	accel_device_destroy_combined_group(dev, &combined_group);
}
