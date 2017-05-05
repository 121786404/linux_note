#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/cache.h>
#include <linux/threads.h>
#include <asm/irq.h>

#define NR_IPI	7

typedef struct {
	unsigned int __softirq_pending;
#ifdef CONFIG_SMP
	unsigned int ipi_irqs[NR_IPI];
#endif
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define __inc_irq_stat(cpu, member)	__IRQ_STAT(cpu, member)++
#define __get_irq_stat(cpu, member)	__IRQ_STAT(cpu, member)

#ifdef CONFIG_SMP
u64 smp_irq_stat_cpu(unsigned int cpu);
#else
#define smp_irq_stat_cpu(cpu)	0
#endif

#define arch_irq_stat_cpu	smp_irq_stat_cpu
/*__ARCH_IRQ_EXIT_IRQS_DISABLED是个体系架构相关的宏,用来决定在HARDIRQ部分
 * 结束时有没有关闭处理器响应外部中断的能力,如果定义了__ARCH_IRQ_EXIT_IRQS_DISABLED,就意味着在处理SOFTIRQ部分时,可以保证外部中断已经关闭,此时可以直接调用_do_softire,不过之前要做一些中断屏蔽的事情,保证_do_softirq开始执行时中断是关闭的*/
#define __ARCH_IRQ_EXIT_IRQS_DISABLED	1

#endif /* __ASM_HARDIRQ_H */
