/*
 *  arch/arm/include/asm/pgtable.h
 *
 *  Copyright (C) 1995-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _ASMARM_PGTABLE_H
#define _ASMARM_PGTABLE_H

#include <linux/const.h>
#include <asm/proc-fns.h>

#ifndef CONFIG_MMU

#include <asm-generic/4level-fixup.h>
#include <asm/pgtable-nommu.h>

#else

#include <asm-generic/pgtable-nopud.h>
#include <asm/memory.h>
#include <asm/pgtable-hwdef.h>


#include <asm/tlbflush.h>

#ifdef CONFIG_ARM_LPAE
#include <asm/pgtable-3level.h>
#else
#include <asm/pgtable-2level.h>
#endif

/*
 * Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */
/*
 VMALLOC_START和VMALLOC_END定义了vmalloc区域的开始和结束，
 该区域用于物理上不连续的内核映射。
 这两个值没有直接定义为常数，而是依赖于几个参数。

 vmalloc区域的起始地址，取决于在直接映射物理内存时，
 使用了多少虚拟地址空间内存（因此也依赖于high_memory变量）。
 内核还考虑到下述事实，即两个区域之间有至少为
 VMALLOC_OFFSET的一个缺口.

VMALLOC_OFFSET 定义了8M的空洞，用于捕捉越界的内存访问。
如果VMALLOC_OFFSET取最小值，那么在lowmem和vmalloc区域之间，
会出现一个缺口。这个缺口可用作针对任何内核故障的保护措施。
如果访问越界地址（即无意地访问物理上不存在的内存区），
则访问失败并生成一个异常，报告该错误。
如果vmalloc区域紧接着直接映射，那么访问将成功而不会注意到错误。
在稳定运行的情况下，肯定不需要这个额外的保护措施，
但它对开发尚未成熟的新内核特性是有用的。
*/
#define VMALLOC_OFFSET		(8*1024*1024)
#define VMALLOC_START		(((unsigned long)high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#define VMALLOC_END		0xff800000UL

#define LIBRARY_TEXT_START	0x0c000000

#ifndef __ASSEMBLY__
extern void __pte_error(const char *file, int line, pte_t);
extern void __pmd_error(const char *file, int line, pmd_t);
extern void __pgd_error(const char *file, int line, pgd_t);

#define pte_ERROR(pte)		__pte_error(__FILE__, __LINE__, pte)
#define pmd_ERROR(pmd)		__pmd_error(__FILE__, __LINE__, pmd)
#define pgd_ERROR(pgd)		__pgd_error(__FILE__, __LINE__, pgd)

/*
 * This is the lowest virtual address we can permit any user space
 * mapping to be mapped at.  This is particularly important for
 * non-high vector CPUs.
 */
#define FIRST_USER_ADDRESS	(PAGE_SIZE * 2)

/*
 * Use TASK_SIZE as the ceiling argument for free_pgtables() and
 * free_pgd_range() to avoid freeing the modules pmd when LPAE is enabled (pmd
 * page shared between user and kernel).
 */
#ifdef CONFIG_ARM_LPAE
#define USER_PGTABLES_CEILING	TASK_SIZE
#endif

/*
 * The pgprot_* and protection_map entries will be fixed up in runtime
 * to include the cachable and bufferable bits based on memory policy,
 * as well as any architecture dependent bits like global/ASID and SMP
 * shared mapping bits.
 */
#define _L_PTE_DEFAULT	L_PTE_PRESENT | L_PTE_YOUNG

extern pgprot_t		pgprot_user;
extern pgprot_t		pgprot_kernel;
extern pgprot_t		pgprot_hyp_device;
extern pgprot_t		pgprot_s2;
extern pgprot_t		pgprot_s2_device;

#define _MOD_PROT(p, b)	__pgprot(pgprot_val(p) | (b))

