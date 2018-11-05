/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_H
#define _LINUX_SCHED_H

/*
 * Define 'struct task_struct' and provide the main scheduler
 * APIs (schedule(), wakeup variants, etc.)
 */

#include <uapi/linux/sched.h>

#include <asm/current.h>

#include <linux/pid.h>
#include <linux/sem.h>
#include <linux/shm.h>
#include <linux/kcov.h>
#include <linux/mutex.h>
#include <linux/plist.h>
#include <linux/hrtimer.h>
#include <linux/seccomp.h>
#include <linux/nodemask.h>
#include <linux/rcupdate.h>
#include <linux/resource.h>
#include <linux/latencytop.h>
#include <linux/sched/prio.h>
#include <linux/signal_types.h>
#include <linux/mm_types_task.h>
#include <linux/task_io_accounting.h>
#include <linux/rseq.h>

/* task_struct member predeclarations (sorted alphabetically): */
struct audit_context;
struct backing_dev_info;
struct bio_list;
struct blk_plug;
struct cfs_rq;
struct fs_struct;
struct futex_pi_state;
struct io_context;
struct mempolicy;
struct nameidata;
struct nsproxy;
struct perf_event_context;
struct pid_namespace;
struct pipe_inode_info;
struct rcu_node;
struct reclaim_state;
struct robust_list_head;
struct sched_attr;
struct sched_param;
struct seq_file;
struct sighand_struct;
struct signal_struct;
struct task_delay_info;
struct task_group;

/*
 * Task state bitmask. NOTE! These bits are also
 * encoded in fs/proc/array.c: get_task_state().
 *
 * We have two separate sets of flags: task->state
 * is about runnability, while task->exit_state are
 * about the task exiting. Confusing, but this way
 * modifying one set can't modify the other one by
 * mistake.
 */
 /*
 表示进程要么正在执行，要么正要准备执行（已经就绪），
 正在等待cpu时间片的调度
 */
/* Used in tsk->state: */
#define TASK_RUNNING			0x0000

/*
Linux 内核提供了两种方法将进程置为睡眠状态。
将进程置为睡眠状态的普通方法是
将进程状态设置为 TASK_INTERRUPTIBLE 或 TASK_UNINTERRUPTIBLE
并调用调度程序的 schedule() 函数。这样会将进程从 CPU 运行队列中移除。

如果进程处于可中断模式的睡眠状态(TASK_INTERRUPTIBLE)
那么可以通过显式的唤醒呼叫（wakeup_process()）
或需要处理的信号来唤醒它。

如果进程处于非可中断模式的睡眠状态(TASK_UNINTERRUPTIBLE)
那么只能通过显式的唤醒呼叫将其唤醒。

除非万不得已，否则我们建议您将进程置为可中断睡眠模式，
而不是不可中断睡眠模式
（比如说在设备 I/O 期间，处理信号非常困难时）。

当处于可中断睡眠模式的任务接收到信号时，
它需要处理该信号（除非它已被屏弊），
离开之前正在处理的任务（此处需要清除代码），
并将 -EINTR 返回给用户空间。
再一次，检查这些返回代码和采取适当操作的工作将由程序员完成。

因此，懒惰的程序员可能比较喜欢将进程置为
不可中断模式的睡眠状态，因为信号不会唤醒这类任务。

但需要注意的一种情况是，对不可中断睡眠模式的进程的
唤醒呼叫可能会由于某些原因不会发生，这会使进程无法被终止，
从而最终引发问题，因为惟一的解决方法就是重启系统。
一方面，您需要考虑一些细节，
因为不这样做会在内核端和用户端引入 bug。
另一方面，您可能会生成永远不会停止的进程
（被阻塞且无法终止的进程）。
*/
/*
进程因为等待一些条件而被挂起（阻塞）而所处的状态。
这些条件主要包括：硬中断、资源、一些信号……，
一旦等待的条件成立，进程就会从该状态（阻塞）迅速转化成为就绪状态TASK_RUNNING
*/
#define TASK_INTERRUPTIBLE		0x0001

/*
处于TASK_UNINTERRUPIBLE状态的进程，哪怕我们传递一个信号或者有一个外部中断
都不能唤醒他们。只有它所等待的资源可用的时候，他才会被唤醒。
这个标志很少用，但是并不代表没有任何用处，其实他的作用非常大，
特别是对于驱动刺探相关的硬件过程很重要，
这个刺探过程不能被一些其他的东西给中断，
否则就会让进程进入不可预测的状态
*/
#define TASK_UNINTERRUPTIBLE		0x0002
#define __TASK_STOPPED			0x0004
#define __TASK_TRACED			0x0008
/* Used in tsk->exit_state: */
#define EXIT_DEAD			0x0010

/*
进程的执行被终止，但是其父进程还没有使用wait()
等系统调用来获知它的终止信息，
此时进程成为僵尸进程
*/
#define EXIT_ZOMBIE			0x0020
#define EXIT_TRACE			(EXIT_ZOMBIE | EXIT_DEAD)
/* Used in tsk->state again: */
/* 进程的最终状态 */
/*
在设置了进程状态为TASK_DEAD后, 进程进入僵死状态,
进程已经无法被再次调度,
因为对应用程序或者用户空间来说此进程已经死了,
但是尽管进程已经不能再被调度，
但系统还是保留了它的进程描述符，这样做是为了让系统有办法在进程终止后仍能获得它的信息。
*/
#define TASK_PARKED			0x0040
/* 用于在接收到致命信号时唤醒进程 */
#define TASK_DEAD			0x0080
#define TASK_WAKEKILL			0x0100
#define TASK_WAKING			0x0200
#define TASK_NOLOAD			0x0400
#define TASK_NEW			0x0800
#define TASK_STATE_MAX			0x1000

/* Convenience macros for the sake of set_current_state: */
/*
完全处于TASK_UNINTERRUPTIBLE 状态的进程甚至都无法被“杀死”，
所以引入TASK_KILLABLE 的状态， 它等于“ TASK_WAKEKILL |TASK_UNINTERRUPTIBLE”，
可以响应致命信号。
*/
#define TASK_KILLABLE			(TASK_WAKEKILL | TASK_UNINTERRUPTIBLE)

/*
进程被停止执行，当进程接收到SIGSTOP、SIGTTIN、SIGTSTP或者SIGTTOU信号之后
就会进入该状态
*/
#define TASK_STOPPED			(TASK_WAKEKILL | __TASK_STOPPED)

/*
表示进程被debugger等进程监视，进程执行被调试程序所停止，
当一个进程被另外的进程所监视，每一个信号都会让进程进入该状态
*/
#define TASK_TRACED			(TASK_WAKEKILL | __TASK_TRACED)

#define TASK_IDLE			(TASK_UNINTERRUPTIBLE | TASK_NOLOAD)

/* Convenience macros for the sake of wake_up(): */
#define TASK_NORMAL			(TASK_INTERRUPTIBLE | TASK_UNINTERRUPTIBLE)

/* get_task_state(): */
#define TASK_REPORT			(TASK_RUNNING | TASK_INTERRUPTIBLE | \
					 TASK_UNINTERRUPTIBLE | __TASK_STOPPED | \
					 __TASK_TRACED | EXIT_DEAD | EXIT_ZOMBIE | \
					 TASK_PARKED)

#define task_is_traced(task)		((task->state & __TASK_TRACED) != 0)

#define task_is_stopped(task)		((task->state & __TASK_STOPPED) != 0)

#define task_is_stopped_or_traced(task)	((task->state & (__TASK_STOPPED | __TASK_TRACED)) != 0)

#define task_contributes_to_load(task)	((task->state & TASK_UNINTERRUPTIBLE) != 0 && \
					 (task->flags & PF_FROZEN) == 0 && \
					 (task->state & TASK_NOLOAD) == 0)

#ifdef CONFIG_DEBUG_ATOMIC_SLEEP

/*
 * Special states are those that do not use the normal wait-loop pattern. See
 * the comment with set_special_state().
 */
#define is_special_task_state(state)				\
	((state) & (__TASK_STOPPED | __TASK_TRACED | TASK_PARKED | TASK_DEAD))

#define __set_current_state(state_value)			\
	do {							\
		WARN_ON_ONCE(is_special_task_state(state_value));\
		current->task_state_change = _THIS_IP_;		\
		current->state = (state_value);			\
	} while (0)

#define set_current_state(state_value)				\
	do {							\
		WARN_ON_ONCE(is_special_task_state(state_value));\
		current->task_state_change = _THIS_IP_;		\
		smp_store_mb(current->state, (state_value));	\
	} while (0)

#define set_special_state(state_value)					\
	do {								\
		unsigned long flags; /* may shadow */			\
		WARN_ON_ONCE(!is_special_task_state(state_value));	\
		raw_spin_lock_irqsave(&current->pi_lock, flags);	\
		current->task_state_change = _THIS_IP_;			\
		current->state = (state_value);				\
		raw_spin_unlock_irqrestore(&current->pi_lock, flags);	\
	} while (0)
#else
/*
 * set_current_state() includes a barrier so that the write of current->state
 * is correctly serialised wrt the caller's subsequent test of whether to
 * actually sleep:
 *
 *   for (;;) {
 *	set_current_state(TASK_UNINTERRUPTIBLE);
 *	if (!need_sleep)
 *		break;
 *
 *	schedule();
 *   }
 *   __set_current_state(TASK_RUNNING);
 *
 * If the caller does not need such serialisation (because, for instance, the
 * condition test and condition change and wakeup are under the same lock) then
 * use __set_current_state().
 *
 * The above is typically ordered against the wakeup, which does:
 *
 *   need_sleep = false;
 *   wake_up_state(p, TASK_UNINTERRUPTIBLE);
 *
 * where wake_up_state() executes a full memory barrier before accessing the
 * task state.
 *
 * Wakeup will do: if (@state & p->state) p->state = TASK_RUNNING, that is,
 * once it observes the TASK_UNINTERRUPTIBLE store the waking CPU can issue a
 * TASK_RUNNING store which can collide with __set_current_state(TASK_RUNNING).
 *
 * However, with slightly different timing the wakeup TASK_RUNNING store can
 * also collide with the TASK_UNINTERRUPTIBLE store. Loosing that store is not
 * a problem either because that will result in one extra go around the loop
 * and our @cond test will save the day.
 *
 * Also see the comments of try_to_wake_up().
 */
