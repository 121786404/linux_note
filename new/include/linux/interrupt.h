/* interrupt.h */
#ifndef _LINUX_INTERRUPT_H
#define _LINUX_INTERRUPT_H

#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/bitops.h>
#include <linux/preempt.h>
#include <linux/cpumask.h>
#include <linux/irqreturn.h>
#include <linux/irqnr.h>
#include <linux/hardirq.h>
#include <linux/irqflags.h>
#include <linux/hrtimer.h>
#include <linux/kref.h>
#include <linux/workqueue.h>

#include <linux/atomic.h>
#include <asm/ptrace.h>
#include <asm/irq.h>

/*
 * These correspond to the IORESOURCE_IRQ_* defines in
 * linux/ioport.h to select the interrupt line behaviour.  When
 * requesting an interrupt without specifying a IRQF_TRIGGER, the
 * setting should be assumed to be "as already configured", which
 * may be as per machine or firmware initialisation.
 */
 /*系统定义的中断信号触发类型标志,设定中断触发信号类型的函数为__irq_set_trigger*/
#define IRQF_TRIGGER_NONE	0x00000000
/*上升沿触发*/
#define IRQF_TRIGGER_RISING	0x00000001
/*下降沿触发*/
#define IRQF_TRIGGER_FALLING	0x00000002
/*高电瓶触发*/
#define IRQF_TRIGGER_HIGH	0x00000004
/*低电平触发*/
#define IRQF_TRIGGER_LOW	0x00000008
/*中断触发信号掩码*/
#define IRQF_TRIGGER_MASK	(IRQF_TRIGGER_HIGH | IRQF_TRIGGER_LOW | \
				 IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)
#define IRQF_TRIGGER_PROBE	0x00000010

/*
 * These flags used only by the kernel as part of the
 * irq handling routines.
 *
 * IRQF_SHARED - allow sharing the irq among several devices
 * IRQF_PROBE_SHARED - set by callers when they expect sharing mismatches to occur
 * IRQF_TIMER - Flag to mark this interrupt as timer interrupt
 * IRQF_PERCPU - Interrupt is per cpu
 * IRQF_NOBALANCING - Flag to exclude this interrupt from irq balancing
 * IRQF_IRQPOLL - Interrupt is used for polling (only the interrupt that is
 *                registered first in an shared interrupt is considered for
 *                performance reasons)
 * IRQF_ONESHOT - Interrupt is not reenabled after the hardirq handler finished.
 *                Used by threaded interrupts which need to keep the
 *                irq line disabled until the threaded handler has been run.
 * IRQF_NO_SUSPEND - Do not disable this IRQ during suspend.  Does not guarantee
 *                   that this interrupt will wake the system from a suspended
 *                   state.  See Documentation/power/suspend-and-interrupts.txt
 * IRQF_FORCE_RESUME - Force enable it on resume even if IRQF_NO_SUSPEND is set
 * IRQF_NO_THREAD - Interrupt cannot be threaded
 * IRQF_EARLY_RESUME - Resume IRQ early during syscore instead of at device
 *                resume time.
 * IRQF_COND_SUSPEND - If the IRQ is shared with a NO_SUSPEND user, execute this
 *                interrupt handler after suspending interrupts. For system
 *                wakeup devices users need to implement wakeup detection in
 *                their interrupt handlers.
 */
#define IRQF_SHARED		0x00000080
#define IRQF_PROBE_SHARED	0x00000100
#define __IRQF_TIMER		0x00000200
#define IRQF_PERCPU		0x00000400
#define IRQF_NOBALANCING	0x00000800
#define IRQF_IRQPOLL		0x00001000
#define IRQF_ONESHOT		0x00002000
#define IRQF_NO_SUSPEND		0x00004000
#define IRQF_FORCE_RESUME	0x00008000
#define IRQF_NO_THREAD		0x00010000
#define IRQF_EARLY_RESUME	0x00020000
#define IRQF_COND_SUSPEND	0x00040000

#define IRQF_TIMER		(__IRQF_TIMER | IRQF_NO_SUSPEND | IRQF_NO_THREAD)

/*
 * These values can be returned by request_any_context_irq() and
 * describe the context the interrupt will be run in.
 *
 * IRQC_IS_HARDIRQ - interrupt runs in hardirq context
 * IRQC_IS_NESTED - interrupt runs in a nested threaded context
 */
