/*
 *  linux/arch/arm/kernel/head-common.S
 *
 *  Copyright (C) 1994-2002 Russell King
 *  Copyright (c) 2003 ARM Limited
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <asm/assembler.h>

#define ATAG_CORE 0x54410001
#define ATAG_CORE_SIZE ((2*4 + 3*4) >> 2)
#define ATAG_CORE_SIZE_EMPTY ((2*4) >> 2)

#ifdef CONFIG_CPU_BIG_ENDIAN
#define OF_DT_MAGIC 0xd00dfeed
#else
#define OF_DT_MAGIC 0xedfe0dd0 /* 0xd00dfeed in big-endian */
#endif

/*
 * Exception handling.  Something went wrong and we can't proceed.  We
 * ought to tell the user, but since we don't have any guarantee that
 * we're even running on the right architecture, we do virtually nothing.
 *
 * If CONFIG_DEBUG_LL is set we try to print out something about the error
 * and hope for the best (useful if bootloader fails to pass a proper
 * machine ID for example).
 */
	__HEAD

/* Determine validity of the r2 atags pointer.  The heuristic requires
 * that the pointer be aligned, in the first 16k of physical RAM and
 * that the ATAG_CORE marker is first and present.  If CONFIG_OF_FLATTREE
 * is selected, then it will also accept a dtb pointer.  Future revisions
 * of this function may be more lenient with the physical address and
 * may also be able to move the ATAGS block if necessary.
 *
 * Returns:
 *  r2 either valid atags pointer, valid dtb pointer, or zero
 *  r5, r6 corrupted
 */
/**
 * 检查BOOT传入的ATAGS是否合法，ATAGS被BOOT中的setup_start_tag函数设置。
 */
__vet_atags:
/**
 * 检查atags是否四字节对齐，不对齐的话直接返回错误
 */
	tst	r2, #0x3			@ aligned?
	bne	1f