#define __set_current_state(state_value)				\
	current->state = (state_value)

#define set_current_state(state_value)					\
	smp_store_mb(current->state, (state_value))

/*
 * set_special_state() should be used for those states when the blocking task
 * can not use the regular condition based wait-loop. In that case we must
 * serialize against wakeups such that any possible in-flight TASK_RUNNING stores
 * will not collide with our state change.
 */
#define set_special_state(state_value)					\
	do {								\
		unsigned long flags; /* may shadow */			\
		raw_spin_lock_irqsave(&current->pi_lock, flags);	\
		current->state = (state_value);				\
		raw_spin_unlock_irqrestore(&current->pi_lock, flags);	\
	} while (0)

#endif

/* Task command name length: */
#define TASK_COMM_LEN			16

extern void scheduler_tick(void);

#define	MAX_SCHEDULE_TIMEOUT		LONG_MAX

extern long schedule_timeout(long timeout);
extern long schedule_timeout_interruptible(long timeout);
extern long schedule_timeout_killable(long timeout);
extern long schedule_timeout_uninterruptible(long timeout);
extern long schedule_timeout_idle(long timeout);
asmlinkage void schedule(void);
extern void schedule_preempt_disabled(void);

extern int __must_check io_schedule_prepare(void);
extern void io_schedule_finish(int token);
extern long io_schedule_timeout(long timeout);
extern void io_schedule(void);

/**
 * struct prev_cputime - snapshot of system and user cputime
 * @utime: time spent in user mode
 * @stime: time spent in system mode
 * @lock: protects the above two fields
 *
 * Stores previous user/system time values such that we can guarantee
 * monotonicity.
 */
struct prev_cputime {
#ifndef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
	u64				utime;
	u64				stime;
	raw_spinlock_t			lock;
#endif
};

/**
 * struct task_cputime - collected CPU time counts
 * @utime:		time spent in user mode, in nanoseconds
 * @stime:		time spent in kernel mode, in nanoseconds
 * @sum_exec_runtime:	total time spent on the CPU, in nanoseconds
 *
 * This structure groups together three kinds of CPU time that are tracked for
 * threads and thread groups.  Most things considering CPU time want to group
 * these counts together and treat all three of them in parallel.
 */
struct task_cputime {
	u64				utime;
	u64				stime;
	unsigned long long		sum_exec_runtime;
};

/* Alternate field names when used on cache expirations: */
#define virt_exp			utime
#define prof_exp			stime
#define sched_exp			sum_exec_runtime

enum vtime_state {
	/* Task is sleeping or running in a CPU with VTIME inactive: */
	VTIME_INACTIVE = 0,
	/* Task runs in userspace in a CPU with VTIME active: */
	VTIME_USER,
	/* Task runs in kernelspace in a CPU with VTIME active: */
	VTIME_SYS,
};

struct vtime {
	seqcount_t		seqcount;
	unsigned long long	starttime;
	enum vtime_state	state;
	u64			utime;
	u64			stime;
	u64			gtime;
};

struct sched_info {
#ifdef CONFIG_SCHED_INFO
	/* Cumulative counters: */

	/* # of times we have run on this CPU: */
	unsigned long			pcount;

	/* Time spent waiting on a runqueue: */
	unsigned long long		run_delay;

	/* Timestamps: */

	/* When did we last run on a CPU? */
	unsigned long long		last_arrival;

	/* When were we last queued to run? */
	unsigned long long		last_queued;

#endif /* CONFIG_SCHED_INFO */
};

/*
 * Integer metrics need fixed point arithmetic, e.g., sched/fair
 * has a few: load, load_avg, util_avg, freq, and capacity.
 *
 * We define a basic fixed point arithmetic range, and then formalize
 * all these metrics based on that basic range.
 */
# define SCHED_FIXEDPOINT_SHIFT		10
# define SCHED_FIXEDPOINT_SCALE		(1L << SCHED_FIXEDPOINT_SHIFT)

struct load_weight {
	unsigned long			weight;
	u32				inv_weight;
};

/**
 * struct util_est - Estimation utilization of FAIR tasks
 * @enqueued: instantaneous estimated utilization of a task/cpu
 * @ewma:     the Exponential Weighted Moving Average (EWMA)
 *            utilization of a task
 *
 * Support data structure to track an Exponential Weighted Moving Average
 * (EWMA) of a FAIR task's utilization. New samples are added to the moving
 * average each time a task completes an activation. Sample's weight is chosen
 * so that the EWMA will be relatively insensitive to transient changes to the
 * task's workload.
 *
 * The enqueued attribute has a slightly different meaning for tasks and cpus:
 * - task:   the task's util_avg at last task dequeue time
 * - cfs_rq: the sum of util_est.enqueued for each RUNNABLE task on that CPU
 * Thus, the util_est.enqueued of a task represents the contribution on the
 * estimated utilization of the CPU where that task is currently enqueued.
 *
 * Only for tasks we track a moving average of the past instantaneous
 * estimated utilization. This allows to absorb sporadic drops in utilization
 * of an otherwise almost periodic task.
 */
struct util_est {
	unsigned int			enqueued;
	unsigned int			ewma;
#define UTIL_EST_WEIGHT_SHIFT		2
} __attribute__((__aligned__(sizeof(u64))));

/*
 * The load_avg/util_avg accumulates an infinite geometric series
 * (see __update_load_avg() in kernel/sched/fair.c).
 *
 * [load_avg definition]
 *
 *   load_avg = runnable% * scale_load_down(load)
 *
 * where runnable% is the time ratio that a sched_entity is runnable.
 * For cfs_rq, it is the aggregated load_avg of all runnable and
 * blocked sched_entities.
 *
 * load_avg may also take frequency scaling into account:
 *
 *   load_avg = runnable% * scale_load_down(load) * freq%
 *
 * where freq% is the CPU frequency normalized to the highest frequency.
 *
 * [util_avg definition]
 *
 *   util_avg = running% * SCHED_CAPACITY_SCALE
 *
 * where running% is the time ratio that a sched_entity is running on
 * a CPU. For cfs_rq, it is the aggregated util_avg of all runnable
 * and blocked sched_entities.
 *
 * util_avg may also factor frequency scaling and CPU capacity scaling:
 *
 *   util_avg = running% * SCHED_CAPACITY_SCALE * freq% * capacity%
 *
 * where freq% is the same as above, and capacity% is the CPU capacity
 * normalized to the greatest capacity (due to uarch differences, etc).
 *
 * N.B., the above ratios (runnable%, running%, freq%, and capacity%)
 * themselves are in the range of [0, 1]. To do fixed point arithmetics,
 * we therefore scale them to as large a range as necessary. This is for
 * example reflected by util_avg's SCHED_CAPACITY_SCALE.
 *
 * [Overflow issue]
 *
 * The 64-bit load_sum can have 4353082796 (=2^64/47742/88761) entities
 * with the highest load (=88761), always runnable on a single cfs_rq,
 * and should not overflow as the number already hits PID_MAX_LIMIT.
 *
 * For all other cases (including 32-bit kernels), struct load_weight's
 * weight will overflow first before we do, because:
 *
 *    Max(load_avg) <= Max(load.weight)
 *
 * Then it is the load_weight's responsibility to consider overflow
 * issues.
 */
/*
 进程负载
*/
struct sched_avg {
	u64				last_update_time;
	u64				load_sum;
	u64				runnable_load_sum;
	u32				util_sum;
	u32				period_contrib;
	unsigned long			load_avg;
	unsigned long			runnable_load_avg;
	unsigned long			util_avg;
	struct util_est			util_est;
} ____cacheline_aligned;

struct sched_statistics {
#ifdef CONFIG_SCHEDSTATS
	u64				wait_start;
	u64				wait_max;
	u64				wait_count;
	u64				wait_sum;
	u64				iowait_count;
	u64				iowait_sum;

	u64				sleep_start;
	u64				sleep_max;
	s64				sum_sleep_runtime;

	u64				block_start;
	u64				block_max;
	u64				exec_max;
	u64				slice_max;

	u64				nr_migrations_cold;
	u64				nr_failed_migrations_affine;
	u64				nr_failed_migrations_running;
	u64				nr_failed_migrations_hot;
	u64				nr_forced_migrations;

	u64				nr_wakeups;
	u64				nr_wakeups_sync;
	u64				nr_wakeups_migrate;
	u64				nr_wakeups_local;
	u64				nr_wakeups_remote;
	u64				nr_wakeups_affine;
	u64				nr_wakeups_affine_attempts;
	u64				nr_wakeups_passive;
	u64				nr_wakeups_idle;
#endif
};
/*
一个调度实体(红黑树的一个结点)，其包含一组或一个指定的进程，
包含一个自己的运行队列，一个父亲指针，
一个指向需要调度的运行队列指针
*/
struct sched_entity {
/*
    权重，在数组prio_to_weight[]包含优先级转权重的数值
    通过优先级转换而成，是vruntime计算的关键。
*/
	struct load_weight		load;
	unsigned long			runnable_weight;
/*
    实体在红黑树对应的结点信息
*/
	struct rb_node			run_node;
/*
    实体所在的进程组
*/
	struct list_head		group_node;
/*
实体是否处于红黑树运行队列中
表明是否处于CFS红黑树运行队列中，需要明确一个观点就是，
CFS运行队列里面包含有一个红黑树，
但这个红黑树并不是CFS运行队列的全部，
因为红黑树仅仅是用于选择出下一个调度程序的算法。
很简单的一个例子，普通程序运行时，其并不在红黑树中，
但是还是处于CFS运行队列中，其on_rq为真。
只有准备退出、即将睡眠等待和转为实时进程的进程其
CFS运行队列的on_rq为假。
*/
	unsigned int			on_rq;
/* 开始运行时间 */
	u64				exec_start;
/* 总运行时间 */
	u64				sum_exec_runtime;
/*
虚拟运行时间，调度的关键，其计算公式：
一次调度间隔的虚拟运行时间 = 实际运行时间 * (NICE_0_LOAD / 权重)。
可以看出跟实际运行时间和权重有关，
红黑树就是以此作为排序的标准，
优先级越高的进程在运行时其vruntime增长的越慢，
其可运行时间相对就长，而且也越有可能处于红黑树的最左结点，
调度器每次都选择最左边的结点为下一个调度进程。
注意其值为单调递增，在每个调度器的时钟中断时当前进程的
虚拟运行时间都会累加。单纯的说就是进程们都在比谁的vruntime最小，
最小的将被调度
*/
	u64				vruntime;
/* 进程在切换进CPU时的sum_exec_runtime值 */
	u64				prev_sum_exec_runtime;
/* 此调度实体中进程移到其他CPU组的数量 */
	u64				nr_migrations;

