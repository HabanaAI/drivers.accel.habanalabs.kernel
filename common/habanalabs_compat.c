// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#include "habanalabs_compat.h"

#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/hwmon.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#ifdef _HAS_DAX_H
#include <linux/dax.h>
#endif

#ifndef _HAS_DEVICE_ADD_GROUPS
int device_add_groups(struct device *dev, const struct attribute_group **groups)
{
	return sysfs_create_groups(&dev->kobj, groups);
}

void device_remove_groups(struct device *dev,
			  const struct attribute_group **groups)
{
	sysfs_remove_groups(&dev->kobj, groups);
}
#endif /* !_HAS_DEVICE_ADD_GROUPS */

#ifndef _HAS_PCI_IOREMAP_WC_BAR
void __iomem *pci_ioremap_wc_bar(struct pci_dev *pdev, int bar)
{
	/*
	 * Make sure the BAR is actually a memory resource, not an IO resource
	 */
	if (!(pci_resource_flags(pdev, bar) & IORESOURCE_MEM)) {
		WARN_ON(1);
		return NULL;
	}
	return ioremap_wc(pci_resource_start(pdev, bar),
			  pci_resource_len(pdev, bar));
}
#endif

#ifndef _HAS_KVMALLOC_ARRAY
void *kvmalloc_node(size_t size, gfp_t flags, int node)
{
	gfp_t kmalloc_flags = flags;
	void *ret;

	/*
	 * vmalloc uses GFP_KERNEL for some internal allocations (e.g page
	 * tables) so the given set of flags has to be compatible.
	 */
	WARN_ON_ONCE((flags & GFP_KERNEL) != GFP_KERNEL);

	/*
	 * We want to attempt a large physically contiguous block first because
	 * it is less likely to fragment multiple larger blocks and therefore
	 * contribute to a long term fragmentation less than vmalloc fallback.
	 * However make sure that larger requests are not too disruptive - no
	 * OOM killer and no allocation failure warnings as we have a fallback.
	 */
	if (size > PAGE_SIZE) {
		kmalloc_flags |= __GFP_NOWARN;

		if (!(kmalloc_flags & __GFP_REPEAT))
			kmalloc_flags |= __GFP_NORETRY;
	}

	ret = kmalloc_node(size, kmalloc_flags, node);

	/*
	 * It doesn't really make sense to fallback to vmalloc for sub page
	 * requests
	 */
	if (ret || size <= PAGE_SIZE)
		return ret;

	/*
	 * __vmalloc_node_range() is not an exported function, so __vmalloc()
	 * must be used instead.
	 * As opposed to __vmalloc_node_range(), __vmalloc() doesn't have a
	 * "node" parameter, and thus the usage of the kernel WARN.
	 */
	WARN_ON_ONCE(node != NUMA_NO_NODE);

	return __vmalloc(size, flags, PAGE_KERNEL);
}
#endif /* !_HAS_KVMALLOC_ARRAY */

#ifndef _HAS_PCI_ALLOC_IRQ_VECTORS
#ifdef CONFIG_PCI_MSI
int pci_alloc_irq_vectors(struct pci_dev *dev, unsigned int min_vecs,
				unsigned int max_vecs, unsigned int flags)
{
	int vecs = -ENOSPC;

	if (flags & PCI_IRQ_MSIX) {
		struct msix_entry *entries;
		int i;

		entries = kmalloc_array(max_vecs, sizeof(*entries),
					GFP_KERNEL | __GFP_ZERO);
		if (!entries)
			return -ENOMEM;

		for (i = 0; i < max_vecs; i++)
			entries[i].entry = i;

		vecs = pci_enable_msix_range(dev, entries, min_vecs, max_vecs);

		kfree(entries);

		if (vecs > 0)
			return vecs;
	}

	if (flags & PCI_IRQ_MSI) {
		vecs = pci_enable_msi_range(dev, min_vecs, max_vecs);
		if (vecs > 0)
			return vecs;
	}

	/* use legacy irq if allowed */
	if (flags & PCI_IRQ_LEGACY) {
		if (min_vecs == 1 && dev->irq) {
			pci_intx(dev, 1);
			return 1;
		}
	}

	return vecs;
}

void pci_free_irq_vectors(struct pci_dev *dev)
{
	pci_disable_msix(dev);
	pci_disable_msi(dev);
}

#define for_each_pci_msi_entry(desc, pdev) \
	list_for_each_entry((desc), &(pdev)->msi_list, list)

#define first_pci_msi_entry(pdev) \
	list_first_entry(&(pdev)->msi_list, struct msi_desc, list)

int pci_irq_vector(struct pci_dev *dev, unsigned int nr)
{
	if (dev->msix_enabled) {
		struct msi_desc *entry;
		int i = 0;

		for_each_pci_msi_entry(entry, dev) {
			if (i == nr)
				return entry->irq;
			i++;
		}
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	if (dev->msi_enabled) {
		struct msi_desc *entry = first_pci_msi_entry(dev);

		if (WARN_ON_ONCE(nr >= entry->nvec_used))
			return -EINVAL;
	} else {
		if (WARN_ON_ONCE(nr > 0))
			return -EINVAL;
	}

	return dev->irq + nr;
}
#endif /* CONFIG_PCI_MSI */
#endif /* !_HAS_PCI_ALLOC_IRQ_VECTORS */

#ifndef _HAS_VMA_IS_FSDAX
static inline bool vma_is_fsdax(struct vm_area_struct *vma)
{
	struct inode *inode;

	if (!vma->vm_file)
		return false;
	if (!vma_is_dax(vma))
		return false;
	inode = file_inode(vma->vm_file);
	if (S_ISCHR(inode->i_mode))
		return false; /* device-dax */
	return true;
}
#endif

/* if _HAS_USER_PAGES_DIRTY_LOCK exists, we don't need this function at all.
 * Therefore, don't even try to compile it (it may have errors on new kernels).
 */
#if !defined(CONFIG_FRAME_VECTOR) && !defined(_HAS_USER_PAGES_DIRTY_LOCK)
int get_vaddr_frames(unsigned long start, unsigned int nr_frames,
		     unsigned int gup_flags, struct frame_vector *vec)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int ret = 0;
	int err;
	int locked;

	if (nr_frames == 0)
		return 0;

	if (WARN_ON_ONCE(nr_frames > vec->nr_allocated))
		nr_frames = vec->nr_allocated;

	down_read(&mm->mmap_sem);
	locked = 1;
	vma = find_vma_intersection(mm, start, start + 1);
	if (!vma) {
		ret = -EFAULT;
		goto out;
	}

	/*
	 * While get_vaddr_frames() could be used for transient (kernel
	 * controlled lifetime) pinning of memory pages all current
	 * users establish long term (userspace controlled lifetime)
	 * page pinning. Treat get_vaddr_frames() like
	 * get_user_pages_longterm() and disallow it for filesystem-dax
	 * mappings.
	 */
	if (vma_is_fsdax(vma)) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (!(vma->vm_flags & (VM_IO | VM_PFNMAP))) {
		vec->got_ref = true;
		vec->is_pfns = false;
#ifdef _HAS_GET_USER_PAGES_LOCKED_WITH_TASK_PTR
		ret = get_user_pages_locked(current, mm, start, nr_frames,
				gup_flags & FOLL_WRITE, gup_flags & FOLL_FORCE,
				(struct page **)(vec->ptrs), &locked);
#else
		ret = get_user_pages_locked(start, nr_frames,
				gup_flags, (struct page **)(vec->ptrs),
				&locked);
#endif /* _HAS_GET_USER_PAGES_LOCKED_WITH_TASK_PTR */
		goto out;
	}

	vec->got_ref = false;
	vec->is_pfns = true;
	do {
		unsigned long *nums = frame_vector_pfns(vec);

		while (ret < nr_frames && start + PAGE_SIZE <= vma->vm_end) {
			err = follow_pfn(vma, start, &nums[ret]);
			if (err) {
				if (ret == 0)
					ret = err;
				goto out;
			}
			start += PAGE_SIZE;
			ret++;
		}
		/*
		 * We stop if we have enough pages or if VMA doesn't completely
		 * cover the tail page.
		 */
		if (ret >= nr_frames || start < vma->vm_end)
			break;
		vma = find_vma_intersection(mm, start, start + 1);
	} while (vma && vma->vm_flags & (VM_IO | VM_PFNMAP));
out:
	if (locked)
		up_read(&mm->mmap_sem);
	if (!ret)
		ret = -EFAULT;
	if (ret > 0)
		vec->nr_frames = ret;
	return ret;
}

