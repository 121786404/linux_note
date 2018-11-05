/*
 * workqueue.h --- work queue handling for Linux.
 */


/**
 * 使用了工作队列执行延迟操作的代码范例:
 
 //定义全局性的struct workqueue_struct(工作队列管理结构)指针demo_dev_wq
 static struct workqueue_struct * demo_dev_wq;
 
 //设备特定的数据结构,实际使用中大部分struct work_struct结构都内嵌在这个数据结构中
 struct demo_device {
 	...
	struct work_struct work;
	...
 }
 static struct demo_device *demo_dev;

 //定义延迟操作函数
 void demo_work_func(struct work_struct *work)
 {
 	...
 }

 //驱动程序模块初始化代码调用creat_singlethread_workqueue创建工作队列
 static int __init demo_dev_init(void)
 {
 	...
	demo_dev = kzalloc(sizeof(*demo_dev), GFP_KERNEL);
	demo_dev_wq = create_singlethread_workqueue("demo_dev_workqueue");
	INIT_WORK(&demo_dev->work, demo_work_func);
	...
 }

 //模块退出函数
 static void demo_dev_exit(void)
 {
 	...
	flush_workqueue(demo_dev_wq);
	destroy_workqueue(demo_dev_wq);
	...
 }

 //中断处理函数
 irqreturn_t demo_isr(iint irq, void *dev_id)
 {
 	...
	queue_work(demo_dev_wq, &demo_dev->work);
	...
 }

 */

#ifndef _LINUX_WORKQUEUE_H
#define _LINUX_WORKQUEUE_H

#include <linux/timer.h>
#include <linux/linkage.h>
#include <linux/bitops.h>
#include <linux/lockdep.h>
#include <linux/threads.h>
#include <linux/atomic.h>
#include <linux/cpumask.h>
#include <linux/rcupdate.h>
/* 工作队列管理结构*/
struct workqueue_struct;

struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);
void delayed_work_timer_fn(struct timer_list *t);

/*
 * The first word is the work queue pointer and the flags rolled into
 * one
 */
#define work_data_bits(work) ((unsigned long *)(&(work)->data))

enum {
/*
    正在pending执行
*/
	WORK_STRUCT_PENDING_BIT	= 0,	/* work item is pending execution */
/*
	被延时执行了
*/
	WORK_STRUCT_DELAYED_BIT	= 1,	/* work item is delayed */
/*
	work的data成员指向pwqs数据结构的指针，
	其中pwqs需要按照256Byte对齐，这样pwqs指针的低8位可以忽略
	只需要其余的bit就可以找回pwqs指针
*/
	WORK_STRUCT_PWQ_BIT	= 2,	/* data points to pwq */
/*
	下一个work连接到该work上
*/
	WORK_STRUCT_LINKED_BIT	= 3,	/* next work is linked to this one */
#ifdef CONFIG_DEBUG_OBJECTS_WORK
	WORK_STRUCT_STATIC_BIT	= 4,	/* static initializer (debugobjects) */
	WORK_STRUCT_COLOR_SHIFT	= 5,	/* color for workqueue flushing */
#else
	WORK_STRUCT_COLOR_SHIFT	= 4,	/* color for workqueue flushing */
#endif

	WORK_STRUCT_COLOR_BITS	= 4,

	WORK_STRUCT_PENDING	= 1 << WORK_STRUCT_PENDING_BIT,
	WORK_STRUCT_DELAYED	= 1 << WORK_STRUCT_DELAYED_BIT,
	WORK_STRUCT_PWQ		= 1 << WORK_STRUCT_PWQ_BIT,
	WORK_STRUCT_LINKED	= 1 << WORK_STRUCT_LINKED_BIT,
#ifdef CONFIG_DEBUG_OBJECTS_WORK
	WORK_STRUCT_STATIC	= 1 << WORK_STRUCT_STATIC_BIT,
#else
	WORK_STRUCT_STATIC	= 0,
#endif

	/*
	 * The last color is no color used for works which don't
	 * participate in workqueue flushing.
	 */
	WORK_NR_COLORS		= (1 << WORK_STRUCT_COLOR_BITS) - 1,
	WORK_NO_COLOR		= WORK_NR_COLORS,

