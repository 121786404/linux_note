/*
 *	linux/kernel/softirq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
 *
 *	Distribute under GPLv2.
 *
 *	Rewritten. Old one was good in 2.2, but in 2.3 it was immoral. --ANK (990903)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/notifier.h>
#include <linux/percpu.h>
#include <linux/cpu.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/ftrace.h>
#include <linux/smp.h>
#include <linux/smpboot.h>
#include <linux/tick.h>
#include <linux/irq.h>

#define CREATE_TRACE_POINTS
#include <trace/events/irq.h>

/*
   - No shared variables, all the data are CPU local.
   - If a softirq needs serialization, let it serialize itself
     by its own spinlocks.
   - Even if softirq is serialized, only local cpu is marked for
     execution. Hence, we get something sort of weak cpu binding.
     Though it is still not clear, will it result in better locality
     or will not.

   Examples:
   - NET RX softirq. It is multithreaded and does not require
     any global serialization.
   - NET TX softirq. It kicks software netdevice queues, hence
     it is logically serialized per device, but this serialization
     is invisible to common code.
   - Tasklets: serialized wrt itself.
 */

#ifndef __ARCH_IRQ_STAT
/*
   每一个CPU有一个软中断状态信息变量
*/
DEFINE_PER_CPU_ALIGNED(irq_cpustat_t, irq_stat);
EXPORT_PER_CPU_SYMBOL(irq_stat);
#endif
// 软中断向量表
/*
每一个Linux 驱动都会拥有一个softirq_vec数组。并且在该Linux 驱动中最多可以指定NR_SOFTIRQS个类型的中断。
软中断和tasklet 的一个重要区别是
同类型的软中断处理程序可以在不同处理器上同时运行（以并发的方式访问使用软中断的驱动），
而同一类型的tasklet 不能在不同处理器上同时执行，
因此如果使用软中断的驱动程序使用了共享数据，需要使用锁、信号量等保护临界区数据。
*/
static struct softirq_action softirq_vec[NR_SOFTIRQS] __cacheline_aligned_in_smp;

DEFINE_PER_CPU(struct task_struct *, ksoftirqd);

const char * const softirq_to_name[NR_SOFTIRQS] = {
	"HI", "TIMER", "NET_TX", "NET_RX", "BLOCK", "IRQ_POLL",
	"TASKLET", "SCHED", "HRTIMER", "RCU"
};

/*
 * we cannot loop indefinitely here to avoid userspace starvation,
 * but we also don't want to introduce a worst case 1/HZ latency
 * to the pending events, so lets the scheduler to balance
 * the softirq load for us.
 */
static void wakeup_softirqd(void)
{
	/* Interrupts are disabled: no need to stop preemption */
	struct task_struct *tsk = __this_cpu_read(ksoftirqd);

	if (tsk && tsk->state != TASK_RUNNING)
		wake_up_process(tsk);// 唤醒ksoftirqd 线程所在的进程
}

/*
 * If ksoftirqd is scheduled, we do not want to process pending softirqs
 * right now. Let ksoftirqd handle this at its own rate, to get fairness,
 * unless we're doing some of the synchronous softirqs.
 */
#define SOFTIRQ_NOW_MASK ((1 << HI_SOFTIRQ) | (1 << TASKLET_SOFTIRQ))
static bool ksoftirqd_running(unsigned long pending)
{
	struct task_struct *tsk = __this_cpu_read(ksoftirqd);

	if (pending & SOFTIRQ_NOW_MASK)
		return false;
	return tsk && (tsk->state == TASK_RUNNING);
}

/*
 * preempt_count and SOFTIRQ_OFFSET usage:
 * - preempt_count is changed by SOFTIRQ_OFFSET on entering or leaving
 *   softirq processing.
 * - preempt_count is changed by SOFTIRQ_DISABLE_OFFSET (= 2 * SOFTIRQ_OFFSET)
 *   on local_bh_disable or local_bh_enable.
 * This lets us distinguish between whether we are currently processing
 * softirq and whether we just have bh disabled.
 */