    /* 用于统计一些数据 */
	struct sched_statistics statistics;

#ifdef CONFIG_FAIR_GROUP_SCHED
    /* 代表此进程组的深度，每个进程组都比其parent调度组深度大1 */
	int				depth;
    /* 父亲调度实体指针，如果是进程则指向其运行队列的调度实体，
       * 如果是进程组则指向其上一个进程组的调度实体
       * 在 set_task_rq 函数中设置
       */
	struct sched_entity		*parent;
	/* rq on which this entity is (to be) queued: */
    /* 此调度实体所处于的CFS运行队列 */
	struct cfs_rq			*cfs_rq;
	/* rq "owned" by this entity/group: */
    /* 实体的红黑树运行队列，如果为NULL表明其是一个进程，
          若非NULL表明其是调度组 */
	struct cfs_rq			*my_q;
#endif

#ifdef CONFIG_SMP
	/*
	 * Per entity load average tracking.
	 *
	 * Put into separate cache line so it does not
	 * collide with read-mostly values above.
	 */
	struct sched_avg		avg;
#endif
};

struct sched_rt_entity {
	struct list_head		run_list;
	unsigned long			timeout;
	unsigned long			watchdog_stamp;
	unsigned int			time_slice;
	unsigned short			on_rq;
	unsigned short			on_list;

	struct sched_rt_entity		*back;
#ifdef CONFIG_RT_GROUP_SCHED
	struct sched_rt_entity		*parent;
	/* rq on which this entity is (to be) queued: */
	struct rt_rq			*rt_rq;
	/* rq "owned" by this entity/group: */
	struct rt_rq			*my_q;
#endif
} __randomize_layout;

struct sched_dl_entity {
	struct rb_node			rb_node;

	/*
	 * Original scheduling parameters. Copied here from sched_attr
	 * during sched_setattr(), they will remain the same until
	 * the next sched_setattr().
	 */
	u64				dl_runtime;	/* Maximum runtime for each instance	*/
	u64				dl_deadline;	/* Relative deadline of each instance	*/
	u64				dl_period;	/* Separation of two instances (period) */
	u64				dl_bw;		/* dl_runtime / dl_period		*/
	u64				dl_density;	/* dl_runtime / dl_deadline		*/

	/*
	 * Actual scheduling parameters. Initialized with the values above,
	 * they are continously updated during task execution. Note that
	 * the remaining runtime could be < 0 in case we are in overrun.
	 */
	s64				runtime;	/* Remaining runtime for this instance	*/
	u64				deadline;	/* Absolute deadline for this instance	*/
	unsigned int			flags;		/* Specifying the scheduler behaviour	*/

	/*
	 * Some bool flags:
	 *
	 * @dl_throttled tells if we exhausted the runtime. If so, the
	 * task has to wait for a replenishment to be performed at the
	 * next firing of dl_timer.
	 *
	 * @dl_boosted tells if we are boosted due to DI. If so we are
	 * outside bandwidth enforcement mechanism (but only until we
	 * exit the critical section);
	 *
	 * @dl_yielded tells if task gave up the CPU before consuming
	 * all its available runtime during the last job.
	 *
	 * @dl_non_contending tells if the task is inactive while still
	 * contributing to the active utilization. In other words, it
	 * indicates if the inactive timer has been armed and its handler
	 * has not been executed yet. This flag is useful to avoid race
	 * conditions between the inactive timer handler and the wakeup
	 * code.
	 *
	 * @dl_overrun tells if the task asked to be informed about runtime
	 * overruns.
	 */
	unsigned int			dl_throttled      : 1;
	unsigned int			dl_boosted        : 1;
	unsigned int			dl_yielded        : 1;
	unsigned int			dl_non_contending : 1;
	unsigned int			dl_overrun	  : 1;

	/*
	 * Bandwidth enforcement timer. Each -deadline task has its
	 * own bandwidth to be enforced, thus we need one timer per task.
	 */
	struct hrtimer			dl_timer;

	/*
	 * Inactive timer, responsible for decreasing the active utilization
	 * at the "0-lag time". When a -deadline task blocks, it contributes
	 * to GRUB's active utilization until the "0-lag time", hence a
	 * timer is needed to decrease the active utilization at the correct
	 * time.
	 */
	struct hrtimer inactive_timer;
};

union rcu_special {
	struct {
		u8			blocked;
		u8			need_qs;
		u8			exp_need_qs;

		/* Otherwise the compiler can store garbage here: */
		u8			pad;
	} b; /* Bits. */
	u32 s; /* Set of bits. */
};

enum perf_event_task_context {
	perf_invalid_context = -1,
	perf_hw_context = 0,
	perf_sw_context,
	perf_nr_task_contexts,
};

struct wake_q_node {
	struct wake_q_node *next;
};

struct task_struct {
#ifdef CONFIG_THREAD_INFO_IN_TASK
	/*
	 * For reasons of header soup (see current_thread_info()), this
	 * must be the first element of task_struct.
	 */
	struct thread_info		thread_info;
#endif
	/* -1 unrunnable, 0 runnable, >0 stopped: */
	volatile long			state;
	/*进程状态  TASK_RUNNING   -1 unrunnable, 0 runnable, >0 stopped */
    /* 进程内核栈 */
    /* 进程描述符task_struct结构中没有直接指向thread_info结构的指针*/
	/*
	 * This begins the randomizable portion of task_struct. Only
	 * scheduling-critical items should be added above here.
	 */
	randomized_struct_fields_start
    /*而是用一个void指针类型的成员表示，然后通过类型转换来访问thread_info结构 */
	void				*stack;
	/* 进程描述符使用计数，被置为2时，
	表示进程描述符正在被使用而且其相应的进程处于活动状态 */
	atomic_t			usage;
	/* 进程状态的信息，但不是运行状态，
	     用于内核识别进程当前的状态，以备下一步操作 */
	/* Per task flags (PF_*), defined further below: */
	unsigned int			flags;
	/* 成员ptrace被设置为0时表示不需要被跟踪 */
	unsigned int			ptrace;

#ifdef CONFIG_SMP
	struct llist_node		wake_entry;
	int				on_cpu;
#ifdef CONFIG_THREAD_INFO_IN_TASK
	/* Current CPU: */
	unsigned int			cpu;
#endif
	unsigned int			wakee_flips;
	unsigned long			wakee_flip_decay_ts;
	struct task_struct		*last_wakee;

	/*
	 * recent_used_cpu is initially set as the last CPU used by a task
	 * that wakes affine another task. Waker/wakee relationships can
	 * push tasks around a CPU where each wakeup moves to the next one.
	 * Tracking a recently used CPU allows a quick search for a recently
	 * used CPU that may be idle.
	 */
	int				recent_used_cpu;
	int				wake_cpu;
#endif
    /* 是否在运行队列 */
	int				on_rq;
	int				prio;
	int				static_prio;
    /*
        prio: 动态优先级，范围为100~139，与静态优先级和补偿(bonus)有关
              比如实时互斥量，需要暂时提高进程的优先级
        static_prio: 静态优先级，static_prio = 100 + nice + 20 (nice值为-20~19,所以static_prio值为100~139)
                    是进程启动时分配的优先级, 可以通过nice和sched_setscheduler系统调用来进行修改, 否则在进程运行期间会一直保持恒定
                    内核不存储nice值，用static_prio表示
        normal_prio 表示基于进程的静态优先级static_prio和调度策略计算出的优先级.
                    普通进程 normal_prio = static_prio
                    实时进程，会根据rt_priority 重新计算normal_prio
                    因此即使普通进程和实时进程具有相同的静态优先级,                    其普通优先级也是不同的,
                    进程分叉(fork)时, 子进程会继承父进程的普通优先级
    */
	int				normal_prio;
	/*
	    实时优先级
	*/
	unsigned int			rt_priority;
	/* 调度类
          目前系統中,Scheduling Class的优先级顺序为StopTask > RealTime > Fair > IdleTask
          开发者可以根据己的设计需求,來把所属的Task配置到不同的Scheduling Class中.
	*/
	const struct sched_class	*sched_class;
    /* 调度实体(红黑树的一个结点) */
	struct sched_entity		se;
    /* 调度实体(实时调度使用) */
	struct sched_rt_entity		rt;
#ifdef CONFIG_CGROUP_SCHED
    /* 指向其所在进程组 */
	struct task_group		*sched_task_group;
#endif
	struct sched_dl_entity		dl;

#ifdef CONFIG_PREEMPT_NOTIFIERS
	/* List of struct preempt_notifier: */
	/* preempt_notifier结构体链表 */
	struct hlist_head		preempt_notifiers;
#endif

#ifdef CONFIG_BLK_DEV_IO_TRACE
/* blktrace是一个针对Linux内核中块设备I/O层的跟踪工具 */
	unsigned int			btrace_seq;
#endif
    /* 调度策略 */
	unsigned int			policy;
	int				nr_cpus_allowed;
	/*
	掩码标志的每一位对应一个系统可用的处理器
	默认情况下，所有的位都被设置，
	进程可以在系统中所有可用的处理器上执行
	1 指定某个CPU专门处理某个任务，提高次任务的实时性
	2 保持高CPU缓存命中率
	*/
	cpumask_t			cpus_allowed;

#ifdef CONFIG_PREEMPT_RCU
/*RCU同步原语*/
	int				rcu_read_lock_nesting;
	union rcu_special		rcu_read_unlock_special;
	struct list_head		rcu_node_entry;
	struct rcu_node			*rcu_blocked_node;
#endif /* #ifdef CONFIG_PREEMPT_RCU */
#ifdef CONFIG_TASKS_RCU
	unsigned long			rcu_tasks_nvcsw;
	u8				rcu_tasks_holdout;
	u8				rcu_tasks_idx;
	int				rcu_tasks_idle_cpu;
	struct list_head		rcu_tasks_holdout_list;
#endif /* #ifdef CONFIG_TASKS_RCU */

/* 用于调度器统计进程的运行信息 */
	struct sched_info		sched_info;

/* 用于构建进程链表*/
	struct list_head		tasks;
#ifdef CONFIG_SMP
	struct plist_node		pushable_tasks;
	struct rb_node			pushable_dl_tasks;
#endif
    /* 进程所拥有的用户空间内存描述符，内核线程无的mm为NULL

     active_mm指向进程运行时所使用的内存描述符，
    对于普通进程而言，这两个指针变量的值相同。
    但是内核线程kernel thread是没有进程地址空间的，
    所以内核线程的tsk->mm域是空（NULL）。
    但是内核必须知道用户空间包含了什么，
    因此它的active_mm成员被初始化为前一个运行进程的active_mm值。

    因此如果当前内核线程被调度之前运行的也是另外一个内核线程时候，
    那么其mm和avtive_mm都是NULL
    */
	struct mm_struct		*mm;
	struct mm_struct		*active_mm;