	/* not bound to any CPU, prefer the local CPU */
/*
	不绑定到任何CPU上
*/
	WORK_CPU_UNBOUND	= NR_CPUS,

	/*
	 * Reserve 7 bits off of pwq pointer w/ debugobjects turned off.
	 * This makes pwqs aligned to 256 bytes and allows 15 workqueue
	 * flush colors.
	 */
	WORK_STRUCT_FLAG_BITS	= WORK_STRUCT_COLOR_SHIFT +
				  WORK_STRUCT_COLOR_BITS,

	/* data contains off-queue information when !WORK_STRUCT_PWQ */
	WORK_OFFQ_FLAG_BASE	= WORK_STRUCT_COLOR_SHIFT,

	__WORK_OFFQ_CANCELING	= WORK_OFFQ_FLAG_BASE,
	WORK_OFFQ_CANCELING	= (1 << __WORK_OFFQ_CANCELING),

	/*
	 * When a work item is off queue, its high bits point to the last
	 * pool it was on.  Cap at 31 bits and use the highest number to
	 * indicate that no pool is associated.
	 */
	WORK_OFFQ_FLAG_BITS	= 1,
	WORK_OFFQ_POOL_SHIFT	= WORK_OFFQ_FLAG_BASE + WORK_OFFQ_FLAG_BITS,
	WORK_OFFQ_LEFT		= BITS_PER_LONG - WORK_OFFQ_POOL_SHIFT,
	WORK_OFFQ_POOL_BITS	= WORK_OFFQ_LEFT <= 31 ? WORK_OFFQ_LEFT : 31,
	WORK_OFFQ_POOL_NONE	= (1LU << WORK_OFFQ_POOL_BITS) - 1,

	/* convenience constants */
	WORK_STRUCT_FLAG_MASK	= (1UL << WORK_STRUCT_FLAG_BITS) - 1,
	WORK_STRUCT_WQ_DATA_MASK = ~WORK_STRUCT_FLAG_MASK,
	WORK_STRUCT_NO_POOL	= (unsigned long)WORK_OFFQ_POOL_NONE << WORK_OFFQ_POOL_SHIFT,

	/* bit mask for work_busy() return values */
	WORK_BUSY_PENDING	= 1 << 0,
	WORK_BUSY_RUNNING	= 1 << 1,

	/* maximum string length for set_worker_desc() */
	WORKER_DESC_LEN		= 24,
};

struct work_struct {
	atomic_long_t data;	/* 驱动程序可以利用data来将设备驱动程序使用的某些指针传递给延迟函数*/
	struct list_head entry;	/* 双向链表对象,用来将提交的等待处理的工作节点形成链表*/
	work_func_t func;	/*工作节点的延迟函数,用来完成实际的延迟操作*/
#ifdef CONFIG_LOCKDEP
	struct lockdep_map lockdep_map;
#endif
};

#define WORK_DATA_INIT()	ATOMIC_LONG_INIT((unsigned long)WORK_STRUCT_NO_POOL)
#define WORK_DATA_STATIC_INIT()	\
	ATOMIC_LONG_INIT((unsigned long)(WORK_STRUCT_NO_POOL | WORK_STRUCT_STATIC))

/**
 * 使用queue_delayed_work时用到此结构体,实现延迟提交功能
 */
struct delayed_work {
	struct work_struct work;
	struct timer_list timer;	/*实现时间上的延迟操作*/

	/* target workqueue and CPU ->timer uses to queue ->work */
	struct workqueue_struct *wq;
	int cpu;
};

struct rcu_work {
	struct work_struct work;
	struct rcu_head rcu;

	/* target workqueue ->rcu uses to queue ->work */
	struct workqueue_struct *wq;
};

/**
 * struct workqueue_attrs - A struct for workqueue attributes.
 *
 * This can be used to change attributes of an unbound workqueue.
 */
struct workqueue_attrs {
	/**
	 * @nice: nice level
	 */
	int nice;

	/**
	 * @cpumask: allowed CPUs
	 */
	cpumask_var_t cpumask;

	/**
	 * @no_numa: disable NUMA affinity
	 *
	 * Unlike other fields, ``no_numa`` isn't a property of a worker_pool. It
	 * only modifies how :c:func:`apply_workqueue_attrs` select pools and thus
	 * doesn't participate in pool hash calculations or equality comparisons.
	 */
	bool no_numa;
};