/*
 * This one is for softirq.c-internal use,
 * where hardirqs are disabled legitimately:
 */
#ifdef CONFIG_TRACE_IRQFLAGS
void __local_bh_disable_ip(unsigned long ip, unsigned int cnt)
{
	unsigned long flags;

	WARN_ON_ONCE(in_irq());

	raw_local_irq_save(flags);
	/*
	 * The preempt tracer hooks into preempt_count_add and will break
	 * lockdep because it calls back into lockdep after SOFTIRQ_OFFSET
	 * is set and before current->softirq_enabled is cleared.
	 * We must manually increment preempt_count here and manually
	 * call the trace_preempt_off later.
	 */
	__preempt_count_add(cnt);
	/*
	 * Were softirqs turned off above:
	 */
	if (softirq_count() == (cnt & SOFTIRQ_MASK))
		trace_softirqs_off(ip);
	raw_local_irq_restore(flags);

	if (preempt_count() == cnt) {
#ifdef CONFIG_DEBUG_PREEMPT
		current->preempt_disable_ip = get_lock_parent_ip();
#endif
		trace_preempt_off(CALLER_ADDR0, get_lock_parent_ip());
	}
}
EXPORT_SYMBOL(__local_bh_disable_ip);
#endif /* CONFIG_TRACE_IRQFLAGS */

static void __local_bh_enable(unsigned int cnt)
{
	lockdep_assert_irqs_disabled();

	if (preempt_count() == cnt)
		trace_preempt_on(CALLER_ADDR0, get_lock_parent_ip());

	if (softirq_count() == (cnt & SOFTIRQ_MASK))
		trace_softirqs_on(_RET_IP_);

	__preempt_count_sub(cnt);
}

/*
 * Special-case - softirqs can safely be enabled by __do_softirq(),
 * without processing still-pending softirqs:
 */
void _local_bh_enable(void)
{
	WARN_ON_ONCE(in_irq());
	__local_bh_enable(SOFTIRQ_DISABLE_OFFSET);
}
EXPORT_SYMBOL(_local_bh_enable);

void __local_bh_enable_ip(unsigned long ip, unsigned int cnt)
{
/*
    in_irq 表示现在正在硬件中断上下文  。 irqs_disabled表明处于关中断状态
    有一些不规范的driver ，可能在 primary handler中调用 local_bh_enable / local_bh_disable
    其实 primary handler 是在关中断环境下执行的，关中断是比关BH更强烈的锁机制，
    此时没有必要再关 BH 
*/
	WARN_ON_ONCE(in_irq());
	lockdep_assert_irqs_enabled();
#ifdef CONFIG_TRACE_IRQFLAGS
	local_irq_disable();
#endif
	/*
	 * Are softirqs going to be turned on now:
	 */
	if (softirq_count() == SOFTIRQ_DISABLE_OFFSET)
		trace_softirqs_on(ip);
	/*
	 * Keep preemption disabled until we are done with
	 * softirq processing:
	 */
	preempt_count_sub(cnt - 1);

	if (unlikely(!in_interrupt() && local_softirq_pending())) {
		/*
		 * Run softirq if any pending. And do it in its own stack
		 * as we may be calling this deep in a task call stack already.
		 */
/*
		 在非中断上下文环境下执行软中断处理
*/
		do_softirq();
	}
/*
    打开抢占
*/
	preempt_count_dec();
#ifdef CONFIG_TRACE_IRQFLAGS
	local_irq_enable();
#endif
/*
    之前执行软中断处理时可能会漏掉一些高优先级任务的抢占需求，这里重新检查
*/
	preempt_check_resched();
}
EXPORT_SYMBOL(__local_bh_enable_ip);

