/* delayacct.h - per-task delay accounting
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2006
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY;  without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License for more details.
 *
 */

#ifndef _LINUX_DELAYACCT_H
#define _LINUX_DELAYACCT_H

#include <linux/sched.h>
#include <linux/slab.h>

/*
 * Per-task flags relevant to delay accounting
 * maintained privately to avoid exhausting similar flags in sched.h:PF_*
 * Used to set current->delays->flags
 */
#define DELAYACCT_PF_SWAPIN	0x00000001	/* I am doing a swapin */
#define DELAYACCT_PF_BLKIO	0x00000002	/* I am waiting on IO */

/*
delayacct是一个缩写，是指per-task delay accounting。
这个feature是统计每一个task的等待系统资源的时间（例如等待CPU、memeory或者IO）。
这些统计信息有助于精准的确定task访问系统资源的优先级。

一个进程/线程可能会因为下面的原因而delay：
1、该进程/线程处于runnable，但是等待调度器调度执行
2、该进程/线程发起synchronous block I/O，进程/线程处于阻塞状态，直到I/O的完成
3、进程/线程在执行过程中等待page swapping in。
       由于内存有限，系统不可能把进程的各个段（正文段、数据段等）都保存在物理内存中，
       当访问那些没有在物理内存的段的地址的时候，就会有磁盘操作，
       导致进程delay，这里有个专业的术语叫做capacity misses
4、进程/线程申请内存，但是由于资源受限而导致page frame reclaim的动作。

系统为何对这些delay信息进行统计呢？主要让系统确定下列的优先级的时候更有针对性：
1、task priority。如果该进程/线程长时间的等待CPU，那么调度器可以调高该任务的优先级。
2、IO priority。如果该进程/线程长时间的等待I/O，那么I/O调度器可以调高该任务的I/O优先级。
3、rss limit value。 rss的全称是resident set size，表示有物理内存对应的虚拟地址空间。
       由于物理内存资源有限，各个进程要合理使用。rss limit value定义了各个进程rss的上限。
*/
#ifdef CONFIG_TASK_DELAY_ACCT

extern int delayacct_on;	/* Delay accounting turned on/off */
extern struct kmem_cache *delayacct_cache;
extern void delayacct_init(void);
extern void __delayacct_tsk_init(struct task_struct *);
extern void __delayacct_tsk_exit(struct task_struct *);
extern void __delayacct_blkio_start(void);
extern void __delayacct_blkio_end(void);
extern int __delayacct_add_tsk(struct taskstats *, struct task_struct *);
extern __u64 __delayacct_blkio_ticks(struct task_struct *);
extern void __delayacct_freepages_start(void);
extern void __delayacct_freepages_end(void);

static inline int delayacct_is_task_waiting_on_io(struct task_struct *p)
{
	if (p->delays)
		return (p->delays->flags & DELAYACCT_PF_BLKIO);
	else
		return 0;
}

static inline void delayacct_set_flag(int flag)
{
	if (current->delays)
		current->delays->flags |= flag;
}

static inline void delayacct_clear_flag(int flag)
{
	if (current->delays)
		current->delays->flags &= ~flag;
}

static inline void delayacct_tsk_init(struct task_struct *tsk)
{
	/* reinitialize in case parent's non-null pointer was dup'ed*/
	tsk->delays = NULL;
	if (delayacct_on)
		__delayacct_tsk_init(tsk);
}

/* Free tsk->delays. Called from bad fork and __put_task_struct
 * where there's no risk of tsk->delays being accessed elsewhere
 */
static inline void delayacct_tsk_free(struct task_struct *tsk)
{
	if (tsk->delays)
		kmem_cache_free(delayacct_cache, tsk->delays);
	tsk->delays = NULL;
}

static inline void delayacct_blkio_start(void)
{
	delayacct_set_flag(DELAYACCT_PF_BLKIO);
	if (current->delays)
		__delayacct_blkio_start();
}

static inline void delayacct_blkio_end(void)
{
	if (current->delays)
		__delayacct_blkio_end();
	delayacct_clear_flag(DELAYACCT_PF_BLKIO);
}

static inline int delayacct_add_tsk(struct taskstats *d,
					struct task_struct *tsk)
{
	if (!delayacct_on || !tsk->delays)
		return 0;
	return __delayacct_add_tsk(d, tsk);
}

static inline __u64 delayacct_blkio_ticks(struct task_struct *tsk)
{
	if (tsk->delays)
		return __delayacct_blkio_ticks(tsk);
	return 0;
}

static inline void delayacct_freepages_start(void)
{
	if (current->delays)
		__delayacct_freepages_start();
}

static inline void delayacct_freepages_end(void)
{
	if (current->delays)
		__delayacct_freepages_end();
}

#else
static inline void delayacct_set_flag(int flag)
{}
static inline void delayacct_clear_flag(int flag)
{}
static inline void delayacct_init(void)
{}
static inline void delayacct_tsk_init(struct task_struct *tsk)
{}
static inline void delayacct_tsk_free(struct task_struct *tsk)
{}
static inline void delayacct_blkio_start(void)
{}
static inline void delayacct_blkio_end(void)
{}
static inline int delayacct_add_tsk(struct taskstats *d,
					struct task_struct *tsk)
{ return 0; }
static inline __u64 delayacct_blkio_ticks(struct task_struct *tsk)
{ return 0; }
static inline int delayacct_is_task_waiting_on_io(struct task_struct *p)
{ return 0; }
static inline void delayacct_freepages_start(void)
{}
static inline void delayacct_freepages_end(void)
{}

#endif /* CONFIG_TASK_DELAY_ACCT */

#endif
