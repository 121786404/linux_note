/*
 *  arch/arm/include/asm/map.h
 *
 *  Copyright (C) 1999-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Page table mapping constructs and function prototypes
 */
#ifndef __ASM_MACH_MAP_H
#define __ASM_MACH_MAP_H

#include <asm/io.h>

struct map_desc {
/*
    虚拟地址的起始地址
*/
	unsigned long virtual;
/*
	物理地址的开始地址的页帧号
*/
	unsigned long pfn;
/*
	被映射的物理内存的大小,注意这里不是页框大小
*/
	unsigned long length;
/*
	映射类型，MT_xxx，决定了相应映射页面的访问权限
*/
	unsigned int type;
};

/* types 0-3 are defined in asm/io.h */
enum {
	MT_UNCACHED = 4,
	MT_CACHECLEAN,
	MT_MINICLEAN,
/*
	对应0地址开始的向量
*/
	MT_LOW_VECTORS,
/*
    对应高地址开始的向量，它有vector_base宏决定。
*/
	MT_HIGH_VECTORS,
	MT_MEMORY_RWX,
	/*
    ARM 页表项有一个XN比特    位，XN 比特位置为1，表示这段内存区域不允许执行。
	*/
	MT_MEMORY_RW,
	MT_ROM,
	MT_MEMORY_RWX_NONCACHED,
	MT_MEMORY_RW_DTCM,
	MT_MEMORY_RWX_ITCM,
	MT_MEMORY_RW_SO,
	MT_MEMORY_DMA_READY,
};

#ifdef CONFIG_MMU
extern void iotable_init(struct map_desc *, int);
extern void vm_reserve_area_early(unsigned long addr, unsigned long size,
				  void *caller);
extern void create_mapping_late(struct mm_struct *mm, struct map_desc *md,
				bool ng);

#ifdef CONFIG_DEBUG_LL
extern void debug_ll_addr(unsigned long *paddr, unsigned long *vaddr);
extern void debug_ll_io_init(void);
#else
static inline void debug_ll_io_init(void) {}
#endif

struct mem_type;
extern const struct mem_type *get_mem_type(unsigned int type);
/*
 * external interface to remap single page with appropriate type
 */
extern int ioremap_page(unsigned long virt, unsigned long phys,
			const struct mem_type *mtype);
#else
#define iotable_init(map,num)	do { } while (0)
#define vm_reserve_area_early(a,s,c)	do { } while (0)
#endif

#endif