static inline struct delayed_work *to_delayed_work(struct work_struct *work)
{
	return container_of(work, struct delayed_work, work);
}

static inline struct rcu_work *to_rcu_work(struct work_struct *work)
{
	return container_of(work, struct rcu_work, work);
}

struct execute_work {
	struct work_struct work;
};

#ifdef CONFIG_LOCKDEP
/*
 * NB: because we have to copy the lockdep_map, setting _key
 * here is required, otherwise it could get initialised to the
 * copy of the lockdep_map!
 */
#define __WORK_INIT_LOCKDEP_MAP(n, k) \
	.lockdep_map = STATIC_LOCKDEP_MAP_INIT(n, k),
#else
#define __WORK_INIT_LOCKDEP_MAP(n, k)
#endif

#define __WORK_INITIALIZER(n, f) {					\
	.data = WORK_DATA_STATIC_INIT(),				\
	.entry	= { &(n).entry, &(n).entry },				\
	.func = (f),							\
	__WORK_INIT_LOCKDEP_MAP(#n, &(n))				\
	}

#define __DELAYED_WORK_INITIALIZER(n, f, tflags) {			\
	.work = __WORK_INITIALIZER((n).work, (f)),			\
	.timer = __TIMER_INITIALIZER(delayed_work_timer_fn,\
				     (tflags) | TIMER_IRQSAFE),		\
	}

/*可以让驱动程序静态定义一个struct work_struct对象同时初始化*/
#define DECLARE_WORK(n, f)						\
	struct work_struct n = __WORK_INITIALIZER(n, f)

#define DECLARE_DELAYED_WORK(n, f)					\
	struct delayed_work n = __DELAYED_WORK_INITIALIZER(n, f, 0)

#define DECLARE_DEFERRABLE_WORK(n, f)					\
	struct delayed_work n = __DELAYED_WORK_INITIALIZER(n, f, TIMER_DEFERRABLE)

#ifdef CONFIG_DEBUG_OBJECTS_WORK
extern void __init_work(struct work_struct *work, int onstack);
extern void destroy_work_on_stack(struct work_struct *work);
extern void destroy_delayed_work_on_stack(struct delayed_work *work);
static inline unsigned int work_static(struct work_struct *work)
{
	return *work_data_bits(work) & WORK_STRUCT_STATIC;
}
#else
static inline void __init_work(struct work_struct *work, int onstack) { }
static inline void destroy_work_on_stack(struct work_struct *work) { }
static inline void destroy_delayed_work_on_stack(struct delayed_work *work) { }
static inline unsigned int work_static(struct work_struct *work) { return 0; }
#endif

/*
 * initialize all of a work item in one go
 *
 * NOTE! No point in using "atomic_long_set()": using a direct
 * assignment of the work data initializer allows the compiler
 * to generate better code.
 */
