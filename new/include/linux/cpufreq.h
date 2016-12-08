/*
 * linux/include/linux/cpufreq.h
 *
 * Copyright (C) 2001 Russell King
 *           (C) 2002 - 2003 Dominik Brodowski <linux@brodo.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _LINUX_CPUFREQ_H
#define _LINUX_CPUFREQ_H

#include <linux/clk.h>
#include <linux/cpumask.h>
#include <linux/completion.h>
#include <linux/kobject.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>

/*
首先，CPU的硬件特性决定了这个CPU的最高和最低工作频率，
所有的频率调整数值都必须在这个范围内，它们用cpuinfo_xxx_freq来表示。

然后，我们可以在这个范围内再次定义出一个软件的调节范围，
它们用scaling_xxx_freq来表示，同时，根据具体的硬件平台的不同，
我们还需要提供一个频率表，这个频率表规定了cpu可以工作的频率值，
当然这些频率值必须要在cpuinfo_xxx_freq的范围内。
有了这些频率信息，CPUFreq系统就可以根据当前cpu的负载轻重状况，
合理地从频率表中选择一个合适的频率供cpu使用，已达到节能的目的。

至于如何选择频率表中的频率，这个要由不同的governor来实现，
目前的内核版本提供了5种governor供我们选择。

选择好适当的频率以后，具体的频率调节工作就交由scaling_driver来完成。

CPUFreq系统把一些公共的逻辑和接口代码抽象出来，
这些代码与平台无关，也与具体的调频策略无关，
内核的文档把它称为CPUFreq Core（/Documents/cpufreq/core.txt）。

另外一部分，与实际的调频策略相关的部分被称作cpufreq_policy，
cpufreq_policy又是由频率信息和具体的governor组成，governor才是具体策略的实现者，
当然governor需要我们提供必要的频率信息，governor的实现最好能做到平台无关，
与平台相关的代码用cpufreq_driver表述，它完成实际的频率调节工作。

最后，如果其他内核模块需要在频率调节的过程中得到通知消息，
则可以通过cpufreq notifiers来完成
*/

/*********************************************************************
 *                        CPUFREQ INTERFACE                          *
 *********************************************************************/
/*
 * Frequency values here are CPU kHz
 *
 * Maximum transition latency is in nanoseconds - if it's unknown,
 * CPUFREQ_ETERNAL shall be used.
 */

#define CPUFREQ_ETERNAL			(-1)
#define CPUFREQ_NAME_LEN		16
/* Print length for names. Extra 1 space for accomodating '\n' in prints */
#define CPUFREQ_NAME_PLEN		(CPUFREQ_NAME_LEN + 1)

struct cpufreq_governor;

enum cpufreq_table_sorting {
	CPUFREQ_TABLE_UNSORTED,
	CPUFREQ_TABLE_SORTED_ASCENDING,
	CPUFREQ_TABLE_SORTED_DESCENDING
};

struct cpufreq_freqs {
	unsigned int cpu;	/* cpu nr */
	unsigned int old;
	unsigned int new;
	u8 flags;		/* flags of cpufreq_driver, see below. */
};

struct cpufreq_cpuinfo {
	unsigned int		max_freq; // 支持的最大的频率
	unsigned int		min_freq; // 支持的最小的频率

	/* in 10^(-9) s = nanoseconds */
	unsigned int		transition_latency; // 切换延迟信息
};

struct cpufreq_user_policy {
	unsigned int		min;    /* in kHz */
	unsigned int		max;    /* in kHz */
};

struct cpufreq_policy {
	/* CPUs sharing clock, require sw coordination */
	cpumask_var_t		cpus;	/* Online CPUs only */
	cpumask_var_t		related_cpus; /* Online + Offline CPUs ,这个 policy所管理的所有cpu编号*/
	cpumask_var_t		real_cpus; /* Related and present */

	unsigned int		shared_type; /* ACPI: ANY or ALL affected CPUs
						should set cpufreq */
    /*  虽然一种policy可以同时用于多个cpu，
           但是通常一种policy只会由其中的一个cpu进行管理，
           cpu变量用于记录用于管理该policy的cpu编号 */
	unsigned int		cpu;    /* cpu managing this policy, must be online */

	struct clk		*clk;
	struct cpufreq_cpuinfo	cpuinfo;/* see above */

