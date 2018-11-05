/*
 *  arch/arm/include/asm/procinfo.h
 *
 *  Copyright (C) 1996-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_PROCINFO_H
#define __ASM_PROCINFO_H

#ifdef __KERNEL__

struct cpu_tlb_fns;
struct cpu_user_fns;
struct cpu_cache_fns;
struct processor;

/*
 * Note!  struct processor is always defined if we're
 * using MULTI_CPU, otherwise this entry is unused,
 * but still exists.
 *
 * NOTE! The following structure is defined by assembly
 * language, NOT C code.  For more information, check:
 *  arch/arm/mm/proc-*.S and arch/arm/kernel/head.S
 */
struct proc_info_list {
	unsigned int		cpu_val;
	unsigned int		cpu_mask;
/*
    用于建立内核代码的线性映射

    指定了临时页表映射的内核空间是section-mapping方式，
    可读可写，并且是cached的
*/
	unsigned long		__cpu_mm_mmu_flags;	/* used by head.S */
/*
    用于早期对于IO空间的映射，如我们需要一个早期的调试串口，
    则可以在create_page_tables中将串口寄存器空间进行映射

    指定了其所映射的IO区域是section-mapping方式，可读可写，
    但是是uncached的
*/
	unsigned long		__cpu_io_mmu_flags;	/* used by head.S */
	unsigned long		__cpu_flush;		/* used by head.S */
	const char		*arch_name;
	const char		*elf_name;
	unsigned int		elf_hwcap;
	const char		*cpu_name;
	struct processor	*proc;
	struct cpu_tlb_fns	*tlb;
	struct cpu_user_fns	*user;
	struct cpu_cache_fns	*cache;
};

#else	/* __KERNEL__ */
#include <asm/elf.h>
#warning "Please include asm/elf.h instead"
#endif	/* __KERNEL__ */
#endif
