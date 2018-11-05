/*
 *  linux/arch/arm/kernel/head.S
 *
 *  Copyright (C) 1994-2002 Russell King
 *  Copyright (c) 2003 ARM Limited
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Kernel startup code for all 32-bit CPUs
 */
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>
#include <asm/cp15.h>
#include <asm/domain.h>
#include <asm/ptrace.h>
#include <asm/asm-offsets.h>
#include <asm/memory.h>
#include <asm/thread_info.h>
#include <asm/pgtable.h>

#if defined(CONFIG_DEBUG_LL) && !defined(CONFIG_DEBUG_SEMIHOSTING)
#include CONFIG_DEBUG_LL_INCLUDE
#endif

/*
 * swapper_pg_dir is the virtual address of the initial page table.
 * We place the page tables 16K below KERNEL_RAM_VADDR.  Therefore, we must
 * make sure that KERNEL_RAM_VADDR is correctly set.  Currently, we expect
 * the least significant 16 bits to be 0x8000, but we could probably
 * relax this restriction to KERNEL_RAM_VADDR >= PAGE_OFFSET + 0x4000.
 */
#define KERNEL_RAM_VADDR	(PAGE_OFFSET + TEXT_OFFSET)
#if (KERNEL_RAM_VADDR & 0xffff) != 0x8000
#error KERNEL_RAM_VADDR must start at 0xXXXX8000
#endif

#ifdef CONFIG_ARM_LPAE
	/* LPAE requires an additional page for the PGD */
#define PG_DIR_SIZE	0x5000
#define PMD_ORDER	3
#else
#define PG_DIR_SIZE	0x4000
#define PMD_ORDER	2
#endif
/*
KERNEL_RAM_VADDR的值与_stext的值相同，代表了内核代码的起始地址。
swapper_pg_dir = KERNEL_RAM_VADDR - PG_DIR_SIZE  =0xc0004000
内核页表机制在系统启动过程中的paging_init函数中使能，
其中对内核主页表的初始化等操作均是通过init_mm.pgd的引用来进行的。
*/
	.globl	swapper_pg_dir
	.equ	swapper_pg_dir, KERNEL_RAM_VADDR - PG_DIR_SIZE

	.macro	pgtbl, rd, phys
	add	\rd, \phys, #TEXT_OFFSET
	sub	\rd, \rd, #PG_DIR_SIZE
	.endm

/*
 * Kernel startup entry point.
 * ---------------------------
 *
 * This is normally called from the decompressor code.  The requirements
 * are: MMU = off, D-cache = off, I-cache = dont care, r0 = 0,
 * r1 = machine nr, r2 = atags or dtb pointer.
 *
 * This code is mostly position independent, so if you link the kernel at
 * 0xc0008000, you call this at __pa(0xc0008000).
 *
 * See linux/arch/arm/tools/mach-types for the complete list of machine
 * numbers for r1.
 *
 * We're trying to keep crap to a minimum; DO NOT add any machine specific
 * crap here - that's what the boot loader (or in extreme, well justified
 * circumstances, zImage) is for.
 *
 * stext的功能是获取处理器类型和机器类型信息，并创建临时的页表，然后开启MMU功能，并跳进第一个C语言函数start_kernel
 */
	.arm

	__HEAD
ENTRY(stext)
 ARM_BE8(setend	be )			@ ensure we are in BE8 mode
/*
检查kernel是ARM模式还是thum模式
*/
 THUMB(	badr	r9, 1f		)	@ Kernel is always entered in ARM.
 THUMB(	bx	r9		)	@ If this is a Thumb-2 kernel,
 THUMB(	.thumb			)	@ switch to Thumb now.
 THUMB(1:			)

#ifdef CONFIG_ARM_VIRT_EXT
	bl	__hyp_stub_install
#endif
	@ ensure svc mode and all interrupts masked
	safe_svcmode_maskall r9
/*
@ 从arm协处理器里面读到CPU ID存储到r9，这里的CPU主要是指arm架构相关的CPU型号，比如ARM9，ARM11等等
*/
	mrc	p15, 0, r9, c0, c0		@ get processor id
/*
	OUT r5=procinfo IN r9=cpuid  在procinfo中，根据CPUID查找对应的proc
*/
	bl	__lookup_processor_type		@ r5=procinfo r9=cpuid
	movs	r10, r5				@ invalid processor (r5=0)?
 THUMB( it	eq )		@ force fixup-able long branch encoding
 /*
 CPUID与proc不匹配，只好打印错误信息并停止了
 */
	beq	__error_p			@ yes, error 'p'

#ifdef CONFIG_ARM_LPAE
	mrc	p15, 0, r3, c0, c1, 4		@ read ID_MMFR0
	and	r3, r3, #0xf			@ extract VMSA support
	cmp	r3, #5				@ long-descriptor translation table format?
 THUMB( it	lo )				@ force fixup-able long branch encoding
	blo	__error_lpae			@ only classic page table format
