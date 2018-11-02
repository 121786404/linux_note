/*
 *  arch/arm/include/asm/pgtable-2level.h
 *
 *  Copyright (C) 1995-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _ASM_PGTABLE_2LEVEL_H
#define _ASM_PGTABLE_2LEVEL_H

#define __PAGETABLE_PMD_FOLDED

/*
分段可以给每个进程分配不同的线性地址空间，
分页可以把同一线性地址空间映射到不同的物理空间。

与分段相比，Linux更喜欢分页方式，因为：
* 当所有进程使用相同的段寄存器值时，内存管理变得更简单，
   也就是说它们能共享同样的一簇线性地址。
* 为了兼容绝大多数的CPU，RISC体系架构对分段的支持很有限，
   比如ARM架构的CPU中的MMU单元通常只  支持分页，而不支持分段。

分页使得不同的虚拟内存页可以转入同一物理页框。
于此同时分页机制可以实现对每个页面的访问控制，
这是在平衡内存使用效率和地址转换效率之间做出的选择。

如果4G的虚拟空间，每一个字节都要使用一个数据结构
来记录它的访问控制信息，那么显然是不可能的。

如果把4G的虚拟空间以4K(为什么是4K大小？
这是由于Linux中的可执行文件中的代码段和数据段等都是相对于4K对齐的)
大小分割成若干个不同的页面，那么每个页面需要一个数据结构进行控制，
只需要1M的内存来存储。但是由于每一个用户进程都有自己的独立空间，
所以每一个进程都需要一个1M的内存来存储页表信息，
这依然是对系统内存的浪费，采用两级甚至多级分页是一种不错的解决方案。
另外有些处理器采用64位体系架构，此时两级也不合适了，
所以Linux使用三级页表。

Linux支持三级页表，作为其默认的页表结构。
ARM是两级页表, PGD和PTE。
从上面可以可以看出一个work around的实现。
PGD和PTE并不是直接对应ARM硬件的页表目录项。
而是做了一些为了linux上层的要求的一个方案。
首先，他把4096个pgd项变成2048个，
物理上还是一个pgd指向一个256个pte项的数组的，这没办法改。
但是pgd指针逻辑上合并成一个，各自指向的pte数组也合并,
并且是连续的。这512个pte项合并起来，这个pte分配的页
（一般linux需要一个pte表在一个页里，代码注释也写了）
还剩下一半的内容，刚好可以存放arm不支持的一些标记(Linux pt 0, 1),
而这些标记是linux必须的，比如dirty。

这个方案还非常具有可扩展性，不依赖arm本身的标记。
dirty标记的实现是通过对arm支持的权限fault的中断来写这个标记,
这样方式是相当于一种模拟。

* pgd:  页全局目录(Page Global Directory)，是多级页表的抽象最高层。
每一级的页表都处理不同大小  的内存。
每项都指向一个更小目录的低级表，
因此pgd就是一个页表目录。
当代码遍历这个结构时  （有些驱动程序就要这样做），
就称为是在遍历页表。

* pmd:  页中间目录 (Page Middle Directory),即pmd，是页表的中间层。
在 x86 架构上，pmd 在硬件中  并不存在，
但是在内核代码中它是与pgd合并在一起的。

* pte:  页表条目 (Page Table Entry)，是页表的最低层，它直接处理页，
该值包含某页的物理地址，
还包含了  说明该条目是否有效及相关页是否在物理内存中的位。
*/

/*
 * Hardware-wise, we have a two level page table structure, where the first
 * level has 4096 entries, and the second level has 256 entries.  Each entry
 * is one 32-bit word.  Most of the bits in the second level entry are used
 * by hardware, and there aren't any "accessed" and "dirty" bits.
 *
 * Linux on the other hand has a three level page table structure, which can
 * be wrapped to fit a two level page table structure easily - using the PGD
 * and PTE only.  However, Linux also expects one "PTE" table per page, and
 * at least a "dirty" bit.
 *
 * Therefore, we tweak the implementation slightly - we tell Linux that we
 * have 2048 entries in the first level, each of which is 8 bytes (iow, two
 * hardware pointers to the second level.)  The second level contains two
 * hardware PTE tables arranged contiguously, preceded by Linux versions
 * which contain the state information Linux needs.  We, therefore, end up
 * with 512 entries in the "PTE" level.
 *
 * This leads to the page tables having the following layout:
 *
 *    pgd             pte
 * |        |
 * +--------+
 * |        |       +------------+ +0
 * +- - - - +       | Linux pt 0 |
 * |        |       +------------+ +1024
 * +--------+ +0    | Linux pt 1 |
 * |        |-----> +------------+ +2048
 * +- - - - + +4    |  h/w pt 0  |
 * |        |-----> +------------+ +3072
 * +--------+ +8    |  h/w pt 1  |
 * |        |       +------------+ +4096
 *
 * See L_PTE_xxx below for definitions of bits in the "Linux pt", and
 * PTE_xxx for definitions of bits appearing in the "h/w pt".
 *
 * PMD_xxx definitions refer to bits in the first level page table.
 *
 * The "dirty" bit is emulated by only granting hardware write permission
 * iff the page is marked "writable" and "dirty" in the Linux PTE.  This
 * means that a write to a clean page will cause a permission fault, and
 * the Linux MM layer will mark the page dirty via handle_pte_fault().
 * For the hardware to notice the permission change, the TLB entry must
 * be flushed, and ptep_set_access_flags() does that for us.
 *
 * The "accessed" or "young" bit is emulated by a similar method; we only
 * allow accesses to the page if the "young" bit is set.  Accesses to the
 * page will cause a fault, and handle_pte_fault() will set the young bit
 * for us as long as the page is marked present in the corresponding Linux
 * PTE entry.  Again, ptep_set_access_flags() will ensure that the TLB is
 * up to date.
 *
 * However, when the "young" bit is cleared, we deny access to the page
 * by clearing the hardware PTE.  Currently Linux does not flush the TLB
 * for us in this case, which means the TLB will retain the transation
 * until either the TLB entry is evicted under pressure, or a context
 * switch which changes the user space mapping occurs.
 */