#ifdef CONFIG_LOCKDEP
#define __INIT_WORK(_work, _func, _onstack)				\
	do {								\
		static struct lock_class_key __key;			\
									\
		__init_work((_work), _onstack);				\
		(_work)->data = (atomic_long_t) WORK_DATA_INIT();	\
		lockdep_init_map(&(_work)->lockdep_map, "(work_completion)"#_work, &__key, 0); \
		INIT_LIST_HEAD(&(_work)->entry);			\
		(_work)->func = (_func);				\
	} while (0)
#else
/* 
初始化一个工作队列节点,INIT_WORK初始化struct work_struct中的每个成员
*/
#define __INIT_WORK(_work, _func, _onstack)				\
	do {								\
		__init_work((_work), _onstack);				\
		(_work)->data = (atomic_long_t) WORK_DATA_INIT();	\
		INIT_LIST_HEAD(&(_work)->entry);			\
		(_work)->func = (_func);				\
	} while (0)
#endif

#define INIT_WORK(_work, _func)						\
	__INIT_WORK((_work), (_func), 0)

#define INIT_WORK_ONSTACK(_work, _func)					\
	__INIT_WORK((_work), (_func), 1)

#define __INIT_DELAYED_WORK(_work, _func, _tflags)			\
	do {								\
		INIT_WORK(&(_work)->work, (_func));			\
		__init_timer(&(_work)->timer,				\
			     delayed_work_timer_fn,			\
			     (_tflags) | TIMER_IRQSAFE);		\
	} while (0)

#define __INIT_DELAYED_WORK_ONSTACK(_work, _func, _tflags)		\
	do {								\
		INIT_WORK_ONSTACK(&(_work)->work, (_func));		\
		__init_timer_on_stack(&(_work)->timer,			\
				      delayed_work_timer_fn,		\
				      (_tflags) | TIMER_IRQSAFE);	\
	} while (0)

#define INIT_DELAYED_WORK(_work, _func)					\
	__INIT_DELAYED_WORK(_work, _func, 0)

#define INIT_DELAYED_WORK_ONSTACK(_work, _func)				\
	__INIT_DELAYED_WORK_ONSTACK(_work, _func, 0)

#define INIT_DEFERRABLE_WORK(_work, _func)				\
	__INIT_DELAYED_WORK(_work, _func, TIMER_DEFERRABLE)

#define INIT_DEFERRABLE_WORK_ONSTACK(_work, _func)			\
	__INIT_DELAYED_WORK_ONSTACK(_work, _func, TIMER_DEFERRABLE)

#define INIT_RCU_WORK(_work, _func)					\
	INIT_WORK(&(_work)->work, (_func))

#define INIT_RCU_WORK_ONSTACK(_work, _func)				\
	INIT_WORK_ONSTACK(&(_work)->work, (_func))

/**
 * work_pending - Find out whether a work item is currently pending
 * @work: The work item in question
 */
#define work_pending(work) \
	test_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))

/**
 * delayed_work_pending - Find out whether a delayable work item is currently
 * pending
 * @w: The work item in question
 */
#define delayed_work_pending(w) \
	work_pending(&(w)->work)

/*
 * Workqueue flags and constants.  For details, please refer to
 * Documentation/core-api/workqueue.rst.
 */
enum {
    /*
    在kernel中，有两种线程池，一种是线程池是per cpu的，也就是说，系统中有多少个cpu，就会创建多少个线程池，
    cpu x上的线程池创建的worker线程也只会运行在cpu x上。
    另外一种是unbound thread pool，该线程池创建的worker线程可以调度到任意的cpu上去。
    由于cache locality的原因，per cpu的线程池的性能会好一些，但是对power saving有一些影响。
    设计往往如此，workqueue需要在performance和power saving之间平衡，想要更好的性能，
    那么最好让一个cpu上的worker thread来处理work，这样的话，cache命中率会比较高，性能会更好。
    
    但是，从电源管理的角度来看，最好的策略是让idle状态的cpu尽可能的保持idle，
    而不是反复idle，working，idle again。
    
    我们来一个例子辅助理解上面的内容。在t1时刻，work被调度到CPU A上执行，t2时刻work执行完毕，
    CPU A进入idle，t3时刻有一个新的work需要处理，这时候调度work到那个CPU会好些呢？
    是处于working状态的CPU B还是处于idle状态的CPU A呢？如果调度到CPU A上运行，那么，
    由于之前处理过work，其cache内容新鲜热辣，处理起work当然是得心应手，速度很快，
    但是，这需要将CPU A从idle状态中唤醒。选择CPU B呢就不存在将CPU 从idle状态唤醒，
    从而获取power saving方面的好处。
    
    了解了上面的基础内容之后，我们再来检视per cpu thread pool和unbound thread pool。
    当workqueue收到一个要处理的work，如果该workqueue是unbound类型的话，
    那么该work由unbound thread pool处理并把调度该work去哪一个CPU执行这样的策略交给系统的调度器模块来完成，
    对于scheduler而言，它会考虑CPU core的idle状态，从而尽可能的让CPU保持在idle状态，从而节省了功耗。
    
    因此，如果一个workqueue有WQ_UNBOUND这样的flag，则说明该workqueue上挂入的work处理是考虑到power saving的。
    如果workqueue没有WQ_UNBOUND flag，则说明该workqueue是per cpu的，这时候，
    调度哪一个CPU core运行worker thread来处理work已经不是scheduler可以控制的了，这样，也就间接影响了功耗。

    如果没有标记WQ_UNBOUND，那么缺省workqueue会创建per cpu thread pool来处理work
    */
	WQ_UNBOUND		= 1 << 1, /* not bound to any cpu */
/*
	标记 WQ_FREEZABLE 的工作队列会参与到系统的suspend过程中，这会让工作线程处理完成当前多有的work
	才完成进程冻结，并且这个过程不会再新开始一个work的执行，直到进程被解冻
*/
	WQ_FREEZABLE		= 1 << 2, /* freeze during suspend */
/*
	当内存紧张时，创建新的工作进程可能会失败，系统的rescuer进程会接管
*/
	WQ_MEM_RECLAIM		= 1 << 3, /* may be used for memory reclaim */
/*
	高优先级
*/
	WQ_HIGHPRI		= 1 << 4, /* high priority */
/*
	CPU消耗型，这类work执行会得到系统进程调度器的监管
	排在这类work后面的non-CPU-intensive类型的work可能会推迟
*/
	WQ_CPU_INTENSIVE	= 1 << 5, /* cpu intensive workqueue */
	WQ_SYSFS		= 1 << 6, /* visible in sysfs, see wq_sysfs_register() */

