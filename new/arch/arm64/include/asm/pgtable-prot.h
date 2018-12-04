/*
 * Copyright (C) 2016 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_PGTABLE_PROT_H
#define __ASM_PGTABLE_PROT_H

#include <asm/memory.h>
#include <asm/pgtable-hwdef.h>

#include <linux/const.h>

/*
 * Software defined PTE bits definition.
 */
#define PTE_VALID		(_AT(pteval_t, 1) << 0)
#define PTE_WRITE		(PTE_DBM)		 /* same as DBM (51) */
/*
PTE_DIRTY: CPU 在写操作时会设置该标志位，表示对应页面被写过，为脏页。
ARM如何模拟PTE_DIRTY 呢?
在ARM MMU 硬件为一个干净页面建立映射时，设置硬件页表项是 只读 权限的。
当往一个干净的页面写入时，会触发写权限缺页中断(虽然Linux版本的页面表项标记了可写权限，但是ARM 硬件页面表项还不具有写入权限) ，
那么在缺页中断处理 handle_pte_fault ()中会在该页的Linux 版本PTE 页面表项标记为"dirty"，并且发现PTE 页表项内容改变了，
ptep_set_access_flags()函数会把新的Linux 版本的页表项内容写入硬件页表，从而完成模拟过程。
*/

#define PTE_DIRTY		(_AT(pteval_t, 1) << 55)
/*
PTE_SPECIAL 软件定义的比特位，主要用于有以下用途:
内核的零页面zero page 。
大量的驱动程序使用 remap_pfn_range 函数来实现映射内核页面到用户空间。
这些用户程序使用的 VMA 通常设置了(VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP) 属性
vm_insert_page/vm_insert_fpn 映射内核页面到用户空间。
*/
#define PTE_SPECIAL		(_AT(pteval_t, 1) << 56)
#define PTE_PROT_NONE		(_AT(pteval_t, 1) << 58) /* only when !PTE_VALID */

#ifndef __ASSEMBLY__

#include <asm/pgtable-types.h>

#define _PROT_DEFAULT		(PTE_TYPE_PAGE | PTE_AF | PTE_SHARED)
#define _PROT_SECT_DEFAULT	(PMD_TYPE_SECT | PMD_SECT_AF | PMD_SECT_S)

#define PTE_MAYBE_NG		(arm64_kernel_unmapped_at_el0() ? PTE_NG : 0)
#define PMD_MAYBE_NG		(arm64_kernel_unmapped_at_el0() ? PMD_SECT_NG : 0)

#define PROT_DEFAULT		(_PROT_DEFAULT | PTE_MAYBE_NG)
#define PROT_SECT_DEFAULT	(_PROT_SECT_DEFAULT | PMD_MAYBE_NG)

#define PROT_DEVICE_nGnRnE	(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_DIRTY | PTE_WRITE | PTE_ATTRINDX(MT_DEVICE_nGnRnE))
#define PROT_DEVICE_nGnRE	(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_DIRTY | PTE_WRITE | PTE_ATTRINDX(MT_DEVICE_nGnRE))
#define PROT_NORMAL_NC		(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_DIRTY | PTE_WRITE | PTE_ATTRINDX(MT_NORMAL_NC))
#define PROT_NORMAL_WT		(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_DIRTY | PTE_WRITE | PTE_ATTRINDX(MT_NORMAL_WT))
#define PROT_NORMAL		(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_DIRTY | PTE_WRITE | PTE_ATTRINDX(MT_NORMAL))

#define PROT_SECT_DEVICE_nGnRE	(PROT_SECT_DEFAULT | PMD_SECT_PXN | PMD_SECT_UXN | PMD_ATTRINDX(MT_DEVICE_nGnRE))
#define PROT_SECT_NORMAL	(PROT_SECT_DEFAULT | PMD_SECT_PXN | PMD_SECT_UXN | PMD_ATTRINDX(MT_NORMAL))
#define PROT_SECT_NORMAL_EXEC	(PROT_SECT_DEFAULT | PMD_SECT_UXN | PMD_ATTRINDX(MT_NORMAL))

#define _PAGE_DEFAULT		(_PROT_DEFAULT | PTE_ATTRINDX(MT_NORMAL))
#define _HYP_PAGE_DEFAULT	_PAGE_DEFAULT