	/* Per-thread vma caching: */
	struct vmacache			vmacache;

#ifdef SPLIT_RSS_COUNTING
    /* 用来记录缓冲信息 */
	struct task_rss_stat		rss_stat;
#endif
	int				exit_state;
	/*
	exit_code 用于设置进程的终止代号，这个值要么是_exit()或
	exit_group()系统调用参数（正常终止），
	要么是由内核提供的一个错误代号（异常终止）*/
	int				exit_code;
/*
	exit_signal 被置为-1时表示是某个线程组中的一员。
	只有当线程组的最后一个成员终止时，才会产生一个信号，
	以通知线程组的领头进程的父进程*/
	int				exit_signal;
	/* 用于判断父进程终止时发送信号 */
	/* The signal sent when the parent dies: */
	int				pdeath_signal;
	/* JOBCTL_*, siglock protected: */
	unsigned long			jobctl;

	/* Used for emulating ABI behavior of previous Linux versions: */
	/* 用于处理不同的ABI */
	unsigned int personality;

	/* Scheduler bits, serialized by scheduler locks: */
	/* fork子进程的时候让子进程恢复到默认的优先级或调度策略 */
	unsigned			sched_reset_on_fork:1;
	unsigned			sched_contributes_to_load:1;
	unsigned			sched_migrated:1;
	unsigned			sched_remote_wakeup:1;
	/* Force alignment to the next boundary: */
	unsigned			:0;

	/* Unserialized, strictly 'current' */
	/* 用于通知LSM是否被do_execve()函数所调用*/
	/* Bit to tell LSMs we're in execve(): */
	unsigned			in_execve:1;
	/* 用于判断是否进行iowait计数 */
	unsigned			in_iowait:1;
#ifndef TIF_RESTORE_SIGMASK
	unsigned			restore_sigmask:1;
#endif
#ifdef CONFIG_MEMCG
	unsigned			in_user_fault:1;
#ifdef CONFIG_MEMCG_KMEM
	unsigned			memcg_kmem_skip_account:1;
#endif
#endif
#ifdef CONFIG_COMPAT_BRK
    /* 用来确定对随机堆内存的探测。
    http://lkml.indiana.edu/hypermail/linux/kernel/1104.1/00196.html
    */
	unsigned			brk_randomized:1;
#endif
#ifdef CONFIG_CGROUPS
	/* disallow userland-initiated cgroup migration */
	unsigned			no_cgroup_migration:1;
#endif
#ifdef CONFIG_BLK_CGROUP
	/* to be used once the psi infrastructure lands upstream. */
	unsigned			use_memdelay:1;
#endif

	unsigned long			atomic_flags; /* Flags requiring atomic access. */

	struct restart_block		restart_block;

    /*
        Unix系统通过pid来标识进程，linux把不同的pid与系统中每个进程或轻量级线程关联，
        而unix程序员希望同一组线程具有共同的pid，遵照这个标准linux引入线程组的概念。
        一个线程组所有线程与领头线程具有相同的pid，存入tgid字段，
        getpid()返回当前进程的tgid值而不是pid的值
    */
    /*
    在内核本身和初始命名空间中唯一的ID，在系统启动期间开始的 init 进程即属于该初始命名空间。
    系统中每个进程都对应了该命名空间的一个PID，叫全局ID，保证在整个系统中唯一。
    */
	pid_t				pid;
	/*
	当前进程所在线程组的线程组ID
	*/
	pid_t				tgid;

#ifdef CONFIG_STACKPROTECTOR
	/* Canary value for the -fstack-protector GCC feature: */
	/* 防止内核堆栈溢出 在GCC编译内核时，需要加上-fstack-protector选项*/
	unsigned long			stack_canary;
#endif
	/*
	 * Pointers to the (original) parent process, youngest child, younger sibling,
	 * older sibling, respectively.  (p->father can be replaced with
	 * p->real_parent->pid)
	 */
	/* 指向其父进程，如果创建它的父进程不再存在，则指向PID为1的init进程 */
	/* Real parent process: */
	struct task_struct __rcu	*real_parent;
    /* 指向其父进程，当它终止时，必须向它的父进程发送信号。它的值通常与real_parent相同 */
	/* Recipient of SIGCHLD, wait4() reports: */
	struct task_struct __rcu	*parent;

	/*
	 * Children/sibling form the list of natural children:
	 */
	 /* 表示链表的头部，链表中的所有元素都是它的子进程 */
	struct list_head		children;
    /* 兄弟进程链表*/
	struct list_head		sibling;
	/*
	指向其所在进程组的领头进程
	除了在多线程的模式下指向主线程，还有一个用处，
	当一些进程组成一个群组时（PIDTYPE_PGID)， 该域指向该群组的leader
	全局的会话标识SID        保存在task_struct->group_leader->pids[PIDTYPE_SID].pid 中
	全局进程组标识PGlD 保存在task_struct->group_leader->pids[PIDTYPE_PGID].pid 中
	*/
	struct task_struct		*group_leader;

	/*
	 * 'ptraced' is the list of tasks this task is using ptrace() on.
	 *
	 * This includes both natural children and PTRACE_ATTACH targets.
	 * 'ptrace_entry' is this task's link on the p->parent->ptraced list.
	 */
	struct list_head		ptraced;
	struct list_head		ptrace_entry;

	/* PID/PID hash table linkage. */
	struct pid			*thread_pid;
	struct hlist_node		pid_links[PIDTYPE_MAX];
	/* 线程组中所有进程的链表*/
	struct list_head		thread_group;
	struct list_head		thread_node;
    /*
    do_fork函数
    在执行do_fork()时，如果给定特别标志，则vfork_done会指向一个特殊地址。
    如果copy_process函数的clone_flags参数的值被置为CLONE_CHILD_SETTID或CLONE_CHILD_CLEARTID，
    则会把child_tidptr参数的值分别复制到set_child_tid和clear_child_tid成员。
    这些标志说明必须改变子进程用户态地址空间的child_tidptr所指向的变量的值
    */
/*
    保证子进程先运行的 completion
*/
	struct completion		*vfork_done;

	/* CLONE_CHILD_SETTID: */
	int __user			*set_child_tid;

	/* CLONE_CHILD_CLEARTID: */
	int __user			*clear_child_tid;

	u64				utime;
	u64				stime;
    /*
    utime 用于记录进程在用户态/内核态下所经过的节拍数（定时器）
    utimescaled/stimescaled  用于记录进程在用户态/内核态的运行时间，但它们以处理器的频率为刻度
    */
#ifdef CONFIG_ARCH_HAS_SCALED_CPUTIME
	u64				utimescaled;
	u64				stimescaled;
#endif
	/* 以节拍计数的虚拟机运行时间（guest time） */
	u64				gtime;
	struct prev_cputime		prev_cputime;
#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
	struct vtime			vtime;
#endif

#ifdef CONFIG_NO_HZ_FULL
	atomic_t			tick_dep_mask;
#endif
    /* 是自愿（voluntary）/非自愿（involuntary）上下文切换计数 */
	/* Context switch counts: */
	unsigned long			nvcsw;
	unsigned long			nivcsw;
	/* 进程创建时间，real_start_time还包含了进程睡眠时间，常用于/proc/pid/stat */
	/* Monotonic time in nsecs: */
	u64				start_time;

	/* Boot based time in nsecs: */
	u64				real_start_time;

	/* MM fault and swap info: this can arguably be seen as either mm-specific or thread-specific: */
	unsigned long			min_flt;
	unsigned long			maj_flt;

/* 用来统计进程或进程组被跟踪的处理器时间，
其中的三个成员对应着cpu_timers[3]的三个链表 */
#ifdef CONFIG_POSIX_TIMERS
	struct task_cputime		cputime_expires;
	struct list_head		cpu_timers[3];
#endif

	/* Process credentials: */

