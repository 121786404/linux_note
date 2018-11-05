/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _linux_POSIX_TIMERS_H
#define _linux_POSIX_TIMERS_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/timex.h>
#include <linux/alarmtimer.h>

struct siginfo;

struct cpu_timer_list {
	struct list_head entry;
	u64 expires, incr;
	struct task_struct *task;
	int firing;
};

/*
 * Bit fields within a clockid:
 *
 * The most significant 29 bits hold either a pid or a file descriptor.
 *
 * Bit 2 indicates whether a cpu clock refers to a thread or a process.
 *
 * Bits 1 and 0 give the type: PROF=0, VIRT=1, SCHED=2, or FD=3.
 *
 * A clockid is invalid if bits 2, 1, and 0 are all set.
 */
#define CPUCLOCK_PID(clock)		((pid_t) ~((clock) >> 3))
#define CPUCLOCK_PERTHREAD(clock) \
	(((clock) & (clockid_t) CPUCLOCK_PERTHREAD_MASK) != 0)

#define CPUCLOCK_PERTHREAD_MASK	4
#define CPUCLOCK_WHICH(clock)	((clock) & (clockid_t) CPUCLOCK_CLOCK_MASK)
#define CPUCLOCK_CLOCK_MASK	3
#define CPUCLOCK_PROF		0
#define CPUCLOCK_VIRT		1
#define CPUCLOCK_SCHED		2
#define CPUCLOCK_MAX		3
#define CLOCKFD			CPUCLOCK_MAX
#define CLOCKFD_MASK		(CPUCLOCK_PERTHREAD_MASK|CPUCLOCK_CLOCK_MASK)

static inline clockid_t make_process_cpuclock(const unsigned int pid,
		const clockid_t clock)
{
	return ((~pid) << 3) | clock;
}
static inline clockid_t make_thread_cpuclock(const unsigned int tid,
		const clockid_t clock)
{
	return make_process_cpuclock(tid, clock | CPUCLOCK_PERTHREAD_MASK);
}

static inline clockid_t fd_to_clockid(const int fd)
{
	return make_process_cpuclock((unsigned int) fd, CLOCKFD);
}

static inline int clockid_to_fd(const clockid_t clk)
{
	return ~(clk >> 3);
}

#define REQUEUE_PENDING 1

/**
 * struct k_itimer - POSIX.1b interval timer structure.
 * @list:		List head for binding the timer to signals->posix_timers
 * @t_hash:		Entry in the posix timer hash table
 * @it_lock:		Lock protecting the timer
 * @kclock:		Pointer to the k_clock struct handling this timer
 * @it_clock:		The posix timer clock id
 * @it_id:		The posix timer id for identifying the timer
 * @it_active:		Marker that timer is active
 * @it_overrun:		The overrun counter for pending signals
 * @it_overrun_last:	The overrun at the time of the last delivered signal
 * @it_requeue_pending:	Indicator that timer waits for being requeued on
 *			signal delivery
 * @it_sigev_notify:	The notify word of sigevent struct for signal delivery
 * @it_interval:	The interval for periodic timers
 * @it_signal:		Pointer to the creators signal struct
 * @it_pid:		The pid of the process/task targeted by the signal
 * @it_process:		The task to wakeup on clock_nanosleep (CPU timers)
 * @sigq:		Pointer to preallocated sigqueue
 * @it:			Union representing the various posix timer type
 *			internals. Also used for rcu freeing the timer.
 */
struct k_itimer {
	/* 进程链表节点 */
	struct list_head	list;
	/* 全局哈希表节点 */
	struct hlist_node	t_hash;
	/**
	 * 保护本数据结构的spin lock
	 */
	spinlock_t		it_lock;
	const struct k_clock	*kclock;
	/*
	 * 以系统中哪一个clock为标准来计算超时时间
	 */
	clockid_t		it_clock;
	/* imer的ID，在一个进程中唯一标识该timer */
	timer_t			it_id;
	int			it_active;
	/**
	 * 用于overrun支持
	 * 当前的overrun计数
	 */
	s64			it_overrun;
	/**
	 * 上次overrun计数
	 */
	s64			it_overrun_last;
	/**
	 * 该timer对应信号挂入signal pending的状态
	 * LSB bit标识该signal已经挂入signal pending队列，其他的bit作为信号的私有数据
	 */
	int			it_requeue_pending;
	/**
	 * timer超期后如何异步通知该进程
	 * 如SIGEV_SIGNAL
	 */
	int			it_sigev_notify;
	ktime_t			it_interval;
	/**
	 * 该timer对应的signal descriptor
	 */
	struct signal_struct	*it_signal;
	/**
	 * 处理timer的线程
	 */
	union {
		struct pid		*it_pid;
		struct task_struct	*it_process;
	};
	/* 超期后，该sigquue成员会挂入signal pending队列 */
	struct sigqueue		*sigq;
	/**
	 * timer interval相关的信息
	 */
	union {
		/* real time clock */
		struct {
			struct hrtimer	timer;
		} real;
		struct cpu_timer_list	cpu;
		/* alarm timer相关的成员 */
		struct {
			struct alarm	alarmtimer;
		} alarm;
		struct rcu_head		rcu;
	} it;
};

void run_posix_cpu_timers(struct task_struct *task);
void posix_cpu_timers_exit(struct task_struct *task);
void posix_cpu_timers_exit_group(struct task_struct *task);
void set_process_cpu_timer(struct task_struct *task, unsigned int clock_idx,
			   u64 *newval, u64 *oldval);

void update_rlimit_cpu(struct task_struct *task, unsigned long rlim_new);

void posixtimer_rearm(struct siginfo *info);
#endif