/*
 * We restart softirq processing for at most MAX_SOFTIRQ_RESTART times,
 * but break the loop if need_resched() is set or after 2 ms.
 * The MAX_SOFTIRQ_TIME provides a nice upper bound in most cases, but in
 * certain cases, such as stop_machine(), jiffies may cease to
 * increment and so we need the MAX_SOFTIRQ_RESTART limit as
 * well to make sure we eventually return from this method.
 *
 * These limits have been established via experimentation.
 * The two things to balance is latency against fairness -
 * we want to handle softirqs as soon as possible, but they
 * should not be able to lock up the box.
 */
#define MAX_SOFTIRQ_TIME  msecs_to_jiffies(2)
#define MAX_SOFTIRQ_RESTART 10

#ifdef CONFIG_TRACE_IRQFLAGS
/*
 * When we run softirqs from irq_exit() and thus on the hardirq stack we need
 * to keep the lockdep irq context tracking as tight as possible in order to
 * not miss-qualify lock contexts and miss possible deadlocks.
 */

static inline bool lockdep_softirq_start(void)
{
	bool in_hardirq = false;

	if (trace_hardirq_context(current)) {
		in_hardirq = true;
		trace_hardirq_exit();
	}

	lockdep_softirq_enter();

	return in_hardirq;
}

static inline void lockdep_softirq_end(bool in_hardirq)
{
	lockdep_softirq_exit();

	if (in_hardirq)
		trace_hardirq_enter();
}
#else
static inline bool lockdep_softirq_start(void) { return false; }
static inline void lockdep_softirq_end(bool in_hardirq) { }
#endif
/*
函数的核心思想是:从CPU本地的_softirq_pending的最低位开始,
依次往高位扫描,如果发现某位为1,说明对应该位有个等待中的softirq需要处理,
那么就调用soft_vec数组中对应项的action函数,这个过程会一直持续下去,知道__softirq_pending为0
*/
asmlinkage __visible void __softirq_entry __do_softirq(void)
{
	unsigned long end = jiffies + MAX_SOFTIRQ_TIME;
	unsigned long old_flags = current->flags;
	int max_restart = MAX_SOFTIRQ_RESTART;
	struct softirq_action *h;
	bool in_hardirq;
	__u32 pending;
	int softirq_bit;

	/*
	 * Mask out PF_MEMALLOC s current task context is borrowed for the
	 * softirq. A softirq handled such as network RX might set PF_MEMALLOC
	 * again if the socket is related to swap
	 */
	current->flags &= ~PF_MEMALLOC;

/*
    获取local cpu __softirq_pending
*/
	pending = local_softirq_pending();
	account_irq_enter_time(current);

    // 禁止底半部
	__local_bh_disable_ip(_RET_IP_, SOFTIRQ_OFFSET);
    // 禁止软中断再次执行
	in_hardirq = lockdep_softirq_start();

restart:
	/* Reset the pending bitmask before enabling irqs */
/*
	清除 __softirq_pending
*/
	set_softirq_pending(0);
/*
    打开本地中断
*/
	local_irq_enable();

	h = softirq_vec;
    
    // 在这个循环中会根据pending 每一位的值调用相应类型的软中断处理函数，依次循环直到所有的软中断处理完成
    // ffs 会寻找 pending 中第一个置位的bit位
	while ((softirq_bit = ffs(pending))) {
		unsigned int vec_nr;
		int prev_count;

		h += softirq_bit - 1;

		vec_nr = h - softirq_vec;
		prev_count = preempt_count();

		kstat_incr_softirqs_this_cpu(vec_nr);

		trace_softirq_entry(vec_nr);
        // 执行相应类型的款中断处理程序
		h->action(h);
		trace_softirq_exit(vec_nr);
		if (unlikely(prev_count != preempt_count())) {
			pr_err("huh, entered softirq %u %s %p with preempt_count %08x, exited with %08x?\n",
			       vec_nr, softirq_to_name[vec_nr], h->action,
			       prev_count, preempt_count());
			preempt_count_set(prev_count);
		}
		h++;
        // 将pending 向右移位，然后继续判断最低位是否为1
		pending >>= softirq_bit;
	}

	rcu_bh_qs();
/*
	关闭本地中断
*/
	local_irq_disable();

/* 
历完成后，会再次获取__softirq_pending，如果在遍历softirq_vec期间又产生了softirq，
__do_softirq会跳转到restart再次遍历处理softirq。
软中断执行过程是开中断的，有可能在这个过程中又发生了中断以及触发了软中断，即有人调用了 raise_softirq
*/
	pending = local_softirq_pending();
	if (pending) {
	    // 最多执行 MAX_SOFTIRQ_RESTART（10） 次
	    // 最多执行 MAX_SOFTIRQ_TIME 2ms
	    // 上次的softirq中没有设定TIF_NEED_RESCHED，也就是说没有有高优先级任务需要调度
		if (time_before(jiffies, end) && !need_resched() &&
		    --max_restart)
			goto restart;

        // 如果还有softirq的pending，则唤醒ksoftirqd内核线程来处理剩余的softirq
		wakeup_softirqd();
	}

	lockdep_softirq_end(in_hardirq);
	account_irq_exit_time(current);
/*
	和 __local_bh_disable_ip(_RET_IP_, SOFTIRQ_OFFSET) 配对
*/
	__local_bh_enable(SOFTIRQ_OFFSET);
	WARN_ON_ONCE(in_interrupt());
	current_restore_flags(old_flags, PF_MEMALLOC);
}