#define PAGE_NONE		_MOD_PROT(pgprot_user, L_PTE_XN | L_PTE_RDONLY | L_PTE_NONE)
#define PAGE_SHARED		_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_XN)
#define PAGE_SHARED_EXEC	_MOD_PROT(pgprot_user, L_PTE_USER)
#define PAGE_COPY		_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_RDONLY | L_PTE_XN)
#define PAGE_COPY_EXEC		_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_RDONLY)
#define PAGE_READONLY		_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_RDONLY | L_PTE_XN)
#define PAGE_READONLY_EXEC	_MOD_PROT(pgprot_user, L_PTE_USER | L_PTE_RDONLY)
#define PAGE_KERNEL		_MOD_PROT(pgprot_kernel, L_PTE_XN)
#define PAGE_KERNEL_EXEC	pgprot_kernel
#define PAGE_HYP		_MOD_PROT(pgprot_kernel, L_PTE_HYP | L_PTE_XN)
#define PAGE_HYP_EXEC		_MOD_PROT(pgprot_kernel, L_PTE_HYP | L_PTE_RDONLY)
#define PAGE_HYP_RO		_MOD_PROT(pgprot_kernel, L_PTE_HYP | L_PTE_RDONLY | L_PTE_XN)
#define PAGE_HYP_DEVICE		_MOD_PROT(pgprot_hyp_device, L_PTE_HYP)
#define PAGE_S2			_MOD_PROT(pgprot_s2, L_PTE_S2_RDONLY)
#define PAGE_S2_DEVICE		_MOD_PROT(pgprot_s2_device, L_PTE_S2_RDONLY)

#define __PAGE_NONE		__pgprot(_L_PTE_DEFAULT | L_PTE_RDONLY | L_PTE_XN | L_PTE_NONE)
#define __PAGE_SHARED		__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_XN)
#define __PAGE_SHARED_EXEC	__pgprot(_L_PTE_DEFAULT | L_PTE_USER)
#define __PAGE_COPY		__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_RDONLY | L_PTE_XN)
#define __PAGE_COPY_EXEC	__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_RDONLY)
#define __PAGE_READONLY		__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_RDONLY | L_PTE_XN)
#define __PAGE_READONLY_EXEC	__pgprot(_L_PTE_DEFAULT | L_PTE_USER | L_PTE_RDONLY)

#define __pgprot_modify(prot,mask,bits)		\
	__pgprot((pgprot_val(prot) & ~(mask)) | (bits))

#define pgprot_noncached(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_UNCACHED)

#define pgprot_writecombine(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_BUFFERABLE)

#define pgprot_stronglyordered(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_UNCACHED)

#ifdef CONFIG_ARM_DMA_MEM_BUFFERABLE
#define pgprot_dmacoherent(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_BUFFERABLE | L_PTE_XN)
#define __HAVE_PHYS_MEM_ACCESS_PROT
struct file;
extern pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				     unsigned long size, pgprot_t vma_prot);
#else
#define pgprot_dmacoherent(prot) \
	__pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_UNCACHED | L_PTE_XN)
#endif

#endif /* __ASSEMBLY__ */

/*
 * The table below defines the page protection levels that we insert into our
 * Linux page table version.  These get translated into the best that the
 * architecture can perform.  Note that on most ARM hardware:
 *  1) We cannot do execute protection
 *  2) If we could do execute protection, then read is implied
 *  3) write implies read permissions
 */
#define __P000  __PAGE_NONE
#define __P001  __PAGE_READONLY
#define __P010  __PAGE_COPY
#define __P011  __PAGE_COPY
#define __P100  __PAGE_READONLY_EXEC
#define __P101  __PAGE_READONLY_EXEC
#define __P110  __PAGE_COPY_EXEC
#define __P111  __PAGE_COPY_EXEC

#define __S000  __PAGE_NONE
#define __S001  __PAGE_READONLY
#define __S010  __PAGE_SHARED
#define __S011  __PAGE_SHARED
#define __S100  __PAGE_READONLY_EXEC
#define __S101  __PAGE_READONLY_EXEC
#define __S110  __PAGE_SHARED_EXEC
#define __S111  __PAGE_SHARED_EXEC

#ifndef __ASSEMBLY__
/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern struct page *empty_zero_page;
#define ZERO_PAGE(vaddr)	(empty_zero_page)


extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

/* to find an entry in a page-table-directory */
// 找到线性地址 addr 对应的的目录项在页全局目录中的索引（相对位置）
#define pgd_index(addr)		((addr) >> PGDIR_SHIFT)
/*
接收内存描述符地址 mm 和线性地址 addr 作为参数。
这个宏产生地址addr 在页全局目录中相应表项的线性地址；
通过内存描述符 mm 内的一个指针可以找到这个页全局目录
*/
#define pgd_offset(mm, addr)	((mm)->pgd + pgd_index(addr))