	unsigned int		min;    /* in kHz */
	unsigned int		max;    /* in kHz */
	unsigned int		cur;    /* in kHz, only needed if cpufreq
					 * governors are used */
	unsigned int		restore_freq; /* = policy->cur before transition */
	unsigned int		suspend_freq; /* freq to set during suspend */

/* 
该变量可以取以下两个值：CPUFREQ_POLICY_POWERSAVE和CPUFREQ_POLICY_PERFORMANCE，
该变量只有当调频驱动支持setpolicy回调函数的时候有效，
这时候由驱动根据policy变量的值来决定系统的工作频率或状态。
如果调频驱动（cpufreq_driver）支持target回调，则频率由相应的governor来决定
*/
	unsigned int		policy; /* see above */
	unsigned int		last_policy; /* policy before unplug */
/* 
指向该policy当前使用的cpufreq_governor结构和它的上下文数据。
governor是实现该policy的关键所在，调频策略的逻辑由governor实现。
*/
	struct cpufreq_governor	*governor; /* see below */
	void			*governor_data;
	char			last_governor[CPUFREQ_NAME_LEN]; /* last governor used */

    /* 有时在中断上下文中需要更新policy，
          需要利用该工作队列把实际的工作移到稍后的进程上下文中执行 */
	struct work_struct	update; /* if update_policy() needs to be
					 * called, but you're in IRQ context */

/*
有时候因为特殊的原因需要修改policy的参数，
比如溫度过高时，最大可允许的运行频率可能会被降低，
为了在适当的时候恢复原有的运行参数，
需要使用user_policy保存原始的参数（min，max，policy，governor）
*/
	struct cpufreq_user_policy user_policy; 
	struct cpufreq_frequency_table	*freq_table;
	enum cpufreq_table_sorting freq_table_sorted;

	struct list_head        policy_list;
	struct kobject		kobj; // 该policy在sysfs中对应的kobj的对象
	struct completion	kobj_unregister;

	/*
	 * The rules for this semaphore:
	 * - Any routine that wants to read from the policy structure will
	 *   do a down_read on this semaphore.
	 * - Any routine that will write to the policy structure and/or may take away
	 *   the policy altogether (eg. CPU hotplug), will hold this lock in write
	 *   mode before doing so.
	 */
	struct rw_semaphore	rwsem;

	/*
	 * Fast switch flags:
	 * - fast_switch_possible should be set by the driver if it can
	 *   guarantee that frequency can be changed on any CPU sharing the
	 *   policy and that the change will affect all of the policy CPUs then.
	 * - fast_switch_enabled is to be set by governors that support fast
	 *   freqnency switching with the help of cpufreq_enable_fast_switch().
	 */
	bool			fast_switch_possible;
	bool			fast_switch_enabled;

	 /* Cached frequency lookup from cpufreq_driver_resolve_freq. */
	unsigned int cached_target_freq;
	int cached_resolved_idx;

	/* Synchronization for frequency transitions */
	bool			transition_ongoing; /* Tracks transition status */
	spinlock_t		transition_lock;
	wait_queue_head_t	transition_wait;
	struct task_struct	*transition_task; /* Task which is doing the transition */

	/* cpufreq-stats */
	struct cpufreq_stats	*stats;

	/* For cpufreq driver's internal use */
	void			*driver_data;
};

/* Only for ACPI */
#define CPUFREQ_SHARED_TYPE_NONE (0) /* None */
#define CPUFREQ_SHARED_TYPE_HW	 (1) /* HW does needed coordination */
#define CPUFREQ_SHARED_TYPE_ALL	 (2) /* All dependent CPUs should set freq */
#define CPUFREQ_SHARED_TYPE_ANY	 (3) /* Freq can be set from any dependent CPU*/

#ifdef CONFIG_CPU_FREQ
struct cpufreq_policy *cpufreq_cpu_get_raw(unsigned int cpu);
struct cpufreq_policy *cpufreq_cpu_get(unsigned int cpu);
void cpufreq_cpu_put(struct cpufreq_policy *policy);
#else
static inline struct cpufreq_policy *cpufreq_cpu_get_raw(unsigned int cpu)
{
	return NULL;
}
static inline struct cpufreq_policy *cpufreq_cpu_get(unsigned int cpu)
{
	return NULL;
}
static inline void cpufreq_cpu_put(struct cpufreq_policy *policy) { }
#endif

static inline bool policy_is_shared(struct cpufreq_policy *policy)
{
	return cpumask_weight(policy->cpus) > 1;
}