	/*
	 * Per-cpu workqueues are generally preferred because they tend to
	 * show better performance thanks to cache locality.  Per-cpu
	 * workqueues exclude the scheduler from choosing the CPU to
	 * execute the worker threads, which has an unfortunate side effect
	 * of increasing power consumption.
	 *
	 * The scheduler considers a CPU idle if it doesn't have any task
	 * to execute and tries to keep idle cores idle to conserve power;
	 * however, for example, a per-cpu work item scheduled from an
	 * interrupt handler on an idle CPU will force the scheduler to
	 * excute the work item on that CPU breaking the idleness, which in
	 * turn may lead to more scheduling choices which are sub-optimal
	 * in terms of power consumption.
	 *
	 * Workqueues marked with WQ_POWER_EFFICIENT are per-cpu by default
	 * but become unbound if workqueue.power_efficient kernel param is
	 * specified.  Per-cpu workqueues which are identified to
	 * contribute significantly to power-consumption are identified and
	 * marked with this flag and enabling the power_efficient mode
	 * leads to noticeable power saving at the cost of small
	 * performance disadvantage.
	 *
	 * http://thread.gmane.org/gmane.linux.kernel/1480396
	 */
	 /*
     各个workqueue需要通过WQ_POWER_EFFICIENT来标记自己在功耗方面的属性
     使用workqueue的用户知道自己在电源管理方面的特点，如果该workqueue在unbound的时候会极大的降低功耗，
     那么就需要加上WQ_POWER_EFFICIENT的标记。
	 */
	WQ_POWER_EFFICIENT	= 1 << 7,
/*
    销毁workqueue
*/
	__WQ_DRAINING		= 1 << 16, /* internal: workqueue is draining */
/*
	同一时间只能执行一个work item
*/
	__WQ_ORDERED		= 1 << 17, /* internal: workqueue is ordered */
	__WQ_LEGACY		= 1 << 18, /* internal: create*_workqueue() */
	__WQ_ORDERED_EXPLICIT	= 1 << 19, /* internal: alloc_ordered_workqueue() */

	WQ_MAX_ACTIVE		= 512,	  /* I like 512, better ideas? */
	WQ_MAX_UNBOUND_PER_CPU	= 4,	  /* 4 * #cpus for unbound wq */
	WQ_DFL_ACTIVE		= WQ_MAX_ACTIVE / 2,
};

/* unbound wq's aren't per-cpu, scale max_active according to #cpus */
#define WQ_UNBOUND_MAX_ACTIVE	\
	max_t(int, WQ_MAX_ACTIVE, num_possible_cpus() * WQ_MAX_UNBOUND_PER_CPU)

/*
 * System-wide workqueues which are always present.
 *
 * system_wq is the one used by schedule[_delayed]_work[_on]().
 * Multi-CPU multi-threaded.  There are users which expect relatively
 * short queue flush time.  Don't queue works which can run for too
 * long.
 *
 * system_highpri_wq is similar to system_wq but for work items which
 * require WQ_HIGHPRI.
 *
 * system_long_wq is similar to system_wq but may host long running
 * works.  Queue flushing might take relatively long.
 *
 * system_unbound_wq is unbound workqueue.  Workers are not bound to
 * any specific CPU, not concurrency managed, and all queued works are
 * executed immediately as long as max_active limit is not reached and
 * resources are available.
 *
 * system_freezable_wq is equivalent to system_wq except that it's
 * freezable.
 *
 * *_power_efficient_wq are inclined towards saving power and converted
 * into WQ_UNBOUND variants if 'wq_power_efficient' is enabled; otherwise,
 * they are same as their non-power-efficient counterparts - e.g.
 * system_power_efficient_wq is identical to system_wq if
 * 'wq_power_efficient' is disabled.  See WQ_POWER_EFFICIENT for more info.
 */
