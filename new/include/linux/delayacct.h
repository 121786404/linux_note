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
delayacct��һ����д����ָper-task delay accounting��
���feature��ͳ��ÿһ��task�ĵȴ�ϵͳ��Դ��ʱ�䣨����ȴ�CPU��memeory����IO����
��Щͳ����Ϣ�����ھ�׼��ȷ��task����ϵͳ��Դ�����ȼ���

һ������/�߳̿��ܻ���Ϊ�����ԭ���delay��
1���ý���/�̴߳���runnable�����ǵȴ�����������ִ��
2���ý���/�̷߳���synchronous block I/O������/�̴߳�������״̬��ֱ��I/O�����
3������/�߳���ִ�й����еȴ�page swapping in��
       �����ڴ����ޣ�ϵͳ�����ܰѽ��̵ĸ����Σ����ĶΡ����ݶεȣ��������������ڴ��У�
       ��������Щû���������ڴ�Ķεĵ�ַ��ʱ�򣬾ͻ��д��̲�����
       ���½���delay�������и�רҵ���������capacity misses
4������/�߳������ڴ棬����������Դ���޶�����page frame reclaim�Ķ�����

ϵͳΪ�ζ���Щdelay��Ϣ����ͳ���أ���Ҫ��ϵͳȷ�����е����ȼ���ʱ���������ԣ�
1��task priority������ý���/�̳߳�ʱ��ĵȴ�CPU����ô���������Ե��߸���������ȼ���
2��IO priority������ý���/�̳߳�ʱ��ĵȴ�I/O����ôI/O���������Ե��߸������I/O���ȼ���
3��rss limit value�� rss��ȫ����resident set size����ʾ�������ڴ��Ӧ�������ַ�ռ䡣
       ���������ڴ���Դ���ޣ���������Ҫ����ʹ�á�rss limit value�����˸�������rss�����ޡ�
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