enum {
	IRQC_IS_HARDIRQ	= 0,
	IRQC_IS_NESTED,
};

/*irq_return_t声明,设备驱动程序负责实现该函数,然后调用request_irq函数，后者会把驱动程序实现的中断服务例程赋值给handler*/
typedef irqreturn_t (*irq_handler_t)(int, void *);

/**
 * struct irqaction - per interrupt action descriptor
 * @handler:	interrupt handler function
 * @name:	name of the device
 * @dev_id:	cookie to identify the device
 * @percpu_dev_id:	cookie to identify the device
 * @next:	pointer to the next irqaction for shared interrupts
 * @irq:	interrupt number
 * @flags:	flags (see IRQF_* above)
 * @thread_fn:	interrupt handler function for threaded interrupts
 * @thread:	thread pointer for threaded interrupts
 * @secondary:	pointer to secondary irqaction (force threading)
 * @thread_flags:	flags related to @thread
 * @thread_mask:	bitmask for keeping track of @thread activity
 * @dir:	pointer to the proc/irq/NN/name entry
 */
 /*设备驱动程序通过这个结构将其中断处理函数挂在在action上*/
struct irqaction {
	irq_handler_t handler;	/*指向设备特定的中断服务例程函数的指针*/
/* 调用handler时传给他的参数，在多个设备共享一个irq的
			 * 情况下特别重要，这种链式的action中，
			 * 设备驱动程序通过dev_id来标识自己*/
	void			*dev_id;
	void __percpu		*percpu_dev_id;
/* 指向下一个action对象,用于多个设备共享一个irq
	 			 * 的情形，此时action通过next构成一个链表*/
	struct irqaction	*next;
/* 当驱动程序调用request_threaded_irq函数
				   * 来安装中断处理例程时，用来实现irq_thread机制*/
	irq_handler_t		thread_fn;
	struct task_struct	*thread;
	struct irqaction	*secondary;
	unsigned int		irq;
	unsigned int		flags;
	unsigned long		thread_flags;
	unsigned long		thread_mask;
	const char		*name;
	/*中断处理函数中用来创建在proc文件系统中的目录项*/
	struct proc_dir_entry	*dir;
} ____cacheline_internodealigned_in_smp;

extern irqreturn_t no_action(int cpl, void *dev_id);

/*
 * If a (PCI) device interrupt is not connected we set dev->irq to
 * IRQ_NOTCONNECTED. This causes request_irq() to fail with -ENOTCONN, so we
 * can distingiush that case from other error returns.
 *
 * 0x80000000 is guaranteed to be outside the available range of interrupts
 * and easy to distinguish from other possible incorrect values.
 */
#define IRQ_NOTCONNECTED	(1U << 31)

extern int __must_check
request_threaded_irq(unsigned int irq, irq_handler_t handler,
		     irq_handler_t thread_fn,
		     unsigned long flags, const char *name, void *dev);

/* 把驱动程序实现的中断服务例程赋值给handler,驱动程序中安装中断服务例程
 * @irq:中断号
 * @handler:中断处理例程ISR,由设备驱动程序负责实现
 * @flags:是标志变量,可影响内核在安装ISR时的一些行为模式,信号触发类型
 * @name:当前安装中断ISR的设备名称，内核在proc文件系统生成name的一个入口点
 * @dev:是个传递到中断处理例程的指针，在中断共享的情况下，将在free_irq时用到，以     区分当前是要释放的哪一个struct irqaction对象*/
static inline int __must_check
request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags,
	    const char *name, void *dev)
{
	return request_threaded_irq(irq, handler, NULL, flags, name, dev);
}

extern int __must_check
request_any_context_irq(unsigned int irq, irq_handler_t handler,
			unsigned long flags, const char *name, void *dev_id);

extern int __must_check
request_percpu_irq(unsigned int irq, irq_handler_t handler,
		   const char *devname, void __percpu *percpu_dev_id);

/*通过request_irq安装的中断处理函数，如果不再需要应该调用free_irq予以释放*/
extern void free_irq(unsigned int, void *);
extern void free_percpu_irq(unsigned int, void __percpu *);

struct device;

extern int __must_check
devm_request_threaded_irq(struct device *dev, unsigned int irq,
			  irq_handler_t handler, irq_handler_t thread_fn,
			  unsigned long irqflags, const char *devname,
			  void *dev_id);