/*
系统中所有的工作队列，共享一组worker-pool
对于BOUND类型的工作队列，每个CPU只有两个工作线程池
每个工作线程池可以和多个workqueue对应
每个workqueue也只能对应这几个工作线程池
*/
/*
    普通优先级BOUND类型的工作队列 system_wq ， 名字为 "event",默认工作队列
*/
extern struct workqueue_struct *system_wq;
/*
    高优先级BOUND类型的工作队列 system_highpri_wq 名称为  "event_highpri"   
*/
extern struct workqueue_struct *system_highpri_wq;
extern struct workqueue_struct *system_long_wq;
/*
    UNBOUND类型的工作队列 
*/
extern struct workqueue_struct *system_unbound_wq;
/*
    Freezable类型的工作队列 
*/
extern struct workqueue_struct *system_freezable_wq;
/*
    省电类型的工作队列 
*/
extern struct workqueue_struct *system_power_efficient_wq;
extern struct workqueue_struct *system_freezable_power_efficient_wq;

extern struct workqueue_struct *
__alloc_workqueue_key(const char *fmt, unsigned int flags, int max_active,
	struct lock_class_key *key, const char *lock_name, ...) __printf(1, 6);

/**
 * alloc_workqueue - allocate a workqueue
 * @fmt: printf format for the name of the workqueue
 * @flags: WQ_* flags
 * @max_active: max in-flight work items, 0 for default
 * @args...: args for @fmt
 *
 * Allocate a workqueue with the specified parameters.  For detailed
 * information on WQ_* flags, please refer to
 * Documentation/core-api/workqueue.rst.
 *
 * The __lock_name macro dance is to guarantee that single lock_class_key
 * doesn't end up with different namesm, which isn't allowed by lockdep.
 *
 * RETURNS:
 * Pointer to the allocated workqueue on success, %NULL on failure.
 */