/*
ARM32 架构中，二级页表也只有256 个页面表项，为何要分配这么多呢?

*/
#define PTRS_PER_PTE		512
#define PTRS_PER_PMD		1
#define PTRS_PER_PGD		2048

#define PTE_HWTABLE_PTRS	(PTRS_PER_PTE)
#define PTE_HWTABLE_OFF		(PTE_HWTABLE_PTRS * sizeof(pte_t))
#define PTE_HWTABLE_SIZE	(PTRS_PER_PTE * sizeof(u32))

/*
 * PMD_SHIFT determines the size of the area a second-level page table can map
 * PGDIR_SHIFT determines what a third-level page table entry can map
 */
/*
ARM 结构中一级页表PGD 的偏移量应该从20 位开始，为何这里的头文件定义从21 位开始呢?
*/
#define PMD_SHIFT		21
#define PGDIR_SHIFT		21

#define PMD_SIZE		(1UL << PMD_SHIFT)
#define PMD_MASK		(~(PMD_SIZE-1))
#define PGDIR_SIZE		(1UL << PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE-1))

/*
 * section address mask and size definitions.
 */
#define SECTION_SHIFT		20
#define SECTION_SIZE		(1UL << SECTION_SHIFT)
#define SECTION_MASK		(~(SECTION_SIZE-1))

/*
 * ARMv6 supersection address mask and size definitions.
 */
#define SUPERSECTION_SHIFT	24
#define SUPERSECTION_SIZE	(1UL << SUPERSECTION_SHIFT)
#define SUPERSECTION_MASK	(~(SUPERSECTION_SIZE-1))

#define USER_PTRS_PER_PGD	(TASK_SIZE / PGDIR_SIZE)

/*
 * "Linux" PTE definitions.
 *
 * We keep two sets of PTEs - the hardware and the linux version.
 * This allows greater flexibility in the way we map the Linux bits
 * onto the hardware tables, and allows us to have YOUNG and DIRTY
 * bits.
 *
 * The PTE table pointer refers to the hardware entries; the "Linux"
 * entries are stored 1024 bytes below.
 */
/*
在x86 的页面表中有3 个标志位是ARM32 硬件页面表没有提供的。
PTE_YOUNG:PTE_PRESENT:PTE_DIRTY,
因此在ARM Linux 实现中需要模拟上述3 个比特位
*/
#define L_PTE_VALID		(_AT(pteval_t, 1) << 0)		/* Valid */
/*
页在内存中。
*/
#define L_PTE_PRESENT		(_AT(pteval_t, 1) << 0)
/*
CPU 访问该页时会设置该标志位。在页面换出时，如果该标志位置位了，
说明该页刚被访问过，页面是young 的，不适合把该页换出，同时清除该标志位。
*/
#define L_PTE_YOUNG		(_AT(pteval_t, 1) << 1)
/*

PTE_DIRTY: CPU 在写操作时会设置该标志位，表示对应页面被写过，为脏页。
如何模拟PTE_DIRTY 呢?
在ARM MMU 硬件为一个干净页面建立映射时，设置硬件页表项是只读权限的。
当往一个干净的页面写入时，会触发写权限缺页中断(虽然Linux版本的页面表项标记了可写权限，但是ARM 硬件页面表项还不具有写入权限) ，
那么在缺页中断处理handle_pte_fault()中会在该页的Linux 版本PTE 页面表项标记为"dirty"，并且发现PTE 页表项内容改变了，
ptep_set_access_flags()函数会把新的Linux 版本的页表项内容写入硬件页表，从而完成模拟过程。
*/
#define L_PTE_DIRTY		(_AT(pteval_t, 1) << 6)
#define L_PTE_RDONLY		(_AT(pteval_t, 1) << 7)
#define L_PTE_USER		(_AT(pteval_t, 1) << 8)
#define L_PTE_XN		(_AT(pteval_t, 1) << 9)
#define L_PTE_SHARED		(_AT(pteval_t, 1) << 10)	/* shared(v6), coherent(xsc3) */
#define L_PTE_NONE		(_AT(pteval_t, 1) << 11)