void put_vaddr_frames(struct frame_vector *vec)
{
	int i;
	struct page **pages;

	if (!vec->got_ref)
		goto out;
	pages = frame_vector_pages(vec);
	/*
	 * frame_vector_pages() might needed to do a conversion when
	 * get_vaddr_frames() got pages but vec was later converted to pfns.
	 * But it shouldn't really fail to convert pfns back...
	 */
	if (WARN_ON(IS_ERR(pages)))
		goto out;
	for (i = 0; i < vec->nr_frames; i++)
		put_page(pages[i]);
	vec->got_ref = false;
out:
	vec->nr_frames = 0;
}

int frame_vector_to_pages(struct frame_vector *vec)
{
	int i;
	unsigned long *nums;
	struct page **pages;

	if (!vec->is_pfns)
		return 0;
	nums = frame_vector_pfns(vec);
	for (i = 0; i < vec->nr_frames; i++)
		if (!pfn_valid(nums[i]))
			return -EINVAL;
	pages = (struct page **)nums;
	for (i = 0; i < vec->nr_frames; i++)
		pages[i] = pfn_to_page(nums[i]);
	vec->is_pfns = false;
	return 0;
}

void frame_vector_to_pfns(struct frame_vector *vec)
{
	int i;
	unsigned long *nums;
	struct page **pages;

	if (vec->is_pfns)
		return;
	pages = (struct page **)(vec->ptrs);
	nums = (unsigned long *)pages;
	for (i = 0; i < vec->nr_frames; i++)
		nums[i] = page_to_pfn(pages[i]);
	vec->is_pfns = true;
}

struct frame_vector *frame_vector_create(unsigned int nr_frames)
{
	struct frame_vector *vec;
	int size = sizeof(struct frame_vector) + sizeof(void *) * nr_frames;

	if (WARN_ON_ONCE(nr_frames == 0))
		return NULL;
	/*
	 * This is absurdly high. It's here just to avoid strange effects when
	 * arithmetic overflows.
	 */
	if (WARN_ON_ONCE(nr_frames > INT_MAX / sizeof(void *) / 2))
		return NULL;
	/*
	 * Avoid higher order allocations, use vmalloc instead. It should
	 * be rare anyway.
	 */
	vec = kvmalloc(size, GFP_KERNEL);
	if (!vec)
		return NULL;
	vec->nr_allocated = nr_frames;
	vec->nr_frames = 0;
	return vec;
}

void frame_vector_destroy(struct frame_vector *vec)
{
	/* Make sure put_vaddr_frames() got called properly... */
	VM_BUG_ON(vec->nr_frames > 0);
	kvfree(vec);
}
#endif /* !CONFIG_FRAME_VECTOR */

#ifndef _HAS_HWMON_DEVICE_REGISTER_WITH_INFO
#define HWMON_ID_PREFIX "hwmon"
#define HWMON_ID_FORMAT HWMON_ID_PREFIX "%d"

struct hwmon_device {
	const char *name;
	struct device dev;
	const struct hwmon_chip_info *chip;

	struct attribute_group group;
	const struct attribute_group **groups;
};

#define MAX_SYSFS_ATTR_NAME_LENGTH	32

struct hwmon_device_attribute {
	struct device_attribute dev_attr;
	const struct hwmon_ops *ops;
	enum hwmon_sensor_types type;
	u32 attr;
	int index;
	char name[MAX_SYSFS_ATTR_NAME_LENGTH];
};

#define to_hwmon_attr(d) \
	container_of(d, struct hwmon_device_attribute, dev_attr)

struct hwmon_thermal_data {
	struct hwmon_device *hwdev;
	int index;
};

#define __take_second_arg(__ignored, val, ...) val

#define __or(x, y)			___or(x, y)
#define ___or(x, y)			____or(__ARG_PLACEHOLDER_##x, y)
#define ____or(arg1_or_junk, y)		__take_second_arg(arg1_or_junk 1, y)

#ifndef IS_REACHABLE
#define IS_REACHABLE(option) __or(IS_BUILTIN(option), \
				__and(IS_MODULE(option), __is_defined(MODULE)))
#endif

#if IS_REACHABLE(CONFIG_THERMAL) && defined(CONFIG_THERMAL_OF) && \
	(!defined(CONFIG_THERMAL_HWMON) || \
	 !(defined(MODULE) && IS_MODULE(CONFIG_THERMAL)))
static int hwmon_thermal_get_temp(void *data, int *temp)
{
	struct hwmon_thermal_data *tdata = data;
	struct hwmon_device *hwdev = tdata->hwdev;
	int ret;
	long t;

	ret = hwdev->chip->ops->read(&hwdev->dev, hwmon_temp, hwmon_temp_input,
				     tdata->index, &t);
	if (ret < 0)
		return ret;

	*temp = t;

	return 0;
}

static const struct thermal_zone_of_device_ops hwmon_thermal_ops = {
	.get_temp = hwmon_thermal_get_temp,
};

static int hwmon_thermal_add_sensor(struct device *dev,
				    struct hwmon_device *hwdev, int index)
{
	struct hwmon_thermal_data *tdata;
	struct thermal_zone_device *tzd;