asmlinkage __visible void do_softirq(void)
{
    // 用于保存软中断的标记，这里用了一个32 位的无符号整数表示最多32 个软中断类型
    // 最多支持32 个软中断类型就是从这里来的
    // 第n 位表示对应类型的软中断已做标记，等待处理。目前最多用了10 位

	__u32 pending;
	unsigned long flags;

    // 如果当前正在中断处理函数中，直接返回
	if (in_interrupt())
		return;

    // 当前软中断的状态已被保存，可以将软中断的状态清零了
	local_irq_save(flags);

    // 获取当前驱动的软中断注册标记值
	pending = local_softirq_pending();

    // 如果pending 不为零（至少有一位被设置成1，开始调用软中断处理程序
	if (pending && !ksoftirqd_running(pending))
		do_softirq_own_stack();

    // 恢复软中断的状态
	local_irq_restore(flags);
}

/*
 * Enter an interrupt context.
 */
/*
HARDIRQ部分的开始,告诉系统进入冲断处理的上半部分，
与irq_enter对应的是irq_exit,irq_enter会更新系统中的一些统计量，
同时会把当前栈中的preemtp_count变量加上HARDIRQ_OFFSET来标识一个HARDIRQ中上下文
*/
void irq_enter(void)
{
	rcu_irq_enter();
	if (is_idle_task(current) && !in_interrupt()) {
		/*
		 * Prevent raise_softirq from needlessly waking up ksoftirqd
		 * here, as softirq will be serviced on return from interrupt.
		 */
		local_bh_disable();
		tick_irq_enter();
		_local_bh_enable();
	}

	__irq_enter();
}
/*invoke_softirq是真正处理SOFTIRQ部分的函数*/
static inline void invoke_softirq(void)
{
	if (ksoftirqd_running(local_softirq_pending()))
		return;

	if (!force_irqthreads) {
#ifdef CONFIG_HAVE_IRQ_EXIT_ON_IRQ_STACK
		/*
		 * We can safely execute softirq on the current stack if
		 * it is the irq stack, because it should be near empty
		 * at this stage.
		 */
		__do_softirq();
#else
		/*
		 * Otherwise, irq_exit() is called on the task stack that can
		 * be potentially deep already. So call softirq in its own stack
		 * to prevent from any overrun.
		 */
		do_softirq_own_stack();
#endif
	} else {
		wakeup_softirqd();
	}
}

