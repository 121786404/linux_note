/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARM_MMU_H
#define __ARM_MMU_H

#ifdef CONFIG_MMU

typedef struct {
#ifdef CONFIG_CPU_HAS_ASID
/*
    地址空间的ID（software asid）。
    software asid（generation＋hw asid）
    这个ID并不是HW asid，其中低16 bit对应HW 的ASID
    （ARM64支持8bit或者16bit的ASID，但是这里假设当前系统的ASID是16bit）。
    其余的bit都是软件扩展的，称之    generation
    software asid初始值为0。
*/
	atomic64_t	id;
#else
	int		switch_pending;
#endif
	unsigned int	vmalloc_seq;
	unsigned long	sigpage;
#ifdef CONFIG_VDSO
	unsigned long	vdso;
#endif
#ifdef CONFIG_BINFMT_ELF_FDPIC
	unsigned long	exec_fdpic_loadmap;
	unsigned long	interp_fdpic_loadmap;
#endif
} mm_context_t;

#ifdef CONFIG_CPU_HAS_ASID
/*
ASID_BITS 就是硬件支持的ASID的bit数目，8或者16，
通过ID_AA64MMFR0_EL1寄存器可以获得该具体的bit数目。
*/
#define ASID_BITS	8
#define ASID_MASK	((~0ULL) << ASID_BITS)
#define ASID(mm)	((unsigned int)((mm)->context.id.counter & ~ASID_MASK))
#else
#define ASID(mm)	(0)
#endif

#else

/*
 * From nommu.h:
 *  Copyright (C) 2002, David McCullough <davidm@snapgear.com>
 *  modified for 2.6 by Hyok S. Choi <hyok.choi@samsung.com>
 */
typedef struct {
	unsigned long	end_brk;
#ifdef CONFIG_BINFMT_ELF_FDPIC
	unsigned long	exec_fdpic_loadmap;
	unsigned long	interp_fdpic_loadmap;
#endif
} mm_context_t;

#endif

#endif
