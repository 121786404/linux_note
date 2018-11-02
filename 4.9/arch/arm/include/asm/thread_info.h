/*
 *  arch/arm/include/asm/thread_info.h
 *
 *  Copyright (C) 2002 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_THREAD_INFO_H
#define __ASM_ARM_THREAD_INFO_H

#ifdef __KERNEL__

#include <linux/compiler.h>
#include <asm/fpstate.h>
#include <asm/page.h>
/*
内核栈
*/
#define THREAD_SIZE_ORDER	1
#define THREAD_SIZE		(PAGE_SIZE << THREAD_SIZE_ORDER) // 8K
#define THREAD_START_SP		(THREAD_SIZE - 8)

#ifndef __ASSEMBLY__

struct task_struct;

#include <asm/types.h>

typedef unsigned long mm_segment_t;
/*
内核线程上下文，当有线程切换发生时会用到
*/
struct cpu_context_save {
	__u32	r4;
	__u32	r5;
	__u32	r6;
	__u32	r7;
	__u32	r8;
	__u32	r9;
	__u32	sl;
	__u32	fp;
	__u32	sp;
	__u32	pc;
	__u32	extra[2];		/* Xscale 'acc' register, etc */
};

/*
 * low level task data that entry.S needs immediate access to.
 * __switch_to() assumes cpu_context follows immediately after cpu_domain.
 */
/*
 thread_info就保存了特定体系结构的汇编代码段
 需要访问的那部分进程的数据

* ------------------------------|
           8 byte               |
* ------------------------------|
                                |
           stack                |-- Section("data..init_task")
                                |
* ------------------------------|
       struct thread_info       |
* ------------------------------|   <---- task_struct.stack
*/
struct thread_info {
/*
    保存各种特定于进程的标志
*/
	unsigned long		flags;		/* low level flags */
/*
	内核抢占计数器
	该计数器初始值为O. 每当使用锁的时候数值加1.释放锁的时候数值减1 。
	当数值为0    的时候，内核就可执行抢占。

	preempt_count 不为0:，可能是
	1) preempt_disable 禁止抢占
	2) 处于中断上下文
    bit[0:7] 抢占计数
	bit[8:15] 用于软中断，一般只是用bit 8，可以表示软中断的嵌套深度，最多表示255次嵌套
    bit[16:19] 用于硬件中断，一般只是用bit 4，可以表示硬中断的嵌套深度，最多表示15次嵌套
    bit[20] NMI中断
    bit[21] 当前被强占或者刚刚被强占，通常用于表示抢占调度
*/
	int			preempt_count;	/* 0 => preemptable, <0 => bug */
/*
	指定了进程可以使用的虚拟地址的上限。
	如前所述，该限制适用于普通进程，
	但内核线程可以访问整个虚拟地址空间，
	包括只有内核能访问的部分。
	这并不意味着限制进程可以分配的内存数量。
*/
	mm_segment_t		addr_limit;	/* address limit */
/*
	便的通过thread_info来查找task_struct
*/
	struct task_struct	*task;		/* main task structure */
/*
	运行在此CPU 上
*/
	__u32			cpu;		/* cpu */
	__u32			cpu_domain;	/* cpu domain */
	struct cpu_context_save	cpu_context;	/* cpu context */
	__u32			syscall;	/* syscall number */
	__u8			used_cp[16];	/* thread used copro */
/*
    TLS 线程局部存储
*/
	unsigned long		tp_value[2];	/* TLS registers */
#ifdef CONFIG_CRUNCH
	struct crunch_state	crunchstate;
#endif
	union fp_state		fpstate __attribute__((aligned(8)));
	union vfp_state		vfpstate;
#ifdef CONFIG_ARM_THUMBEE
	unsigned long		thumbee_state;	/* ThumbEE Handler Base register */
#endif
};