static inline void tick_irq_exit(void)
{
#ifdef CONFIG_NO_HZ_COMMON
	int cpu = smp_processor_id();

	/* Make sure that timer wheel updates are propagated */
	if ((idle_cpu(cpu) && !need_resched()) || tick_nohz_full_cpu(cpu)) {
		if (!in_irq())
			tick_nohz_irq_exit();
	}
#endif
}

/*
 * Exit an interrupt context. Process softirqs if needed and possible:
 */
 /*中断下半部SOFTIRQ*/
void irq_exit(void)
{
#ifndef __ARCH_IRQ_EXIT_IRQS_DISABLED
	local_irq_disable();
#else
	lockdep_assert_irqs_disabled();
#endif
	account_irq_exit_time(current);
	preempt_count_sub(HARDIRQ_OFFSET);
	/* 
      中断退出的时候不能处于 (soft and hard) irq interrupt context 中
      
      a) 硬件中断处理过程一般都是关中断的，中断退出时也就退出了硬件中断上下文，因此该条件会满足
      b) 如果本次中断点发生在一个软中断处理过程中，那么中断退出时会返回到软中断上下文中
             此时，不允许重新调度软中断，因为软中断在一个CPU上总是串行执行的

	  检查__softirq_pending是否有置位，有则invoke_softirq调用do_softirq 
	*/
	if (!in_interrupt() && local_softirq_pending())
		invoke_softirq();

	tick_irq_exit();
	rcu_irq_exit();
	trace_hardirq_exit(); /* must be last! */
}

/*
 * This function must run with irqs disabled!
 */
inline void raise_softirq_irqoff(unsigned int nr)
{
	__raise_softirq_irqoff(nr);

	/*
	 * If we're in an interrupt or softirq, we're done
	 * (this also catches softirq-disabled code). We will
	 * actually run the softirq once we return from
	 * the irq or softirq.
	 *
	 * Otherwise we wake up ksoftirqd to make sure we
	 * schedule the softirq soon.
	 */
	if (!in_interrupt())
/*
	    运行在中断上下文中，需要调用wakeup_softirqd唤醒ksoftirqd内核线程来处理
*/
		wakeup_softirqd();
}

/*
    主动触发一个软中断
*/
void raise_softirq(unsigned int nr)
{
	unsigned long flags;

	local_irq_save(flags);
/*
	在触发软中断之前会禁用中断，触发后，再回复原来的状态，
	如果中断本身已经被禁止了，可以直接调用 raise_softirq_irqoff
	根据触发中断的场景来考虑使用 raise_softirq_irqoff 还是 raise_softirq

	raise_softirq_irqoff 会修改__softirq_pending ，由于__softirq_pending是Per-CPU的
	因此不需要考虑多CPU并发的情况，因此不需要考虑使用 spin_lock等机制
	只需要是否关闭 local irq即可
*/
	raise_softirq_irqoff(nr);
	local_irq_restore(flags);
}
/*
    设置local cpu 的irq_stat 数据结构中__softirq_pending成员的第nr个bit
*/
void __raise_softirq_irqoff(unsigned int nr)
{
	trace_softirq_raise(nr);
	/* 触发softirq都是调用or_softirq_pending，将内核变量 __softirq_pending 的nr位置位 */
	or_softirq_pending(1UL << nr);
}
/*
注册软中断处理程序
open_softirq(HI_SOFTIRQ, tasklet_hi_action);
open_softirq(TIMER_SOFTIRQ, run_timer_softirq);
open_softirq(NET_TX_SOFTIRQ, net_tx_action);
open_softirq(NET_RX_SOFTIRQ, net_rx_action);
open_softirq(BLOCK_SOFTIRQ, blk_done_softirq);
open_softirq(TASKLET_SOFTIRQ, tasklet_action);
open_softirq(SCHED_SOFTIRQ, run_rebalance_domains);
open_softirq(RCU_SOFTIRQ, rcu_process_callbacks);
*/
void open_softirq(int nr, void (*action)(struct softirq_action *))
{
	softirq_vec[nr].action = action;
}