	tdata = devm_kzalloc(dev, sizeof(*tdata), GFP_KERNEL);
	if (!tdata)
		return -ENOMEM;

	tdata->hwdev = hwdev;
	tdata->index = index;

	tzd = devm_thermal_zone_of_sensor_register(&hwdev->dev, index, tdata,
						   &hwmon_thermal_ops);
	/*
	 * If CONFIG_THERMAL_OF is disabled, this returns -ENODEV,
	 * so ignore that error but forward any other error.
	 */
	if (IS_ERR(tzd) && (PTR_ERR(tzd) != -ENODEV))
		return PTR_ERR(tzd);

	return 0;
}
#else
static int hwmon_thermal_add_sensor(struct device *dev,
				    struct hwmon_device *hwdev, int index)
{
	return 0;
}
#endif /* IS_REACHABLE(CONFIG_THERMAL) && ... */

static ssize_t hwmon_attr_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct hwmon_device_attribute *hattr = to_hwmon_attr(devattr);
	long val;
	int ret;

	ret = hattr->ops->read(dev, hattr->type, hattr->attr, hattr->index,
			       &val);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%ld\n", val);
}

static ssize_t hwmon_attr_show_string(struct device *dev,
				      struct device_attribute *devattr,
				      char *buf)
{
	struct hwmon_device_attribute *hattr = to_hwmon_attr(devattr);
	const char *s;
	int ret;

	ret = hattr->ops->read_string(dev, hattr->type, hattr->attr,
				      hattr->index, &s);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%s\n", s);
}

static ssize_t hwmon_attr_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	struct hwmon_device_attribute *hattr = to_hwmon_attr(devattr);
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret < 0)
		return ret;

	ret = hattr->ops->write(dev, hattr->type, hattr->attr, hattr->index,
				val);
	if (ret < 0)
		return ret;

	return count;
}

static int hwmon_attr_base(enum hwmon_sensor_types type)
{
	if (type == hwmon_in)
		return 0;
	return 1;
}

static bool is_string_attr(enum hwmon_sensor_types type, u32 attr)
{
	return (type == hwmon_temp && attr == hwmon_temp_label) ||
	       (type == hwmon_in && attr == hwmon_in_label) ||
	       (type == hwmon_curr && attr == hwmon_curr_label) ||
	       (type == hwmon_power && attr == hwmon_power_label) ||
	       (type == hwmon_energy && attr == hwmon_energy_label) ||
	       (type == hwmon_humidity && attr == hwmon_humidity_label) ||
	       (type == hwmon_fan && attr == hwmon_fan_label);
}

static struct attribute *hwmon_genattr(struct device *dev,
				       const void *drvdata,
				       enum hwmon_sensor_types type,
				       u32 attr,
				       int index,
				       const char *template,
				       const struct hwmon_ops *ops)
{
	struct hwmon_device_attribute *hattr;
	struct device_attribute *dattr;
	struct attribute *a;
	umode_t mode;
	char *name;
	bool is_string = is_string_attr(type, attr);

	/* The attribute is invisible if there is no template string */
	if (!template)
		return ERR_PTR(-ENOENT);

	mode = ops->is_visible(drvdata, type, attr, index);
	if (!mode)
		return ERR_PTR(-ENOENT);

	if ((mode & 0444) && ((is_string && !ops->read_string) ||
			      (!is_string && !ops->read)))
		return ERR_PTR(-EINVAL);
	if ((mode & 0222) && !ops->write)
		return ERR_PTR(-EINVAL);

	hattr = devm_kzalloc(dev, sizeof(*hattr), GFP_KERNEL);
	if (!hattr)
		return ERR_PTR(-ENOMEM);

	if (type == hwmon_chip) {
		name = (char *)template;
	} else {
		scnprintf(hattr->name, sizeof(hattr->name), template,
			  index + hwmon_attr_base(type));
		name = hattr->name;
	}

	hattr->type = type;
	hattr->attr = attr;
	hattr->index = index;
	hattr->ops = ops;

	dattr = &hattr->dev_attr;
	dattr->show = is_string ? hwmon_attr_show_string : hwmon_attr_show;
	dattr->store = hwmon_attr_store;

	a = &dattr->attr;
	sysfs_attr_init(a);
	a->name = name;
	a->mode = mode;

	return a;
}

static const char * const hwmon_chip_attrs[] = {
	[hwmon_chip_temp_reset_history] = "temp_reset_history",
	[hwmon_chip_in_reset_history] = "in_reset_history",
	[hwmon_chip_curr_reset_history] = "curr_reset_history",
	[hwmon_chip_power_reset_history] = "power_reset_history",
	[hwmon_chip_update_interval] = "update_interval",
	[hwmon_chip_alarms] = "alarms",
};

static const char * const hwmon_temp_attr_templates[] = {
	[hwmon_temp_input] = "temp%d_input",
	[hwmon_temp_type] = "temp%d_type",
	[hwmon_temp_lcrit] = "temp%d_lcrit",
	[hwmon_temp_lcrit_hyst] = "temp%d_lcrit_hyst",
	[hwmon_temp_min] = "temp%d_min",
	[hwmon_temp_min_hyst] = "temp%d_min_hyst",
	[hwmon_temp_max] = "temp%d_max",
	[hwmon_temp_max_hyst] = "temp%d_max_hyst",
	[hwmon_temp_crit] = "temp%d_crit",
	[hwmon_temp_crit_hyst] = "temp%d_crit_hyst",
	[hwmon_temp_emergency] = "temp%d_emergency",
	[hwmon_temp_emergency_hyst] = "temp%d_emergency_hyst",
	[hwmon_temp_alarm] = "temp%d_alarm",
	[hwmon_temp_lcrit_alarm] = "temp%d_lcrit_alarm",
	[hwmon_temp_min_alarm] = "temp%d_min_alarm",
	[hwmon_temp_max_alarm] = "temp%d_max_alarm",
	[hwmon_temp_crit_alarm] = "temp%d_crit_alarm",
	[hwmon_temp_emergency_alarm] = "temp%d_emergency_alarm",
	[hwmon_temp_fault] = "temp%d_fault",
	[hwmon_temp_offset] = "temp%d_offset",
	[hwmon_temp_label] = "temp%d_label",
	[hwmon_temp_lowest] = "temp%d_lowest",
	[hwmon_temp_highest] = "temp%d_highest",
	[hwmon_temp_reset_history] = "temp%d_reset_history",
};