	/* Tracer's credentials at attach: */
	const struct cred __rcu		*ptracer_cred;
/*
在进程对象在操作其他内核对象时候使用cred成员，
而在其他对象操作该进程对象的时候，需要获取该进程的credential的时候，
需要使用real_cred成员。
*/
	/* Objective and real subjective task credentials (COW): */
	const struct cred __rcu		*real_cred;

	/* Effective (overridable) subjective task credentials (COW): */
	const struct cred __rcu		*cred;

	/*
	 * executable name, excluding path.
	 *
	 * - normally initialized setup_new_exec()
	 * - access it with [gs]et_task_comm()
	 * - lock it with task_lock()
	 */
	 /* 相应的程序名*/
	char				comm[TASK_COMM_LEN];

	struct nameidata		*nameidata;

#ifdef CONFIG_SYSVIPC
/* 进程通信（SYSVIPC）*/
	struct sysv_sem			sysvsem;
	struct sysv_shm			sysvshm;
#endif
#ifdef CONFIG_DETECT_HUNG_TASK
/* hung task detection */
/* nvcsw和nivcsw的总和 */
	unsigned long			last_switch_count;
	unsigned long			last_switch_time;
#endif
	/* Filesystem information: */
/* fs用来表示进程与文件系统的联系，包括当前目录和根目录*/
	struct fs_struct		*fs;

	/* Open file information: */
/* files表示进程当前打开的文件*/
	struct files_struct		*files;

	/* Namespaces: */
/*
指针指向namespace相关的域，通过nsproxy域可以知道该task_struct属于哪个pid_namespace
*/
	struct nsproxy			*nsproxy;

	/* Signal handlers: */
/* 指向进程的信号描述符 */
	struct signal_struct		*signal;
/* 指向进程的信号处理程序描述符 */
	struct sighand_struct		*sighand;
	sigset_t			blocked;
/* 表示被阻塞信号的掩码，real_blocked表示临时掩码 */
	sigset_t			real_blocked;
	/* Restored if set_restore_sigmask() was used: */
	sigset_t			saved_sigmask;
	/* 存放私有挂起信号的数据结构 */
	struct sigpending		pending;
/* 是信号处理程序备用堆栈的地址，sas_ss_size表示堆栈的大小 */
	unsigned long			sas_ss_sp;
	size_t				sas_ss_size;
	unsigned int			sas_ss_flags;

	struct callback_head		*task_works;
    /* 进程审计*/
	struct audit_context		*audit_context;
#ifdef CONFIG_AUDITSYSCALL
/*
进程审计
*/
	kuid_t				loginuid;
	unsigned int			sessionid;
#endif
	struct seccomp			seccomp;

	/* Thread group tracking: */
/* 用于copy_process函数使用CLONE_PARENT 标记时*/
	u32				parent_exec_id;
	u32				self_exec_id;

	/* Protection against (de-)allocation: mm, files, fs, tty, keyrings, mems_allowed, mempolicy: */
 /* 于保护资源分配或释放的自旋锁 */
	spinlock_t			alloc_lock;

	/* Protection of the PI data structures: */
	/* task_rq_lock函数所使用的锁 */
	raw_spinlock_t			pi_lock;

	struct wake_q_node		wake_q;

#ifdef CONFIG_RT_MUTEXES
/* 基于PI协议的等待互斥锁，其中PI指的是priority inheritance（优先级继承） */
	/* PI waiters blocked on a rt_mutex held by this task: */
	struct rb_root_cached		pi_waiters;
	/* Updated under owner's pi_lock and rq lock */
	struct task_struct		*pi_top_task;
	/* Deadlock detection and priority inheritance handling: */
	struct rt_mutex_waiter		*pi_blocked_on;
#endif

#ifdef CONFIG_DEBUG_MUTEXES
/* 死锁检测*/
	/* Mutex deadlock detection: */
	struct mutex_waiter		*blocked_on;
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
	unsigned int			irq_events;
	unsigned long			hardirq_enable_ip;
	unsigned long			hardirq_disable_ip;
	unsigned int			hardirq_enable_event;
	unsigned int			hardirq_disable_event;
	int				hardirqs_enabled;
	int				hardirq_context;
	unsigned long			softirq_disable_ip;
	unsigned long			softirq_enable_ip;
	unsigned int			softirq_disable_event;
	unsigned int			softirq_enable_event;
	int				softirqs_enabled;
	int				softirq_context;
#endif

#ifdef CONFIG_LOCKDEP
# define MAX_LOCK_DEPTH			48UL
	u64				curr_chain_key;
	int				lockdep_depth;
	unsigned int			lockdep_recursion;
	struct held_lock		held_locks[MAX_LOCK_DEPTH];
#endif

#ifdef CONFIG_UBSAN
	unsigned int in_ubsan;
#endif

	/* Journalling filesystem info: */
/* JFS文件系统  */
	void				*journal_info;

	/* Stacked block device info: */
/* 块设备链表*/
	struct bio_list			*bio_list;

#ifdef CONFIG_BLOCK
	/* Stack plugging: */
	struct blk_plug			*plug;
#endif

	/* VM state: */
    /* 内存回收*/
	struct reclaim_state		*reclaim_state;
    /* 存放块设备I/O数据流量信息*/
	struct backing_dev_info		*backing_dev_info;
    /* I/O调度器所使用的信息*/
	struct io_context		*io_context;

	/* Ptrace state: */
	unsigned long			ptrace_message;
	siginfo_t			*last_siginfo;
	/* 记录进程的I/O计数*/
	struct task_io_accounting	ioac;
#ifdef CONFIG_TASK_XACCT
	/* Accumulated RSS usage: */
	u64				acct_rss_mem1;
	/* Accumulated virtual memory usage: */
	u64				acct_vm_mem1;
	/* stime + utime since last update: */
	u64				acct_timexpd;
#endif
#ifdef CONFIG_CPUSETS
	/* Protected by ->alloc_lock: */
	nodemask_t			mems_allowed;
	/* Seqence number to catch updates: */
	seqcount_t			mems_allowed_seq;
	int				cpuset_mem_spread_rotor;
	int				cpuset_slab_spread_rotor;
#endif
#ifdef CONFIG_CGROUPS
	/* Control Group info protected by css_set_lock: */
	struct css_set __rcu		*cgroups;
	/* cg_list protected by css_set_lock and tsk->alloc_lock: */
	struct list_head		cg_list;
#endif
#ifdef CONFIG_INTEL_RDT
	u32				closid;
	u32				rmid;
#endif
#ifdef CONFIG_FUTEX
/* futex同步机制 */
	struct robust_list_head __user	*robust_list;
#ifdef CONFIG_COMPAT
	struct compat_robust_list_head __user *compat_robust_list;
#endif
	struct list_head		pi_state_list;
	struct futex_pi_state		*pi_state_cache;
#endif
#ifdef CONFIG_PERF_EVENTS
	struct perf_event_context	*perf_event_ctxp[perf_nr_task_contexts];
	struct mutex			perf_event_mutex;
	struct list_head		perf_event_list;
#endif
#ifdef CONFIG_DEBUG_PREEMPT
	unsigned long			preempt_disable_ip;
#endif
#ifdef CONFIG_NUMA
/* 非一致内存访问（NUMA  Non-Uniform Memory Access）*/
/* Protected by alloc_lock: */
	struct mempolicy		*mempolicy;
	short				il_prev;
	short				pref_node_fork;
#endif
#ifdef CONFIG_NUMA_BALANCING
	int				numa_scan_seq;
	unsigned int			numa_scan_period;
	unsigned int			numa_scan_period_max;
	int				numa_preferred_nid;
	unsigned long			numa_migrate_retry;
	/* Migration stamp: */
	u64				node_stamp;
	u64				last_task_numa_placement;
	u64				last_sum_exec_runtime;
	struct callback_head		numa_work;

	struct numa_group		*numa_group;

	/*
	 * numa_faults is an array split into four regions:
	 * faults_memory, faults_cpu, faults_memory_buffer, faults_cpu_buffer
	 * in this precise order.
	 *
	 * faults_memory: Exponential decaying average of faults on a per-node
	 * basis. Scheduling placement decisions are made based on these
	 * counts. The values remain static for the duration of a PTE scan.
	 * faults_cpu: Track the nodes the process was running on when a NUMA
	 * hinting fault was incurred.
	 * faults_memory_buffer and faults_cpu_buffer: Record faults per node
	 * during the current scan window. When the scan completes, the counts
	 * in faults_memory and faults_cpu decay and these values are copied.
	 */
	unsigned long			*numa_faults;
	unsigned long			total_numa_faults;

	/*
	 * numa_faults_locality tracks if faults recorded during the last
	 * scan window were remote/local or failed to migrate. The task scan
	 * period is adapted based on the locality of the faults with different
	 * weights depending on whether they were shared or private faults
	 */
	unsigned long			numa_faults_locality[3];

	unsigned long			numa_pages_migrated;
#endif /* CONFIG_NUMA_BALANCING */

#ifdef CONFIG_RSEQ
	struct rseq __user *rseq;
	u32 rseq_len;
	u32 rseq_sig;
	/*
	 * RmW on rseq_event_mask must be performed atomically
	 * with respect to preemption.
	 */
	unsigned long rseq_event_mask;
#endif

	struct tlbflush_unmap_batch	tlb_ubc;

    /* RCU链表 */
	struct rcu_head			rcu;

	/* Cache last used pipe for splice(): */
	 /* 管道*/
	struct pipe_inode_info		*splice_pipe;