/*
 * Tasklets
 * tasklet_vec的声明
 */
struct tasklet_head {
	struct tasklet_struct *head;	/*head总是指向tasklet对象链表的第一个节点*/
	struct tasklet_struct **tail;	/*这是个指向tasklet对象指针的指针,tail总是保存tasklet链表最后一个节点所在tasklet对象中next成员的地址*/
};
/*
    每个CPU维护两个tasklet链表，一个用于普通优先级的tasklet_vec ，优先级是 TASKLET_SOFTIRQ（6）
    一个用于高优先级的tasklet_hi_vec 优先级是 HI_SOFTIRQ （0）
    它们都是Per-CPU变量
*/
static DEFINE_PER_CPU(struct tasklet_head, tasklet_vec);
static DEFINE_PER_CPU(struct tasklet_head, tasklet_hi_vec);

static void __tasklet_schedule_common(struct tasklet_struct *t,
				      struct tasklet_head __percpu *headp,
				      unsigned int softirq_nr)
{
	struct tasklet_head *head;
	unsigned long flags;
    // 保存中断状态，并禁止中断
	local_irq_save(flags);
	head = this_cpu_ptr(headp);
	t->next = NULL;
	*head->tail = t;
	head->tail = &(t->next);
	// 挂起TASKLET_SOFTIRQ 中断
	raise_softirq_irqoff(softirq_nr);
	// 恢复中断状态，并激活中断
	local_irq_restore(flags);
}

/*
在 tasklet 被调度后，只要有机会它就会尽早的执行，
如果它还没运行，又被调度了一次，那么它仍然只会运行一次。

如果它正在执行，在另一个处理器上 tasklet 又被调度了一次，
那么新的 tasklet 会被重新调度再运行一次
（不是同时运行，再次调度，下次有机会运行时才运行）
*/
void __tasklet_schedule(struct tasklet_struct *t)
{
	__tasklet_schedule_common(t, &tasklet_vec,
				  TASKLET_SOFTIRQ);
}
EXPORT_SYMBOL(__tasklet_schedule);

void __tasklet_hi_schedule(struct tasklet_struct *t)
{
	__tasklet_schedule_common(t, &tasklet_hi_vec,
				  HI_SOFTIRQ);
}
EXPORT_SYMBOL(__tasklet_hi_schedule);

static void tasklet_action_common(struct softirq_action *a,
				  struct tasklet_head *tl_head,
				  unsigned int softirq_nr)
{
	struct tasklet_struct *list;

	local_irq_disable();
	list = tl_head->head;
	tl_head->head = NULL;
	tl_head->tail = &tl_head->head;
	local_irq_enable();

	while (list) {
		struct tasklet_struct *t = list;

		list = list->next;

		if (tasklet_trylock(t)) {
			if (!atomic_read(&t->count)) {
				if (!test_and_clear_bit(TASKLET_STATE_SCHED,
							&t->state))
					BUG();
				t->func(t->data);
				tasklet_unlock(t);
				continue;
			}
			tasklet_unlock(t);
		}

		local_irq_disable();
		t->next = NULL;
		*tl_head->tail = t;
		tl_head->tail = &t->next;
		__raise_softirq_irqoff(softirq_nr);
		local_irq_enable();
	}
}

static __latent_entropy void tasklet_action(struct softirq_action *a)
{
	tasklet_action_common(a, this_cpu_ptr(&tasklet_vec), TASKLET_SOFTIRQ);
}

static __latent_entropy void tasklet_hi_action(struct softirq_action *a)
{
	tasklet_action_common(a, this_cpu_ptr(&tasklet_hi_vec), HI_SOFTIRQ);
}
/*动态初始化tasklet对象*/
void tasklet_init(struct tasklet_struct *t,
		  void (*func)(unsigned long), unsigned long data)
{
	t->next = NULL;
	t->state = 0;
	// 使用动态方式创建tasklet struct 结构体会自动设置成激活状态。
	atomic_set(&t->count, 0);
	t->func = func;
	t->data = data;
}
EXPORT_SYMBOL(tasklet_init);