static const char * const hwmon_in_attr_templates[] = {
	[hwmon_in_input] = "in%d_input",
	[hwmon_in_min] = "in%d_min",
	[hwmon_in_max] = "in%d_max",
	[hwmon_in_lcrit] = "in%d_lcrit",
	[hwmon_in_crit] = "in%d_crit",
	[hwmon_in_average] = "in%d_average",
	[hwmon_in_lowest] = "in%d_lowest",
	[hwmon_in_highest] = "in%d_highest",
	[hwmon_in_reset_history] = "in%d_reset_history",
	[hwmon_in_label] = "in%d_label",
	[hwmon_in_alarm] = "in%d_alarm",
	[hwmon_in_min_alarm] = "in%d_min_alarm",
	[hwmon_in_max_alarm] = "in%d_max_alarm",
	[hwmon_in_lcrit_alarm] = "in%d_lcrit_alarm",
	[hwmon_in_crit_alarm] = "in%d_crit_alarm",
};

static const char * const hwmon_curr_attr_templates[] = {
	[hwmon_curr_input] = "curr%d_input",
	[hwmon_curr_min] = "curr%d_min",
	[hwmon_curr_max] = "curr%d_max",
	[hwmon_curr_lcrit] = "curr%d_lcrit",
	[hwmon_curr_crit] = "curr%d_crit",
	[hwmon_curr_average] = "curr%d_average",
	[hwmon_curr_lowest] = "curr%d_lowest",
	[hwmon_curr_highest] = "curr%d_highest",
	[hwmon_curr_reset_history] = "curr%d_reset_history",
	[hwmon_curr_label] = "curr%d_label",
	[hwmon_curr_alarm] = "curr%d_alarm",
	[hwmon_curr_min_alarm] = "curr%d_min_alarm",
	[hwmon_curr_max_alarm] = "curr%d_max_alarm",
	[hwmon_curr_lcrit_alarm] = "curr%d_lcrit_alarm",
	[hwmon_curr_crit_alarm] = "curr%d_crit_alarm",
};

static const char * const hwmon_power_attr_templates[] = {
	[hwmon_power_average] = "power%d_average",
	[hwmon_power_average_interval] = "power%d_average_interval",
	[hwmon_power_average_interval_max] = "power%d_interval_max",
	[hwmon_power_average_interval_min] = "power%d_interval_min",
	[hwmon_power_average_highest] = "power%d_average_highest",
	[hwmon_power_average_lowest] = "power%d_average_lowest",
	[hwmon_power_average_max] = "power%d_average_max",
	[hwmon_power_average_min] = "power%d_average_min",
	[hwmon_power_input] = "power%d_input",
	[hwmon_power_input_highest] = "power%d_input_highest",
	[hwmon_power_input_lowest] = "power%d_input_lowest",
	[hwmon_power_reset_history] = "power%d_reset_history",
	[hwmon_power_accuracy] = "power%d_accuracy",
	[hwmon_power_cap] = "power%d_cap",
	[hwmon_power_cap_hyst] = "power%d_cap_hyst",
	[hwmon_power_cap_max] = "power%d_cap_max",
	[hwmon_power_cap_min] = "power%d_cap_min",
	[hwmon_power_max] = "power%d_max",
	[hwmon_power_crit] = "power%d_crit",
	[hwmon_power_label] = "power%d_label",
	[hwmon_power_alarm] = "power%d_alarm",
	[hwmon_power_cap_alarm] = "power%d_cap_alarm",
	[hwmon_power_max_alarm] = "power%d_max_alarm",
	[hwmon_power_crit_alarm] = "power%d_crit_alarm",
};

static const char * const hwmon_energy_attr_templates[] = {
	[hwmon_energy_input] = "energy%d_input",
	[hwmon_energy_label] = "energy%d_label",
};

static const char * const hwmon_humidity_attr_templates[] = {
	[hwmon_humidity_input] = "humidity%d_input",
	[hwmon_humidity_label] = "humidity%d_label",
	[hwmon_humidity_min] = "humidity%d_min",
	[hwmon_humidity_min_hyst] = "humidity%d_min_hyst",
	[hwmon_humidity_max] = "humidity%d_max",
	[hwmon_humidity_max_hyst] = "humidity%d_max_hyst",
	[hwmon_humidity_alarm] = "humidity%d_alarm",
	[hwmon_humidity_fault] = "humidity%d_fault",
};

static const char * const hwmon_fan_attr_templates[] = {
	[hwmon_fan_input] = "fan%d_input",
	[hwmon_fan_label] = "fan%d_label",
	[hwmon_fan_min] = "fan%d_min",
	[hwmon_fan_max] = "fan%d_max",
	[hwmon_fan_div] = "fan%d_div",
	[hwmon_fan_pulses] = "fan%d_pulses",
	[hwmon_fan_target] = "fan%d_target",
	[hwmon_fan_alarm] = "fan%d_alarm",
	[hwmon_fan_min_alarm] = "fan%d_min_alarm",
	[hwmon_fan_max_alarm] = "fan%d_max_alarm",
	[hwmon_fan_fault] = "fan%d_fault",
};

static const char * const hwmon_pwm_attr_templates[] = {
	[hwmon_pwm_input] = "pwm%d",
	[hwmon_pwm_enable] = "pwm%d_enable",
	[hwmon_pwm_mode] = "pwm%d_mode",
	[hwmon_pwm_freq] = "pwm%d_freq",
};

static const char * const *__templates[] = {
	[hwmon_chip] = hwmon_chip_attrs,
	[hwmon_temp] = hwmon_temp_attr_templates,
	[hwmon_in] = hwmon_in_attr_templates,
	[hwmon_curr] = hwmon_curr_attr_templates,
	[hwmon_power] = hwmon_power_attr_templates,
	[hwmon_energy] = hwmon_energy_attr_templates,
	[hwmon_humidity] = hwmon_humidity_attr_templates,
	[hwmon_fan] = hwmon_fan_attr_templates,
	[hwmon_pwm] = hwmon_pwm_attr_templates,
};

static const int __templates_size[] = {
	[hwmon_chip] = ARRAY_SIZE(hwmon_chip_attrs),
	[hwmon_temp] = ARRAY_SIZE(hwmon_temp_attr_templates),
	[hwmon_in] = ARRAY_SIZE(hwmon_in_attr_templates),
	[hwmon_curr] = ARRAY_SIZE(hwmon_curr_attr_templates),
	[hwmon_power] = ARRAY_SIZE(hwmon_power_attr_templates),
	[hwmon_energy] = ARRAY_SIZE(hwmon_energy_attr_templates),
	[hwmon_humidity] = ARRAY_SIZE(hwmon_humidity_attr_templates),
	[hwmon_fan] = ARRAY_SIZE(hwmon_fan_attr_templates),
	[hwmon_pwm] = ARRAY_SIZE(hwmon_pwm_attr_templates),
};

static int hwmon_num_channel_attrs(const struct hwmon_channel_info *info)
{
	int i, n;

	for (i = n = 0; info->config[i]; i++)
		n += hweight32(info->config[i]);

	return n;
}