static inline int __must_check
devm_request_irq(struct device *dev, unsigned int irq, irq_handler_t handler,
		 unsigned long irqflags, const char *devname, void *dev_id)
{
	return devm_request_threaded_irq(dev, irq, handler, NULL, irqflags,
					 devname, dev_id);
}

extern int __must_check
devm_request_any_context_irq(struct device *dev, unsigned int irq,
		 irq_handler_t handler, unsigned long irqflags,
		 const char *devname, void *dev_id);

extern void devm_free_irq(struct device *dev, unsigned int irq, void *dev_id);

/*
 * On lockdep we dont want to enable hardirqs in hardirq
 * context. Use local_irq_enable_in_hardirq() to annotate
 * kernel code that has to do this nevertheless (pretty much
 * the only valid case is for old/broken hardware that is
 * insanely slow).
 *
 * NOTE: in theory this might break fragile code that relies
 * on hardirq delivery - in practice we dont seem to have such
 * places left. So the only effect should be slightly increased
 * irqs-off latencies.
 */
#ifdef CONFIG_LOCKDEP
# define local_irq_enable_in_hardirq()	do { } while (0)
#else
# define local_irq_enable_in_hardirq()	local_irq_enable()
#endif

extern void disable_irq_nosync(unsigned int irq);
extern bool disable_hardirq(unsigned int irq);
extern void disable_irq(unsigned int irq);
extern void disable_percpu_irq(unsigned int irq);
extern void enable_irq(unsigned int irq);
extern void enable_percpu_irq(unsigned int irq, unsigned int type);
extern bool irq_percpu_is_enabled(unsigned int irq);
extern void irq_wake_thread(unsigned int irq, void *dev_id);

/* The following three functions are for the core kernel use only. */
extern void suspend_device_irqs(void);
extern void resume_device_irqs(void);

/**
 * struct irq_affinity_notify - context for notification of IRQ affinity changes
 * @irq:		Interrupt to which notification applies
 * @kref:		Reference count, for internal use
 * @work:		Work item, for internal use
 * @notify:		Function to be called on change.  This will be
 *			called in process context.
 * @release:		Function to be called on release.  This will be
 *			called in process context.  Once registered, the
 *			structure must only be freed when this function is
 *			called or later.
 */
struct irq_affinity_notify {
	unsigned int irq;
	struct kref kref;
	struct work_struct work;
	void (*notify)(struct irq_affinity_notify *, const cpumask_t *mask);
	void (*release)(struct kref *ref);
};

/**
 * struct irq_affinity - Description for automatic irq affinity assignements
 * @pre_vectors:	Don't apply affinity to @pre_vectors at beginning of
 *			the MSI(-X) vector space
 * @post_vectors:	Don't apply affinity to @post_vectors at end of
 *			the MSI(-X) vector space
 */
struct irq_affinity {
	int	pre_vectors;
	int	post_vectors;
};

#if defined(CONFIG_SMP)

extern cpumask_var_t irq_default_affinity;

/* Internal implementation. Use the helpers below */
extern int __irq_set_affinity(unsigned int irq, const struct cpumask *cpumask,
			      bool force);

/**
 * irq_set_affinity - Set the irq affinity of a given irq
 * @irq:	Interrupt to set affinity
 * @cpumask:	cpumask
 *
 * Fails if cpumask does not contain an online CPU
 */
static inline int
irq_set_affinity(unsigned int irq, const struct cpumask *cpumask)
{
	return __irq_set_affinity(irq, cpumask, false);
}

/**
 * irq_force_affinity - Force the irq affinity of a given irq
 * @irq:	Interrupt to set affinity
 * @cpumask:	cpumask
 *
 * Same as irq_set_affinity, but without checking the mask against
 * online cpus.
 *
 * Solely for low level cpu hotplug code, where we need to make per
 * cpu interrupts affine before the cpu becomes online.
 */
static inline int
irq_force_affinity(unsigned int irq, const struct cpumask *cpumask)
{
	return __irq_set_affinity(irq, cpumask, true);
}

extern int irq_can_set_affinity(unsigned int irq);
extern int irq_select_affinity(unsigned int irq);

extern int irq_set_affinity_hint(unsigned int irq, const struct cpumask *m);