/* /sys/devices/system/cpu/cpufreq: entry point for global variables */
extern struct kobject *cpufreq_global_kobject;

#ifdef CONFIG_CPU_FREQ
unsigned int cpufreq_get(unsigned int cpu);
unsigned int cpufreq_quick_get(unsigned int cpu);
unsigned int cpufreq_quick_get_max(unsigned int cpu);
void disable_cpufreq(void);

u64 get_cpu_idle_time(unsigned int cpu, u64 *wall, int io_busy);
int cpufreq_get_policy(struct cpufreq_policy *policy, unsigned int cpu);
int cpufreq_update_policy(unsigned int cpu);
bool have_governor_per_policy(void);
struct kobject *get_governor_parent_kobj(struct cpufreq_policy *policy);
void cpufreq_enable_fast_switch(struct cpufreq_policy *policy);
void cpufreq_disable_fast_switch(struct cpufreq_policy *policy);
#else
static inline unsigned int cpufreq_get(unsigned int cpu)
{
	return 0;
}
static inline unsigned int cpufreq_quick_get(unsigned int cpu)
{
	return 0;
}
static inline unsigned int cpufreq_quick_get_max(unsigned int cpu)
{
	return 0;
}
static inline void disable_cpufreq(void) { }
#endif

#ifdef CONFIG_CPU_FREQ_STAT
void cpufreq_stats_create_table(struct cpufreq_policy *policy);
void cpufreq_stats_free_table(struct cpufreq_policy *policy);
void cpufreq_stats_record_transition(struct cpufreq_policy *policy,
				     unsigned int new_freq);
#else
static inline void cpufreq_stats_create_table(struct cpufreq_policy *policy) { }
static inline void cpufreq_stats_free_table(struct cpufreq_policy *policy) { }
static inline void cpufreq_stats_record_transition(struct cpufreq_policy *policy,
						   unsigned int new_freq) { }
#endif /* CONFIG_CPU_FREQ_STAT */

/*********************************************************************
 *                      CPUFREQ DRIVER INTERFACE                     *
 *********************************************************************/

#define CPUFREQ_RELATION_L 0  /* lowest frequency at or above target */
#define CPUFREQ_RELATION_H 1  /* highest frequency below or at target */
#define CPUFREQ_RELATION_C 2  /* closest frequency to target */

struct freq_attr {
	struct attribute attr;
	ssize_t (*show)(struct cpufreq_policy *, char *);
	ssize_t (*store)(struct cpufreq_policy *, const char *, size_t count);
};