static int hwmon_genattrs(struct device *dev,
			  const void *drvdata,
			  struct attribute **attrs,
			  const struct hwmon_ops *ops,
			  const struct hwmon_channel_info *info)
{
	const char * const *templates;
	int template_size;
	int i, aindex = 0;

	if (info->type >= ARRAY_SIZE(__templates))
		return -EINVAL;

	templates = __templates[info->type];
	template_size = __templates_size[info->type];

	for (i = 0; info->config[i]; i++) {
		u32 attr_mask = info->config[i];
		u32 attr;

		while (attr_mask) {
			struct attribute *a;

			attr = __ffs(attr_mask);
			attr_mask &= ~BIT(attr);
			if (attr >= template_size)
				return -EINVAL;
			a = hwmon_genattr(dev, drvdata, info->type, attr, i,
					  templates[attr], ops);
			if (IS_ERR(a)) {
				if (PTR_ERR(a) != -ENOENT)
					return PTR_ERR(a);
				continue;
			}
			attrs[aindex++] = a;
		}
	}
	return aindex;
}

static struct attribute **
__hwmon_create_attrs(struct device *dev, const void *drvdata,
		     const struct hwmon_chip_info *chip)
{
	int ret, i, aindex = 0, nattrs = 0;
	struct attribute **attrs;

	for (i = 0; chip->info[i]; i++)
		nattrs += hwmon_num_channel_attrs(chip->info[i]);

	if (nattrs == 0)
		return ERR_PTR(-EINVAL);

	attrs = devm_kcalloc(dev, nattrs + 1, sizeof(*attrs), GFP_KERNEL);
	if (!attrs)
		return ERR_PTR(-ENOMEM);

	for (i = 0; chip->info[i]; i++) {
		ret = hwmon_genattrs(dev, drvdata, &attrs[aindex], chip->ops,
				     chip->info[i]);
		if (ret < 0)
			return ERR_PTR(ret);
		aindex += ret;
	}

	return attrs;
}

static struct device *
__hwmon_device_register(struct device *dev, const char *name, void *drvdata,
			const struct hwmon_chip_info *chip,
			const struct attribute_group **groups,
			struct class *hwmon_class, int id)
{
	struct hwmon_device *hwdev;
	struct device *hdev;
	int i, j, err;

	/* Complain about invalid characters in hwmon name attribute */
	if (name && (!strlen(name) || strpbrk(name, "-* \t\n")))
		dev_warn(dev,
			 "hwmon: '%s' is not a valid name attribute, please fix\n",
			 name);

	hwdev = kzalloc(sizeof(*hwdev), GFP_KERNEL);
	if (hwdev == NULL)
		return ERR_PTR(-ENOMEM);

	hdev = &hwdev->dev;

	if (chip) {
		struct attribute **attrs;
		int ngroups = 2; /* terminating NULL plus &hwdev->groups */

		if (groups)
			for (i = 0; groups[i]; i++)
				ngroups++;

		hwdev->groups = devm_kcalloc(dev, ngroups, sizeof(*groups),
					     GFP_KERNEL);
		if (!hwdev->groups) {
			err = -ENOMEM;
			goto free_hwmon;
		}

		attrs = __hwmon_create_attrs(dev, drvdata, chip);
		if (IS_ERR(attrs)) {
			err = PTR_ERR(attrs);
			goto free_hwmon;
		}

		hwdev->group.attrs = attrs;
		ngroups = 0;
		hwdev->groups[ngroups++] = &hwdev->group;

		if (groups) {
			for (i = 0; groups[i]; i++)
				hwdev->groups[ngroups++] = groups[i];
		}

		hdev->groups = hwdev->groups;
	} else {
		hdev->groups = groups;
	}

	hwdev->name = name;
	hdev->class = hwmon_class;
	hdev->parent = dev;
	hdev->of_node = dev ? dev->of_node : NULL;
	hwdev->chip = chip;
	dev_set_drvdata(hdev, drvdata);
	dev_set_name(hdev, HWMON_ID_FORMAT, id);
	err = device_register(hdev);
	if (err)
		goto free_hwmon;

	if (dev && chip && chip->ops->read &&
	    chip->info[0]->type == hwmon_chip &&
	    (chip->info[0]->config[0] & HWMON_C_REGISTER_TZ)) {
		const struct hwmon_channel_info **info = chip->info;

		for (i = 1; info[i]; i++) {
			if (info[i]->type != hwmon_temp)
				continue;

			for (j = 0; info[i]->config[j]; j++) {
				if (!chip->ops->is_visible(drvdata, hwmon_temp,
							   hwmon_temp_input, j))
					continue;
				if (info[i]->config[j] & HWMON_T_INPUT) {
					err = hwmon_thermal_add_sensor(dev,
								hwdev, j);
					if (err)
						goto free_device;
				}
			}
		}
	}

	return hdev;

free_device:
	device_unregister(hdev);
free_hwmon:
	kfree(hwdev);
	return ERR_PTR(err);
}

struct device *
hwmon_device_register_with_info(struct device *dev, const char *name,
				void *drvdata,
				const struct hwmon_chip_info *chip,
				const struct attribute_group **extra_groups)
{
	struct device *dummy_hwmon_dev;
	struct class *hwmon_class;
	int id;

	if (!name)
		return ERR_PTR(-EINVAL);

	if (chip && (!chip->ops || !chip->ops->is_visible || !chip->info))
		return ERR_PTR(-EINVAL);

	/*
	 * Dummy hwmon device is created for getting a pointer to the hwmon
	 * class and for allocating a hwmon_ida index. The class and index are
	 * later used by the actual hwmon device.
	 * The dummy device is then released using a direct call to
	 * device_unregister(), so the index is kept allocated.
	 * A call to hwmon_device_unregister() with the actual device will free
	 * the hwmon_ida index.
	 */
	dummy_hwmon_dev = hwmon_device_register(dev);
	if (IS_ERR(dummy_hwmon_dev))
		return dummy_hwmon_dev;

	hwmon_class = dummy_hwmon_dev->class;
	if (!hwmon_class)
		return ERR_PTR(-EINVAL);

	if (sscanf(dev_name(dummy_hwmon_dev), HWMON_ID_FORMAT, &id) != 1)
		return ERR_PTR(-ENXIO);

	device_unregister(dummy_hwmon_dev);

	return __hwmon_device_register(dev, name, drvdata, chip, extra_groups,
					hwmon_class, id);
}
#endif /* !_HAS_HWMON_DEVICE_REGISTER_WITH_INFO */

#ifndef _HAS_GEN_POOL_CHUNK_WITH_ATOMIC_LONG
static inline size_t chunk_size(const struct gen_pool_chunk *chunk)
{
	return chunk->end_addr - chunk->start_addr + 1;
}

static int set_bits_ll(unsigned long *addr, unsigned long mask_to_set)
{
	unsigned long val, nval;

	nval = *addr;
	do {
		val = nval;
		if (val & mask_to_set)
			return -EBUSY;
		cpu_relax();
	} while ((nval = cmpxchg(addr, val, val | mask_to_set)) != val);

	return 0;
}