extern int
irq_set_affinity_notifier(unsigned int irq, struct irq_affinity_notify *notify);

struct cpumask *irq_create_affinity_masks(int nvec, const struct irq_affinity *affd);
int irq_calc_affinity_vectors(int maxvec, const struct irq_affinity *affd);

#else /* CONFIG_SMP */

static inline int irq_set_affinity(unsigned int irq, const struct cpumask *m)
{
	return -EINVAL;
}

static inline int irq_force_affinity(unsigned int irq, const struct cpumask *cpumask)
{
	return 0;
}

static inline int irq_can_set_affinity(unsigned int irq)
{
	return 0;
}

static inline int irq_select_affinity(unsigned int irq)  { return 0; }

static inline int irq_set_affinity_hint(unsigned int irq,
					const struct cpumask *m)
{
	return -EINVAL;
}

static inline int
irq_set_affinity_notifier(unsigned int irq, struct irq_affinity_notify *notify)
{
	return 0;
}

static inline struct cpumask *
irq_create_affinity_masks(int nvec, const struct irq_affinity *affd)
{
	return NULL;
}

static inline int
irq_calc_affinity_vectors(int maxvec, const struct irq_affinity *affd)
{
	return maxvec;
}

#endif /* CONFIG_SMP */

/*
 * Special lockdep variants of irq disabling/enabling.
 * These should be used for locking constructs that
 * know that a particular irq context which is disabled,
 * and which is the only irq-context user of a lock,
 * that it's safe to take the lock in the irq-disabled
 * section without disabling hardirqs.
 *
 * On !CONFIG_LOCKDEP they are equivalent to the normal
 * irq disable/enable methods.
 */
static inline void disable_irq_nosync_lockdep(unsigned int irq)
{
	disable_irq_nosync(irq);
#ifdef CONFIG_LOCKDEP
	local_irq_disable();
#endif
}

static inline void disable_irq_nosync_lockdep_irqsave(unsigned int irq, unsigned long *flags)
{
	disable_irq_nosync(irq);
#ifdef CONFIG_LOCKDEP
	local_irq_save(*flags);
#endif
}

static inline void disable_irq_lockdep(unsigned int irq)
{
	disable_irq(irq);
#ifdef CONFIG_LOCKDEP
	local_irq_disable();
#endif
}

static inline void enable_irq_lockdep(unsigned int irq)
{
#ifdef CONFIG_LOCKDEP
	local_irq_enable();
#endif
	enable_irq(irq);
}

static inline void enable_irq_lockdep_irqrestore(unsigned int irq, unsigned long *flags)
{
#ifdef CONFIG_LOCKDEP
	local_irq_restore(*flags);
#endif
	enable_irq(irq);
}

/* IRQ wakeup (PM) control: */
extern int irq_set_irq_wake(unsigned int irq, unsigned int on);

static inline int enable_irq_wake(unsigned int irq)
{
	return irq_set_irq_wake(irq, 1);
}

static inline int disable_irq_wake(unsigned int irq)
{
	return irq_set_irq_wake(irq, 0);
}

/*
 * irq_get_irqchip_state/irq_set_irqchip_state specific flags
 */
enum irqchip_irq_state {
	IRQCHIP_STATE_PENDING,		/* Is interrupt pending? */
	IRQCHIP_STATE_ACTIVE,		/* Is interrupt in progress? */
	IRQCHIP_STATE_MASKED,		/* Is interrupt masked? */
	IRQCHIP_STATE_LINE_LEVEL,	/* Is IRQ line high? */
};

extern int irq_get_irqchip_state(unsigned int irq, enum irqchip_irq_state which,
				 bool *state);
extern int irq_set_irqchip_state(unsigned int irq, enum irqchip_irq_state which,
				 bool state);

#ifdef CONFIG_IRQ_FORCED_THREADING
extern bool force_irqthreads;
#else
#define force_irqthreads	(0)
#endif

#ifndef __ARCH_SET_SOFTIRQ_PENDING
#define set_softirq_pending(x) (local_softirq_pending() = (x))
#define or_softirq_pending(x)  (local_softirq_pending() |= (x))
#endif

/* Some architectures might implement lazy enabling/disabling of
 * interrupts. In some cases, such as stop_machine, we might want
 * to ensure that after a local_irq_disable(), interrupts have
 * really been disabled in hardware. Such architectures need to
 * implement the following hook.
 */