/*
 * These are the memory types, defined to be compatible with
 * pre-ARMv6 CPUs cacheable and bufferable bits: n/a,n/a,C,B
 * ARMv6+ without TEX remapping, they are a table index.
 * ARMv6+ with TEX remapping, they correspond to n/a,TEX(0),C,B
 *
 * MT type		Pre-ARMv6	ARMv6+ type / cacheable status
 * UNCACHED		Uncached	Strongly ordered
 * BUFFERABLE		Bufferable	Normal memory / non-cacheable
 * WRITETHROUGH		Writethrough	Normal memory / write through
 * WRITEBACK		Writeback	Normal memory / write back, read alloc
 * MINICACHE		Minicache	N/A
 * WRITEALLOC		Writeback	Normal memory / write back, write alloc
 * DEV_SHARED		Uncached	Device memory (shared)
 * DEV_NONSHARED	Uncached	Device memory (non-shared)
 * DEV_WC		Bufferable	Normal memory / non-cacheable
 * DEV_CACHED		Writeback	Normal memory / write back, read alloc
 * VECTORS		Variable	Normal memory / variable
 *
 * All normal memory mappings have the following properties:
 * - reads can be repeated with no side effects
 * - repeated reads return the last value written
 * - reads can fetch additional locations without side effects
 * - writes can be repeated (in certain cases) with no side effects
 * - writes can be merged before accessing the target
 * - unaligned accesses can be supported
 *
 * All device mappings have the following properties:
 * - no access speculation
 * - no repetition (eg, on return from an exception)
 * - number, order and size of accesses are maintained
 * - unaligned accesses are "unpredictable"
 */
#define L_PTE_MT_UNCACHED	(_AT(pteval_t, 0x00) << 2)	/* 0000 */
#define L_PTE_MT_BUFFERABLE	(_AT(pteval_t, 0x01) << 2)	/* 0001 */
#define L_PTE_MT_WRITETHROUGH	(_AT(pteval_t, 0x02) << 2)	/* 0010 */
#define L_PTE_MT_WRITEBACK	(_AT(pteval_t, 0x03) << 2)	/* 0011 */
#define L_PTE_MT_MINICACHE	(_AT(pteval_t, 0x06) << 2)	/* 0110 (sa1100, xscale) */
#define L_PTE_MT_WRITEALLOC	(_AT(pteval_t, 0x07) << 2)	/* 0111 */
#define L_PTE_MT_DEV_SHARED	(_AT(pteval_t, 0x04) << 2)	/* 0100 */
#define L_PTE_MT_DEV_NONSHARED	(_AT(pteval_t, 0x0c) << 2)	/* 1100 */
#define L_PTE_MT_DEV_WC		(_AT(pteval_t, 0x09) << 2)	/* 1001 */
#define L_PTE_MT_DEV_CACHED	(_AT(pteval_t, 0x0b) << 2)	/* 1011 */
#define L_PTE_MT_VECTORS	(_AT(pteval_t, 0x0f) << 2)	/* 1111 */
#define L_PTE_MT_MASK		(_AT(pteval_t, 0x0f) << 2)

#ifndef __ASSEMBLY__

/*
 * The "pud_xxx()" functions here are trivial when the pmd is folded into
 * the pud: the pud entry is never bad, always exists, and can't be set or
 * cleared.
 */
#define pud_none(pud)		(0)
#define pud_bad(pud)		(0)
#define pud_present(pud)	(1)
#define pud_clear(pudp)		do { } while (0)
#define set_pud(pud,pudp)	do { } while (0)

/*
接收指向页上级目录项的指针 pud 和线性地址 addr 作为参数。
这个宏产生目录项 addr 在页中间目录中的偏移地址。
在两级或三级分页系统中，它产生 pud ，即页全局目录项的地址
*/
static inline pmd_t *pmd_offset(pud_t *pud, unsigned long addr)
{
	return (pmd_t *)pud;
}

#define pmd_large(pmd)		(pmd_val(pmd) & 2)
#define pmd_bad(pmd)		(pmd_val(pmd) & 2)
#define pmd_present(pmd)	(pmd_val(pmd))

#define copy_pmd(pmdpd,pmdps)		\
	do {				\
		pmdpd[0] = pmdps[0];	\
		pmdpd[1] = pmdps[1];	\
		flush_pmd_entry(pmdpd);	\
	} while (0)

#define pmd_clear(pmdp)			\
	do {				\
		pmdp[0] = __pmd(0);	\
		pmdp[1] = __pmd(0);	\
		clean_pmd_entry(pmdp);	\
	} while (0)

/* we don't need complex calculations here as the pmd is folded into the pgd */
#define pmd_addr_end(addr,end) (end)

#define set_pte_ext(ptep,pte,ext) cpu_set_pte_ext(ptep,pte,ext)
#define pte_special(pte)	(0)
static inline pte_t pte_mkspecial(pte_t pte) { return pte; }

/*
 * We don't have huge page support for short descriptors, for the moment
 * define empty stubs for use by pin_page_for_write.
 */
#define pmd_hugewillfault(pmd)	(0)
#define pmd_thp_or_huge(pmd)	(0)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_PGTABLE_2LEVEL_H */