static int clear_bits_ll(unsigned long *addr, unsigned long mask_to_clear)
{
	unsigned long val, nval;

	nval = *addr;
	do {
		val = nval;
		if ((val & mask_to_clear) != mask_to_clear)
			return -EBUSY;
		cpu_relax();
	} while ((nval = cmpxchg(addr, val, val & ~mask_to_clear)) != val);

	return 0;
}

static int bitmap_set_ll(unsigned long *map, int start, int nr)
{
	unsigned long *p = map + BIT_WORD(start);
	const int size = start + nr;
	int bits_to_set = BITS_PER_LONG - (start % BITS_PER_LONG);
	unsigned long mask_to_set = BITMAP_FIRST_WORD_MASK(start);

	while (nr - bits_to_set >= 0) {
		if (set_bits_ll(p, mask_to_set))
			return nr;
		nr -= bits_to_set;
		bits_to_set = BITS_PER_LONG;
		mask_to_set = ~0UL;
		p++;
	}
	if (nr) {
		mask_to_set &= BITMAP_LAST_WORD_MASK(size);
		if (set_bits_ll(p, mask_to_set))
			return nr;
	}

	return 0;
}

static int bitmap_clear_ll(unsigned long *map, int start, int nr)
{
	unsigned long *p = map + BIT_WORD(start);
	const int size = start + nr;
	int bits_to_clear = BITS_PER_LONG - (start % BITS_PER_LONG);
	unsigned long mask_to_clear = BITMAP_FIRST_WORD_MASK(start);

	while (nr - bits_to_clear >= 0) {
		if (clear_bits_ll(p, mask_to_clear))
			return nr;
		nr -= bits_to_clear;
		bits_to_clear = BITS_PER_LONG;
		mask_to_clear = ~0UL;
		p++;
	}
	if (nr) {
		mask_to_clear &= BITMAP_LAST_WORD_MASK(size);
		if (clear_bits_ll(p, mask_to_clear))
			return nr;
	}

	return 0;
}

struct gen_pool *hl_gen_pool_create(int min_alloc_order, int nid)
{
	struct gen_pool *pool;

	pool = kmalloc_node(sizeof(struct gen_pool), GFP_KERNEL, nid);
	if (pool != NULL) {
		spin_lock_init(&pool->lock);
		INIT_LIST_HEAD(&pool->chunks);
		pool->min_alloc_order = min_alloc_order;
		pool->algo = hl_gen_pool_first_fit;
		pool->data = NULL;
	}
	return pool;
}

int hl_gen_pool_add_virt(struct gen_pool *pool, unsigned long virt,
		phys_addr_t phys, size_t size, int nid)
{
	struct gen_pool_chunk *chunk;
	int nbits = size >> pool->min_alloc_order;
	int nbytes = sizeof(struct gen_pool_chunk) +
				BITS_TO_LONGS(nbits) * sizeof(long);

	chunk = kzalloc_node(nbytes, GFP_KERNEL, nid);
	if (unlikely(chunk == NULL))
		return -ENOMEM;

	chunk->phys_addr = phys;
	chunk->start_addr = virt;
	chunk->end_addr = virt + size - 1;
	atomic_long_set(&chunk->avail, size);

	spin_lock(&pool->lock);
	list_add_rcu(&chunk->next_chunk, &pool->chunks);
	spin_unlock(&pool->lock);

	return 0;
}

phys_addr_t hl_gen_pool_virt_to_phys(struct gen_pool *pool, unsigned long addr)
{
	struct gen_pool_chunk *chunk;
	phys_addr_t paddr = -1;

	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &pool->chunks, next_chunk) {
		if (addr >= chunk->start_addr && addr <= chunk->end_addr) {
			paddr = chunk->phys_addr + (addr - chunk->start_addr);
			break;
		}
	}
	rcu_read_unlock();

	return paddr;
}

void hl_gen_pool_destroy(struct gen_pool *pool)
{
	struct list_head *_chunk, *_next_chunk;
	struct gen_pool_chunk *chunk;
	int order = pool->min_alloc_order;
	int bit, end_bit;

	list_for_each_safe(_chunk, _next_chunk, &pool->chunks) {
		chunk = list_entry(_chunk, struct gen_pool_chunk, next_chunk);
		list_del(&chunk->next_chunk);

		end_bit = chunk_size(chunk) >> order;
		bit = find_next_bit(chunk->bits, end_bit, 0);
		WARN_ON(bit < end_bit);

		kfree(chunk);
	}
	kfree(pool);
}

unsigned long hl_gen_pool_alloc(struct gen_pool *pool, size_t size)
{
	return gen_pool_alloc_algo(pool, size, pool->algo, pool->data);
}

unsigned long hl_gen_pool_alloc_algo(struct gen_pool *pool, size_t size,
		genpool_algo_t algo, void *data)
{
	struct gen_pool_chunk *chunk;
	unsigned long addr = 0;
	int order = pool->min_alloc_order;
	int nbits, start_bit, end_bit, remain;

#ifndef CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG
	WARN_ON(in_nmi());
#endif

	if (size == 0)
		return 0;

	nbits = (size + (1UL << order) - 1) >> order;
	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &pool->chunks, next_chunk) {
		if (size > atomic_long_read(&chunk->avail))
			continue;

		start_bit = 0;
		end_bit = chunk_size(chunk) >> order;
retry:
		start_bit = algo(chunk->bits, end_bit, start_bit,
				 nbits, data, pool);
		if (start_bit >= end_bit)
			continue;
		remain = bitmap_set_ll(chunk->bits, start_bit, nbits);
		if (remain) {
			remain = bitmap_clear_ll(chunk->bits, start_bit,
						 nbits - remain);
			WARN_ON(remain);
			goto retry;
		}

		addr = chunk->start_addr + ((unsigned long)start_bit << order);
		size = nbits << order;
		atomic_long_sub(size, &chunk->avail);
		break;
	}
	rcu_read_unlock();
	return addr;
}

void hl_gen_pool_free(struct gen_pool *pool, unsigned long addr, size_t size)
{
	struct gen_pool_chunk *chunk;
	int order = pool->min_alloc_order;
	int start_bit, nbits, remain;

#ifndef CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG
	WARN_ON(in_nmi());
#endif

	nbits = (size + (1UL << order) - 1) >> order;
	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &pool->chunks, next_chunk) {
		if (addr >= chunk->start_addr && addr <= chunk->end_addr) {
			WARN_ON(addr + size - 1 > chunk->end_addr);
			start_bit = (addr - chunk->start_addr) >> order;
			remain = bitmap_clear_ll(chunk->bits, start_bit, nbits);
			WARN_ON(remain);
			size = nbits << order;
			atomic_long_add(size, &chunk->avail);
			rcu_read_unlock();
			return;
		}
	}
	rcu_read_unlock();
	WARN_ON(1);
}