#ifndef hard_irq_disable
#define hard_irq_disable()	do { } while(0)
#endif

/* PLEASE, avoid to allocate new softirqs, if you need not _really_ high
   frequency threaded job scheduling. For almost all the purposes
   tasklets are more than enough. F.e. all serial device BHs et
   al. should be converted to tasklets, not to softirqs.
 */
/*softirq的类型*/
enum
{
	HI_SOFTIRQ=0,	/*用来实现tasklet,优先级高于TASKLET_SOFTIRQ*/
	TIMER_SOFTIRQ,	/*用于定时器*/
	NET_TX_SOFTIRQ,	/*网络设备的发送操作*/
	NET_RX_SOFTIRQ,	/*网络设备的接收操作*/
	BLOCK_SOFTIRQ,	/*块设备的操作*/
	IRQ_POLL_SOFTIRQ, //块设备poll软中断
	TASKLET_SOFTIRQ,	/*用来实现tasklet*/
	SCHED_SOFTIRQ,		/*用于调度器*/
	//高精度时钟软中断，未用
	HRTIMER_SOFTIRQ, /* Unused, but kept as tools rely on the
			    numbering. Sigh! */
	//RCU软中断
	RCU_SOFTIRQ,    /* Preferable RCU should always be the last softirq */

	NR_SOFTIRQS
};

#define SOFTIRQ_STOP_IDLE_MASK (~(1 << RCU_SOFTIRQ))

/* map softirq index to softirq name. update 'softirq_to_name' in
 * kernel/softirq.c when adding a new softirq.
 */
extern const char * const softirq_to_name[NR_SOFTIRQS];

/* softirq mask and active fields moved to irq_cpustat_t in
 * asm/hardirq.h to get better cache usage.  KAO
 */

/*
软中断处理程序运行在中断上下文，允许中断响应，但它不能休眠。
当一个处理程序运行的时候，当前处理器的软中断被禁止。
但其它处理器仍可以执行别的软中断。

实际上，如果同一个软中断在它被执行的同时再次触发了，
那么另一个处理器可以同时运行其处理程序，
这意味着任何的数据共享，都需要严格的锁保护。

（tasklet更受青睐正是因此，tasklet处理程序正在执行时，
在任何处理器都不会再执行） 一个软中断不会抢占另一个软中断。

实际上，唯一可以抢占软中断的是中断处理程序。
*/
struct softirq_action
{
	void	(*action)(struct softirq_action *);
};

asmlinkage void do_softirq(void);
asmlinkage void __do_softirq(void);

#ifdef __ARCH_HAS_DO_SOFTIRQ
void do_softirq_own_stack(void);
#else
static inline void do_softirq_own_stack(void)
{
	__do_softirq();
}
#endif

extern void open_softirq(int nr, void (*action)(struct softirq_action *));
extern void softirq_init(void);
extern void __raise_softirq_irqoff(unsigned int nr);

extern void raise_softirq_irqoff(unsigned int nr);
extern void raise_softirq(unsigned int nr);

DECLARE_PER_CPU(struct task_struct *, ksoftirqd);

static inline struct task_struct *this_cpu_ksoftirqd(void)
{
	return this_cpu_read(ksoftirqd);
}

/* Tasklets --- multithreaded analogue of BHs.

   Main feature differing them of generic softirqs: tasklet
   is running only on one CPU simultaneously.

   Main feature differing them of BHs: different tasklets
   may be run simultaneously on different CPUs.

   Properties:
   * If tasklet_schedule() is called, then tasklet is guaranteed
     to be executed on some cpu at least once after this.
   * If the tasklet is already scheduled, but its execution is still not
     started, it will be executed only once.
   * If this tasklet is already running on another CPU (or schedule is called
     from tasklet itself), it is rescheduled for later.
   * Tasklet is strictly serialized wrt itself, but not
     wrt another tasklets. If client needs some intertask synchronization,
     he makes it with spinlocks.
 */