/* to find an entry in a kernel page-table-directory */
/*
pgd_offset_k宏将一个0-4G范围内的虚拟地址转换为
内核进程主页表中的对应页表项所在的地址。

它首先根据pgd_index计算该虚拟地址对应的页表项
在主页表中的索引值

这里需要注意PGDIR_SHIFT的值为21，而非20，
所以它的偏移是取2M大小区块的索引，
这是由于pgd_t的类型为两个长整形的元素。
然后根据索引值和内核进程中的init_mm.pgd取得页表项地址。
*/
#define pgd_offset_k(addr)	pgd_offset(&init_mm, addr)

#define pmd_none(pmd)		(!pmd_val(pmd))

static inline pte_t *pmd_page_vaddr(pmd_t pmd)
{
	return __va(pmd_val(pmd) & PHYS_MASK & (s32)PAGE_MASK);
}
/*
通过页中间目录项 pmd 产生相应页表的页描述符地址。
在两级或三级分页系统中， pmd 实际上是页全局目录中的一项
*/
#define pmd_page(pmd)		pfn_to_page(__phys_to_pfn(pmd_val(pmd) & PHYS_MASK))

#ifndef CONFIG_HIGHPTE
#define __pte_map(pmd)		pmd_page_vaddr(*(pmd))
#define __pte_unmap(pte)	do { } while (0)
#else
#define __pte_map(pmd)		(pte_t *)kmap_atomic(pmd_page(*(pmd)))
#define __pte_unmap(pte)	kunmap_atomic(pte)
#endif
/*
产生线性地址 addr 对应的表项在页表中的索引（相对位置）
*/
#define pte_index(addr)		(((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
/*
线性地址 addr 在页中间目录 dir 中有一个对应的项，该宏就产生这个对应项，
即页表的线性地址。另外，该宏只在主内核页表上使用
*/
#define pte_offset_kernel(pmd,addr)	(pmd_page_vaddr(*(pmd)) + pte_index(addr))
/*
接收指向一个页中间目录项的指针 dir 和线性地址 addr 作为参数，
它产生与线性地址 addr 相对应的页表项的线性地址。
如果页表被保存在高端存储器中，那么内核建立一个临时内核映射，
并用 pte_unmap 对它进行释放。 pte_offset_map_nested 宏和 pte_unmap_nested 宏是相同的，
但它们使用不同的临时内核映射
*/
#define pte_offset_map(pmd,addr)	(__pte_map(pmd) + pte_index(addr))
#define pte_unmap(pte)			__pte_unmap(pte)

#define pte_pfn(pte)		((pte_val(pte) & PHYS_MASK) >> PAGE_SHIFT)
#define pfn_pte(pfn,prot)	__pte(__pfn_to_phys(pfn) | pgprot_val(prot))
/*
返回页表项 x 所引用页的描述符地址
*/
#define pte_page(pte)		pfn_to_page(pte_pfn(pte))
/*
接收页描述符地址 p 和一组访问权限 prot 作为参数，并创建相应的页表项 
*/
#define mk_pte(page,prot)	pfn_pte(page_to_pfn(page), prot)

#define pte_clear(mm,addr,ptep)	set_pte_ext(ptep, __pte(0), 0)

#define pte_isset(pte, val)	((u32)(val) == (val) ? pte_val(pte) & (val) \
						: !!(pte_val(pte) & (val)))
#define pte_isclear(pte, val)	(!(pte_val(pte) & (val)))

#define pte_none(pte)		(!pte_val(pte))
#define pte_present(pte)	(pte_isset((pte), L_PTE_PRESENT))
#define pte_valid(pte)		(pte_isset((pte), L_PTE_VALID))
#define pte_accessible(mm, pte)	(mm_tlb_flush_pending(mm) ? pte_present(pte) : pte_valid(pte))
#define pte_write(pte)		(pte_isclear((pte), L_PTE_RDONLY))
#define pte_dirty(pte)		(pte_isset((pte), L_PTE_DIRTY))
#define pte_young(pte)		(pte_isset((pte), L_PTE_YOUNG))
#define pte_exec(pte)		(pte_isclear((pte), L_PTE_XN))

#define pte_valid_user(pte)	\
	(pte_valid(pte) && pte_isset((pte), L_PTE_USER) && pte_young(pte))