#ifdef CONFIG_LOCKDEP
#define alloc_workqueue(fmt, flags, max_active, args...)		\
({									\
	static struct lock_class_key __key;				\
	const char *__lock_name;					\
									\
	__lock_name = "(wq_completion)"#fmt#args;			\
									\
	__alloc_workqueue_key((fmt), (flags), (max_active),		\
			      &__key, __lock_name, ##args);		\
})
#else
#define alloc_workqueue(fmt, flags, max_active, args...)		\
	__alloc_workqueue_key((fmt), (flags), (max_active),		\
			      NULL, NULL, ##args)
#endif

/**
 * alloc_ordered_workqueue - allocate an ordered workqueue
 * @fmt: printf format for the name of the workqueue
 * @flags: WQ_* flags (only WQ_FREEZABLE and WQ_MEM_RECLAIM are meaningful)
 * @args...: args for @fmt
 *
 * Allocate an ordered workqueue.  An ordered workqueue executes at
 * most one work item at any given time in the queued order.  They are
 * implemented as unbound workqueues with @max_active of one.
 *
 * RETURNS:
 * Pointer to the allocated workqueue on success, %NULL on failure.
 */
#define alloc_ordered_workqueue(fmt, flags, args...)			\
	alloc_workqueue(fmt, WQ_UNBOUND | __WQ_ORDERED |		\
			__WQ_ORDERED_EXPLICIT | (flags), 1, ##args)

#define create_workqueue(name)						\
	alloc_workqueue("%s", __WQ_LEGACY | WQ_MEM_RECLAIM, 1, (name))
#define create_freezable_workqueue(name)				\
	alloc_workqueue("%s", __WQ_LEGACY | WQ_FREEZABLE | WQ_UNBOUND |	\
			WQ_MEM_RECLAIM, 1, (name))
/* 生成单线程的工作队列 singlethread=1,与create_workqueue函数的区别是
 * create_singlethread_workqueue只在系统中的第一个CPU(singlethread_cpu)上创建
 * 工作队列和工人线程,而create_workquque函数会在系统中的每个CPU上都创建工作
 * 队列和工人线程*/
#define create_singlethread_workqueue(name)				\
	alloc_ordered_workqueue("%s", __WQ_LEGACY | WQ_MEM_RECLAIM, name)

extern void destroy_workqueue(struct workqueue_struct *wq);

struct workqueue_attrs *alloc_workqueue_attrs(gfp_t gfp_mask);
void free_workqueue_attrs(struct workqueue_attrs *attrs);
int apply_workqueue_attrs(struct workqueue_struct *wq,
			  const struct workqueue_attrs *attrs);
int workqueue_set_unbound_cpumask(cpumask_var_t cpumask);
/* 在用queue_work向工作队列提交工作节点时，如果工作队列是singlethread类型的，
 * 因为此时只有一个worklist，所以工作节点只能提交到这唯一的一个worklist上，
 * 反之，如果工作队列不是singlethread类型的，那么工作节点将会提交到当前运行
 * queue_work的CPU所在的worklist中*/
extern bool queue_work_on(int cpu, struct workqueue_struct *wq,
			struct work_struct *work);
extern bool queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
			struct delayed_work *work, unsigned long delay);
extern bool mod_delayed_work_on(int cpu, struct workqueue_struct *wq,
			struct delayed_work *dwork, unsigned long delay);
extern bool queue_rcu_work(struct workqueue_struct *wq, struct rcu_work *rwork);

extern void flush_workqueue(struct workqueue_struct *wq);
extern void drain_workqueue(struct workqueue_struct *wq);

extern int schedule_on_each_cpu(work_func_t func);

int execute_in_process_context(work_func_t fn, struct execute_work *);

extern bool flush_work(struct work_struct *work);
extern bool cancel_work_sync(struct work_struct *work);

extern bool flush_delayed_work(struct delayed_work *dwork);
extern bool cancel_delayed_work(struct delayed_work *dwork);
extern bool cancel_delayed_work_sync(struct delayed_work *dwork);

extern bool flush_rcu_work(struct rcu_work *rwork);

extern void workqueue_set_max_active(struct workqueue_struct *wq,
				     int max_active);
extern struct work_struct *current_work(void);
extern bool current_is_workqueue_rescuer(void);
extern bool workqueue_congested(int cpu, struct workqueue_struct *wq);
extern unsigned int work_busy(struct work_struct *work);
extern __printf(1, 2) void set_worker_desc(const char *fmt, ...);
extern void print_worker_info(const char *log_lvl, struct task_struct *task);
extern void show_workqueue_state(void);
extern void wq_worker_comm(char *buf, size_t size, struct task_struct *task);

/**
 * queue_work - queue work on a workqueue
 * @wq: workqueue to use
 * @work: work to queue
 *
 * Returns %false if @work was already on a queue, %true otherwise.
 *
 * We queue the work to the CPU on which it was submitted, but if the CPU dies
 * it can be processed by another CPU.
 */
static inline bool queue_work(struct workqueue_struct *wq,
			      struct work_struct *work)
{
	return queue_work_on(WORK_CPU_UNBOUND, wq, work);
}

/**
 * queue_delayed_work - queue work on a workqueue after delay
 * @wq: workqueue to use
 * @dwork: delayable work to queue
 * @delay: number of jiffies to wait before queueing
 *
 * Equivalent to queue_delayed_work_on() but tries to use the local CPU.
 */
static inline bool queue_delayed_work(struct workqueue_struct *wq,
				      struct delayed_work *dwork,
				      unsigned long delay)
{
	return queue_delayed_work_on(WORK_CPU_UNBOUND, wq, dwork, delay);
}

/**
 * mod_delayed_work - modify delay of or queue a delayed work
 * @wq: workqueue to use
 * @dwork: work to queue
 * @delay: number of jiffies to wait before queueing
 *
 * mod_delayed_work_on() on local CPU.
 */
static inline bool mod_delayed_work(struct workqueue_struct *wq,
				    struct delayed_work *dwork,
				    unsigned long delay)
{
	return mod_delayed_work_on(WORK_CPU_UNBOUND, wq, dwork, delay);
}

/**
 * schedule_work_on - put work task on a specific cpu
 * @cpu: cpu to put the work task on
 * @work: job to be done
 *
 * This puts a job on a specific cpu
 */
static inline bool schedule_work_on(int cpu, struct work_struct *work)
{
	return queue_work_on(cpu, system_wq, work);
}

/**
 * schedule_work - put work task in global workqueue
 * @work: job to be done
 *
 * Returns %false if @work was already on the kernel-global workqueue and
 * %true otherwise.
 *
 * This puts a job in the kernel-global workqueue if it was not already
 * queued and leaves it in the same position on the kernel-global
 * workqueue otherwise.
 */
/*
    将worj挂载入系统默认的workqueue
*/
static inline bool schedule_work(struct work_struct *work)
{
	return queue_work(system_wq, work);
}

/**
 * flush_scheduled_work - ensure that any scheduled work has run to completion.
 *
 * Forces execution of the kernel-global workqueue and blocks until its
 * completion.
 *
 * Think twice before calling this function!  It's very easy to get into
 * trouble if you don't take great care.  Either of the following situations
 * will lead to deadlock:
 *
 *	One of the work items currently on the workqueue needs to acquire
 *	a lock held by your code or its caller.
 *
 *	Your code is running in the context of a work routine.
 *
 * They will be detected by lockdep when they occur, but the first might not
 * occur very often.  It depends on what work items are on the workqueue and
 * what locks they need, which you have no control over.
 *
 * In most situations flushing the entire workqueue is overkill; you merely
 * need to know that a particular work item isn't queued and isn't running.
 * In such cases you should use cancel_delayed_work_sync() or
 * cancel_work_sync() instead.
 */
static inline void flush_scheduled_work(void)
{
	flush_workqueue(system_wq);
}

/**
 * schedule_delayed_work_on - queue work in global workqueue on CPU after delay
 * @cpu: cpu to use
 * @dwork: job to be done
 * @delay: number of jiffies to wait
 *
 * After waiting for a given time this puts a job in the kernel-global
 * workqueue on the specified CPU.
 */
static inline bool schedule_delayed_work_on(int cpu, struct delayed_work *dwork,
					    unsigned long delay)
{
	return queue_delayed_work_on(cpu, system_wq, dwork, delay);
}

/**
 * schedule_delayed_work - put work task in global workqueue after delay
 * @dwork: job to be done
 * @delay: number of jiffies to wait or 0 for immediate execution
 *
 * After waiting for a given time this puts a job in the kernel-global
 * workqueue.
 */
static inline bool schedule_delayed_work(struct delayed_work *dwork,
					 unsigned long delay)
{
	return queue_delayed_work(system_wq, dwork, delay);
}

#ifndef CONFIG_SMP
static inline long work_on_cpu(int cpu, long (*fn)(void *), void *arg)
{
	return fn(arg);
}
static inline long work_on_cpu_safe(int cpu, long (*fn)(void *), void *arg)
{
	return fn(arg);
}
#else
long work_on_cpu(int cpu, long (*fn)(void *), void *arg);
long work_on_cpu_safe(int cpu, long (*fn)(void *), void *arg);
#endif /* CONFIG_SMP */

#ifdef CONFIG_FREEZER
extern void freeze_workqueues_begin(void);
extern bool freeze_workqueues_busy(void);
extern void thaw_workqueues(void);
#endif /* CONFIG_FREEZER */

#ifdef CONFIG_SYSFS
int workqueue_sysfs_register(struct workqueue_struct *wq);
#else	/* CONFIG_SYSFS */
static inline int workqueue_sysfs_register(struct workqueue_struct *wq)
{ return 0; }
#endif	/* CONFIG_SYSFS */

#ifdef CONFIG_WQ_WATCHDOG
void wq_watchdog_touch(int cpu);
#else	/* CONFIG_WQ_WATCHDOG */
static inline void wq_watchdog_touch(int cpu) { }
#endif	/* CONFIG_WQ_WATCHDOG */

#ifdef CONFIG_SMP
int workqueue_prepare_cpu(unsigned int cpu);
int workqueue_online_cpu(unsigned int cpu);
int workqueue_offline_cpu(unsigned int cpu);
#endif

int __init workqueue_init_early(void);
int __init workqueue_init(void);

#endif