// 将tasklet struct.state 设为0
void tasklet_kill(struct tasklet_struct *t)
{
	if (in_interrupt())
		pr_notice("Attempt to kill tasklet from interrupt\n");

	while (test_and_set_bit(TASKLET_STATE_SCHED, &t->state)) {
		do {
			yield();
		} while (test_bit(TASKLET_STATE_SCHED, &t->state));
	}
	tasklet_unlock_wait(t);
	clear_bit(TASKLET_STATE_SCHED, &t->state);
}
EXPORT_SYMBOL(tasklet_kill);

/*
 * tasklet_hrtimer
 */

/*
 * The trampoline is called when the hrtimer expires. It schedules a tasklet
 * to run __tasklet_hrtimer_trampoline() which in turn will call the intended
 * hrtimer callback, but from softirq context.
 */
static enum hrtimer_restart __hrtimer_tasklet_trampoline(struct hrtimer *timer)
{
	struct tasklet_hrtimer *ttimer =
		container_of(timer, struct tasklet_hrtimer, timer);

	tasklet_hi_schedule(&ttimer->tasklet);
	return HRTIMER_NORESTART;
}

/*
 * Helper function which calls the hrtimer callback from
 * tasklet/softirq context
 */
static void __tasklet_hrtimer_trampoline(unsigned long data)
{
	struct tasklet_hrtimer *ttimer = (void *)data;
	enum hrtimer_restart restart;

	restart = ttimer->function(&ttimer->timer);
	if (restart != HRTIMER_NORESTART)
		hrtimer_restart(&ttimer->timer);
}

/**
 * tasklet_hrtimer_init - Init a tasklet/hrtimer combo for softirq callbacks
 * @ttimer:	 tasklet_hrtimer which is initialized
 * @function:	 hrtimer callback function which gets called from softirq context
 * @which_clock: clock id (CLOCK_MONOTONIC/CLOCK_REALTIME)
 * @mode:	 hrtimer mode (HRTIMER_MODE_ABS/HRTIMER_MODE_REL)
 */
void tasklet_hrtimer_init(struct tasklet_hrtimer *ttimer,
			  enum hrtimer_restart (*function)(struct hrtimer *),
			  clockid_t which_clock, enum hrtimer_mode mode)
{
	hrtimer_init(&ttimer->timer, which_clock, mode);
	ttimer->timer.function = __hrtimer_tasklet_trampoline;
	tasklet_init(&ttimer->tasklet, __tasklet_hrtimer_trampoline,
		     (unsigned long)ttimer);
	ttimer->function = function;
}
EXPORT_SYMBOL_GPL(tasklet_hrtimer_init);

/**
 * 初始化软中断
 */
/*系统初始化期间调用该函数为TASKLET_SOFTIRQ和HI_SOFTIRQ安装了执行函数*/
void __init softirq_init(void)
{
	int cpu;

	//初始化每CPU上的高级、普通tasklet链表
	for_each_possible_cpu(cpu) {
		per_cpu(tasklet_vec, cpu).tail =
			&per_cpu(tasklet_vec, cpu).head;
		per_cpu(tasklet_hi_vec, cpu).tail =
			&per_cpu(tasklet_hi_vec, cpu).head;
	}

	//注册tasklet软中断
	/*用来给TASKLET_SOFTIRQ和HI_SOFTIRQ安装对应的执行函数*/
	open_softirq(TASKLET_SOFTIRQ, tasklet_action);
	open_softirq(HI_SOFTIRQ, tasklet_hi_action);
}

static int ksoftirqd_should_run(unsigned int cpu)
{
	return local_softirq_pending();
}

