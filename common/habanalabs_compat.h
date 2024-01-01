/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HABANALABS_COMPAT_H_
#define HABANALABS_COMPAT_H_

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/sizes.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/poll.h>
#include <linux/etherdevice.h>
#ifdef _HAS_GEN_POOL_CHUNK_WITH_ATOMIC_LONG
#include <linux/genalloc.h>
#endif
#ifdef _HAS_MMGRAB
#include <linux/sched/mm.h>
#endif
#ifdef _HAS_PEER2PEER
#include <linux/dma-resv.h>
#endif
#ifndef _HAS_DEVICE_IOMMU_MAPPED
#include <linux/iommu.h>
#include <linux/pci.h>
#endif
#ifndef _HAS_RANDOM_U32
#include <linux/random.h>
#endif
#ifdef _HAS_DMA_MAP_RESOURCE_WITH_DMA_ATTRS
#include <linux/dma-attrs.h>
#endif

#ifndef SZ_16G
#define SZ_16G				_AC(0x400000000, ULL)
#endif

#ifndef _HAS_MODULE_IMPORT_NS
#define MODULE_IMPORT_NS(ns)
#endif

#ifndef _HAS_U64_TO_USERPTR
#define u64_to_user_ptr(x) (		\
{					\
	typecheck(u64, (x));		\
	(void __user *)(uintptr_t)(x);	\
}					\
)
#endif

#ifndef _HAS_CORESIGHT_H
#define CORESIGHT_UNLOCK	0xc5acce55
#endif

#ifndef _HAS_DIV_ROUND_DOWN_ULL
#define DIV_ROUND_DOWN_ULL(ll, d) \
	({ unsigned long long _tmp = (ll); do_div(_tmp, d); _tmp; })
#endif

#ifndef _HAS_DIV_ROUND_UP_ULL
#define DIV_ROUND_UP_ULL(ll, d) \
	DIV_ROUND_DOWN_ULL((unsigned long long)(ll) + (d) - 1, (d))
#endif

#ifndef _HAS_CAST_IN_IS_ERR_VALUE
#undef IS_ERR_VALUE
#define IS_ERR_VALUE(x) \
	unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)
#endif

#ifndef _HAS_FIX_IN_IOC_TYPECHECK
#undef _IOC_TYPECHECK
#ifdef __CHECKER__
#define _IOC_TYPECHECK(t) (sizeof(t))
#else
/* provoke compile error for invalid uses of size argument */
extern unsigned int __invalid_size_argument_for_IOC;
#define _IOC_TYPECHECK(t) \
	((sizeof(t) == sizeof(t[1]) && \
	  sizeof(t) < (1 << _IOC_SIZEBITS)) ? \
	  sizeof(t) : __invalid_size_argument_for_IOC)
#endif
#endif /* !_HAS_FIX_IN_IOC_TYPECHECK */

#ifndef ALIGN_DOWN
#define ALIGN_DOWN(x, a)	__ALIGN_KERNEL((x) - ((a) - 1), (a))
#endif