#if __LINUX_ARM_ARCH__ < 6
static inline void __sync_icache_dcache(pte_t pteval)
{
}
#else
extern void __sync_icache_dcache(pte_t pteval);
#endif

static inline void set_pte_at(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep, pte_t pteval)
{
	unsigned long ext = 0;

	if (addr < TASK_SIZE && pte_valid_user(pteval)) {
		if (!pte_special(pteval))
			__sync_icache_dcache(pteval);
		ext |= PTE_EXT_NG;
	}

	set_pte_ext(ptep, pteval, ext);
}

static inline pte_t clear_pte_bit(pte_t pte, pgprot_t prot)
{
	pte_val(pte) &= ~pgprot_val(prot);
	return pte;
}

static inline pte_t set_pte_bit(pte_t pte, pgprot_t prot)
{
	pte_val(pte) |= pgprot_val(prot);
	return pte;
}
//清除 Read/Write 标志
static inline pte_t pte_wrprotect(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(L_PTE_RDONLY));
}
// 设置 Read/Write 标志
static inline pte_t pte_mkwrite(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(L_PTE_RDONLY));
}
// 清除 Dirty 标志
static inline pte_t pte_mkclean(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(L_PTE_DIRTY));
}
// 设置 Dirty 标志
static inline pte_t pte_mkdirty(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(L_PTE_DIRTY));
}
// 清除 Accessed 标志（把此页标记为未访问）
static inline pte_t pte_mkold(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(L_PTE_YOUNG));
}
// 设置 Accessed 标志（把此页标记为访问过）
static inline pte_t pte_mkyoung(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(L_PTE_YOUNG));
}
// 设置 User/Supervisor 标志
static inline pte_t pte_mkexec(pte_t pte)
{
	return clear_pte_bit(pte, __pgprot(L_PTE_XN));
}

static inline pte_t pte_mknexec(pte_t pte)
{
	return set_pte_bit(pte, __pgprot(L_PTE_XN));
}
// 把页表项 p 的所有访问权限设置为指定的值
static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	const pteval_t mask = L_PTE_XN | L_PTE_RDONLY | L_PTE_USER |
		L_PTE_NONE | L_PTE_VALID;
	pte_val(pte) = (pte_val(pte) & ~mask) | (pgprot_val(newprot) & mask);
	return pte;
}

/*
 * Encode and decode a swap entry.  Swap entries are stored in the Linux
 * page tables as follows:
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   <--------------- offset ------------------------> < type -> 0 0
 *
 * This gives us up to 31 swap files and 128GB per swap file.  Note that
 * the offset field is always non-zero.
 */
#define __SWP_TYPE_SHIFT	2
#define __SWP_TYPE_BITS		5
#define __SWP_TYPE_MASK		((1 << __SWP_TYPE_BITS) - 1)
#define __SWP_OFFSET_SHIFT	(__SWP_TYPE_BITS + __SWP_TYPE_SHIFT)

#define __swp_type(x)		(((x).val >> __SWP_TYPE_SHIFT) & __SWP_TYPE_MASK)
#define __swp_offset(x)		((x).val >> __SWP_OFFSET_SHIFT)
#define __swp_entry(type,offset) ((swp_entry_t) { ((type) << __SWP_TYPE_SHIFT) | ((offset) << __SWP_OFFSET_SHIFT) })

#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(swp)	((pte_t) { (swp).val })

/*
 * It is an error for the kernel to have more swap files than we can
 * encode in the PTEs.  This ensures that we know when MAX_SWAPFILES
 * is increased beyond what we presently support.
 */
#define MAX_SWAPFILES_CHECK() BUILD_BUG_ON(MAX_SWAPFILES_SHIFT > __SWP_TYPE_BITS)

/* Needs to be defined here and not in linux/mm.h, as it is arch dependent */
/* FIXME: this is not correct */
#define kern_addr_valid(addr)	(1)

#include <asm-generic/pgtable.h>

/*
 * We provide our own arch_get_unmapped_area to cope with VIPT caches.
 */
#define HAVE_ARCH_UNMAPPED_AREA
#define HAVE_ARCH_UNMAPPED_AREA_TOPDOWN

#define pgtable_cache_init() do { } while (0)

#endif /* !__ASSEMBLY__ */

#endif /* CONFIG_MMU */

#endif /* _ASMARM_PGTABLE_H */
