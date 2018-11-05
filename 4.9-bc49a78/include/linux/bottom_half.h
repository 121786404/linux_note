#ifndef _LINUX_BH_H
#define _LINUX_BH_H

#include <linux/preempt.h>
/*
local_bh_disable和local_bh_disable是关bh（bottom half critical region）
的api，运行在进程上下文中，网络子系统中大量使用该接口
*/
#ifdef CONFIG_TRACE_IRQFLAGS
extern void __local_bh_disable_ip(unsigned long ip, unsigned int cnt);
#else
static __always_inline void __local_bh_disable_ip(unsigned long ip, unsigned int cnt)
{
	preempt_count_add(cnt);
/*
	防止编译器优化，thread_info->preempt_count 相当于Per-CPU变量
	因此不需要使用内存屏障指令
*/
	barrier();
}
#endif
/*
       禁止local cpu 在中断返回前夕执行软中断
*/
static inline void local_bh_disable(void)
{
/*
    将当前进程的 preempt_count 成员加上 SOFTIRQ_DISABLE_OFFSET
    那么现在内核状态进入了软中断上下文
*/
	__local_bh_disable_ip(_THIS_IP_, SOFTIRQ_DISABLE_OFFSET);
}

extern void _local_bh_enable(void);
extern void __local_bh_enable_ip(unsigned long ip, unsigned int cnt);

static inline void local_bh_enable_ip(unsigned long ip)
{
	__local_bh_enable_ip(ip, SOFTIRQ_DISABLE_OFFSET);
}

static inline void local_bh_enable(void)
{
	__local_bh_enable_ip(_THIS_IP_, SOFTIRQ_DISABLE_OFFSET);
}

#endif /* _LINUX_BH_H */