#define cpufreq_freq_attr_ro(_name)		\
static struct freq_attr _name =			\
__ATTR(_name, 0444, show_##_name, NULL)

#define cpufreq_freq_attr_ro_perm(_name, _perm)	\
static struct freq_attr _name =			\
__ATTR(_name, _perm, show_##_name, NULL)

#define cpufreq_freq_attr_rw(_name)		\
static struct freq_attr _name =			\
__ATTR(_name, 0644, show_##_name, store_##_name)

struct global_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj,
			struct attribute *attr, char *buf);
	ssize_t (*store)(struct kobject *a, struct attribute *b,
			 const char *c, size_t count);
};

#define define_one_global_ro(_name)		\
static struct global_attr _name =		\
__ATTR(_name, 0444, show_##_name, NULL)

#define define_one_global_rw(_name)		\
static struct global_attr _name =		\
__ATTR(_name, 0644, show_##_name, store_##_name)

/*
gonvernor只是负责计算并提出合适的频率，
但是频率的设定工作是平台相关的，这需要cpufreq_driver驱动来完成
*/
struct cpufreq_driver {
	char		name[CPUFREQ_NAME_LEN];
	u8		flags;
	void		*driver_data;

	/* needed by all drivers */
	int		(*init)(struct cpufreq_policy *policy); // 该驱动进行必要的初始化工作
	int		(*verify)(struct cpufreq_policy *policy); // 检查policy的参数是否被驱动支持

/*
setpolicy/target    回调函数，驱动必须实现这两个函数中的其中一个，

如果不支持通过governor选择合适的运行频率，则实现setpolicy回调函数，
这样系统只能支持CPUFREQ_POLICY_POWERSAVE和CPUFREQ_POLICY_PERFORMANCE这两种工作策略。

反之，实现target回调函数，通过target回调设定governor所需要的频率。
*/
	/* define one out of two */
	int		(*setpolicy)(struct cpufreq_policy *policy);

	/*
	 * On failure, should always restore frequency to policy->restore_freq
	 * (i.e. old freq).
	 */
	int		(*target)(struct cpufreq_policy *policy,
				  unsigned int target_freq,
				  unsigned int relation);	/* Deprecated */
	int		(*target_index)(struct cpufreq_policy *policy,
					unsigned int index);
	unsigned int	(*fast_switch)(struct cpufreq_policy *policy,
				       unsigned int target_freq);

	/*
	 * Caches and returns the lowest driver-supported frequency greater than
	 * or equal to the target frequency, subject to any driver limitations.
	 * Does not set the frequency. Only to be implemented for drivers with
	 * target().
	 */
	unsigned int	(*resolve_freq)(struct cpufreq_policy *policy,
					unsigned int target_freq);

	/*
	 * Only for drivers with target_index() and CPUFREQ_ASYNC_NOTIFICATION
	 * unset.
	 *
	 * get_intermediate should return a stable intermediate frequency
	 * platform wants to switch to and target_intermediate() should set CPU
	 * to to that frequency, before jumping to the frequency corresponding
	 * to 'index'. Core will take care of sending notifications and driver
	 * doesn't have to handle them in target_intermediate() or
	 * target_index().
	 *
	 * Drivers can return '0' from get_intermediate() in case they don't
	 * wish to switch to intermediate frequency for some target frequency.
	 * In that case core will directly call ->target_index().
	 */
	unsigned int	(*get_intermediate)(struct cpufreq_policy *policy,
					    unsigned int index);
	int		(*target_intermediate)(struct cpufreq_policy *policy,
					       unsigned int index);

	/* should be defined, if possible */
	unsigned int	(*get)(unsigned int cpu);

	/* optional */
	int		(*bios_limit)(int cpu, unsigned int *limit);

	int		(*exit)(struct cpufreq_policy *policy);
	void		(*stop_cpu)(struct cpufreq_policy *policy);
	int		(*suspend)(struct cpufreq_policy *policy);
	int		(*resume)(struct cpufreq_policy *policy);

	/* Will be called after the driver is fully initialized */
	void		(*ready)(struct cpufreq_policy *policy);

	struct freq_attr **attr;

	/* platform specific boost support code */
	bool		boost_enabled;
	int		(*set_boost)(int state);
};

/* flags */
#define CPUFREQ_STICKY		(1 << 0)	/* driver isn't removed even if
						   all ->init() calls failed */
#define CPUFREQ_CONST_LOOPS	(1 << 1)	/* loops_per_jiffy or other
						   kernel "constants" aren't
						   affected by frequency
						   transitions */
#define CPUFREQ_PM_NO_WARN	(1 << 2)	/* don't warn on suspend/resume
						   speed mismatches */

/*
 * This should be set by platforms having multiple clock-domains, i.e.
 * supporting multiple policies. With this sysfs directories of governor would
 * be created in cpu/cpu<num>/cpufreq/ directory and so they can use the same
 * governor with different tunables for different clusters.
 */
#define CPUFREQ_HAVE_GOVERNOR_PER_POLICY (1 << 3)

/*
 * Driver will do POSTCHANGE notifications from outside of their ->target()
 * routine and so must set cpufreq_driver->flags with this flag, so that core
 * can handle them specially.
 */
#define CPUFREQ_ASYNC_NOTIFICATION  (1 << 4)

/*
 * Set by drivers which want cpufreq core to check if CPU is running at a
 * frequency present in freq-table exposed by the driver. For these drivers if
 * CPU is found running at an out of table freq, we will try to set it to a freq
 * from the table. And if that fails, we will stop further boot process by
 * issuing a BUG_ON().
 */
#define CPUFREQ_NEED_INITIAL_FREQ_CHECK	(1 << 5)

int cpufreq_register_driver(struct cpufreq_driver *driver_data);
int cpufreq_unregister_driver(struct cpufreq_driver *driver_data);

const char *cpufreq_get_current_driver(void);
void *cpufreq_get_driver_data(void);

static inline void cpufreq_verify_within_limits(struct cpufreq_policy *policy,
		unsigned int min, unsigned int max)
{
	if (policy->min < min)
		policy->min = min;
	if (policy->max < min)
		policy->max = min;
	if (policy->min > max)
		policy->min = max;
	if (policy->max > max)
		policy->max = max;
	if (policy->min > policy->max)
		policy->min = policy->max;
	return;
}

static inline void
cpufreq_verify_within_cpu_limits(struct cpufreq_policy *policy)
{
	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
			policy->cpuinfo.max_freq);
}

#ifdef CONFIG_CPU_FREQ
void cpufreq_suspend(void);
void cpufreq_resume(void);
int cpufreq_generic_suspend(struct cpufreq_policy *policy);
#else
static inline void cpufreq_suspend(void) {}
static inline void cpufreq_resume(void) {}
#endif

/*********************************************************************
 *                     CPUFREQ NOTIFIER INTERFACE                    *
 *********************************************************************/
/*
CPUFreq的通知系统使用了内核的标准通知接口。
它对外提供了两个通知事件：policy通知和transition通知

policy通知用于通知其它模块cpu的policy需要改变，
每次policy改变时，该通知链上的回调将会用不同的事件参数被调用3次
*/

#define CPUFREQ_TRANSITION_NOTIFIER	(0)
#define CPUFREQ_POLICY_NOTIFIER		(1)

/* Transition notifiers */
#define CPUFREQ_PRECHANGE		(0) // 调整前的通知
#define CPUFREQ_POSTCHANGE		(1) // 完成调整后的通知

/* Policy Notifiers  */
/*
只要有需要，所有的被通知者可以在此时修改policy的限制信息，
比如温控系统可能会修改在大允许运行的频率
*/
#define CPUFREQ_ADJUST			(0)
/*
真正切换policy前，该通知会发往所有的被通知者
*/
#define CPUFREQ_NOTIFY			(1)
#define CPUFREQ_START			(2)
#define CPUFREQ_CREATE_POLICY		(3)
#define CPUFREQ_REMOVE_POLICY		(4)

#ifdef CONFIG_CPU_FREQ
int cpufreq_register_notifier(struct notifier_block *nb, unsigned int list);
int cpufreq_unregister_notifier(struct notifier_block *nb, unsigned int list);

void cpufreq_freq_transition_begin(struct cpufreq_policy *policy,
		struct cpufreq_freqs *freqs);
void cpufreq_freq_transition_end(struct cpufreq_policy *policy,
		struct cpufreq_freqs *freqs, int transition_failed);

#else /* CONFIG_CPU_FREQ */
static inline int cpufreq_register_notifier(struct notifier_block *nb,
						unsigned int list)
{
	return 0;
}
static inline int cpufreq_unregister_notifier(struct notifier_block *nb,
						unsigned int list)
{
	return 0;
}
#endif /* !CONFIG_CPU_FREQ */

/**
 * cpufreq_scale - "old * mult / div" calculation for large values (32-bit-arch
 * safe)
 * @old:   old value
 * @div:   divisor
 * @mult:  multiplier
 *
 *
 * new = old * mult / div
 */
static inline unsigned long cpufreq_scale(unsigned long old, u_int div,
		u_int mult)
{
#if BITS_PER_LONG == 32
	u64 result = ((u64) old) * ((u64) mult);
	do_div(result, div);
	return (unsigned long) result;

#elif BITS_PER_LONG == 64
	unsigned long result = old * ((u64) mult);
	result /= div;
	return result;
#endif
}

/*********************************************************************
 *                          CPUFREQ GOVERNORS                        *
 *********************************************************************/

/*
 * If (cpufreq_driver->target) exists, the ->governor decides what frequency
 * within the limits is used. If (cpufreq_driver->setpolicy> exists, these
 * two generic policies are available:
 */
#define CPUFREQ_POLICY_POWERSAVE	(1)
#define CPUFREQ_POLICY_PERFORMANCE	(2)

/*
 * The polling frequency depends on the capability of the processor. Default
 * polling frequency is 1000 times the transition latency of the processor. The
 * ondemand governor will work on any processor with transition latency <= 10ms,
 * using appropriate sampling rate.
 *
 * For CPUs with transition latency > 10ms (mostly drivers with CPUFREQ_ETERNAL)
 * the ondemand governor will not work. All times here are in us (microseconds).
 */
#define MIN_SAMPLING_RATE_RATIO		(2)
#define LATENCY_MULTIPLIER		(1000)
#define MIN_LATENCY_MULTIPLIER		(20)
#define TRANSITION_LATENCY_LIMIT	(10 * 1000 * 1000)

/*
governor负责检测cpu的使用状况, 从而在可用的范围中选择一个合适的频率
*/
struct cpufreq_governor {
	char	name[CPUFREQ_NAME_LEN];
	int	(*init)(struct cpufreq_policy *policy);
	void	(*exit)(struct cpufreq_policy *policy);
	int	(*start)(struct cpufreq_policy *policy);
	void	(*stop)(struct cpufreq_policy *policy);
	void	(*limits)(struct cpufreq_policy *policy);
	ssize_t	(*show_setspeed)	(struct cpufreq_policy *policy,
					 char *buf);
	int	(*store_setspeed)	(struct cpufreq_policy *policy,
					 unsigned int freq);
	unsigned int max_transition_latency; /* HW must be able to switch to
			next freq faster than this value in nano secs or we
			will fallback to performance governor */
	struct list_head	governor_list;
	struct module		*owner;
};

/* Pass a target to the cpufreq driver */
unsigned int cpufreq_driver_fast_switch(struct cpufreq_policy *policy,
					unsigned int target_freq);
int cpufreq_driver_target(struct cpufreq_policy *policy,
				 unsigned int target_freq,
				 unsigned int relation);
int __cpufreq_driver_target(struct cpufreq_policy *policy,
				   unsigned int target_freq,
				   unsigned int relation);
unsigned int cpufreq_driver_resolve_freq(struct cpufreq_policy *policy,
					 unsigned int target_freq);
int cpufreq_register_governor(struct cpufreq_governor *governor);
void cpufreq_unregister_governor(struct cpufreq_governor *governor);

struct cpufreq_governor *cpufreq_default_governor(void);
struct cpufreq_governor *cpufreq_fallback_governor(void);

static inline void cpufreq_policy_apply_limits(struct cpufreq_policy *policy)
{
	if (policy->max < policy->cur)
		__cpufreq_driver_target(policy, policy->max, CPUFREQ_RELATION_H);
	else if (policy->min > policy->cur)
		__cpufreq_driver_target(policy, policy->min, CPUFREQ_RELATION_L);
}

/* Governor attribute set */
struct gov_attr_set {
	struct kobject kobj;
	struct list_head policy_list;
	struct mutex update_lock;
	int usage_count;
};

/* sysfs ops for cpufreq governors */
extern const struct sysfs_ops governor_sysfs_ops;

void gov_attr_set_init(struct gov_attr_set *attr_set, struct list_head *list_node);
void gov_attr_set_get(struct gov_attr_set *attr_set, struct list_head *list_node);
unsigned int gov_attr_set_put(struct gov_attr_set *attr_set, struct list_head *list_node);

/* Governor sysfs attribute */
struct governor_attr {
	struct attribute attr;
	ssize_t (*show)(struct gov_attr_set *attr_set, char *buf);
	ssize_t (*store)(struct gov_attr_set *attr_set, const char *buf,
			 size_t count);
};

/*********************************************************************
 *                     FREQUENCY TABLE HELPERS                       *
 *********************************************************************/

/* Special Values of .frequency field */
#define CPUFREQ_ENTRY_INVALID	~0u
#define CPUFREQ_TABLE_END	~1u
/* Special Values of .flags field */
#define CPUFREQ_BOOST_FREQ	(1 << 0)

struct cpufreq_frequency_table {
	unsigned int	flags;
	unsigned int	driver_data; /* driver specific data, not used by core */
	unsigned int	frequency; /* kHz - doesn't need to be in ascending
				    * order */
};

#if defined(CONFIG_CPU_FREQ) && defined(CONFIG_PM_OPP)
int dev_pm_opp_init_cpufreq_table(struct device *dev,
				  struct cpufreq_frequency_table **table);
void dev_pm_opp_free_cpufreq_table(struct device *dev,
				   struct cpufreq_frequency_table **table);
#else
static inline int dev_pm_opp_init_cpufreq_table(struct device *dev,
						struct cpufreq_frequency_table
						**table)
{
	return -EINVAL;
}

static inline void dev_pm_opp_free_cpufreq_table(struct device *dev,
						 struct cpufreq_frequency_table
						 **table)
{
}
#endif

/*
 * cpufreq_for_each_entry -	iterate over a cpufreq_frequency_table
 * @pos:	the cpufreq_frequency_table * to use as a loop cursor.
 * @table:	the cpufreq_frequency_table * to iterate over.
 */

#define cpufreq_for_each_entry(pos, table)	\
	for (pos = table; pos->frequency != CPUFREQ_TABLE_END; pos++)

/*
 * cpufreq_for_each_valid_entry -     iterate over a cpufreq_frequency_table
 *	excluding CPUFREQ_ENTRY_INVALID frequencies.
 * @pos:        the cpufreq_frequency_table * to use as a loop cursor.
 * @table:      the cpufreq_frequency_table * to iterate over.
 */

#define cpufreq_for_each_valid_entry(pos, table)			\
	for (pos = table; pos->frequency != CPUFREQ_TABLE_END; pos++)	\
		if (pos->frequency == CPUFREQ_ENTRY_INVALID)		\
			continue;					\
		else

int cpufreq_frequency_table_cpuinfo(struct cpufreq_policy *policy,
				    struct cpufreq_frequency_table *table);

int cpufreq_frequency_table_verify(struct cpufreq_policy *policy,
				   struct cpufreq_frequency_table *table);
int cpufreq_generic_frequency_table_verify(struct cpufreq_policy *policy);

int cpufreq_table_index_unsorted(struct cpufreq_policy *policy,
				 unsigned int target_freq,
				 unsigned int relation);
int cpufreq_frequency_table_get_index(struct cpufreq_policy *policy,
		unsigned int freq);

ssize_t cpufreq_show_cpus(const struct cpumask *mask, char *buf);

#ifdef CONFIG_CPU_FREQ
int cpufreq_boost_trigger_state(int state);
int cpufreq_boost_enabled(void);
int cpufreq_enable_boost_support(void);
bool policy_has_boost_freq(struct cpufreq_policy *policy);

/* Find lowest freq at or above target in a table in ascending order */
static inline int cpufreq_table_find_index_al(struct cpufreq_policy *policy,
					      unsigned int target_freq)
{
	struct cpufreq_frequency_table *table = policy->freq_table;
	struct cpufreq_frequency_table *pos, *best = table - 1;
	unsigned int freq;

	cpufreq_for_each_valid_entry(pos, table) {
		freq = pos->frequency;

		if (freq >= target_freq)
			return pos - table;

		best = pos;
	}

	return best - table;
}

/* Find lowest freq at or above target in a table in descending order */
static inline int cpufreq_table_find_index_dl(struct cpufreq_policy *policy,
					      unsigned int target_freq)
{
	struct cpufreq_frequency_table *table = policy->freq_table;
	struct cpufreq_frequency_table *pos, *best = table - 1;
	unsigned int freq;

	cpufreq_for_each_valid_entry(pos, table) {
		freq = pos->frequency;

		if (freq == target_freq)
			return pos - table;

		if (freq > target_freq) {
			best = pos;
			continue;
		}

		/* No freq found above target_freq */
		if (best == table - 1)
			return pos - table;

		return best - table;
	}

	return best - table;
}

/* Works only on sorted freq-tables */
static inline int cpufreq_table_find_index_l(struct cpufreq_policy *policy,
					     unsigned int target_freq)
{
	target_freq = clamp_val(target_freq, policy->min, policy->max);

	if (policy->freq_table_sorted == CPUFREQ_TABLE_SORTED_ASCENDING)
		return cpufreq_table_find_index_al(policy, target_freq);
	else
		return cpufreq_table_find_index_dl(policy, target_freq);
}

/* Find highest freq at or below target in a table in ascending order */
static inline int cpufreq_table_find_index_ah(struct cpufreq_policy *policy,
					      unsigned int target_freq)
{
	struct cpufreq_frequency_table *table = policy->freq_table;
	struct cpufreq_frequency_table *pos, *best = table - 1;
	unsigned int freq;

	cpufreq_for_each_valid_entry(pos, table) {
		freq = pos->frequency;

		if (freq == target_freq)
			return pos - table;

		if (freq < target_freq) {
			best = pos;
			continue;
		}

		/* No freq found below target_freq */
		if (best == table - 1)
			return pos - table;

		return best - table;
	}

	return best - table;
}

/* Find highest freq at or below target in a table in descending order */
static inline int cpufreq_table_find_index_dh(struct cpufreq_policy *policy,
					      unsigned int target_freq)
{
	struct cpufreq_frequency_table *table = policy->freq_table;
	struct cpufreq_frequency_table *pos, *best = table - 1;
	unsigned int freq;

	cpufreq_for_each_valid_entry(pos, table) {
		freq = pos->frequency;

		if (freq <= target_freq)
			return pos - table;

		best = pos;
	}

	return best - table;
}

/* Works only on sorted freq-tables */
static inline int cpufreq_table_find_index_h(struct cpufreq_policy *policy,
					     unsigned int target_freq)
{
	target_freq = clamp_val(target_freq, policy->min, policy->max);

	if (policy->freq_table_sorted == CPUFREQ_TABLE_SORTED_ASCENDING)
		return cpufreq_table_find_index_ah(policy, target_freq);
	else
		return cpufreq_table_find_index_dh(policy, target_freq);
}

/* Find closest freq to target in a table in ascending order */
static inline int cpufreq_table_find_index_ac(struct cpufreq_policy *policy,
					      unsigned int target_freq)
{
	struct cpufreq_frequency_table *table = policy->freq_table;
	struct cpufreq_frequency_table *pos, *best = table - 1;
	unsigned int freq;

	cpufreq_for_each_valid_entry(pos, table) {
		freq = pos->frequency;

		if (freq == target_freq)
			return pos - table;

		if (freq < target_freq) {
			best = pos;
			continue;
		}

		/* No freq found below target_freq */
		if (best == table - 1)
			return pos - table;

		/* Choose the closest freq */
		if (target_freq - best->frequency > freq - target_freq)
			return pos - table;

		return best - table;
	}

	return best - table;
}

/* Find closest freq to target in a table in descending order */
static inline int cpufreq_table_find_index_dc(struct cpufreq_policy *policy,
					      unsigned int target_freq)
{
	struct cpufreq_frequency_table *table = policy->freq_table;
	struct cpufreq_frequency_table *pos, *best = table - 1;
	unsigned int freq;

	cpufreq_for_each_valid_entry(pos, table) {
		freq = pos->frequency;

		if (freq == target_freq)
			return pos - table;

		if (freq > target_freq) {
			best = pos;
			continue;
		}

		/* No freq found above target_freq */
		if (best == table - 1)
			return pos - table;

		/* Choose the closest freq */
		if (best->frequency - target_freq > target_freq - freq)
			return pos - table;

		return best - table;
	}

	return best - table;
}

/* Works only on sorted freq-tables */
static inline int cpufreq_table_find_index_c(struct cpufreq_policy *policy,
					     unsigned int target_freq)
{
	target_freq = clamp_val(target_freq, policy->min, policy->max);

	if (policy->freq_table_sorted == CPUFREQ_TABLE_SORTED_ASCENDING)
		return cpufreq_table_find_index_ac(policy, target_freq);
	else
		return cpufreq_table_find_index_dc(policy, target_freq);
}

static inline int cpufreq_frequency_table_target(struct cpufreq_policy *policy,
						 unsigned int target_freq,
						 unsigned int relation)
{
	if (unlikely(policy->freq_table_sorted == CPUFREQ_TABLE_UNSORTED))
		return cpufreq_table_index_unsorted(policy, target_freq,
						    relation);

	switch (relation) {
	case CPUFREQ_RELATION_L:
		return cpufreq_table_find_index_l(policy, target_freq);
	case CPUFREQ_RELATION_H:
		return cpufreq_table_find_index_h(policy, target_freq);
	case CPUFREQ_RELATION_C:
		return cpufreq_table_find_index_c(policy, target_freq);
	default:
		pr_err("%s: Invalid relation: %d\n", __func__, relation);
		return -EINVAL;
	}
}
#else
static inline int cpufreq_boost_trigger_state(int state)
{
	return 0;
}
static inline int cpufreq_boost_enabled(void)
{
	return 0;
}

static inline int cpufreq_enable_boost_support(void)
{
	return -EINVAL;
}

static inline bool policy_has_boost_freq(struct cpufreq_policy *policy)
{
	return false;
}
#endif

/* the following are really really optional */
extern struct freq_attr cpufreq_freq_attr_scaling_available_freqs;
extern struct freq_attr cpufreq_freq_attr_scaling_boost_freqs;
extern struct freq_attr *cpufreq_generic_attr[];
int cpufreq_table_validate_and_show(struct cpufreq_policy *policy,
				      struct cpufreq_frequency_table *table);

unsigned int cpufreq_generic_get(unsigned int cpu);
int cpufreq_generic_init(struct cpufreq_policy *policy,
		struct cpufreq_frequency_table *table,
		unsigned int transition_latency);
#endif /* _LINUX_CPUFREQ_H */