#endif

#ifndef CONFIG_XIP_KERNEL
/*
r3寄存器中保存标号2处的物理地址值
*/
	adr	r3, 2f
/*
r4保存标号2处的虚拟地址值，r8保存PAGE_OFFSET值
*/
	ldmia	r3, {r4, r8}
/*
计算RAM物理地址和虚拟地址的偏移值 --> r4
*/
	sub	r4, r3, r4			@ (PHYS_OFFSET - PAGE_OFFSET)
	add	r8, r8, r4			@ PHYS_OFFSET
#else
	ldr	r8, =PLAT_PHYS_OFFSET		@ always constant in this case
#endif

	/*
	 * r1 = machine no, r2 = atags or dtb,
	 * r8 = phys_offset, r9 = cpuid, r10 = procinfo
	 * 检查atags parameter的有效性
	 */
	bl	__vet_atags
#ifdef CONFIG_SMP_ON_UP
	bl	__fixup_smp
#endif
#ifdef CONFIG_ARM_PATCH_PHYS_VIRT
	bl	__fixup_pv_table
#endif
/*
创建临时页表，它所要做的工作就是将RAM基地址开始的4M空间的物理地址映射到0xC0000000开始的虚拟地址处
*/
	bl	__create_page_tables
/*
保存__mmap_switched虚拟地址到sp寄存器建立初始化页表以后，
会首先将__mmap_switched这个symbol的链接地址放在sp里面，
然后会跳到__proc_info_list里面的INITFUNC执行.
这个偏移是定义在arch/arm/kernel/asm-offset.c里面，
实际上就是取得__proc_info_list里面的__cpu_flush这个函数执行。
*/
	/*
	 * The following calls CPU specific code in a position independent
	 * manner.  See arch/arm/mm/proc-*.S for details.  r10 = base of
	 * xxx_proc_info structure selected by __lookup_processor_type
	 * above.
	 *
	 * The processor init function will be called with:
	 *  r1 - machine type
	 *  r2 - boot data (atags/dt) pointer
	 *  r4 - translation table base (low word)
	 *  r5 - translation table base (high word, if LPAE)
	 *  r8 - translation table base 1 (pfn if LPAE)
	 *  r9 - cpuid
	 *  r13 - virtual address for __enable_mmu -> __turn_mmu_on
	 *
	 * On return, the CPU will be ready for the MMU to be turned on,
	 * r0 will hold the CPU control register value, r1, r2, r4, and
	 * r9 will be preserved.  r5 will also be preserved if LPAE.
	 */
/**
 * 启用MMU后，会调用__mmap_switched
/* r13中存下__mmap_switched地址，需要注意的是该地址是VMA(虚拟地址) */
	ldr	r13, =__mmap_switched		@ address to jump to after
						@ mmu has been enabled
/**
 * 返回地址，调用procinfo中的初始化函数后执行__enable_mmu
 */
	badr	lr, 1f				@ return (PIC) address
#ifdef CONFIG_ARM_LPAE
	mov	r5, #0				@ high TTBR0
	mov	r8, r4, lsr #12			@ TTBR1 is swapper_pg_dir pfn
#else
/*
对于ARM指令(不是THUMB指令)，跳转到对应芯片结构的__cpu_flush执行
*/
	mov	r8, r4				@ set TTBR1 to swapper_pg_dir