	struct page_frag		task_frag;

#ifdef CONFIG_TASK_DELAY_ACCT
    /*记录了该task的delay统计信息*/
	struct task_delay_info		*delays;
#endif

#ifdef CONFIG_FAULT_INJECTION
/* fault injection，参考内核说明文件linux-2.6.38.8/Documentation/fault-injection/fault-injection.txt */
	int make_it_fail;
	unsigned int			fail_nth;
#endif
	/*
	 * When (nr_dirtied >= nr_dirtied_pause), it's time to call
	 * balance_dirty_pages() for a dirty throttling pause:
	 */
	int				nr_dirtied;
	int				nr_dirtied_pause;
	/* Start of a write-and-pause period: */
	unsigned long			dirty_paused_when;

#ifdef CONFIG_LATENCYTOP
	int				latency_record_count;
	struct latency_record		latency_record[LT_SAVECOUNT];
#endif
	/*
	 * Time slack values; these are used to round up poll() and
	 * select() etc timeout values. These are in nanoseconds.
	 */
	 /* time slack values，常用于poll和select函数*/
	u64				timer_slack_ns;
	u64				default_timer_slack_ns;

#ifdef CONFIG_KASAN
	unsigned int			kasan_depth;
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/* Index of current stored address in ret_stack: */
	int				curr_ret_stack;

	/* Stack of return addresses for return function tracing: */
	struct ftrace_ret_stack		*ret_stack;

	/* Timestamp for last schedule: */
	unsigned long long		ftrace_timestamp;

	/*
	 * Number of functions that haven't been traced
	 * because of depth overrun:
	 */
	atomic_t			trace_overrun;

	/* Pause tracing: */
	atomic_t			tracing_graph_pause;
#endif

#ifdef CONFIG_TRACING
	/* State flags for use by tracers: */
	unsigned long			trace;

	/* Bitmask and counter of trace recursion: */
	unsigned long			trace_recursion;
#endif /* CONFIG_TRACING */

#ifdef CONFIG_KCOV
	/* Coverage collection mode enabled for this task (0 if disabled): */
	unsigned int			kcov_mode;

	/* Size of the kcov_area: */
	unsigned int			kcov_size;

	/* Buffer for coverage collection: */
	void				*kcov_area;

	/* KCOV descriptor wired with this task or NULL: */
	struct kcov			*kcov;
#endif

#ifdef CONFIG_MEMCG
	struct mem_cgroup		*memcg_in_oom;
	gfp_t				memcg_oom_gfp_mask;
	int				memcg_oom_order;

	/* Number of pages to reclaim on returning to userland: */
	unsigned int			memcg_nr_pages_over_high;

	/* Used by memcontrol for targeted memcg charge: */
	struct mem_cgroup		*active_memcg;
#endif

#ifdef CONFIG_BLK_CGROUP
	struct request_queue		*throttle_queue;
#endif

#ifdef CONFIG_UPROBES
	struct uprobe_task		*utask;
#endif
#if defined(CONFIG_BCACHE) || defined(CONFIG_BCACHE_MODULE)
	unsigned int			sequential_io;
	unsigned int			sequential_io_avg;
#endif
#ifdef CONFIG_DEBUG_ATOMIC_SLEEP
	unsigned long			task_state_change;
#endif
	int				pagefault_disabled;
#ifdef CONFIG_MMU
	struct task_struct		*oom_reaper_list;
#endif
#ifdef CONFIG_VMAP_STACK
	struct vm_struct		*stack_vm_area;
#endif
#ifdef CONFIG_THREAD_INFO_IN_TASK
	/* A live task holds one reference: */
	atomic_t			stack_refcount;
#endif
#ifdef CONFIG_LIVEPATCH
	int patch_state;
#endif
#ifdef CONFIG_SECURITY
	/* Used by LSM modules for access restriction: */
	void				*security;
#endif

	/*
	 * New fields for task_struct should be added above here, so that
	 * they are included in the randomized portion of task_struct.
	 */
	randomized_struct_fields_end

	/* CPU-specific state of this task: */
	struct thread_struct		thread;

	/*
	 * WARNING: on x86, 'thread_struct' contains a variable-sized
	 * structure.  It *MUST* be at the end of 'task_struct'.
	 *
	 * Do not put anything below here!
	 */
};

static inline struct pid *task_pid(struct task_struct *task)
{
	return task->thread_pid;
}

/*
 * the helpers to get the task's different pids as they are seen
 * from various namespaces
 *
 * task_xid_nr()     : global id, i.e. the id seen from the init namespace;
 * task_xid_vnr()    : virtual id, i.e. the id seen from the pid namespace of
 *                     current.
 * task_xid_nr_ns()  : id seen from the ns specified;
 *
 * see also pid_nr() etc in include/linux/pid.h
 */
pid_t __task_pid_nr_ns(struct task_struct *task, enum pid_type type, struct pid_namespace *ns);

static inline pid_t task_pid_nr(struct task_struct *tsk)
{
	return tsk->pid;
}

static inline pid_t task_pid_nr_ns(struct task_struct *tsk, struct pid_namespace *ns)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_PID, ns);
}

static inline pid_t task_pid_vnr(struct task_struct *tsk)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_PID, NULL);
}


static inline pid_t task_tgid_nr(struct task_struct *tsk)
{
	return tsk->tgid;
}

/**
 * pid_alive - check that a task structure is not stale
 * @p: Task structure to be checked.
 *
 * Test if a process is not yet dead (at most zombie state)
 * If pid_alive fails, then pointers within the task structure
 * can be stale and must not be dereferenced.
 *
 * Return: 1 if the process is alive. 0 otherwise.
 */
static inline int pid_alive(const struct task_struct *p)
{
	return p->thread_pid != NULL;
}

static inline pid_t task_pgrp_nr_ns(struct task_struct *tsk, struct pid_namespace *ns)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_PGID, ns);
}

static inline pid_t task_pgrp_vnr(struct task_struct *tsk)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_PGID, NULL);
}


static inline pid_t task_session_nr_ns(struct task_struct *tsk, struct pid_namespace *ns)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_SID, ns);
}

static inline pid_t task_session_vnr(struct task_struct *tsk)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_SID, NULL);
}

static inline pid_t task_tgid_nr_ns(struct task_struct *tsk, struct pid_namespace *ns)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_TGID, ns);
}

static inline pid_t task_tgid_vnr(struct task_struct *tsk)
{
	return __task_pid_nr_ns(tsk, PIDTYPE_TGID, NULL);
}

static inline pid_t task_ppid_nr_ns(const struct task_struct *tsk, struct pid_namespace *ns)
{
	pid_t pid = 0;

	rcu_read_lock();
	if (pid_alive(tsk))
		pid = task_tgid_nr_ns(rcu_dereference(tsk->real_parent), ns);
	rcu_read_unlock();

	return pid;
}

static inline pid_t task_ppid_nr(const struct task_struct *tsk)
{
	return task_ppid_nr_ns(tsk, &init_pid_ns);
}

/* Obsolete, do not use: */
static inline pid_t task_pgrp_nr(struct task_struct *tsk)
{
	return task_pgrp_nr_ns(tsk, &init_pid_ns);
}

#define TASK_REPORT_IDLE	(TASK_REPORT + 1)
#define TASK_REPORT_MAX		(TASK_REPORT_IDLE << 1)

static inline unsigned int task_state_index(struct task_struct *tsk)
{
	unsigned int tsk_state = READ_ONCE(tsk->state);
	unsigned int state = (tsk_state | tsk->exit_state) & TASK_REPORT;

	BUILD_BUG_ON_NOT_POWER_OF_2(TASK_REPORT_MAX);

	if (tsk_state == TASK_IDLE)
		state = TASK_REPORT_IDLE;

	return fls(state);
}

static inline char task_index_to_char(unsigned int state)
{
	static const char state_char[] = "RSDTtXZPI";

	BUILD_BUG_ON(1 + ilog2(TASK_REPORT_MAX) != sizeof(state_char) - 1);

	return state_char[state];
}

static inline char task_state_to_char(struct task_struct *tsk)
{
	return task_index_to_char(task_state_index(tsk));
}

/**
 * is_global_init - check if a task structure is init. Since init
 * is free to have sub-threads we need to check tgid.
 * @tsk: Task structure to be checked.
 *
 * Check if a task structure is the first user space task the kernel created.
 *
 * Return: 1 if the task structure is init. 0 otherwise.
 */
static inline int is_global_init(struct task_struct *tsk)
{
	return task_tgid_nr(tsk) == 1;
}

extern struct pid *cad_pid;

/*
 * Per process flags
 */
#define PF_IDLE			0x00000002	/* I am an IDLE thread */
/*
进程正在被删除
*/
#define PF_EXITING		0x00000004	/* Getting shut down */
#define PF_EXITPIDONE		0x00000008	/* PI exit done on shut down */
#define PF_VCPU			0x00000010	/* I'm a virtual CPU */
/*
PF_WQ_WORKER是用来标识该task是一个workqueue worker。
如果新创建的内核线程的确是一个workqueue worker的话，
那么在其worker thread function（worker_thread）中会进行设定的。
具体worker、workqueue等概念请参考Concurrency-managed workqueues相关的描述
*/
#define PF_WQ_WORKER		0x00000020	/* I'm a workqueue worker */
/* 进程刚创建，但还没执行*/
#define PF_FORKNOEXEC		0x00000040	/* Forked but didn't exec */
#define PF_MCE_PROCESS		0x00000080      /* Process policy on mce errors */
/*
标识进程曾经使用了super-user privileges（并不表示该进程有超级用户的权限）。
*/
#define PF_SUPERPRIV		0x00000100	/* Used super-user privileges */
#define PF_DUMPCORE		0x00000200	/* Dumped core */
/* 进程被信号(signal)杀出 */
#define PF_SIGNALED		0x00000400	/* Killed by a signal */
/*
    两处使用
   1) direct compaction 内存压缩
   2) Linux 3.6 为解决网络磁盘设备 network block device 使用交换分区时出现死锁的问题而引入
*/
#define PF_MEMALLOC		0x00000800	/* Allocating memory */
#define PF_NPROC_EXCEEDED	0x00001000	/* set_user() noticed that RLIMIT_NPROC was exceeded */
#define PF_USED_MATH		0x00002000	/* If unset the fpu must be initialized before use */
#define PF_USED_ASYNC		0x00004000	/* Used async_schedule*(), used by module init */
#define PF_NOFREEZE		0x00008000	/* This thread should not be frozen */
#define PF_FROZEN		0x00010000	/* Frozen for system suspend */
#define PF_KSWAPD		0x00020000	/* I am kswapd */
/*

*/
#define PF_MEMALLOC_NOFS	0x00040000	/* All allocation requests will inherit GFP_NOFS */
#define PF_MEMALLOC_NOIO	0x00080000	/* All allocation requests will inherit GFP_NOIO */
#define PF_LESS_THROTTLE	0x00100000	/* Throttle me less: I clean memory */
#define PF_KTHREAD		0x00200000	/* I am a kernel thread */
/*
内核不会为栈和内存映射的起点选择固定位置,
而是在每次新进程启动时随机改变这些值的设置.

这引入了一些复杂性, 例如, 使得攻击因缓冲区溢出导致的安全漏洞更加困难.
如果攻击者无法依靠固定地址找到栈
,那么想要构建恶意代码, 通过缓冲器溢出获得栈内存区域的访问权,
而后恶意操纵栈的内容,将会困难得多.
*/
#define PF_RANDOMIZE		0x00400000	/* Randomize virtual address space */
#define PF_SWAPWRITE		0x00800000	/* Allowed to write to swap */
/*
    不允许用户程序修改其CPU亲和性
*/
#define PF_NO_SETAFFINITY	0x04000000	/* Userland is not allowed to meddle with cpus_allowed */
#define PF_MCE_EARLY		0x08000000      /* Early kill for mce process policy */
#define PF_MUTEX_TESTER		0x20000000	/* Thread belongs to the rt mutex tester */
#define PF_FREEZER_SKIP		0x40000000	/* Freezer should not count it as freezable */
#define PF_SUSPEND_TASK		0x80000000      /* This thread called freeze_processes() and should not be frozen */