/*表示tasklet对象的数据结构
  * 驱动程序为了实现基于tasklet机制的延迟操作,
  首先需要声明一个tasklet对象

  tasklet 是通过软中断实现的，因此它本质也是软中断。
  与软中断最大的区别是，如果是多处理器系统，
  tasklet在运行前会检查这个tasklet是否正在其它处理器上运行，
  如果是则不运行
*/
struct tasklet_struct
{
	struct tasklet_struct *next;	/*将系统中的tasklet对象构建成链表*/
	unsigned long state; /*记录每个tasklet在系统中的状态,其值时枚举型变量
				TASKLET_STATE_SCHED和TASKLET_STATE_RUN两者之一*/

	atomic_t count;	/*用来实现tasklet的disable和enable操作,count.counter=0
			表示当前的tasklet是enabled的,可以被调度执行,否则便是
			disabled的tasklet,不可被执行*/
	void (*func)(unsigned long);	/*该tasklet上的执行函数或者延迟函数,当该tasklet在SOFTIRQ部分被调度执行时,该函数指针指向的函数被调用,用来完成驱动程序中实际的延迟操作任务*/
	unsigned long data;	/*作为参数传给func函数*/
};

/*声明并初始化一个tasklet对象*/
#define DECLARE_TASKLET(name, func, data) \
struct tasklet_struct name = { NULL, 0, ATOMIC_INIT(0), func, data }

/*用来声明一个处于disabled状态的tasklet对象*/
#define DECLARE_TASKLET_DISABLED(name, func, data) \
struct tasklet_struct name = { NULL, 0, ATOMIC_INIT(1), func, data }


enum
{
	/*TASKLET_STATE_SCHED表示当前tasklet已经被提交*/
	TASKLET_STATE_SCHED,	/* Tasklet is scheduled for execution */

	/* TASKLET_STATE_RUN只用在对称多处理器系统SMP中,
	 * 表示当前tasklet正在执行*/
	TASKLET_STATE_RUN	/* Tasklet is running (SMP only) */
};

#ifdef CONFIG_SMP
static inline int tasklet_trylock(struct tasklet_struct *t)
{
	return !test_and_set_bit(TASKLET_STATE_RUN, &(t)->state);
}

static inline void tasklet_unlock(struct tasklet_struct *t)
{
	smp_mb__before_atomic();
	clear_bit(TASKLET_STATE_RUN, &(t)->state);
}

static inline void tasklet_unlock_wait(struct tasklet_struct *t)
{
	while (test_bit(TASKLET_STATE_RUN, &(t)->state)) { barrier(); }
}
#else
#define tasklet_trylock(t) 1
#define tasklet_unlock_wait(t) do { } while (0)
#define tasklet_unlock(t) do { } while (0)
#endif

extern void __tasklet_schedule(struct tasklet_struct *t);

/* 
* 驱动程序向系统提交这个tasklet,即将tastlet对象加入到tasklet_vec管理的链表中
* 对于HI_SOFTIRQ,提交tasklet对象的函数为tasklet_hi_schedule
*/
static inline void tasklet_schedule(struct tasklet_struct *t)
{
	if (!test_and_set_bit(TASKLET_STATE_SCHED, &t->state))
		__tasklet_schedule(t);
}

extern void __tasklet_hi_schedule(struct tasklet_struct *t);

/* 驱动程序项系统提交这个tasklet,即将tastlet对象加入到tasklet_hi_vec管理的链表中*/
static inline void tasklet_hi_schedule(struct tasklet_struct *t)
{
	if (!test_and_set_bit(TASKLET_STATE_SCHED, &t->state))
		__tasklet_hi_schedule(t);
}

extern void __tasklet_hi_schedule_first(struct tasklet_struct *t);

/*
 * This version avoids touching any other tasklets. Needed for kmemcheck
 * in order not to take any page faults while enqueueing this tasklet;
 * consider VERY carefully whether you really need this or
 * tasklet_hi_schedule()...
 */
static inline void tasklet_hi_schedule_first(struct tasklet_struct *t)
{
	if (!test_and_set_bit(TASKLET_STATE_SCHED, &t->state))
		__tasklet_hi_schedule_first(t);
}


/*disable一个tasklet,使之无法被SOFTIRQ调度运行*/
static inline void tasklet_disable_nosync(struct tasklet_struct *t)
{
	atomic_inc(&t->count);
	smp_mb__after_atomic();
}

/*disable一个tasklet,使之无法被SOFTIRQ调度运行*/
static inline void tasklet_disable(struct tasklet_struct *t)
{
	tasklet_disable_nosync(t);
	tasklet_unlock_wait(t);
	smp_mb();
}