#ifndef dev_dbg_once
#ifdef CONFIG_PRINTK
#define dev_level_once(dev_level, dev, fmt, ...)			\
do {									\
	static bool __print_once __read_mostly;				\
									\
	if (!__print_once) {						\
		__print_once = true;					\
		dev_level(dev, fmt, ##__VA_ARGS__);			\
	}								\
} while (0)
#else
#define dev_level_once(dev_level, dev, fmt, ...)			\
do {									\
	if (0)								\
		dev_level(dev, fmt, ##__VA_ARGS__);			\
} while (0)
#endif

#define dev_dbg_once(dev, fmt, ...)					\
	dev_level_once(dev_dbg, dev, fmt, ##__VA_ARGS__)
#endif /* !dev_dbg_once */

#ifndef readq
static inline __u64 lo_hi_readq(const volatile void __iomem *addr)
{
	const volatile u32 __iomem *p = addr;
	u32 low, high;

	low = readl(p);
	high = readl(p + 1);

	return low + ((u64)high << 32);
}

#define readq lo_hi_readq
#endif /* !readq */

#ifndef writeq
static inline void lo_hi_writeq(__u64 val, volatile void __iomem *addr)
{
	writel(val, addr);
	writel(val >> 32, addr + 4);
}

#define writeq lo_hi_writeq
#endif /* !writeq */

#ifndef _LOWER_32_BITS_USES_MASK
#undef lower_32_bits
#define lower_32_bits(n) ((u32)((n) & 0xffffffff))
#endif

#ifndef _HAS_DMA_POOL_ZALLOC
static inline void *dma_pool_zalloc(struct dma_pool *pool, gfp_t mem_flags,
				    dma_addr_t *handle)
{
	return dma_pool_alloc(pool, mem_flags | __GFP_ZERO, handle);
}
#endif

#ifndef _HAS_KREF_READ
static inline unsigned int kref_read(const struct kref *kref)
{
	return atomic_read(&kref->refcount);
}
#endif

#ifndef _HAS_DEVICE_ADD_GROUPS
int device_add_groups(struct device *dev,
			const struct attribute_group **groups);
void device_remove_groups(struct device *dev,
				const struct attribute_group **groups);
#endif /* !_HAS_DEVICE_ADD_GROUPS */

#ifndef _HAS_DEVICE_ADD_GROUP
static inline int device_add_group(struct device *dev, const struct attribute_group *grp)
{
	const struct attribute_group *groups[] = { grp, NULL };

	return device_add_groups(dev, groups);
}

static inline void device_remove_group(struct device *dev, const struct attribute_group *grp)
{
	const struct attribute_group *groups[] = { grp, NULL };

	return device_remove_groups(dev, groups);
}
#endif /* !_HAS_DEVICE_ADD_GROUP */

#ifndef _HAS_PCI_IOREMAP_WC_BAR
void __iomem *pci_ioremap_wc_bar(struct pci_dev *pdev, int bar);
#endif

#ifndef _HAS_KVMALLOC_ARRAY
void *kvmalloc_node(size_t size, gfp_t flags, int node);

static inline void *kvmalloc(size_t size, gfp_t flags)
{
	return kvmalloc_node(size, flags, NUMA_NO_NODE);
}

static inline void *kvmalloc_array(size_t n, size_t size, gfp_t flags)
{
	if (size != 0 && n > SIZE_MAX / size)
		return NULL;

	return kvmalloc(n * size, flags);
}
#endif /* !_HAS_KVMALLOC_ARRAY */

#ifndef _HAS_KVCALLOC

static inline void *kvcalloc(size_t n, size_t size, gfp_t flags)
{
	return kvmalloc_array(n, size, flags | __GFP_ZERO);
}

#endif /* !_HAS_KVCALLOC */

#ifndef _HAS_PCI_ALLOC_IRQ_VECTORS
#define PCI_IRQ_LEGACY		(1 << 0)
#define PCI_IRQ_MSI		(1 << 1)
#define PCI_IRQ_MSIX		(1 << 2)

#ifdef CONFIG_PCI_MSI
int pci_alloc_irq_vectors(struct pci_dev *dev, unsigned int min_vecs,
				unsigned int max_vecs, unsigned int flags);
void pci_free_irq_vectors(struct pci_dev *dev);
int pci_irq_vector(struct pci_dev *dev, unsigned int nr);
#else
int pci_alloc_irq_vectors(struct pci_dev *dev, unsigned int min_vecs,
				unsigned int max_vecs, unsigned int flags)
{
	if ((flags & PCI_IRQ_LEGACY) && min_vecs == 1 && dev->irq)
		return 1;
	return -ENOSPC;
}

static inline void pci_free_irq_vectors(struct pci_dev *dev)
{
}

static inline int pci_irq_vector(struct pci_dev *dev, unsigned int nr)
{
	if (WARN_ON_ONCE(nr > 0))
		return -EINVAL;
	return dev->irq;
}
#endif /* CONFIG_PCI_MSI */
#endif /* !_HAS_PCI_ALLOC_IRQ_VECTORS */

#ifndef _HAS_MMAP_READ_LOCK
static inline void mmap_read_lock(struct mm_struct *mm)
{
	down_read(&mm->mmap_sem);
}
static inline void mmap_read_unlock(struct mm_struct *mm)
{
	up_read(&mm->mmap_sem);
}
#endif /* _HAS_MMAP_READ_LOCK */

#ifndef _HAS_MMGRAB
static inline void mmgrab(struct mm_struct *mm)
{
	atomic_inc(&mm->mm_count);
}
/* unlike mmgrab, mmdrop is defined since day 1 */
#endif /* _HAS_MMGRAB */

#ifndef _HAS_JIFFIES64_TO_MSEC
static inline u64 jiffies64_to_msecs(const u64 j)
{
#if HZ <= MSEC_PER_SEC && !(MSEC_PER_SEC % HZ)
	return (MSEC_PER_SEC / HZ) * j;
#else
	return div_u64(j * HZ_TO_MSEC_NUM, HZ_TO_MSEC_DEN);
#endif
}
#endif /* _HAS_JIFFIES64_TO_MSEC */

#ifndef _HAS_FRAME_VECTOR
struct frame_vector {
	unsigned int nr_allocated;
	unsigned int nr_frames;
	bool got_ref;
	bool is_pfns;
	void *ptrs[0];
};

struct frame_vector *frame_vector_create(unsigned int nr_frames);
void frame_vector_destroy(struct frame_vector *vec);
int get_vaddr_frames(unsigned long start, unsigned int nr_pfns,
		     unsigned int gup_flags, struct frame_vector *vec);
void put_vaddr_frames(struct frame_vector *vec);
int frame_vector_to_pages(struct frame_vector *vec);
void frame_vector_to_pfns(struct frame_vector *vec);

static inline unsigned int frame_vector_count(struct frame_vector *vec)
{
	return vec->nr_frames;
}

static inline struct page **frame_vector_pages(struct frame_vector *vec)
{
	if (vec->is_pfns) {
		int err = frame_vector_to_pages(vec);

		if (err)
			return ERR_PTR(err);
	}
	return (struct page **)(vec->ptrs);
}

static inline unsigned long *frame_vector_pfns(struct frame_vector *vec)
{
	if (!vec->is_pfns)
		frame_vector_to_pfns(vec);
	return (unsigned long *)(vec->ptrs);
}
#endif /* !_HAS_FRAME_VECTOR */

#ifndef _HAS_HWMON_DEVICE_REGISTER_WITH_INFO
enum hwmon_sensor_types {
	hwmon_chip,
	hwmon_temp,
	hwmon_in,
	hwmon_curr,
	hwmon_power,
	hwmon_energy,
	hwmon_humidity,
	hwmon_fan,
	hwmon_pwm,
	hwmon_max
};

enum hwmon_chip_attributes {
	hwmon_chip_temp_reset_history,
	hwmon_chip_in_reset_history,
	hwmon_chip_curr_reset_history,
	hwmon_chip_power_reset_history,
	hwmon_chip_register_tz,
	hwmon_chip_update_interval,
	hwmon_chip_alarms,
};

#define HWMON_C_REGISTER_TZ		BIT(hwmon_chip_register_tz)

enum hwmon_temp_attributes {
	hwmon_temp_input = 0,
	hwmon_temp_type,
	hwmon_temp_lcrit,
	hwmon_temp_lcrit_hyst,
	hwmon_temp_min,
	hwmon_temp_min_hyst,
	hwmon_temp_max,
	hwmon_temp_max_hyst,
	hwmon_temp_crit,
	hwmon_temp_crit_hyst,
	hwmon_temp_emergency,
	hwmon_temp_emergency_hyst,
	hwmon_temp_alarm,
	hwmon_temp_lcrit_alarm,
	hwmon_temp_min_alarm,
	hwmon_temp_max_alarm,
	hwmon_temp_crit_alarm,
	hwmon_temp_emergency_alarm,
	hwmon_temp_fault,
	hwmon_temp_offset,
	hwmon_temp_label,
	hwmon_temp_lowest,
	hwmon_temp_highest,
	hwmon_temp_reset_history,
};

#define HWMON_T_INPUT		BIT(hwmon_temp_input)

enum hwmon_in_attributes {
	hwmon_in_input,
	hwmon_in_min,
	hwmon_in_max,
	hwmon_in_lcrit,
	hwmon_in_crit,
	hwmon_in_average,
	hwmon_in_lowest,
	hwmon_in_highest,
	hwmon_in_reset_history,
	hwmon_in_label,
	hwmon_in_alarm,
	hwmon_in_min_alarm,
	hwmon_in_max_alarm,
	hwmon_in_lcrit_alarm,
	hwmon_in_crit_alarm,
};

enum hwmon_curr_attributes {
	hwmon_curr_input,
	hwmon_curr_min,
	hwmon_curr_max,
	hwmon_curr_lcrit,
	hwmon_curr_crit,
	hwmon_curr_average,
	hwmon_curr_lowest,
	hwmon_curr_highest,
	hwmon_curr_reset_history,
	hwmon_curr_label,
	hwmon_curr_alarm,
	hwmon_curr_min_alarm,
	hwmon_curr_max_alarm,
	hwmon_curr_lcrit_alarm,
	hwmon_curr_crit_alarm,
};

enum hwmon_power_attributes {
	hwmon_power_average,
	hwmon_power_average_interval,
	hwmon_power_average_interval_max,
	hwmon_power_average_interval_min,
	hwmon_power_average_highest,
	hwmon_power_average_lowest,
	hwmon_power_average_max,
	hwmon_power_average_min,
	hwmon_power_input,
	hwmon_power_input_highest,
	hwmon_power_input_lowest,
	hwmon_power_reset_history,
	hwmon_power_accuracy,
	hwmon_power_cap,
	hwmon_power_cap_hyst,
	hwmon_power_cap_max,
	hwmon_power_cap_min,
	hwmon_power_max,
	hwmon_power_crit,
	hwmon_power_label,
	hwmon_power_alarm,
	hwmon_power_cap_alarm,
	hwmon_power_max_alarm,
	hwmon_power_crit_alarm,
};

enum hwmon_energy_attributes {
	hwmon_energy_input,
	hwmon_energy_label,
};

enum hwmon_humidity_attributes {
	hwmon_humidity_input,
	hwmon_humidity_label,
	hwmon_humidity_min,
	hwmon_humidity_min_hyst,
	hwmon_humidity_max,
	hwmon_humidity_max_hyst,
	hwmon_humidity_alarm,
	hwmon_humidity_fault,
};

enum hwmon_fan_attributes {
	hwmon_fan_input,
	hwmon_fan_label,
	hwmon_fan_min,
	hwmon_fan_max,
	hwmon_fan_div,
	hwmon_fan_pulses,
	hwmon_fan_target,
	hwmon_fan_alarm,
	hwmon_fan_min_alarm,
	hwmon_fan_max_alarm,
	hwmon_fan_fault,
};

enum hwmon_pwm_attributes {
	hwmon_pwm_input,
	hwmon_pwm_enable,
	hwmon_pwm_mode,
	hwmon_pwm_freq,
};

struct hwmon_ops {
	umode_t (*is_visible)(const void *drvdata, enum hwmon_sensor_types type,
			      u32 attr, int channel);
	int (*read)(struct device *dev, enum hwmon_sensor_types type,
		    u32 attr, int channel, long *val);
	int (*read_string)(struct device *dev, enum hwmon_sensor_types type,
		    u32 attr, int channel, const char **str);
	int (*write)(struct device *dev, enum hwmon_sensor_types type,
		     u32 attr, int channel, long val);
};

struct hwmon_channel_info {
	enum hwmon_sensor_types type;
	const u32 *config;
};

struct hwmon_chip_info {
	const struct hwmon_ops *ops;
	const struct hwmon_channel_info **info;
};

struct device *
hwmon_device_register_with_info(struct device *dev,
				const char *name, void *drvdata,
				const struct hwmon_chip_info *info,
				const struct attribute_group **extra_groups);

#else

#ifndef _HAS_HWMON_HWMON_MAX
#define hwmon_max (hwmon_pwm + 1)
#endif /* !_HAS_HWMON_HWMON_MAX */

#endif /* !_HAS_HWMON_DEVICE_REGISTER_WITH_INFO */

#ifndef _HAS_HWMON_HWMON_I_ENABLE

#define hwmon_in_enable	0
#define HWMON_I_ENABLE		BIT(hwmon_in_enable)

#endif /* !_HAS_HWMON_HWMON_I_ENABLE */

#ifndef _HAS_HWMON_HWMON_T_ENABLE

#define hwmon_temp_enable	0
#define HWMON_T_ENABLE		BIT(hwmon_temp_enable)
#define hwmon_curr_enable	0
#define HWMON_C_ENABLE		BIT(hwmon_curr_enable)
#define hwmon_power_enable	0
#define HWMON_P_ENABLE		BIT(hwmon_power_enable)
#define hwmon_fan_enable	0
#define HWMON_F_ENABLE		BIT(hwmon_fan_enable)

#endif /* !_HAS_HWMON_HWMON_T_ENABLE */

#ifndef _HAS_HWMON_HWMON_PWM_ENABLE

#define HWMON_PWM_ENABLE	BIT(hwmon_pwm_enable)

#endif /* !_HAS_HWMON_HWMON_PWM_ENABLE */

#ifndef _HAS_GEN_POOL_CHUNK_WITH_ATOMIC_LONG
#define gen_pool_create		hl_gen_pool_create
#define gen_pool_virt_to_phys	hl_gen_pool_virt_to_phys
#define gen_pool_add_virt	hl_gen_pool_add_virt
#define gen_pool_add		hl_gen_pool_add
#define gen_pool_destroy	hl_gen_pool_destroy
#define gen_pool_alloc		hl_gen_pool_alloc
#define gen_pool_free		hl_gen_pool_free
#define gen_pool_for_each_chunk	hl_gen_pool_for_each_chunk
#define gen_pool_alloc_algo	hl_gen_pool_alloc_algo
#define gen_pool_set_algo	hl_gen_pool_set_algo
#define gen_pool_best_fit	hl_gen_pool_best_fit

struct gen_pool;

typedef unsigned long (*genpool_algo_t)(unsigned long *map,
			unsigned long size,
			unsigned long start,
			unsigned int nr,
			void *data, struct gen_pool *pool);

struct gen_pool {
	spinlock_t lock;
	struct list_head chunks;
	int min_alloc_order;
	genpool_algo_t algo;
	void *data;
};

struct gen_pool_chunk {
	struct list_head next_chunk;
	atomic_long_t avail;
	phys_addr_t phys_addr;
	unsigned long start_addr;
	unsigned long end_addr;
	unsigned long bits[0];
};

struct gen_pool *hl_gen_pool_create(int min_alloc_order, int nid);
phys_addr_t hl_gen_pool_virt_to_phys(struct gen_pool *pool, unsigned long addr);
int hl_gen_pool_add_virt(struct gen_pool *pool, unsigned long virt,
		phys_addr_t phys, size_t size, int nid);

static inline int hl_gen_pool_add(struct gen_pool *pool, unsigned long addr,
					size_t size, int nid)
{
	return hl_gen_pool_add_virt(pool, addr, -1, size, nid);
}

void hl_gen_pool_destroy(struct gen_pool *pool);
unsigned long hl_gen_pool_alloc(struct gen_pool *pool, size_t size);
unsigned long hl_gen_pool_alloc_algo(struct gen_pool *pool, size_t size,
		genpool_algo_t algo, void *data);
void hl_gen_pool_free(struct gen_pool *pool, unsigned long addr, size_t size);
void hl_gen_pool_for_each_chunk(struct gen_pool *pool,
	void (*func)(struct gen_pool *pool, struct gen_pool_chunk *chunk,
			void *data),
	void *data);

unsigned long hl_gen_pool_first_fit(unsigned long *map, unsigned long size,
		unsigned long start, unsigned int nr, void *data,
		struct gen_pool *pool);
void hl_gen_pool_set_algo(struct gen_pool *pool, genpool_algo_t algo,
		void *data);
#ifdef _HAS_GENPOOL_ALGO_T_WITH_START_ADDR
unsigned long hl_gen_pool_best_fit(unsigned long *map, unsigned long size,
		unsigned long start, unsigned int nr, void *data,
		struct gen_pool *pool, unsigned long start_addr);
#else
unsigned long hl_gen_pool_best_fit(unsigned long *map, unsigned long size,
		unsigned long start, unsigned int nr, void *data,
		struct gen_pool *pool);
#endif /* _HAS_GENPOOL_ALGO_T_WITH_START_ADDR */
#endif /* !_HAS_GEN_POOL_CHUNK_WITH_ATOMIC_LONG */

#ifndef _HAS_GEN_POOL_DMA_ALLOC
void *gen_pool_dma_alloc(struct gen_pool *pool, size_t size, dma_addr_t *dma);
#endif

#ifndef _HAS_GENPOOL_DATA_ALIGN_STRUCT
struct genpool_data_align {
	int align;		/* alignment by bytes for starting address */
};
#endif

#ifndef _HAS_GEN_POOL_DMA_ALLOC_ALIGN
#ifndef _HAS_GEN_POOL_FIRST_FIT_ALIGN
#ifdef _HAS_GENPOOL_ALGO_T_WITH_START_ADDR
unsigned long gen_pool_first_fit_align(unsigned long *map, unsigned long size,
		unsigned long start, unsigned int nr, void *data,
		struct gen_pool *pool, unsigned long start_addr);
#else
unsigned long gen_pool_first_fit_align(unsigned long *map, unsigned long size,
		unsigned long start, unsigned int nr, void *data,
		struct gen_pool *pool);
#endif /* _HAS_GENPOOL_ALGO_T_WITH_START_ADDR */
#endif /* _HAS_GEN_POOL_FIRST_FIT_ALIGN */

void *gen_pool_dma_alloc_align(struct gen_pool *pool, size_t size, dma_addr_t *dma, int align);
#endif /* _HAS_GEN_POOL_DMA_ALLOC_ALIGN */

#ifndef _HAS_GEN_POOL_DMA_ALLOC_ALGO
void *gen_pool_dma_alloc_algo(struct gen_pool *pool, size_t size,
				dma_addr_t *dma, genpool_algo_t algo, void *data);
#endif

#ifndef _HAS_GEN_POOL_DMA_ZALLOC
void *gen_pool_dma_zalloc(struct gen_pool *pool, size_t size, dma_addr_t *dma);
#endif

#ifndef _HAS_GEN_POOL_DMA_ZALLOC_ALIGN
void *gen_pool_dma_zalloc_align(struct gen_pool *pool, size_t size, dma_addr_t *dma, int align);
#endif

#ifndef _HAS_GEN_POOL_DMA_ZALLOC_ALGO
void *gen_pool_dma_zalloc_algo(struct gen_pool *pool, size_t size, dma_addr_t *dma,
				genpool_algo_t algo, void *data);
#endif

#ifndef _HAS_CDEV_DEVICE_ADD
void cdev_set_parent(struct cdev *p, struct kobject *kobj);
int cdev_device_add(struct cdev *cdev, struct device *dev);
void cdev_device_del(struct cdev *cdev, struct device *dev);
#endif

#ifndef _HAS_KTIME_GET_RAW_NS
static inline u64 ktime_get_raw_ns(void)
{
	struct timespec now;

	getrawmonotonic(&now);
	return timespec_to_ns(&now);
}
#endif

#ifndef _HAS_BITFIELD_H
#define __BUILD_BUG_ON_NOT_POWER_OF_2(n)	\
	BUILD_BUG_ON(((n) & ((n) - 1)) != 0)

#define __bf_shf(x) (__builtin_ffsll(x) - 1)

#define __BF_FIELD_CHECK(_mask, _reg, _val, _pfx)			\
	({								\
		BUILD_BUG_ON_MSG(!__builtin_constant_p(_mask),		\
				 _pfx "mask is not constant");		\
		BUILD_BUG_ON_MSG(!(_mask), _pfx "mask is zero");	\
		BUILD_BUG_ON_MSG(__builtin_constant_p(_val) ?		\
				 ~((_mask) >> __bf_shf(_mask)) & (_val) : 0, \
				 _pfx "value too large for the field"); \
		BUILD_BUG_ON_MSG((_mask) > (typeof(_reg))~0ull,		\
				 _pfx "type of reg too small for mask"); \
		__BUILD_BUG_ON_NOT_POWER_OF_2((_mask) +			\
					      (1ULL << __bf_shf(_mask))); \
	})

#define FIELD_PREP(_mask, _val)						\
	({								\
		__BF_FIELD_CHECK(_mask, 0ULL, _val, "FIELD_PREP: ");	\
		((typeof(_mask))(_val) << __bf_shf(_mask)) & (_mask);	\
	})

#define FIELD_GET(_mask, _reg)						\
	({								\
		__BF_FIELD_CHECK(_mask, _reg, 0U, "FIELD_GET: ");	\
		(typeof(_mask))(((_reg) & (_mask)) >> __bf_shf(_mask));	\
	})
#endif /* !_HAS_BITFIELD_H */

#ifndef _HAS_OVERFLOW_PROT_IN_GENMASK
#undef GENMASK
#define GENMASK(h, l) \
	(((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))

#undef GENMASK_ULL
#define GENMASK_ULL(h, l) \
	(((~0ULL) - (1ULL << (l)) + 1) & \
	 (~0ULL >> (BITS_PER_LONG_LONG - 1 - (h))))
#endif /* !_HAS_OVERFLOW_PROT_IN_GENMASK */

#ifndef _HAS_STATIC_ASSERT
#define static_assert(expr, ...) __static_assert(expr, ##__VA_ARGS__, #expr)
#define __static_assert(expr, msg, ...) _Static_assert(expr, msg)
#endif

#ifndef pci_err
#define pci_err(pdev, fmt, arg...)	dev_err(&(pdev)->dev, fmt, ##arg)
#endif
#ifndef pci_info
#define pci_info(pdev, fmt, arg...)	dev_info(&(pdev)->dev, fmt, ##arg)
#endif

#ifndef _HAS_PCI_CONFIGURE_EXTENDED_TAGS
int hl_pci_configure_extended_tags(struct pci_dev *pdev);
#endif

#ifndef _HAS_STRSCPY
ssize_t strscpy(char *, const char *, size_t);
#endif

#ifndef __has_attribute
#define __has_attribute(x)			__GCC4_has_attribute_##x
#define __GCC4_has_attribute___fallthrough__	0
#endif

#ifndef fallthrough
#if __has_attribute(__fallthrough__)
#define fallthrough			__attribute__((__fallthrough__))
#else
#define fallthrough			do {} while (0)  /* fallthrough */
#endif
#endif

#ifndef for_each_sgtable_sg
/*
 * Loop over each sg element in the given sg_table object.
 */
#define for_each_sgtable_sg(sgt, sg, i)		\
	for_each_sg((sgt)->sgl, sg, (sgt)->orig_nents, i)
#endif

#ifndef for_each_sgtable_dma_sg
/*
 * Loop over each sg element in the given *DMA mapped* sg_table object.
 * Please use sg_dma_address(sg) and sg_dma_len(sg) to extract DMA addresses
 * of the each element.
 */
#define for_each_sgtable_dma_sg(sgt, sg, i)	\
	for_each_sg((sgt)->sgl, sg, (sgt)->nents, i)
#endif

#ifndef DMA_MAPPING_ERROR
#define DMA_MAPPING_ERROR		(~(dma_addr_t) 0)
#endif

#ifndef _HAS_DMA_MAP_RESOURCE
dma_addr_t dma_map_resource(struct device *dev, phys_addr_t phys_addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs);
void dma_unmap_resource(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir, unsigned long attrs);
#endif

#ifndef _HAS_DMA_MAP_SGTABLE
#ifdef _HAS_DMA_MAP_RESOURCE_WITH_DMA_ATTRS
int dma_map_sgtable(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir,
		    struct dma_attrs *attrs);
#else
int dma_map_sgtable(struct device *dev, struct sg_table *sgt, enum dma_data_direction dir,
			unsigned long attrs);
#endif

static inline void dma_unmap_sgtable(struct device *dev, struct sg_table *sgt,
				     enum dma_data_direction dir,
#ifdef _HAS_DMA_MAP_RESOURCE_WITH_DMA_ATTRS
				     struct dma_attrs *attrs)
#else
				     unsigned long attrs)
#endif
{
	dma_unmap_sg_attrs(dev, sgt->sgl, sgt->orig_nents, dir, attrs);
}
#endif /* _HAS_DMA_MAP_SGTABLE */

#ifndef _HAS_BITMAP_FIND_NEXT_ZERO_AREA_OFF
unsigned long bitmap_find_next_zero_area_off(unsigned long *map,
					     unsigned long size,
					     unsigned long start,
					     unsigned int nr,
					     unsigned long align_mask,
					     unsigned long align_offset);
#endif

#ifndef _HAS_BITMAP_ZALLOC
unsigned long *bitmap_zalloc(unsigned int nbits, gfp_t flags);
#endif

#ifndef _HAS_BITMAP_FREE
void bitmap_free(const unsigned long *bitmap);
#endif

#ifndef _HAS_IRQ_ALLOC_DESCS_AFFINITY
#define __irq_alloc_descs(irq, from, cnt, node, owner, affinity) \
	__irq_alloc_descs(irq, from, cnt, node, owner)
#endif

#ifdef _HAS_HANDLE_SIMPLE_IRQ_2_ARGUMENTS
#define handle_simple_irq(desc) handle_simple_irq(desc->irq_data.irq, desc)
#endif

#ifndef _HAS_POLL_T
#define __poll_t unsigned int
#endif

#ifndef EPOLLIN
#define EPOLLIN POLLIN
#endif

#ifndef EPOLLRDNORM
#define EPOLLRDNORM POLLRDNORM
#endif

#ifndef _HAS_HMM_PFN_TO_PAGE
#define hmm_pfn_to_page(pfn) hmm_device_entry_to_page(range, pfn)
#endif

#ifndef _HAS_DEVICE_IOMMU_MAPPED
static inline bool device_iommu_mapped(struct device *dev)
{
	return iommu_present(&pci_bus_type);
}
#endif

#ifndef _HAS_RANDOM_U32
static inline u32 get_random_u32(void)
{
	return get_random_int();
}
#endif

#ifndef _HAS_CHECK_ADD_OVERFLOW
#define check_add_overflow(a, b, d) ({		\
	typeof(a) __a = (a);			\
	typeof(b) __b = (b);			\
	typeof(d) __d = (d);			\
	(void) (&__a == &__b);			\
	(void) (&__a == __d);			\
	__builtin_add_overflow(__a, __b, __d);	\
})
#endif

#ifndef _HAS_MEMSET32
void *memset32(uint32_t *s, uint32_t v, size_t count);
#endif

#ifndef _HAS_VM_FLAGS_SET
static inline void vm_flags_set(struct vm_area_struct *vma, vm_flags_t flags)
{
	vma->vm_flags |= flags;
}
#endif

#ifndef _HAS_XARRAY_LIB
struct xa_limit {
	u32 max;
	u32 min;
};

struct xarray {
	struct idr idr;
	spinlock_t xa_lock;
};

#define XA_LIMIT(_min, _max) ((struct xa_limit) { .min = _min, .max = _max })
#define XA_FLAGS_ALLOC	0

#define xa_for_each(xa, index, entry) \
	idr_for_each_entry(xa.idr, entry, *(int *)&index)

static inline void xa_init_flags(struct xarray *xa, gfp_t flags)
{
	spin_lock_init(&xa->xa_lock);
	idr_init(&xa->idr);
}

static inline void xa_destroy(struct xarray *xa)
{
	idr_destroy(&xa->idr);
}

static inline void *xa_load(struct xarray *xa, unsigned long index)
{
	return idr_find(&xa->idr, index);
}

static inline __must_check int xa_alloc(struct xarray *xa, u32 *id,
		void *entry, struct xa_limit limit, gfp_t gfp)
{
	int rc;

	spin_lock(&xa->xa_lock);
	rc = idr_alloc(&xa->idr, entry, limit.min, limit.max + 1, gfp);
	spin_unlock(&xa->xa_lock);

	if (rc < 0) {
		*id = 0;
		return rc;
	}

	*id = rc;

	return 0;
}

static inline void *xa_store(struct xarray *xa, unsigned long index, void *entry, gfp_t gfp)
{
	return idr_replace(&xa->idr, entry, index);
}

static inline void *idr_remove_compat(struct idr *idr, unsigned long idx)
{
#ifndef _HAS_OLD_IDR_LIB
	return idr_remove(idr, idx);
#else /* _HAS_OLD_IDR_LIB */
	/* Old kernels have idr_remove with void signature */
	idr_remove(idr, idx);
	return NULL;
#endif
}

static inline void *xa_erase(struct xarray *xa, unsigned long index)
{
	void *ptr;

	spin_lock(&xa->xa_lock);
	ptr = idr_remove_compat(&xa->idr, index);
	spin_unlock(&xa->xa_lock);

	return ptr;
}

static inline void *__xa_erase(struct xarray *xa, unsigned long index)
{
	return idr_remove_compat(&xa->idr, index);
}

static inline bool xa_empty(struct xarray *xa)
{
	return idr_is_empty(&xa->idr);
}

/* Some old kernels have basic xarray header with defines. */
#ifndef _HAS_XA_LOCK_DEFINES
#define xa_lock(xa)		spin_lock(&(xa)->xa_lock)
#define xa_unlock(xa)		spin_unlock(&(xa)->xa_lock)
#endif

#endif /* !_HAS_XARRAY_LIB */

#ifdef _HAS_CLASS_CREATE_WITH_MODULE
#undef class_create
#define class_create	hl_class_create
struct class *hl_class_create(const char *name);
#endif

#ifndef _HAS_IRQ_AFFINITY_NEW_INTERFACE
static inline int
irq_set_affinity_and_hint(unsigned int irq, const struct cpumask *m)
{
	return irq_set_affinity_hint(irq, m);
}
#endif /* !_HAS_IRQ_AFFINITY_NEW_INTERFACE */

#endif /* HABANALABS_COMPAT_H_ */