/*
 * Only the _current_ task can read/write to tsk->flags, but other
 * tasks can access tsk->flags in readonly mode for example
 * with tsk_used_math (like during threaded core dumping).
 * There is however an exception to this rule during ptrace
 * or during fork: the ptracer task is allowed to write to the
 * child->flags of its traced child (same goes for fork, the parent
 * can write to the child->flags), because we're guaranteed the
 * child is not running and in turn not changing child->flags
 * at the same time the parent does it.
 */
#define clear_stopped_child_used_math(child)	do { (child)->flags &= ~PF_USED_MATH; } while (0)
#define set_stopped_child_used_math(child)	do { (child)->flags |= PF_USED_MATH; } while (0)
#define clear_used_math()			clear_stopped_child_used_math(current)
#define set_used_math()				set_stopped_child_used_math(current)

#define conditional_stopped_child_used_math(condition, child) \
	do { (child)->flags &= ~PF_USED_MATH, (child)->flags |= (condition) ? PF_USED_MATH : 0; } while (0)

#define conditional_used_math(condition)	conditional_stopped_child_used_math(condition, current)

#define copy_to_stopped_child_used_math(child) \
	do { (child)->flags &= ~PF_USED_MATH, (child)->flags |= current->flags & PF_USED_MATH; } while (0)

/* NOTE: this will return 0 or PF_USED_MATH, it will never return 1 */
#define tsk_used_math(p)			((p)->flags & PF_USED_MATH)
#define used_math()				tsk_used_math(current)

static inline bool is_percpu_thread(void)
{
#ifdef CONFIG_SMP
	return (current->flags & PF_NO_SETAFFINITY) &&
		(current->nr_cpus_allowed  == 1);
#else
	return true;
#endif
}

/* Per-process atomic flags. */
#define PFA_NO_NEW_PRIVS		0	/* May not gain new privileges. */
#define PFA_SPREAD_PAGE			1	/* Spread page cache over cpuset */
#define PFA_SPREAD_SLAB			2	/* Spread some slab caches over cpuset */
#define PFA_SPEC_SSB_DISABLE		3	/* Speculative Store Bypass disabled */
#define PFA_SPEC_SSB_FORCE_DISABLE	4	/* Speculative Store Bypass force disabled*/

#define TASK_PFA_TEST(name, func)					\
	static inline bool task_##func(struct task_struct *p)		\
	{ return test_bit(PFA_##name, &p->atomic_flags); }

#define TASK_PFA_SET(name, func)					\
	static inline void task_set_##func(struct task_struct *p)	\
	{ set_bit(PFA_##name, &p->atomic_flags); }

#define TASK_PFA_CLEAR(name, func)					\
	static inline void task_clear_##func(struct task_struct *p)	\
	{ clear_bit(PFA_##name, &p->atomic_flags); }

TASK_PFA_TEST(NO_NEW_PRIVS, no_new_privs)
TASK_PFA_SET(NO_NEW_PRIVS, no_new_privs)

TASK_PFA_TEST(SPREAD_PAGE, spread_page)
TASK_PFA_SET(SPREAD_PAGE, spread_page)
TASK_PFA_CLEAR(SPREAD_PAGE, spread_page)

TASK_PFA_TEST(SPREAD_SLAB, spread_slab)
TASK_PFA_SET(SPREAD_SLAB, spread_slab)
TASK_PFA_CLEAR(SPREAD_SLAB, spread_slab)

TASK_PFA_TEST(SPEC_SSB_DISABLE, spec_ssb_disable)
TASK_PFA_SET(SPEC_SSB_DISABLE, spec_ssb_disable)
TASK_PFA_CLEAR(SPEC_SSB_DISABLE, spec_ssb_disable)

TASK_PFA_TEST(SPEC_SSB_FORCE_DISABLE, spec_ssb_force_disable)
TASK_PFA_SET(SPEC_SSB_FORCE_DISABLE, spec_ssb_force_disable)

static inline void
current_restore_flags(unsigned long orig_flags, unsigned long flags)
{
	current->flags &= ~flags;
	current->flags |= orig_flags & flags;
}

extern int cpuset_cpumask_can_shrink(const struct cpumask *cur, const struct cpumask *trial);
extern int task_can_attach(struct task_struct *p, const struct cpumask *cs_cpus_allowed);
#ifdef CONFIG_SMP
extern void do_set_cpus_allowed(struct task_struct *p, const struct cpumask *new_mask);
extern int set_cpus_allowed_ptr(struct task_struct *p, const struct cpumask *new_mask);
#else
static inline void do_set_cpus_allowed(struct task_struct *p, const struct cpumask *new_mask)
{
}
static inline int set_cpus_allowed_ptr(struct task_struct *p, const struct cpumask *new_mask)
{
	if (!cpumask_test_cpu(0, new_mask))
		return -EINVAL;
	return 0;
}
#endif

#ifndef cpu_relax_yield
#define cpu_relax_yield() cpu_relax()
#endif

extern int yield_to(struct task_struct *p, bool preempt);
extern void set_user_nice(struct task_struct *p, long nice);
extern int task_prio(const struct task_struct *p);

/**
 * task_nice - return the nice value of a given task.
 * @p: the task in question.
 *
 * Return: The nice value [ -20 ... 0 ... 19 ].
 */
static inline int task_nice(const struct task_struct *p)
{
	return PRIO_TO_NICE((p)->static_prio);
}

extern int can_nice(const struct task_struct *p, const int nice);
extern int task_curr(const struct task_struct *p);
extern int idle_cpu(int cpu);
extern int available_idle_cpu(int cpu);
extern int sched_setscheduler(struct task_struct *, int, const struct sched_param *);
extern int sched_setscheduler_nocheck(struct task_struct *, int, const struct sched_param *);
extern int sched_setattr(struct task_struct *, const struct sched_attr *);
extern int sched_setattr_nocheck(struct task_struct *, const struct sched_attr *);
extern struct task_struct *idle_task(int cpu);

/**
 * is_idle_task - is the specified task an idle task?
 * @p: the task in question.
 *
 * Return: 1 if @p is an idle task. 0 otherwise.
 */
static inline bool is_idle_task(const struct task_struct *p)
{
	return !!(p->flags & PF_IDLE);
}

extern struct task_struct *curr_task(int cpu);
extern void ia64_set_curr_task(int cpu, struct task_struct *p);

void yield(void);

/*
将内核栈和进程控制块thread_info融合在一起, 组成一个联合体thread_union

对每个进程，Linux内核都把两个不同的数据结构紧凑的存放在
一个单独为进程分配的内存区域中：
一个是内核态的进程堆栈stack
另一个是紧挨着进程描述符的小数据结构thread_info，叫做线程描述符。
这两个结构被紧凑的放在一个联合体中thread_union中,
这块区域32位上通常是8K=8192（占两个页框），
64位上通常是16K,其实地址必须是8192的整数倍。
出于效率考虑，内核让这8K(或者16K)空间占据连续的两个页框
并让第一个页框的起始地址是213的倍数。

内核态的进程访问处于内核数据段的栈，
这个栈不同于用户态的进程所用的栈。
用户态进程所用的栈，是在进程线性地址空间中；
而内核栈是当进程从用户空间进入内核空间时，
特权级发生变化，需要切换堆栈，
那么内核空间中使用的就是这个内核栈。

因为内核控制路径使用很少的栈空间，
所以只需要几千个字节的内核态堆栈。

需要注意的是，内核态堆栈仅用于内核例程，
Linux内核另外为中断提供了单独的硬中断栈和软中断栈

由于内核栈的大小是有限的，就会有发生溢出的可能，
比如调用嵌套太多、参数太多都会导致内核栈的使用超出设定的大小。
内核栈溢出的结果往往是系统崩溃，
因为溢出会覆盖掉本不该触碰的数据，
首当其冲的就是thread_info — 它就在内核栈的底部，
内核栈是从高地址往低地址生长的，一旦溢出首先就破坏了thread_info，
thread_info里存放着指向进程的指针等关键数据，迟早会被访问到，
那时系统崩溃就是必然的事。
*/
union thread_union {
#ifndef CONFIG_ARCH_TASK_STRUCT_ON_STACK
	struct task_struct task;
#endif
#ifndef CONFIG_THREAD_INFO_IN_TASK
	struct thread_info thread_info;
#endif
	unsigned long stack[THREAD_SIZE/sizeof(long)];
};

#ifndef CONFIG_THREAD_INFO_IN_TASK
extern struct thread_info init_thread_info;
#endif

extern unsigned long init_stack[THREAD_SIZE / sizeof(unsigned long)];

#ifdef CONFIG_THREAD_INFO_IN_TASK
static inline struct thread_info *task_thread_info(struct task_struct *task)
{
	return &task->thread_info;
}
#elif !defined(__HAVE_THREAD_FUNCTIONS)
# define task_thread_info(task)	((struct thread_info *)(task)->stack)
#endif

/*
 * find a task by one of its numerical ids
 *
 * find_task_by_pid_ns():
 *      finds a task by its pid in the specified namespace
 * find_task_by_vpid():
 *      finds a task by its virtual pid
 *
 * see also find_vpid() etc in include/linux/pid.h
 */

extern struct task_struct *find_task_by_vpid(pid_t nr);
extern struct task_struct *find_task_by_pid_ns(pid_t nr, struct pid_namespace *ns);

/*
 * find a task by its virtual pid and get the task struct
 */
extern struct task_struct *find_get_task_by_vpid(pid_t nr);

extern int wake_up_state(struct task_struct *tsk, unsigned int state);
extern int wake_up_process(struct task_struct *tsk);
extern void wake_up_new_task(struct task_struct *tsk);

#ifdef CONFIG_SMP
extern void kick_process(struct task_struct *tsk);
#else
static inline void kick_process(struct task_struct *tsk) { }
#endif

extern void __set_task_comm(struct task_struct *tsk, const char *from, bool exec);

static inline void set_task_comm(struct task_struct *tsk, const char *from)
{
	__set_task_comm(tsk, from, false);
}

extern char *__get_task_comm(char *to, size_t len, struct task_struct *tsk);
#define get_task_comm(buf, tsk) ({			\
	BUILD_BUG_ON(sizeof(buf) != TASK_COMM_LEN);	\
	__get_task_comm(buf, sizeof(buf), tsk);		\
})

#ifdef CONFIG_SMP
void scheduler_ipi(void);
extern unsigned long wait_task_inactive(struct task_struct *, long match_state);
#else
static inline void scheduler_ipi(void) { }
static inline unsigned long wait_task_inactive(struct task_struct *p, long match_state)
{
	return 1;
}
#endif

/*
 * Set thread flags in other task's structures.
 * See asm/thread_info.h for TIF_xxxx flags available:
 */
static inline void set_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	set_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline void clear_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	clear_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline void update_tsk_thread_flag(struct task_struct *tsk, int flag,
					  bool value)
{
	update_ti_thread_flag(task_thread_info(tsk), flag, value);
}

static inline int test_and_set_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	return test_and_set_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline int test_and_clear_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	return test_and_clear_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline int test_tsk_thread_flag(struct task_struct *tsk, int flag)
{
	return test_ti_thread_flag(task_thread_info(tsk), flag);
}

static inline void set_tsk_need_resched(struct task_struct *tsk)
{
	set_tsk_thread_flag(tsk,TIF_NEED_RESCHED);
}

static inline void clear_tsk_need_resched(struct task_struct *tsk)
{
	clear_tsk_thread_flag(tsk,TIF_NEED_RESCHED);
}

static inline int test_tsk_need_resched(struct task_struct *tsk)
{
	return unlikely(test_tsk_thread_flag(tsk,TIF_NEED_RESCHED));
}

/*
 * cond_resched() and cond_resched_lock(): latency reduction via
 * explicit rescheduling in places that are safe. The return
 * value indicates whether a reschedule was done in fact.
 * cond_resched_lock() will drop the spinlock before scheduling,
 */
#ifndef CONFIG_PREEMPT
extern int _cond_resched(void);
#else
static inline int _cond_resched(void) { return 0; }
#endif

#define cond_resched() ({			\
	___might_sleep(__FILE__, __LINE__, 0);	\
	_cond_resched();			\
})

extern int __cond_resched_lock(spinlock_t *lock);

#define cond_resched_lock(lock) ({				\
	___might_sleep(__FILE__, __LINE__, PREEMPT_LOCK_OFFSET);\
	__cond_resched_lock(lock);				\
})