static inline void tasklet_enable(struct tasklet_struct *t)
{
	smp_mb__before_atomic();
	atomic_dec(&t->count);
}

extern void tasklet_kill(struct tasklet_struct *t);
extern void tasklet_kill_immediate(struct tasklet_struct *t, unsigned int cpu);
/*动态初始化tasklet对象*/
extern void tasklet_init(struct tasklet_struct *t,
			 void (*func)(unsigned long), unsigned long data);

struct tasklet_hrtimer {
	struct hrtimer		timer;
	struct tasklet_struct	tasklet;
	enum hrtimer_restart	(*function)(struct hrtimer *);
};

extern void
tasklet_hrtimer_init(struct tasklet_hrtimer *ttimer,
		     enum hrtimer_restart (*function)(struct hrtimer *),
		     clockid_t which_clock, enum hrtimer_mode mode);

static inline
void tasklet_hrtimer_start(struct tasklet_hrtimer *ttimer, ktime_t time,
			   const enum hrtimer_mode mode)
{
	hrtimer_start(&ttimer->timer, time, mode);
}

static inline
void tasklet_hrtimer_cancel(struct tasklet_hrtimer *ttimer)
{
	hrtimer_cancel(&ttimer->timer);
	tasklet_kill(&ttimer->tasklet);
}

/*
 * Autoprobing for irqs:
 *
 * probe_irq_on() and probe_irq_off() provide robust primitives
 * for accurate IRQ probing during kernel initialization.  They are
 * reasonably simple to use, are not "fooled" by spurious interrupts,
 * and, unlike other attempts at IRQ probing, they do not get hung on
 * stuck interrupts (such as unused PS2 mouse interfaces on ASUS boards).
 *
 * For reasonably foolproof probing, use them as follows:
 *
 * 1. clear and/or mask the device's internal interrupt.
 * 2. sti();
 * 3. irqs = probe_irq_on();      // "take over" all unassigned idle IRQs
 * 4. enable the device and cause it to trigger an interrupt.
 * 5. wait for the device to interrupt, using non-intrusive polling or a delay.
 * 6. irq = probe_irq_off(irqs);  // get IRQ number, 0=none, negative=multiple
 * 7. service the device to clear its pending interrupt.
 * 8. loop again if paranoia is required.
 *
 * probe_irq_on() returns a mask of allocated irq's.
 *
 * probe_irq_off() takes the mask as a parameter,
 * and returns the irq number which occurred,
 * or zero if none occurred, or a negative irq number
 * if more than one irq occurred.
 */

#if !defined(CONFIG_GENERIC_IRQ_PROBE) 
static inline unsigned long probe_irq_on(void)
{
	return 0;
}
static inline int probe_irq_off(unsigned long val)
{
	return 0;
}
static inline unsigned int probe_irq_mask(unsigned long val)
{
	return 0;
}
#else
extern unsigned long probe_irq_on(void);	/* returns 0 on failure */
extern int probe_irq_off(unsigned long);	/* returns 0 or negative on failure */
extern unsigned int probe_irq_mask(unsigned long);	/* returns mask of ISA interrupts */
#endif

#ifdef CONFIG_PROC_FS
/* Initialize /proc/irq/ */
extern void init_irq_proc(void);
#else
static inline void init_irq_proc(void)
{
}
#endif

struct seq_file;
int show_interrupts(struct seq_file *p, void *v);
int arch_show_interrupts(struct seq_file *p, int prec);

extern int early_irq_init(void);
extern int arch_probe_nr_irqs(void);
extern int arch_early_irq_init(void);

#if defined(CONFIG_FUNCTION_GRAPH_TRACER) || defined(CONFIG_KASAN)
/*
 * We want to know which function is an entrypoint of a hardirq or a softirq.
 */
#define __irq_entry		 __attribute__((__section__(".irqentry.text")))
#define __softirq_entry  \
	__attribute__((__section__(".softirqentry.text")))

/* Limits of hardirq entrypoints */
extern char __irqentry_text_start[];
extern char __irqentry_text_end[];
/* Limits of softirq entrypoints */
extern char __softirqentry_text_start[];
extern char __softirqentry_text_end[];

#else
#define __irq_entry
#define __softirq_entry
#endif

#endif
