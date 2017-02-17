/*
 *  arch/arm/include/asm/processor.h
 *
 *  Copyright (C) 1995-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARM_PROCESSOR_H
#define __ASM_ARM_PROCESSOR_H

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

#ifdef __KERNEL__

#include <asm/hw_breakpoint.h>
#include <asm/ptrace.h>
#include <asm/types.h>
#include <asm/unified.h>

#ifdef __KERNEL__
#define STACK_TOP	((current->personality & ADDR_LIMIT_32BIT) ? \
			 TASK_SIZE : TASK_SIZE_26)
#define STACK_TOP_MAX	TASK_SIZE
#endif

struct debug_info {
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	struct perf_event	*hbp[ARM_MAX_HBP_SLOTS];
#endif
};

struct thread_struct {
							/* fault info	  */
	unsigned long		address;
	unsigned long		trap_no;
	unsigned long		error_code;
							/* debugging	  */
	struct debug_info	debug;
};

#define INIT_THREAD  {	}

#ifdef CONFIG_MMU
#define nommu_start_thread(regs) do { } while (0)
#else
#define nommu_start_thread(regs) regs->ARM_r10 = current->mm->start_data
#endif

/*
start_thread()这个宏操作会将eip和esp改成新的地址，
就使得CPU在返回用户空间时就进入新的程序入口。
如果存在解释器映像，那么这就是解释器映像的程序入口，
否则就是目标映像的程序入口。那么什么情况下有解释器映像存在，
什么情况下没有呢？如果目标映像与各种库的链接是静态链接，
因而无需依靠共享库、即动态链接库，那就不需要解释器映像；
否则就一定要有解释器映像存在
*/
#define start_thread(regs,pc,sp)					\
({									\
	memset(regs->uregs, 0, sizeof(regs->uregs));			\
	if (current->personality & ADDR_LIMIT_32BIT)			\
		regs->ARM_cpsr = USR_MODE;				\
	else								\
		regs->ARM_cpsr = USR26_MODE;				\
	if (elf_hwcap & HWCAP_THUMB && pc & 1)				\
		regs->ARM_cpsr |= PSR_T_BIT;				\
	regs->ARM_cpsr |= PSR_ENDSTATE;					\
	regs->ARM_pc = pc & ~1;		/* pc */			\
	regs->ARM_sp = sp;		/* sp */			\
	nommu_start_thread(regs);					\
})

/* Forward declaration, a strange C thing */
struct task_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

unsigned long get_wchan(struct task_struct *p);

/*不会导致让出处理器,实现是一个内存屏障类的函数调用*/
#if __LINUX_ARM_ARCH__ == 6 || defined(CONFIG_ARM_ERRATA_754327)
#define cpu_relax()			smp_mb()
#else
#define cpu_relax()			barrier()
#endif
/*
获取stack中存放用户态寄存器组的偏移地址
*/
#define task_pt_regs(p) \
	((struct pt_regs *)(THREAD_START_SP + task_stack_page(p)) - 1)

#define KSTK_EIP(tsk)	task_pt_regs(tsk)->ARM_pc
#define KSTK_ESP(tsk)	task_pt_regs(tsk)->ARM_sp

#ifdef CONFIG_SMP
#define __ALT_SMP_ASM(smp, up)						\
	"9998:	" smp "\n"						\
	"	.pushsection \".alt.smp.init\", \"a\"\n"		\
	"	.long	9998b\n"					\
	"	" up "\n"						\
	"	.popsection\n"
#else
#define __ALT_SMP_ASM(smp, up)	up
#endif

/*
 * Prefetching support - only ARMv5.
 */
#if __LINUX_ARM_ARCH__ >= 5

#define ARCH_HAS_PREFETCH
static inline void prefetch(const void *ptr)
{
	__asm__ __volatile__(
		"pld\t%a0"
		:: "p" (ptr));
}

#if __LINUX_ARM_ARCH__ >= 7 && defined(CONFIG_SMP)
#define ARCH_HAS_PREFETCHW
static inline void prefetchw(const void *ptr)
{
	__asm__ __volatile__(
		".arch_extension	mp\n"
		__ALT_SMP_ASM(
			WASM(pldw)		"\t%a0",
			WASM(pld)		"\t%a0"
		)
		:: "p" (ptr));
}
#endif
#endif

#define HAVE_ARCH_PICK_MMAP_LAYOUT

#endif

#endif /* __ASM_ARM_PROCESSOR_H */