void hl_gen_pool_for_each_chunk(struct gen_pool *pool,
	void (*func)(struct gen_pool *pool, struct gen_pool_chunk *chunk,
			void *data),
	void *data)
{

	struct gen_pool_chunk *chunk;

	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &(pool)->chunks, next_chunk)
		func(pool, chunk, data);
	rcu_read_unlock();
}

unsigned long hl_gen_pool_first_fit(unsigned long *map, unsigned long size,
		unsigned long start, unsigned int nr, void *data,
		struct gen_pool *pool)
{
	return bitmap_find_next_zero_area(map, size, start, nr, 0);
}

void hl_gen_pool_set_algo(struct gen_pool *pool, genpool_algo_t algo,
		void *data)
{
	rcu_read_lock();

	pool->algo = algo;
	if (!pool->algo)
		pool->algo = hl_gen_pool_first_fit;

	pool->data = data;

	rcu_read_unlock();
}

#ifdef _HAS_GENPOOL_ALGO_T_WITH_START_ADDR
unsigned long hl_gen_pool_best_fit(unsigned long *map, unsigned long size,
		unsigned long start, unsigned int nr, void *data,
		struct gen_pool *pool, unsigned long start_addr)
{
	unsigned long start_bit = size;
	unsigned long len = size + 1;
	unsigned long index;

	index = bitmap_find_next_zero_area(map, size, start, nr, 0);

	while (index < size) {
		unsigned long next_bit = find_next_bit(map, size, index + nr);

		if ((next_bit - index) < len) {
			len = next_bit - index;
			start_bit = index;
			if (len == nr)
				return start_bit;
		}
		index = bitmap_find_next_zero_area(map, size,
						   next_bit + 1, nr, 0);
	}

	return start_bit;
}
#else
unsigned long hl_gen_pool_best_fit(unsigned long *map, unsigned long size,
		unsigned long start, unsigned int nr, void *data,
		struct gen_pool *pool)
{
	unsigned long start_bit = size;
	unsigned long len = size + 1;
	unsigned long index;

	index = bitmap_find_next_zero_area(map, size, start, nr, 0);

	while (index < size) {
		int next_bit = find_next_bit(map, size, index + nr);

		if ((next_bit - index) < len) {
			len = next_bit - index;
			start_bit = index;
			if (len == nr)
				return start_bit;
		}
		index = bitmap_find_next_zero_area(map, size,
						   next_bit + 1, nr, 0);
	}

	return start_bit;
}
#endif /* _HAS_GENPOOL_ALGO_T_WITH_START_ADDR */
#endif /* !_HAS_GEN_POOL_CHUNK_WITH_ATOMIC_LONG */

#ifndef _HAS_GEN_POOL_DMA_ALLOC
void *gen_pool_dma_alloc(struct gen_pool *pool, size_t size, dma_addr_t *dma)
{
	unsigned long vaddr;

	if (!pool)
		return NULL;

	vaddr = gen_pool_alloc(pool, size);
	if (!vaddr)
		return NULL;

	if (dma)
		*dma = gen_pool_virt_to_phys(pool, vaddr);

	return (void *)vaddr;
}
#endif

#ifndef _HAS_BITMAP_FIND_NEXT_ZERO_AREA_OFF
unsigned long bitmap_find_next_zero_area_off(unsigned long *map,
					     unsigned long size,
					     unsigned long start,
					     unsigned int nr,
					     unsigned long align_mask,
					     unsigned long align_offset)
{
	unsigned long index, end, i;
again:
	index = find_next_zero_bit(map, size, start);

	/* Align allocation */
	index = __ALIGN_MASK(index + align_offset, align_mask) - align_offset;

	end = index + nr;
	if (end > size)
		return end;
	i = find_next_bit(map, end, index);
	if (i < end) {
		start = i + 1;
		goto again;
	}
	return index;
}
#endif /* _HAS_BITMAP_FIND_NEXT_ZERO_AREA_OFF */

#ifndef _HAS_GEN_POOL_DMA_ALLOC_ALGO
void *gen_pool_dma_alloc_algo(struct gen_pool *pool, size_t size,
		dma_addr_t *dma, genpool_algo_t algo, void *data)
{
	unsigned long vaddr;

	if (!pool)
		return NULL;

	vaddr = gen_pool_alloc_algo(pool, size, algo, data);
	if (!vaddr)
		return NULL;

	if (dma)
		*dma = gen_pool_virt_to_phys(pool, vaddr);

	return (void *)vaddr;
}
#endif /* _HAS_GEN_POOL_DMA_ALLOC_ALGO */

#ifndef _HAS_GEN_POOL_DMA_ALLOC_ALIGN
#ifndef _HAS_GEN_POOL_FIRST_FIT_ALIGN
#ifdef _HAS_GENPOOL_ALGO_T_WITH_START_ADDR
unsigned long gen_pool_first_fit_align(unsigned long *map, unsigned long size,
		unsigned long start, unsigned int nr, void *data,
		struct gen_pool *pool, unsigned long start_addr)
{
	struct genpool_data_align *alignment;
	unsigned long align_mask, align_off;
	int order;

	alignment = data;
	order = pool->min_alloc_order;
	align_mask = ((alignment->align + (1UL << order) - 1) >> order) - 1;
	align_off = (start_addr & (alignment->align - 1)) >> order;

	return bitmap_find_next_zero_area_off(map, size, start, nr,
					      align_mask, align_off);
}
#else
unsigned long gen_pool_first_fit_align(unsigned long *map, unsigned long size,
		unsigned long start, unsigned int nr, void *data,
		struct gen_pool *pool)
{
	struct genpool_data_align *alignment;
	unsigned long align_mask;
	int order;

	alignment = data;
	order = pool->min_alloc_order;
	align_mask = ((alignment->align + (1UL << order) - 1) >> order) - 1;
	return bitmap_find_next_zero_area(map, size, start, nr, align_mask);
}
#endif /* _HAS_GENPOOL_ALGO_T_WITH_START_ADDR */
#endif /* _HAS_GEN_POOL_FIRST_FIT_ALIGN */

void *gen_pool_dma_alloc_align(struct gen_pool *pool, size_t size,
		dma_addr_t *dma, int align)
{
	struct genpool_data_align data = { .align = align };

	return gen_pool_dma_alloc_algo(pool, size, dma,
			gen_pool_first_fit_align, &data);
}
#endif /* _HAS_GEN_POOL_DMA_ALLOC_ALIGN */

#ifndef _HAS_GEN_POOL_DMA_ZALLOC
void *gen_pool_dma_zalloc(struct gen_pool *pool, size_t size, dma_addr_t *dma)
{
	void *vaddr = gen_pool_dma_alloc(pool, size, dma);

	if (vaddr)
		memset(vaddr, 0, size);

	return vaddr;
}
#endif