static inline void cond_resched_rcu(void)
{
#if defined(CONFIG_DEBUG_ATOMIC_SLEEP) || !defined(CONFIG_PREEMPT_RCU)
	rcu_read_unlock();
	cond_resched();
	rcu_read_lock();
#endif
}

/*
 * Does a critical section need to be broken due to another
 * task waiting?: (technically does not depend on CONFIG_PREEMPT,
 * but a general need for low latency)
 */
static inline int spin_needbreak(spinlock_t *lock)
{
#ifdef CONFIG_PREEMPT
	return spin_is_contended(lock);
#else
	return 0;
#endif
}

static __always_inline bool need_resched(void)
{
	return unlikely(tif_need_resched());
}

/*
 * Wrappers for p->thread_info->cpu access. No-op on UP.
 */
#ifdef CONFIG_SMP

static inline unsigned int task_cpu(const struct task_struct *p)
{
#ifdef CONFIG_THREAD_INFO_IN_TASK
	return p->cpu;
#else
	return task_thread_info(p)->cpu;
#endif
}

extern void set_task_cpu(struct task_struct *p, unsigned int cpu);

#else

static inline unsigned int task_cpu(const struct task_struct *p)
{
	return 0;
}

static inline void set_task_cpu(struct task_struct *p, unsigned int cpu)
{
}

#endif /* CONFIG_SMP */

/*
 * In order to reduce various lock holder preemption latencies provide an
 * interface to see if a vCPU is currently running or not.
 *
 * This allows us to terminate optimistic spin loops and block, analogous to
 * the native optimistic spin heuristic of testing if the lock owner task is
 * running or not.
 */
#ifndef vcpu_is_preempted
# define vcpu_is_preempted(cpu)	false
#endif

extern long sched_setaffinity(pid_t pid, const struct cpumask *new_mask);
extern long sched_getaffinity(pid_t pid, struct cpumask *mask);

#ifndef TASK_SIZE_OF
#define TASK_SIZE_OF(tsk)	TASK_SIZE
#endif

#ifdef CONFIG_RSEQ

/*
 * Map the event mask on the user-space ABI enum rseq_cs_flags
 * for direct mask checks.
 */
enum rseq_event_mask_bits {
	RSEQ_EVENT_PREEMPT_BIT	= RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT_BIT,
	RSEQ_EVENT_SIGNAL_BIT	= RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL_BIT,
	RSEQ_EVENT_MIGRATE_BIT	= RSEQ_CS_FLAG_NO_RESTART_ON_MIGRATE_BIT,
};

enum rseq_event_mask {
	RSEQ_EVENT_PREEMPT	= (1U << RSEQ_EVENT_PREEMPT_BIT),
	RSEQ_EVENT_SIGNAL	= (1U << RSEQ_EVENT_SIGNAL_BIT),
	RSEQ_EVENT_MIGRATE	= (1U << RSEQ_EVENT_MIGRATE_BIT),
};

static inline void rseq_set_notify_resume(struct task_struct *t)
{
	if (t->rseq)
		set_tsk_thread_flag(t, TIF_NOTIFY_RESUME);
}

void __rseq_handle_notify_resume(struct ksignal *sig, struct pt_regs *regs);

static inline void rseq_handle_notify_resume(struct ksignal *ksig,
					     struct pt_regs *regs)
{
	if (current->rseq)
		__rseq_handle_notify_resume(ksig, regs);
}

static inline void rseq_signal_deliver(struct ksignal *ksig,
				       struct pt_regs *regs)
{
	preempt_disable();
	__set_bit(RSEQ_EVENT_SIGNAL_BIT, &current->rseq_event_mask);
	preempt_enable();
	rseq_handle_notify_resume(ksig, regs);
}

/* rseq_preempt() requires preemption to be disabled. */
static inline void rseq_preempt(struct task_struct *t)
{
	__set_bit(RSEQ_EVENT_PREEMPT_BIT, &t->rseq_event_mask);
	rseq_set_notify_resume(t);
}

/* rseq_migrate() requires preemption to be disabled. */
static inline void rseq_migrate(struct task_struct *t)
{
	__set_bit(RSEQ_EVENT_MIGRATE_BIT, &t->rseq_event_mask);
	rseq_set_notify_resume(t);
}

/*
 * If parent process has a registered restartable sequences area, the
 * child inherits. Only applies when forking a process, not a thread.
 */
static inline void rseq_fork(struct task_struct *t, unsigned long clone_flags)
{
	if (clone_flags & CLONE_THREAD) {
		t->rseq = NULL;
		t->rseq_len = 0;
		t->rseq_sig = 0;
		t->rseq_event_mask = 0;
	} else {
		t->rseq = current->rseq;
		t->rseq_len = current->rseq_len;
		t->rseq_sig = current->rseq_sig;
		t->rseq_event_mask = current->rseq_event_mask;
	}
}

static inline void rseq_execve(struct task_struct *t)
{
	t->rseq = NULL;
	t->rseq_len = 0;
	t->rseq_sig = 0;
	t->rseq_event_mask = 0;
}

#else

static inline void rseq_set_notify_resume(struct task_struct *t)
{
}
static inline void rseq_handle_notify_resume(struct ksignal *ksig,
					     struct pt_regs *regs)
{
}
static inline void rseq_signal_deliver(struct ksignal *ksig,
				       struct pt_regs *regs)
{
}
static inline void rseq_preempt(struct task_struct *t)
{
}
static inline void rseq_migrate(struct task_struct *t)
{
}
static inline void rseq_fork(struct task_struct *t, unsigned long clone_flags)
{
}
static inline void rseq_execve(struct task_struct *t)
{
}

#endif

#ifdef CONFIG_DEBUG_RSEQ

void rseq_syscall(struct pt_regs *regs);

#else

static inline void rseq_syscall(struct pt_regs *regs)
{
}

#endif

#endif