static void run_ksoftirqd(unsigned int cpu)
{
	local_irq_disable();
	if (local_softirq_pending()) {
		/*
		 * We can safely run softirq on inline stack, as we are not deep
		 * in the task stack here.
		 */
		__do_softirq();
		local_irq_enable();
		cond_resched();
		return;
	}
	local_irq_enable();
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * tasklet_kill_immediate is called to remove a tasklet which can already be
 * scheduled for execution on @cpu.
 *
 * Unlike tasklet_kill, this function removes the tasklet
 * _immediately_, even if the tasklet is in TASKLET_STATE_SCHED state.
 *
 * When this function is called, @cpu must be in the CPU_DEAD state.
 */
void tasklet_kill_immediate(struct tasklet_struct *t, unsigned int cpu)
{
	struct tasklet_struct **i;

	BUG_ON(cpu_online(cpu));
	BUG_ON(test_bit(TASKLET_STATE_RUN, &t->state));

	if (!test_bit(TASKLET_STATE_SCHED, &t->state))
		return;

	/* CPU is dead, so no lock needed. */
	for (i = &per_cpu(tasklet_vec, cpu).head; *i; i = &(*i)->next) {
		if (*i == t) {
			*i = t->next;
			/* If this was the tail element, move the tail ptr */
			if (*i == NULL)
				per_cpu(tasklet_vec, cpu).tail = i;
			return;
		}
	}
	BUG();
}

static int takeover_tasklets(unsigned int cpu)
{
	/* CPU is dead, so no lock needed. */
	local_irq_disable();

	/* Find end, append list for that CPU. */
	if (&per_cpu(tasklet_vec, cpu).head != per_cpu(tasklet_vec, cpu).tail) {
		*__this_cpu_read(tasklet_vec.tail) = per_cpu(tasklet_vec, cpu).head;
		this_cpu_write(tasklet_vec.tail, per_cpu(tasklet_vec, cpu).tail);
		per_cpu(tasklet_vec, cpu).head = NULL;
		per_cpu(tasklet_vec, cpu).tail = &per_cpu(tasklet_vec, cpu).head;
	}
	raise_softirq_irqoff(TASKLET_SOFTIRQ);

	if (&per_cpu(tasklet_hi_vec, cpu).head != per_cpu(tasklet_hi_vec, cpu).tail) {
		*__this_cpu_read(tasklet_hi_vec.tail) = per_cpu(tasklet_hi_vec, cpu).head;
		__this_cpu_write(tasklet_hi_vec.tail, per_cpu(tasklet_hi_vec, cpu).tail);
		per_cpu(tasklet_hi_vec, cpu).head = NULL;
		per_cpu(tasklet_hi_vec, cpu).tail = &per_cpu(tasklet_hi_vec, cpu).head;
	}
	raise_softirq_irqoff(HI_SOFTIRQ);

	local_irq_enable();
	return 0;
}
#else
#define takeover_tasklets	NULL
#endif /* CONFIG_HOTPLUG_CPU */

static struct smp_hotplug_thread softirq_threads = {
	.store			= &ksoftirqd,
	.thread_should_run	= ksoftirqd_should_run,
	.thread_fn		= run_ksoftirqd,
	.thread_comm		= "ksoftirqd/%u",
};

static __init int spawn_ksoftirqd(void)
{
	cpuhp_setup_state_nocalls(CPUHP_SOFTIRQ_DEAD, "softirq:dead", NULL,
				  takeover_tasklets);
	BUG_ON(smpboot_register_percpu_thread(&softirq_threads));

	return 0;
}
early_initcall(spawn_ksoftirqd);

/*
 * [ These __weak aliases are kept in a separate compilation unit, so that
 *   GCC does not inline them incorrectly. ]
 */

int __init __weak early_irq_init(void)
{
	return 0;
}

int __init __weak arch_probe_nr_irqs(void)
{
	return NR_IRQS_LEGACY;
}

int __init __weak arch_early_irq_init(void)
{
	return 0;
}

unsigned int __weak arch_dynirq_lower_bound(unsigned int from)
{
	return from;
}