#ifndef _HAS_GEN_POOL_DMA_ZALLOC_ALIGN
void *gen_pool_dma_zalloc_align(struct gen_pool *pool, size_t size,
		dma_addr_t *dma, int align)
{
	struct genpool_data_align data = { .align = align };

	return gen_pool_dma_zalloc_algo(pool, size, dma,
			gen_pool_first_fit_align, &data);
}
#endif

#ifndef _HAS_GEN_POOL_DMA_ZALLOC_ALGO
void *gen_pool_dma_zalloc_algo(struct gen_pool *pool, size_t size,
		dma_addr_t *dma, genpool_algo_t algo, void *data)
{
	void *vaddr = gen_pool_dma_alloc_algo(pool, size, dma, algo, data);

	if (vaddr)
		memset(vaddr, 0, size);

	return vaddr;
}
#endif

#ifndef _HAS_CDEV_DEVICE_ADD
void cdev_set_parent(struct cdev *p, struct kobject *kobj)
{
	WARN_ON(!kobj->state_initialized);
	p->kobj.parent = kobj;
}

int cdev_device_add(struct cdev *cdev, struct device *dev)
{
	int rc = 0;

	if (dev->devt) {
		cdev_set_parent(cdev, &dev->kobj);

		rc = cdev_add(cdev, dev->devt, 1);
		if (rc)
			return rc;
	}

	rc = device_add(dev);
	if (rc)
		cdev_del(cdev);

	return rc;
}

void cdev_device_del(struct cdev *cdev, struct device *dev)
{
	device_del(dev);
	if (dev->devt)
		cdev_del(cdev);
}

#endif /* !_HAS_CDEV_DEVICE_ADD */

#ifndef _HAS_PCI_CONFIGURE_EXTENDED_TAGS
int hl_pci_configure_extended_tags(struct pci_dev *pdev)
{
	u32 cap;
	u16 ctl;
	int rc;

	rc = pcie_capability_read_dword(pdev, PCI_EXP_DEVCAP, &cap);
	if (rc) {
		pci_err(pdev,
			"Failed to read PCIe device capabilities register\n");
		return rc;
	}

	if (!(cap & PCI_EXP_DEVCAP_EXT_TAG))
		return 0;

	rc = pcie_capability_read_word(pdev, PCI_EXP_DEVCTL, &ctl);
	if (rc) {
		pci_err(pdev, "Failed to read PCIe device control register\n");
		return rc;
	}

	if (ctl & PCI_EXP_DEVCTL_EXT_TAG)
		return 0;

	pci_info(pdev, "Enabling PCIe extended tags\n");
	rc = pcie_capability_set_word(pdev, PCI_EXP_DEVCTL,
					PCI_EXP_DEVCTL_EXT_TAG);
	if (rc)
		pci_err(pdev,
			"Failed to write to PCIe device control register\n");

	return rc;
}
#endif

#ifndef _HAS_STRSCPY
#include <asm/word-at-a-time.h>

static unsigned long read_word_at_a_time(const void *addr)
{
	return *(unsigned long *)addr;
}

ssize_t strscpy(char *dest, const char *src, size_t count)
{
	const struct word_at_a_time constants = WORD_AT_A_TIME_CONSTANTS;
	size_t max = count;
	long res = 0;

	if (count == 0 || WARN_ON_ONCE(count > INT_MAX))
		return -E2BIG;

#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	/*
	 * If src is unaligned, don't cross a page boundary,
	 * since we don't know if the next page is mapped.
	 */
	if ((long)src & (sizeof(long) - 1)) {
		size_t limit = PAGE_SIZE - ((long)src & (PAGE_SIZE - 1));

		if (limit < max)
			max = limit;
	}
#else
	/* If src or dest is unaligned, don't do word-at-a-time. */
	if (((long) dest | (long) src) & (sizeof(long) - 1))
		max = 0;
#endif

	while (max >= sizeof(unsigned long)) {
		unsigned long c, data;

		c = read_word_at_a_time(src+res);
		if (has_zero(c, &data, &constants)) {
			data = prep_zero_mask(c, data, &constants);
			data = create_zero_mask(data);
			*(unsigned long *)(dest+res) = c & zero_bytemask(data);
			return res + find_zero(data);
		}
		*(unsigned long *)(dest+res) = c;
		res += sizeof(unsigned long);
		count -= sizeof(unsigned long);
		max -= sizeof(unsigned long);
	}

	while (count) {
		char c;

		c = src[res];
		dest[res] = c;
		if (!c)
			return res;
		res++;
		count--;
	}

	/* Hit buffer length without finding a NUL; force NUL-termination. */
	if (res)
		dest[res-1] = '\0';

	return -E2BIG;
}
#endif

#ifndef _HAS_DMA_MAP_RESOURCE

dma_addr_t dma_map_resource(struct device *dev, phys_addr_t phys_addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	return DMA_MAPPING_ERROR;
}

void dma_unmap_resource(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir, unsigned long attrs)
{
}

#endif

#ifndef _HAS_DMA_MAP_SGTABLE
int dma_map_sgtable(struct device *dev, struct sg_table *sgt,
		    enum dma_data_direction dir, unsigned long attrs)
{
	int nents;

	/*
	 * original call is to __dma_map_sg_attrs which exist in newer kernels but contains
	 * the same functionality
	 */
	nents = dma_map_sg_attrs(dev, sgt->sgl, sgt->orig_nents, dir, attrs);
	if (nents < 0)
		return nents;
	sgt->nents = nents;
	return 0;
}
#endif

#ifndef _HAS_BITMAP_ZALLOC
unsigned long *bitmap_zalloc(unsigned int nbits, gfp_t flags)
{
	return kmalloc_array(BITS_TO_LONGS(nbits), sizeof(unsigned long), flags | __GFP_ZERO);
}
#endif

#ifndef _HAS_BITMAP_FREE
void bitmap_free(const unsigned long *bitmap)
{
	kfree(bitmap);
}
#endif

#ifndef _HAS_MEMSET32
/**
 * memset32() - Fill a memory area with a uint32_t
 * @s: Pointer to the start of the area.
 * @v: The value to fill the area with
 * @count: The number of values to store
 *
 * Differs from memset() in that it fills with a uint32_t instead
 * of a byte.  Remember that @count is the number of uint32_ts to
 * store, not the number of bytes.
 */
void *memset32(uint32_t *s, uint32_t v, size_t count)
{
	uint32_t *xs = s;

	while (count--)
		*xs++ = v;
	return s;
}
#endif /* _HAS_MEMSET32 */

#ifdef _HAS_CLASS_CREATE_WITH_MODULE
struct class *hl_class_create(const char *name)
{
	static struct lock_class_key __key;

	return __class_create(THIS_MODULE, name, &__key);
}
#endif