#endif
/**
 * r10中保存了procinfo，调用它的init函数
 * 对V7来说，调用的是__v7_setup
 */
	ldr	r12, [r10, #PROCINFO_INITFUNC]
	add	r12, r12, r10
/**
 * 返回后，会执行lr中保存的返回地址，即1f，调用__enable_mmu
 */
	ret	r12
/*
打开MMU以后我们就可以使用虚拟地址了，而不需要我们自己来进行地址的重定位，
ARM硬件会完成这部分的工作。打开MMU以后，会将SP的值赋给PC，这样代码就会跳到__mmap_switched处执行的。
*/
1:	b	__enable_mmu
ENDPROC(stext)
	.ltorg
#ifndef CONFIG_XIP_KERNEL
2:	.long	.
	.long	PAGE_OFFSET
#endif

/*
 * Setup the initial page tables.  We only setup the barest
 * amount which are required to get the kernel running, which
 * generally means mapping in the kernel code.
 *
 * r8 = phys_offset, r9 = cpuid, r10 = procinfo
 *
 * Returns:
 *  r0, r3, r5-r7 corrupted
 *  r4 = physical page table address
 */
__create_page_tables:
/*
add r4,r8,#TEXT_OFFSET;sub r4,r4,#PG_DIR_SIZE
设置r4为内核页表起始地址，物理地址:内核代码段起始位置TEXT_OFFSET(0x8000)前移PG_DIR_SIZE(0x4000)
arch/arm/Makefile   textofs-y:= 0x00008000  TEXT_OFFSET := $(textofs-y)

kernel编译链接时的入口地址在0xc0008000(PAGE_OFFSET + TEXT_OFFSET)，
但其物理地址不等于其链接的虚拟地址，image的线性映射实现其运行地址等于链接地址。
kernel的每一页表映射1M，所以入口处在(0x80200000-->0xc0000000)映射页表中完成映射。
物理地址和虚拟地址的1M内偏移必须一致呀。kernel定义的TEXT_OFFSET = 0x8000.
所以加载的物理地址必须为0x****8000. 这样，开启MMU后，访问0xc0008000附近指令，MMU根据TLB才能正确映射找到0x****8000附近的指令。

为kernel建立临时页表, kernel里面的所有符号在链接时，都使用了虚拟地址值。
在完成基本的初始化后，kernel代码将跳到第一个C语言函数start_kernel来执行，
在哪个时候，这些虚拟地址必须能够对它所存放在真正内存位置，否则运行将为出错。
为此，CPU必须开启MMU，但在开启 MMU前，必须为虚拟地址到物理地址的映射建立相应的页表。
在开启MMU后，kernel指并不马上将PC值指向start_kernl，而是要做一些C语言运行期的设置，如堆栈，重定义等工作后才跳到start_kernel去执行。
在此过程中，PC值还是物理地址，因此还需要为这段内存空间建立va = pa的内存映射关系。
当然，本函数建立的所有页表都会在将来paging_init销毁再重建，这是临时过度性的映射关系和页表。
计算页表起始物理地址保存到r4寄存器

r8寄存器保存的是物理内存的起始地址(在这里我们是0x80200000),TEXT_OFFSET是0x8000, PG_DIR_SIZE是0x4000，
根据宏pgtbl计算得到的页表起始地址为0x80204000,而kernel image起始地址为0x80208000,
即页表是存放在kernel其实地址下16KB的地方(0x80204000 ~ 0x80208000 | 0xc0004000 ~ 0xc0008000).

create_page_table完成了3种地址映射的页表空间填写：
* turn_mmu_on所在1M空间的平映射
* kernel image的线性映射
* atags所在1M空间的线性映射
*/
	pgtbl	r4, r8				@ page table address

	/*
	 * Clear the swapper page table
     * 清空16KB页表空间
	 */
	mov	r0, r4
	mov	r3, #0
	add	r6, r0, #PG_DIR_SIZE
1:	str	r3, [r0], #4
	str	r3, [r0], #4
	str	r3, [r0], #4
	str	r3, [r0], #4
	teq	r0, r6
	bne	1b

#ifdef CONFIG_ARM_LPAE
	/*
	 * Build the PGD table (first level) to point to the PMD table. A PGD
	 * entry is 64-bit wide.
	 */
	mov	r0, r4
	add	r3, r4, #0x1000			@ first PMD table address
	orr	r3, r3, #3			@ PGD block type
	mov	r6, #4				@ PTRS_PER_PGD
	mov	r7, #1 << (55 - 32)		@ L_PGD_SWAPPER
1:
#ifdef CONFIG_CPU_ENDIAN_BE8
	str	r7, [r0], #4			@ set top PGD entry bits
	str	r3, [r0], #4			@ set bottom PGD entry bits
#else
	str	r3, [r0], #4			@ set bottom PGD entry bits
	str	r7, [r0], #4			@ set top PGD entry bits
#endif
	add	r3, r3, #0x1000			@ next PMD table
	subs	r6, r6, #1
	bne	1b

	add	r4, r4, #0x1000			@ point to the PMD tables
#ifdef CONFIG_CPU_ENDIAN_BE8
	add	r4, r4, #4			@ we only write the bottom word
#endif
#endif
/*
获得proc_info_list的__cpu_mm_mmu_flags的值，并存储到r7中
r10 = proc_info_list类型结构体的基地址.
*/
	ldr	r7, [r10, #PROCINFO_MM_MMUFLAGS] @ mm_mmuflags

	/*
	 * Create identity mapping to cater for __enable_mmu.
	 * This identity mapping will be removed by paging_init().
     * 首先建立包含__turn_mmu_on函数1M空间的平映射(virt addr = phy addr)
	 */
	adr	r0, __turn_mmu_on_loc
	ldmia	r0, {r3, r5, r6}
/*
计算函数__turn_mmu_on和__turn_mmu_on_end函数的物理地址.
*/
	sub	r0, r0, r3			@ virt->phys offset
	add	r5, r5, r0			@ phys __turn_mmu_on
	add	r6, r6, r0			@ phys __turn_mmu_on_end
/*
因1页(4KB)映射1M空间，所以SECTION_SHIFT为20, 右移20位后，
r5，r6代表该段地址空间的物理地址(页内偏移值)
*/
	mov	r5, r5, lsr #SECTION_SHIFT
	mov	r6, r6, lsr #SECTION_SHIFT

/*
r5左移20位，获取该页基地址，或上CPU的mmuflags，存在r3中
*/
1:	orr	r3, r7, r5, lsl #SECTION_SHIFT	@ flags + kernel base
/*
将r3值存储在页目录表空间（r4起始）的（r5<<2）的页表中, 因一页用4bytes表示，所以PMD_ORDER=2
*/
	str	r3, [r4, r5, lsl #PMD_ORDER]	@ identity mapping
/*
r5与r6之前相距多个1M，则需要填写多个页表。
因turn_mmu_on函数很短，所以肯定在1M内，该处r5=r6
*/
	cmp	r5, r6
	addlo	r5, r5, #1			@ next section
	blo	1b

	/*
	 * Map our RAM from the start to the end of the kernel .bss section.
	 */
	add	r0, r4, #PAGE_OFFSET >> (SECTION_SHIFT - PMD_ORDER)
	ldr	r6, =(_end - 1)
	orr	r3, r8, r7
	add	r6, r4, r6, lsr #(SECTION_SHIFT - PMD_ORDER)
1:	str	r3, [r0], #1 << PMD_ORDER
	add	r3, r3, #1 << SECTION_SHIFT
	cmp	r0, r6
	bls	1b

#ifdef CONFIG_XIP_KERNEL
	/*
	 * Map the kernel image separately as it is not located in RAM.
	 */
#define XIP_START XIP_VIRT_ADDR(CONFIG_XIP_PHYS_ADDR)
	mov	r3, pc
/*
计算当前kernel运行的物理地址所在物理页面的起始地址
*/
	mov	r3, r3, lsr #SECTION_SHIFT
	orr	r3, r7, r3, lsl #SECTION_SHIFT
/*
因为KERNEL_START是内核的起始虚拟地址(0xC0008000)，KERNEL_END为内核的结束虚拟地址，
所以下面的代码实际上是将物理地址为kernel的起始地址(0x80208000)的一段内存空间
(大小为内核映像文件的大小)映射到虚拟地址0xC0008000。
*/
	add	r0, r4,  #(XIP_START & 0xff000000) >> (SECTION_SHIFT - PMD_ORDER)
	str	r3, [r0, #((XIP_START & 0x00f00000) >> SECTION_SHIFT) << PMD_ORDER]!
	ldr	r6, =(_edata_loc - 1)
	add	r0, r0, #1 << PMD_ORDER
	add	r6, r4, r6, lsr #(SECTION_SHIFT - PMD_ORDER)
1:	cmp	r0, r6
	add	r3, r3, #1 << SECTION_SHIFT
	strls	r3, [r0], #1 << PMD_ORDER
	bls	1b
#endif

	/*
	 * Then map boot params address in r2 if specified.
	 * We map 2 sections in case the ATAGs/DTB crosses a section boundary.
	 */
/*
将atags的1M地址空间做线性映射，方便start_kernel中对args进行分析
kernel image可以加载到任何sdram地址空间的0x****8000即可。atags地址是有bootloader中指定，然后告诉kernel。
那就有这样一种情况，加入sdram起始地址为0x80200000，atags起始地址为0x80200100。
但kernel image我加载到0x81008000，可以看出，这时atags跟kernel image就在不同一1M空间啦
atags单独的线性映射操作还是很有必要的。
*/
	mov	r0, r2, lsr #SECTION_SHIFT
	movs	r0, r0, lsl #SECTION_SHIFT
	subne	r3, r0, r8
	addne	r3, r3, #PAGE_OFFSET
	addne	r3, r4, r3, lsr #(SECTION_SHIFT - PMD_ORDER)
	orrne	r6, r7, r0
	strne	r6, [r3], #1 << PMD_ORDER
	addne	r6, r6, #1 << SECTION_SHIFT
	strne	r6, [r3]

#if defined(CONFIG_ARM_LPAE) && defined(CONFIG_CPU_ENDIAN_BE8)
	sub	r4, r4, #4			@ Fixup page table pointer
						@ for 64-bit descriptors
#endif

#ifdef CONFIG_DEBUG_LL
#if !defined(CONFIG_DEBUG_ICEDCC) && !defined(CONFIG_DEBUG_SEMIHOSTING)
	/*
	 * Map in IO space for serial debugging.
	 * This allows debug messages to be output
	 * via a serial console before paging_init.
	 */
	addruart r7, r3, r0

	mov	r3, r3, lsr #SECTION_SHIFT
	mov	r3, r3, lsl #PMD_ORDER

	add	r0, r4, r3
	mov	r3, r7, lsr #SECTION_SHIFT
	ldr	r7, [r10, #PROCINFO_IO_MMUFLAGS] @ io_mmuflags
	orr	r3, r7, r3, lsl #SECTION_SHIFT
#ifdef CONFIG_ARM_LPAE
	mov	r7, #1 << (54 - 32)		@ XN
#ifdef CONFIG_CPU_ENDIAN_BE8
	str	r7, [r0], #4
	str	r3, [r0], #4
#else
	str	r3, [r0], #4
	str	r7, [r0], #4
#endif
#else
	orr	r3, r3, #PMD_SECT_XN
	str	r3, [r0], #4
#endif

#else /* CONFIG_DEBUG_ICEDCC || CONFIG_DEBUG_SEMIHOSTING */
	/* we don't need any serial debugging mappings */
	ldr	r7, [r10, #PROCINFO_IO_MMUFLAGS] @ io_mmuflags
#endif

#if defined(CONFIG_ARCH_NETWINDER) || defined(CONFIG_ARCH_CATS)
	/*
	 * If we're using the NetWinder or CATS, we also need to map
	 * in the 16550-type serial port for the debug messages
	 */
	add	r0, r4, #0xff000000 >> (SECTION_SHIFT - PMD_ORDER)
	orr	r3, r7, #0x7c000000
	str	r3, [r0]
#endif
#ifdef CONFIG_ARCH_RPC
	/*
	 * Map in screen at 0x02000000 & SCREEN2_BASE
	 * Similar reasons here - for debug.  This is
	 * only for Acorn RiscPC architectures.
	 */
	add	r0, r4, #0x02000000 >> (SECTION_SHIFT - PMD_ORDER)
	orr	r3, r7, #0x02000000
	str	r3, [r0]
	add	r0, r4, #0xd8000000 >> (SECTION_SHIFT - PMD_ORDER)
	str	r3, [r0]
#endif
#endif
#ifdef CONFIG_ARM_LPAE
	sub	r4, r4, #0x1000		@ point to the PGD table
#endif
	ret	lr
ENDPROC(__create_page_tables)
	.ltorg
	.align
__turn_mmu_on_loc:
	.long	.
	.long	__turn_mmu_on
	.long	__turn_mmu_on_end

#if defined(CONFIG_SMP)
	.text
	.arm
ENTRY(secondary_startup_arm)
 THUMB(	badr	r9, 1f		)	@ Kernel is entered in ARM.
 THUMB(	bx	r9		)	@ If this is a Thumb-2 kernel,
 THUMB(	.thumb			)	@ switch to Thumb now.
 THUMB(1:			)
ENTRY(secondary_startup)
	/*
	 * Common entry point for secondary CPUs.
	 *
	 * Ensure that we're in SVC mode, and IRQs are disabled.  Lookup
	 * the processor type - there is no need to check the machine type
	 * as it has already been validated by the primary processor.
	 */

 ARM_BE8(setend	be)				@ ensure we are in BE8 mode

#ifdef CONFIG_ARM_VIRT_EXT
	bl	__hyp_stub_install_secondary
#endif
	safe_svcmode_maskall r9

	mrc	p15, 0, r9, c0, c0		@ get processor id
	bl	__lookup_processor_type
	movs	r10, r5				@ invalid processor?
	moveq	r0, #'p'			@ yes, error 'p'
 THUMB( it	eq )		@ force fixup-able long branch encoding
	beq	__error_p

	/*
	 * Use the page tables supplied from  __cpu_up.
	 */
	adr	r4, __secondary_data
	ldmia	r4, {r5, r7, r12}		@ address to jump to after
	sub	lr, r4, r5			@ mmu has been enabled
	add	r3, r7, lr
	ldrd	r4, [r3, #0]			@ get secondary_data.pgdir
ARM_BE8(eor	r4, r4, r5)			@ Swap r5 and r4 in BE:
ARM_BE8(eor	r5, r4, r5)			@ it can be done in 3 steps
ARM_BE8(eor	r4, r4, r5)			@ without using a temp reg.
	ldr	r8, [r3, #8]			@ get secondary_data.swapper_pg_dir
	badr	lr, __enable_mmu		@ return address
	mov	r13, r12			@ __secondary_switched address
	ldr	r12, [r10, #PROCINFO_INITFUNC]
	add	r12, r12, r10			@ initialise processor
						@ (return control reg)
	ret	r12
ENDPROC(secondary_startup)
ENDPROC(secondary_startup_arm)

	/*
	 * r6  = &secondary_data
	 */
ENTRY(__secondary_switched)
	ldr	sp, [r7, #12]			@ get secondary_data.stack
	mov	fp, #0
	b	secondary_start_kernel
ENDPROC(__secondary_switched)

	.align

	.type	__secondary_data, %object
__secondary_data:
	.long	.
	.long	secondary_data
	.long	__secondary_switched
#endif /* defined(CONFIG_SMP) */



/*
 * Setup common bits before finally enabling the MMU.  Essentially
 * this is just loading the page table pointer and domain access
 * registers.  All these registers need to be preserved by the
 * processor setup function (or set in the case of r0)
 *
 *  r0  = cp#15 control register
 *  r1  = machine ID
 *  r2  = atags or dtb pointer
 *  r4  = TTBR pointer (low word)
 *  r5  = TTBR pointer (high word if LPAE)
 *  r9  = processor ID
 *  r13 = *virtual* address to jump to upon completion
 */
/**
 * 启用MMU，r13中保存的是__mmap_switched
 */
__enable_mmu:
/**
 * 设置CR_A位，该位控制alignment_abort异常
 */
#if defined(CONFIG_ALIGNMENT_TRAP) && __LINUX_ARM_ARCH__ < 6
	orr	r0, r0, #CR_A
#else
	bic	r0, r0, #CR_A
#endif
/**
 * 设置数据缓存控制位
 */
#ifdef CONFIG_CPU_DCACHE_DISABLE
	bic	r0, r0, #CR_C
#endif
/**
 * 禁用分支预测
 */
#ifdef CONFIG_CPU_BPREDICT_DISABLE
	bic	r0, r0, #CR_Z
#endif
/**
 * 禁用指令cache
 */
#ifdef CONFIG_CPU_ICACHE_DISABLE
	bic	r0, r0, #CR_I
#endif
#ifdef CONFIG_ARM_LPAE
	mcrr	p15, 0, r4, r5, c2		@ load TTBR0
#else
/**
 * 设置域，用于控制不同模式下能访问的虚拟地址空间范围
 */
	mov	r5, #DACR_INIT
/**
 * 将域设置字段加载到CP15的C3寄存器
 * 配置CP15 c3寄存器，配置ARM域的访问权限
 */
	mcr	p15, 0, r5, c3, c0, 0		@ load domain access register
/**
 * 加载页表转换表指针到CP15寄存器
 * r4寄存器保存页表物理地址,配置CP15 c2寄存器，指定内存页表地址
 */
	mcr	p15, 0, r4, c2, c0, 0		@ load page table pointer
#endif
/**
 * 打开MMU
 */
	b	__turn_mmu_on
ENDPROC(__enable_mmu)

/*
 * Enable the MMU.  This completely changes the structure of the visible
 * memory space.  You will not be able to trace execution through this.
 * If you have an enquiry about this, *please* check the linux-arm-kernel
 * mailing list archives BEFORE sending another post to the list.
 *
 *  r0  = cp#15 control register
 *  r1  = machine ID
 *  r2  = atags or dtb pointer
 *  r9  = processor ID
 *  r13 = *virtual* address to jump to upon completion
 *
 * other registers depend on the function called upon completion
 */
/*
turn_mmu_on，主要是完成开启MMU的操作。那为什么将turn_mmu_on处做一个平映射？
可以想象，执行开启MMU指令之前，CPU取指是在0x80208000附近turn_mmu_on中。
如果只是做kernel image的线性映射，执行开启MMU指令后，CPU所看到的地址就全变啦。
turn_mmu_on对于CPU来说在0xc0008000附近，0x80208000附近对于CPU来说已经不可预知了。
但是CPU不知道这些，它只管按照地址一条条取指令，执行指令。
所以不做turn_mmu_on的平映射(virt addr = phy addr)，turn_mmu_on在开启MMU后的运行是完全不可知。
完成turn_mmu_on的平映射，我们可以在turn_mmu_on末尾MMU已经开启稳定后，修改PC到0xc0008000附近，就可以解决从0x8xxxxxxx到0xcxxxxxxx的跳转。
*/
	.align	5
	.pushsection	.idmap.text, "ax"
ENTRY(__turn_mmu_on)
/**
 * r0中已经包含MMU硬件相关的位了
 */
	mov	r0, r0
	instr_sync
/**
 * 将其写入控制器寄存器c1，激活MMU
 */
/*
* 将r0值写入CP15的c0寄存器，查看CP15说明，c0寄存器的第0位表征MMU的开启，
* 刚才__v7_setup中v7_crval给出的mmuset值已经将第0位置1了，因此写入c0后MMU就开启了
*/
	mcr	p15, 0, r0, c1, c0, 0		@ write control reg
/*
* 接下来CPU取指地址是当前pc+4,由于做了平映射，虽然接下来是虚拟地址，
* 但是该虚拟地址跟之前的物理地址是完全一致的，pc+4可以取到
* mrc p15, 0, r3, c0, c0, 0指令，并不会导致CPU取指上的问题
*/
	mrc	p15, 0, r3, c0, c0, 0		@ read id reg
	instr_sync
/**
 * 这条指令相当于NOP指令，刚切换了MMU，必须在CPU流水线中放这样的指令
 */
	mov	r3, r3
/**
 * r13中保存的是__mmap_switched函数的地址，跳过去执行吧。
 * 开启MMU之旅:)
 */
/* 开启MMU后，修改PC值为r13执行，也就是__mmap_switched，该函数已经是运行在MMU之上的虚拟地址了 */
	mov	r3, r13
	ret	r3
__turn_mmu_on_end:
ENDPROC(__turn_mmu_on)
	.popsection


#ifdef CONFIG_SMP_ON_UP
	__HEAD
__fixup_smp:
	and	r3, r9, #0x000f0000	@ architecture version
	teq	r3, #0x000f0000		@ CPU ID supported?
	bne	__fixup_smp_on_up	@ no, assume UP

	bic	r3, r9, #0x00ff0000
	bic	r3, r3, #0x0000000f	@ mask 0xff00fff0
	mov	r4, #0x41000000
	orr	r4, r4, #0x0000b000
	orr	r4, r4, #0x00000020	@ val 0x4100b020
	teq	r3, r4			@ ARM 11MPCore?
	reteq	lr			@ yes, assume SMP

	mrc	p15, 0, r0, c0, c0, 5	@ read MPIDR
	and	r0, r0, #0xc0000000	@ multiprocessing extensions and
	teq	r0, #0x80000000		@ not part of a uniprocessor system?
	bne    __fixup_smp_on_up	@ no, assume UP

	@ Core indicates it is SMP. Check for Aegis SOC where a single
	@ Cortex-A9 CPU is present but SMP operations fault.
	mov	r4, #0x41000000
	orr	r4, r4, #0x0000c000
	orr	r4, r4, #0x00000090
	teq	r3, r4			@ Check for ARM Cortex-A9
	retne	lr			@ Not ARM Cortex-A9,

	@ If a future SoC *does* use 0x0 as the PERIPH_BASE, then the
	@ below address check will need to be #ifdef'd or equivalent
	@ for the Aegis platform.
	mrc	p15, 4, r0, c15, c0	@ get SCU base address
	teq	r0, #0x0		@ '0' on actual UP A9 hardware
	beq	__fixup_smp_on_up	@ So its an A9 UP
	ldr	r0, [r0, #4]		@ read SCU Config
ARM_BE8(rev	r0, r0)			@ byteswap if big endian
	and	r0, r0, #0x3		@ number of CPUs
	teq	r0, #0x0		@ is 1?
	retne	lr

__fixup_smp_on_up:
	adr	r0, 1f
	ldmia	r0, {r3 - r5}
	sub	r3, r0, r3
	add	r4, r4, r3
	add	r5, r5, r3
	b	__do_fixup_smp_on_up
ENDPROC(__fixup_smp)

	.align
1:	.word	.
	.word	__smpalt_begin
	.word	__smpalt_end

	.pushsection .data
	.align	2
	.globl	smp_on_up
smp_on_up:
	ALT_SMP(.long	1)
	ALT_UP(.long	0)
	.popsection
#endif

	.text
__do_fixup_smp_on_up:
	cmp	r4, r5
	reths	lr
	ldmia	r4!, {r0, r6}
 ARM(	str	r6, [r0, r3]	)
 THUMB(	add	r0, r0, r3	)
#ifdef __ARMEB__
 THUMB(	mov	r6, r6, ror #16	)	@ Convert word order for big-endian.
#endif
 THUMB(	strh	r6, [r0], #2	)	@ For Thumb-2, store as two halfwords
 THUMB(	mov	r6, r6, lsr #16	)	@ to be robust against misaligned r3.
 THUMB(	strh	r6, [r0]	)
	b	__do_fixup_smp_on_up
ENDPROC(__do_fixup_smp_on_up)

ENTRY(fixup_smp)
	stmfd	sp!, {r4 - r6, lr}
	mov	r4, r0
	add	r5, r0, r1
	mov	r3, #0
	bl	__do_fixup_smp_on_up
	ldmfd	sp!, {r4 - r6, pc}
ENDPROC(fixup_smp)

#ifdef __ARMEB__
#define LOW_OFFSET	0x4
#define HIGH_OFFSET	0x0
#else
#define LOW_OFFSET	0x0
#define HIGH_OFFSET	0x4
#endif

#ifdef CONFIG_ARM_PATCH_PHYS_VIRT

/* __fixup_pv_table - patch the stub instructions with the delta between
 * PHYS_OFFSET and PAGE_OFFSET, which is assumed to be 16MiB aligned and
 * can be expressed by an immediate shifter operand. The stub instruction
 * has a form of '(add|sub) rd, rn, #imm'.
 */
	__HEAD
__fixup_pv_table:
	adr	r0, 1f
	ldmia	r0, {r3-r7}
	mvn	ip, #0
	subs	r3, r0, r3	@ PHYS_OFFSET - PAGE_OFFSET
	add	r4, r4, r3	@ adjust table start address
	add	r5, r5, r3	@ adjust table end address
	add	r6, r6, r3	@ adjust __pv_phys_pfn_offset address
	add	r7, r7, r3	@ adjust __pv_offset address
	mov	r0, r8, lsr #PAGE_SHIFT	@ convert to PFN
	str	r0, [r6]	@ save computed PHYS_OFFSET to __pv_phys_pfn_offset
	strcc	ip, [r7, #HIGH_OFFSET]	@ save to __pv_offset high bits
	mov	r6, r3, lsr #24	@ constant for add/sub instructions
	teq	r3, r6, lsl #24 @ must be 16MiB aligned
THUMB(	it	ne		@ cross section branch )
	bne	__error
	str	r3, [r7, #LOW_OFFSET]	@ save to __pv_offset low bits
	b	__fixup_a_pv_table
ENDPROC(__fixup_pv_table)

	.align
1:	.long	.
	.long	__pv_table_begin
	.long	__pv_table_end
2:	.long	__pv_phys_pfn_offset
	.long	__pv_offset

	.text
__fixup_a_pv_table:
	adr	r0, 3f
	ldr	r6, [r0]
	add	r6, r6, r3
	ldr	r0, [r6, #HIGH_OFFSET]	@ pv_offset high word
	ldr	r6, [r6, #LOW_OFFSET]	@ pv_offset low word
	mov	r6, r6, lsr #24
	cmn	r0, #1
#ifdef CONFIG_THUMB2_KERNEL
	moveq	r0, #0x200000	@ set bit 21, mov to mvn instruction
	lsls	r6, #24
	beq	2f
	clz	r7, r6
	lsr	r6, #24
	lsl	r6, r7
	bic	r6, #0x0080
	lsrs	r7, #1
	orrcs	r6, #0x0080
	orr	r6, r6, r7, lsl #12
	orr	r6, #0x4000
	b	2f
1:	add     r7, r3
	ldrh	ip, [r7, #2]
ARM_BE8(rev16	ip, ip)
	tst	ip, #0x4000
	and	ip, #0x8f00
	orrne	ip, r6	@ mask in offset bits 31-24
	orreq	ip, r0	@ mask in offset bits 7-0
ARM_BE8(rev16	ip, ip)
	strh	ip, [r7, #2]
	bne	2f
	ldrh	ip, [r7]
ARM_BE8(rev16	ip, ip)
	bic	ip, #0x20
	orr	ip, ip, r0, lsr #16
ARM_BE8(rev16	ip, ip)
	strh	ip, [r7]
2:	cmp	r4, r5
	ldrcc	r7, [r4], #4	@ use branch for delay slot
	bcc	1b
	bx	lr
#else
#ifdef CONFIG_CPU_ENDIAN_BE8
	moveq	r0, #0x00004000	@ set bit 22, mov to mvn instruction
#else
	moveq	r0, #0x400000	@ set bit 22, mov to mvn instruction
#endif
	b	2f
1:	ldr	ip, [r7, r3]
#ifdef CONFIG_CPU_ENDIAN_BE8
	@ in BE8, we load data in BE, but instructions still in LE
	bic	ip, ip, #0xff000000
	tst	ip, #0x000f0000	@ check the rotation field
	orrne	ip, ip, r6, lsl #24 @ mask in offset bits 31-24
	biceq	ip, ip, #0x00004000 @ clear bit 22
	orreq	ip, ip, r0      @ mask in offset bits 7-0
#else
	bic	ip, ip, #0x000000ff
	tst	ip, #0xf00	@ check the rotation field
	orrne	ip, ip, r6	@ mask in offset bits 31-24
	biceq	ip, ip, #0x400000	@ clear bit 22
	orreq	ip, ip, r0	@ mask in offset bits 7-0
#endif
	str	ip, [r7, r3]
2:	cmp	r4, r5
	ldrcc	r7, [r4], #4	@ use branch for delay slot
	bcc	1b
	ret	lr
#endif
ENDPROC(__fixup_a_pv_table)

	.align
3:	.long __pv_offset

ENTRY(fixup_pv_table)
	stmfd	sp!, {r4 - r7, lr}
	mov	r3, #0			@ no offset
	mov	r4, r0			@ r0 = table start
	add	r5, r0, r1		@ r1 = table size
	bl	__fixup_a_pv_table
	ldmfd	sp!, {r4 - r7, pc}
ENDPROC(fixup_pv_table)

	.data
	.align	2
	.globl	__pv_phys_pfn_offset
	.type	__pv_phys_pfn_offset, %object
__pv_phys_pfn_offset:
	.word	0
	.size	__pv_phys_pfn_offset, . -__pv_phys_pfn_offset

	.globl	__pv_offset
	.type	__pv_offset, %object
__pv_offset:
	.quad	0
	.size	__pv_offset, . -__pv_offset
#endif

#include "head-common.S"