#define PAGE_KERNEL		__pgprot(PROT_NORMAL)
#define PAGE_KERNEL_RO		__pgprot((PROT_NORMAL & ~PTE_WRITE) | PTE_RDONLY)
#define PAGE_KERNEL_ROX		__pgprot((PROT_NORMAL & ~(PTE_WRITE | PTE_PXN)) | PTE_RDONLY)
#define PAGE_KERNEL_EXEC	__pgprot(PROT_NORMAL & ~PTE_PXN)
#define PAGE_KERNEL_EXEC_CONT	__pgprot((PROT_NORMAL & ~PTE_PXN) | PTE_CONT)

#define PAGE_HYP		__pgprot(_HYP_PAGE_DEFAULT | PTE_HYP | PTE_HYP_XN)
#define PAGE_HYP_EXEC		__pgprot(_HYP_PAGE_DEFAULT | PTE_HYP | PTE_RDONLY)
#define PAGE_HYP_RO		__pgprot(_HYP_PAGE_DEFAULT | PTE_HYP | PTE_RDONLY | PTE_HYP_XN)
#define PAGE_HYP_DEVICE		__pgprot(PROT_DEVICE_nGnRE | PTE_HYP)

#define PAGE_S2_MEMATTR(attr)						\
	({								\
		u64 __val;						\
		if (cpus_have_const_cap(ARM64_HAS_STAGE2_FWB))		\
			__val = PTE_S2_MEMATTR(MT_S2_FWB_ ## attr);	\
		else							\
			__val = PTE_S2_MEMATTR(MT_S2_ ## attr);		\
		__val;							\
	 })

#define PAGE_S2_XN							\
	({								\
		u64 __val;						\
		if (cpus_have_const_cap(ARM64_HAS_CACHE_DIC))		\
			__val = 0;					\
		else							\
			__val = PTE_S2_XN;				\
		__val;							\
	})

#define PAGE_S2			__pgprot(_PROT_DEFAULT | PAGE_S2_MEMATTR(NORMAL) | PTE_S2_RDONLY | PAGE_S2_XN)
#define PAGE_S2_DEVICE		__pgprot(_PROT_DEFAULT | PAGE_S2_MEMATTR(DEVICE_nGnRE) | PTE_S2_RDONLY | PAGE_S2_XN)

#define PAGE_NONE		__pgprot(((_PAGE_DEFAULT) & ~PTE_VALID) | PTE_PROT_NONE | PTE_RDONLY | PTE_NG | PTE_PXN | PTE_UXN)
#define PAGE_SHARED		__pgprot(_PAGE_DEFAULT | PTE_USER | PTE_NG | PTE_PXN | PTE_UXN | PTE_WRITE)
#define PAGE_SHARED_EXEC	__pgprot(_PAGE_DEFAULT | PTE_USER | PTE_NG | PTE_PXN | PTE_WRITE)
#define PAGE_READONLY		__pgprot(_PAGE_DEFAULT | PTE_USER | PTE_RDONLY | PTE_NG | PTE_PXN | PTE_UXN)
#define PAGE_READONLY_EXEC	__pgprot(_PAGE_DEFAULT | PTE_USER | PTE_RDONLY | PTE_NG | PTE_PXN)
#define PAGE_EXECONLY		__pgprot(_PAGE_DEFAULT | PTE_RDONLY | PTE_NG | PTE_PXN)

#define __P000  PAGE_NONE
#define __P001  PAGE_READONLY
#define __P010  PAGE_READONLY
#define __P011  PAGE_READONLY
#define __P100  PAGE_EXECONLY
#define __P101  PAGE_READONLY_EXEC
#define __P110  PAGE_READONLY_EXEC
#define __P111  PAGE_READONLY_EXEC

#define __S000  PAGE_NONE
#define __S001  PAGE_READONLY
#define __S010  PAGE_SHARED
#define __S011  PAGE_SHARED
#define __S100  PAGE_EXECONLY
#define __S101  PAGE_READONLY_EXEC
#define __S110  PAGE_SHARED_EXEC
#define __S111  PAGE_SHARED_EXEC

#endif /* __ASSEMBLY__ */

#endif /* __ASM_PGTABLE_PROT_H */
