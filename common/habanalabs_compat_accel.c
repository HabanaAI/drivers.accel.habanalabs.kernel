// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2023 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#include <linux/debugfs.h>
#include <linux/device.h>

#include "habanalabs_compat_accel.h"

#define ACCEL_MAX_MINORS	256
#define ACCEL_NAME		"accel"

static struct dentry *accel_debugfs_root;
static struct class *accel_class;
static int accel_major;

static char *accel_devnode(struct device *dev, umode_t *mode)
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

void hl_accel_exit(void)
{
	unregister_chrdev_region(MKDEV(accel_major, 0), ACCEL_MAX_MINORS);
	debugfs_remove(accel_debugfs_root);
	accel_sysfs_destroy();
}

int __init hl_accel_init(void)
{
	dev_t dev;
	int rc;

	rc = accel_sysfs_init();
	if (rc < 0) {
		pr_err("Cannot create ACCEL class: %d\n", rc);
		goto error;
	}

	accel_debugfs_root = debugfs_create_dir("accel", NULL);

	rc = alloc_chrdev_region(&dev, 0, ACCEL_MAX_MINORS, ACCEL_NAME);
	if (rc < 0) {
		pr_err("unable to get major\n");
		return rc;
	}

	accel_major = MAJOR(dev);

error:
	return rc;
}