#define INIT_THREAD_INFO(tsk)						\
{									\
	.task		= &tsk,						\
	.flags		= 0,						\
	.preempt_count	= INIT_PREEMPT_COUNT,				\
	.addr_limit	= KERNEL_DS,					\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/*
 * how to get the current stack pointer in C
 */
register unsigned long current_stack_pointer asm ("sp");

/*
 * how to get the thread information struct from C
 */
 /*
 esp指向的是内核堆栈的结尾，由于堆栈是向下增长的，
 esp和thread_info位于同一个8KB或者4KB的块当中。  也就是thread_union的长度了。
 如果是8KB，屏蔽esp的低13位就可以得到thread_info的地址，
 也就是8KB块的开始位置。4KB的话，就屏蔽低12位
 */
static inline struct thread_info *current_thread_info(void) __attribute_const__;
/*
把thread_info放在内核栈的底部是一个精巧的设计，
因为当前进程是一个使用率极高的数据结构，
在高端CPU中往往都保留了一个专门的寄存器来存放当前进程的指针，
比如PowerPC、Itanium，然而x86的寄存器实在是太少了，
专门分配一个寄存器实在太奢侈，所以Linux设计了thread_info，
把它放在内核栈的底部，这样通过栈寄存器里的指针可以很方便地算出thread_info的地址，
进而得到进程的指针
*/
static inline struct thread_info *current_thread_info(void)
{
	return (struct thread_info *)
		(current_stack_pointer & ~(THREAD_SIZE - 1));
}

#define thread_saved_pc(tsk)	\
	((unsigned long)(task_thread_info(tsk)->cpu_context.pc))
#define thread_saved_sp(tsk)	\
	((unsigned long)(task_thread_info(tsk)->cpu_context.sp))

#ifndef CONFIG_THUMB2_KERNEL
#define thread_saved_fp(tsk)	\
	((unsigned long)(task_thread_info(tsk)->cpu_context.fp))
#else
#define thread_saved_fp(tsk)	\
	((unsigned long)(task_thread_info(tsk)->cpu_context.r7))
#endif

extern void crunch_task_disable(struct thread_info *);
extern void crunch_task_copy(struct thread_info *, void *);
extern void crunch_task_restore(struct thread_info *, void *);
extern void crunch_task_release(struct thread_info *);

extern void iwmmxt_task_disable(struct thread_info *);
extern void iwmmxt_task_copy(struct thread_info *, void *);
extern void iwmmxt_task_restore(struct thread_info *, void *);
extern void iwmmxt_task_release(struct thread_info *);
extern void iwmmxt_task_switch(struct thread_info *);

extern void vfp_sync_hwstate(struct thread_info *);
extern void vfp_flush_hwstate(struct thread_info *);

struct user_vfp;
struct user_vfp_exc;

extern int vfp_preserve_user_clear_hwstate(struct user_vfp __user *,
					   struct user_vfp_exc __user *);
extern int vfp_restore_user_hwstate(struct user_vfp __user *,
				    struct user_vfp_exc __user *);
#endif

/*
 * thread information flags:
 *  TIF_USEDFPU		- FPU was used by this task this quantum (SMP)
 *  TIF_POLLING_NRFLAG	- true if poll_idle() is polling TIF_NEED_RESCHED
 */
/*
 进程有待决信号
*/
#define TIF_SIGPENDING		0	/* signal pending */
/*
进程应该或想要调度器选择另一个进程替换本进程执行
*/
#define TIF_NEED_RESCHED	1	/* rescheduling necessary */
#define TIF_NOTIFY_RESUME	2	/* callback before returning to user */
#define TIF_UPROBE		3	/* breakpointed or singlestepping */
#define TIF_SYSCALL_TRACE	4	/* syscall trace active */
#define TIF_SYSCALL_AUDIT	5	/* syscall auditing active */
#define TIF_SYSCALL_TRACEPOINT	6	/* syscall tracepoint instrumentation */
#define TIF_SECCOMP		7	/* seccomp syscall filtering active */

#define TIF_NOHZ		12	/* in adaptive nohz mode */
#define TIF_USING_IWMMXT	17
#define TIF_MEMDIE		18	/* is terminating due to OOM killer */
#define TIF_RESTORE_SIGMASK	20

#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_UPROBE		(1 << TIF_UPROBE)
#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SYSCALL_AUDIT	(1 << TIF_SYSCALL_AUDIT)
#define _TIF_SYSCALL_TRACEPOINT	(1 << TIF_SYSCALL_TRACEPOINT)
#define _TIF_SECCOMP		(1 << TIF_SECCOMP)
#define _TIF_USING_IWMMXT	(1 << TIF_USING_IWMMXT)

/* Checks for any syscall work in entry-common.S */
#define _TIF_SYSCALL_WORK (_TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT | \
			   _TIF_SYSCALL_TRACEPOINT | _TIF_SECCOMP)

/*
 * Change these and you break ASM code in entry-common.S
 */
#define _TIF_WORK_MASK		(_TIF_NEED_RESCHED | _TIF_SIGPENDING | \
				 _TIF_NOTIFY_RESUME | _TIF_UPROBE)

#endif /* __KERNEL__ */
#endif /* __ASM_ARM_THREAD_INFO_H */