/**
 * 读入其长度，如果其长度不等于ATAG_CORE_SIZE，表示存储的内容不是ATAG
 */
	ldr	r5, [r2, #0]
#ifdef CONFIG_OF_FLATTREE
	ldr	r6, =OF_DT_MAGIC		@ is it a DTB?
	cmp	r5, r6
	beq	2f
#endif
	cmp	r5, #ATAG_CORE_SIZE		@ is first tag ATAG_CORE?
	cmpne	r5, #ATAG_CORE_SIZE_EMPTY
	bne	1f
/**
 * 判断其标志是否是ATAG_CORE，第一个元素必须是此类型
 */
	ldr	r5, [r2, #4]
	ldr	r6, =ATAG_CORE
	cmp	r5, r6
	bne	1f

2:	ret	lr				@ atag/dtb pointer is ok

1:	mov	r2, #0
	ret	lr
ENDPROC(__vet_atags)

/*
 * The following fragment of code is executed with the MMU on in MMU mode,
 * and uses absolute addresses; this is not position independent.
 *
 *  r0  = cp#15 control register
 *  r1  = machine ID
 *  r2  = atags/dtb pointer
 *  r9  = processor ID
 */
	__INIT
/**
 * 从这里开始，代码运行在MMU地址空间中。以绝对地址的方式执行，不必用PIC方式执行了。
 * 为start_kernel准备C运行环境。
 */
__mmap_switched:

	mov	r7, r1
	mov	r8, r2
	mov	r10, r0

/**
 * 计算PC相对于__mmap_switched_data地址的差异，计算PC减去相对地址的结果。
 * 这里可以不用这种PIC的方式，但是这样做应该可以节约一条指令。
 * __mmap_switched_data 出定义了
	.long	__data_loc			@ r4
	.long	_sdata				@ r5
	.long	__bss_start			@ r6
	.long	_end				@ r7
	.long	processor_id			@ r4
	.long	__machine_arch_type		@ r5
	.long	__atags_pointer			@ r6
	.long	cr_alignment			@ r7
	.long	init_thread_union + THREAD_START_SP @ sp
	.size	__mmap_switched_data, . - __mmap_switched_data
 */
	adr	r4, __mmap_switched_data
	mov	fp, #0

#if defined(CONFIG_XIP_DEFLATED_DATA)
   ARM(	ldr	sp, [r4], #4 )
 THUMB(	ldr	sp, [r4] )
 THUMB(	add	r4, #4 )
	bl	__inflate_kernel_data		@ decompress .data to RAM
	teq	r0, #0
	bne	__error
#elif defined(CONFIG_XIP_KERNEL)
   ARM(	ldmia	r4!, {r0, r1, r2, sp} )
 THUMB(	ldmia	r4!, {r0, r1, r2, r3} )
 THUMB(	mov	sp, r3 )
	sub	r2, r2, r1
	bl	memcpy				@ copy .data to RAM
#endif

   ARM(	ldmia	r4!, {r0, r1, sp} )
 THUMB(	ldmia	r4!, {r0, r1, r3} )
 THUMB(	mov	sp, r3 )
	sub	r2, r1, r0
	mov	r1, #0
	bl	memset				@ clear .bss

	ldmia	r4, {r0, r1, r2, r3}
	str	r9, [r0]			@ Save processor ID
	str	r7, [r1]			@ Save machine type
	str	r8, [r2]			@ Save atags pointer
	cmp	r3, #0
	strne	r10, [r3]			@ Save control register values
	mov	lr, #0
/**
 * 跳转到C运行环境
__mmap_switched最后跳转到start_kernel开始进入C函数运行环境，
这时整个kernel image已经运行在虚拟地址之上，运行地址和链接地址保持了一致.
 */
	b	start_kernel
ENDPROC(__mmap_switched)

	.align	2
	.type	__mmap_switched_data, %object
__mmap_switched_data:
#ifdef CONFIG_XIP_KERNEL
#ifndef CONFIG_XIP_DEFLATED_DATA
	.long	_sdata				@ r0
	.long	__data_loc			@ r1
	.long	_edata_loc			@ r2
#endif
	.long	__bss_stop			@ sp (temporary stack in .bss)
#endif

	.long	__bss_start			@ r0
	.long	__bss_stop			@ r1
	.long	init_thread_union + THREAD_START_SP @ sp

	.long	processor_id			@ r0
	.long	__machine_arch_type		@ r1
	.long	__atags_pointer			@ r2
#ifdef CONFIG_CPU_CP15
	.long	cr_alignment			@ r3
#else
	.long	0				@ r3
#endif
/*
sp = init_thread_union + THREAD_START_SP(8K-8)，设置栈指针，为接下来的C函数运行做准备。
对于arm-linux来说，start_kernel之前全部都是汇编代码。
init_thread_union代表的是内核第一个进程，pid = 0，该进程是内核人为造出来的，而不是fork出来的.
*/
	.size	__mmap_switched_data, . - __mmap_switched_data

/*
 * This provides a C-API version of __lookup_processor_type
 */
ENTRY(lookup_processor_type)
	stmfd	sp!, {r4 - r6, r9, lr}
	mov	r9, r0
	bl	__lookup_processor_type
	mov	r0, r5
	ldmfd	sp!, {r4 - r6, r9, pc}
ENDPROC(lookup_processor_type)

	__FINIT
	.text

/*
 * Read processor ID register (CP#15, CR0), and look up in the linker-built
 * supported processor list.  Note that we can't use the absolute addresses
 * for the __proc_info lists since we aren't running with the MMU on
 * (and therefore, we are not in the correct address space).  We have to
 * calculate the offset.
 *
 *	r9 = cpuid
 * Returns:
 *	r3, r4, r6 corrupted
 *	r5 = proc_info pointer in physical address space
 *	r9 = cpuid (preserved)
 */
/*
Kernel 代码将所有CPU信息的定义都放到.proc.info.init段中，因此可以认为.proc.info.init段就是一个数组，
每个元素都定义了一个或一种CPU的信息。目前__lookup_processor_type使用该元素的前两个字段cpuid和mask来匹配当前CPUID，
如果满足 CPUID & mask == cpuid，则找到当前cpu的定义并返回。
*/
__lookup_processor_type:
/*
r3=(phys)__lookup_processor_type_data
adr是相对寻址，它的寻计算结果是将当前PC值加上__lookup_processor_type_data符号与PC的偏移量，
而PC是物理地址，因此r3的结果也是符号__lookup_processor_type_data的物理地址
*/
	adr	r3, __lookup_processor_type_data
/*
	@ r4=(virt). r5=(virt)__proc_info_begin r6=(virt)__proc_info_end
*/
/*
r5值为__proc_info_bein, r6值为__proc_ino_end，而r4值为.，
也即符号__lookup_processor_type_data的虚拟地址。
请注意，在链接期间，__proc_info_begin和__proc_info_end以及.均是虚执地址
*/
	ldmia	r3, {r4 - r6}
/*
	r3=(phys)r3-(virt)r4 (接下来两行把virt转换成phys的时候使用)
*/
/*
r3为__lookup_processor_type_data的物理地址，
r4为__lookup_processor_type_data的虚拟地址.
结果是r3为物理地址与虚拟地址的差值，即PHYS_OFFSET - PAGE_OFFSET
*/
	sub	r3, r3, r4			@ get offset between virt&phys
/*
	(phys)r5=(virt)r5+r3
*/
/*
将r5和r6转换为__proc_info_begin和__proc_info_end的物理地址
*/
	add	r5, r5, r3			@ convert virt addresses to
/*
(phys)r6=(virt)r6+r3
*/
	add	r6, r6, r3			@ physical address space
/*
	r3=value r4=mask (参考struct proc_info_list)
*/
/*
读取r5指向的__proc_info数组元素的CPUID和mask值分别保存到r3和r4寄存器
*/
1:	ldmia	r5, {r3, r4}			@ value, mask
/*
对processor id进行mask
*/
/*
将当前CPUID和mask相与，并与数组元素中的CPUID比较是否相同
若相同，则找到当前CPU的__proc_info定义，r5指向访元素并返回
*/
	and	r4, r4, r9			@ mask wanted bits
/*
	比较cpu_val
*/
	teq	r3, r4
/*
	 匹配成功即退出
*/
	beq	2f
/*
	r5往后偏移sizeof(struct proc_info_list)
*/
/*
r5指向下一个__proc_info元素
*/
	add	r5, r5, #PROC_INFO_SZ		@ sizeof(proc_info_list)
/*
	r6是存放proc_info_list的末尾
    是否遍历完所有__proc_info元素
*/
	cmp	r5, r6
/*
	小于__proc_info_end ,继续查找
*/
	blo	1b
/*
	如果没有匹配成功结果是unknown processor(r5=0)
    找不到则返回NULL
*/
	mov	r5, #0				@ unknown processor
/*
	lr中保存的地址是__enable_mmu
*/
2:	ret	lr
ENDPROC(__lookup_processor_type)

/*
 * Look in <asm/procinfo.h> for information about the __proc_info structure.
 */
	.align	2
	.type	__lookup_processor_type_data, %object
__lookup_processor_type_data:
/*r4*/
	.long	.
/*r5*/
	.long	__proc_info_begin
/*r6*/
	.long	__proc_info_end
	.size	__lookup_processor_type_data, . - __lookup_processor_type_data

__error_lpae:
#ifdef CONFIG_DEBUG_LL
	adr	r0, str_lpae
	bl 	printascii
	b	__error
str_lpae: .asciz "\nError: Kernel with LPAE support, but CPU does not support LPAE.\n"
#else
	b	__error
#endif
	.align
ENDPROC(__error_lpae)

__error_p:
#ifdef CONFIG_DEBUG_LL
	adr	r0, str_p1
	bl	printascii
	mov	r0, r9
	bl	printhex8
	adr	r0, str_p2
	bl	printascii
	b	__error
str_p1:	.asciz	"\nError: unrecognized/unsupported processor variant (0x"
str_p2:	.asciz	").\n"
	.align
#endif
ENDPROC(__error_p)

__error:
#ifdef CONFIG_ARCH_RPC
/*
 * Turn the screen red on a error - RiscPC only.
 */
	mov	r0, #0x02000000
	mov	r3, #0x11
	orr	r3, r3, r3, lsl #8
	orr	r3, r3, r3, lsl #16
	str	r3, [r0], #4
	str	r3, [r0], #4
	str	r3, [r0], #4
	str	r3, [r0], #4
#endif
1:	mov	r0, r0
	b	1b
ENDPROC(__error)
