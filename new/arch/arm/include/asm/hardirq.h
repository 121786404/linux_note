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
/*__ARCH_IRQ_EXIT_IRQS_DISABLED�Ǹ���ϵ�ܹ���صĺ�,����������HARDIRQ����
 * ����ʱ��û�йرմ�������Ӧ�ⲿ�жϵ�����,���������__ARCH_IRQ_EXIT_IRQS_DISABLED,����ζ���ڴ���SOFTIRQ����ʱ,���Ա�֤�ⲿ�ж��Ѿ��ر�,��ʱ����ֱ�ӵ���_do_softire,����֮ǰҪ��һЩ�ж����ε�����,��֤_do_softirq��ʼִ��ʱ�ж��ǹرյ�*/
#define __ARCH_IRQ_EXIT_IRQS_DISABLED	1

#endif /* __ASM_HARDIRQ_H */
