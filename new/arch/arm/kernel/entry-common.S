/*
 *  linux/arch/arm/kernel/entry-common.S
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/assembler.h>
#include <asm/unistd.h>
#include <asm/ftrace.h>
#include <asm/unwind.h>
#include <asm/memory.h>
#ifdef CONFIG_AEABI
#include <asm/unistd-oabi.h>
#endif

	.equ	NR_syscalls, __NR_syscalls

#ifdef CONFIG_NEED_RET_TO_USER
#include <mach/entry-macro.S>
#else
	.macro  arch_ret_to_user, tmp1, tmp2
	.endm
#endif

#include "entry-header.S"

saved_psr	.req	r8
#if defined(CONFIG_TRACE_IRQFLAGS) || defined(CONFIG_CONTEXT_TRACKING)
saved_pc	.req	r9
#define TRACE(x...) x
#else
saved_pc	.req	lr
#define TRACE(x...)
#endif

	.section .entry.text,"ax",%progbits
	.align	5
#if !(IS_ENABLED(CONFIG_TRACE_IRQFLAGS) || IS_ENABLED(CONFIG_CONTEXT_TRACKING) || \
	IS_ENABLED(CONFIG_DEBUG_RSEQ))
/*
 * This is the fast syscall return path.  We do as little as possible here,
 * such as avoiding writing r0 to the stack.  We only use this path if we
 * have tracing, context tracking and rseq debug disabled - the overheads
 * from those features make this path too inefficient.
 */
ret_fast_syscall:
__ret_fast_syscall:
 UNWIND(.fnstart	)
 UNWIND(.cantunwind	)
	disable_irq_notrace			@ disable interrupts
	ldr	r2, [tsk, #TI_ADDR_LIMIT]
	cmp	r2, #TASK_SIZE
	blne	addr_limit_check_failed
	ldr	r1, [tsk, #TI_FLAGS]		@ re-check for syscall tracing
	tst	r1, #_TIF_SYSCALL_WORK | _TIF_WORK_MASK
	bne	fast_work_pending


	/* perform architecture specific actions before user return */
	arch_ret_to_user r1, lr
/*
__switch_to函数中已经恢复过一次当前线程的上下文了(struct cpu_context_save),
接下来又调用restore_user_regs来恢复模式上下文(struct pt_regs).
*/
	restore_user_regs fast = 1, offset = S_OFF
 UNWIND(.fnend		)
ENDPROC(ret_fast_syscall)

	/* Ok, we need to do extra processing, enter the slow path. */
fast_work_pending:
	str	r0, [sp, #S_R0+S_OFF]!		@ returned r0
	/* fall through to work_pending */
#else
/*
 * The "replacement" ret_fast_syscall for when tracing, context tracking,
 * or rseq debug is enabled.  As we will need to call out to some C functions,
 * we save r0 first to avoid needing to save registers around each C function
 * call.
 */
ret_fast_syscall:
__ret_fast_syscall:
 UNWIND(.fnstart	)
 UNWIND(.cantunwind	)
	str	r0, [sp, #S_R0 + S_OFF]!	@ save returned r0
#if IS_ENABLED(CONFIG_DEBUG_RSEQ)
	/* do_rseq_syscall needs interrupts enabled. */
	mov	r0, sp				@ 'regs'
	bl	do_rseq_syscall
#endif
	disable_irq_notrace			@ disable interrupts
	ldr	r2, [tsk, #TI_ADDR_LIMIT]
	cmp	r2, #TASK_SIZE
	blne	addr_limit_check_failed
	ldr	r1, [tsk, #TI_FLAGS]		@ re-check for syscall tracing
	tst	r1, #_TIF_SYSCALL_WORK | _TIF_WORK_MASK
	beq	no_work_pending
 UNWIND(.fnend		)
ENDPROC(ret_fast_syscall)

	/* Slower path - fall through to work_pending */
#endif

	tst	r1, #_TIF_SYSCALL_WORK
	bne	__sys_trace_return_nosave
slow_work_pending:
	mov	r0, sp				@ 'regs'
	mov	r2, why				@ 'syscall'
	bl	do_work_pending
	cmp	r0, #0
	beq	no_work_pending
	movlt	scno, #(__NR_restart_syscall - __NR_SYSCALL_BASE)
	ldmia	sp, {r0 - r6}			@ have to reload r0 - r6
	b	local_restart			@ ... and off we go
ENDPROC(ret_fast_syscall)

/*
 * "slow" syscall return path.  "why" tells us if this was a real syscall.
 * IRQs may be enabled here, so always disable them.  Note that we use the
 * "notrace" version to avoid calling into the tracing code unnecessarily.
 * do_work_pending() will update this state if necessary.
 */
ENTRY(ret_to_user)
ret_slow_syscall:
#if IS_ENABLED(CONFIG_DEBUG_RSEQ)
	/* do_rseq_syscall needs interrupts enabled. */
	enable_irq_notrace			@ enable interrupts
	mov	r0, sp				@ 'regs'
	bl	do_rseq_syscall
#endif
/**
 * 在中断处理流程中，可能打开了中断。此处关闭
 */
	disable_irq_notrace			@ disable interrupts
ENTRY(ret_to_user_from_irq)
	ldr	r2, [tsk, #TI_ADDR_LIMIT]
	cmp	r2, #TASK_SIZE
	blne	addr_limit_check_failed
/**
 * 处理抢占调度、信号等等
*/
	ldr	r1, [tsk, #TI_FLAGS]
	tst	r1, #_TIF_WORK_MASK
	bne	slow_work_pending
no_work_pending:
	asm_trace_hardirqs_on save = 0

	/* perform architecture specific actions before user return */
/**
 * 只有特殊架构才会在此处理
 */
	arch_ret_to_user r1, lr
	ct_user_enter save = 0

/**
 * 恢复寄存器。
 */
	restore_user_regs fast = 0, offset = 0
ENDPROC(ret_to_user_from_irq)
ENDPROC(ret_to_user)

/*
 * This is how we return from a fork.
 */
ENTRY(ret_from_fork)
	bl	schedule_tail
/*
比较r5寄存器是否为空，如果不为空则表示为内核线程，则执行如下指令，        
否则执行标号1处之后的指令调用ret_slow_syscall返回到用户态执行.
*/
	cmp	r5, #0
	movne	r0, r4
	badrne	lr, 1f
/*
retne r5：跳转到由r5目标寄存器指定的地址处。        
r5寄存器在copy_thread函数中指定为内核线程的入口地址
*/
	retne	r5
1:	get_thread_info tsk
	b	ret_slow_syscall
ENDPROC(ret_from_fork)

/*=============================================================================
 * SWI handler
 *-----------------------------------------------------------------------------
 */

	.align	5
ENTRY(vector_swi)
#ifdef CONFIG_CPU_V7M
	v7m_exception_entry
#else
/*
这个栈帧大小正好是struct pt_regs的大小. struct pt_regs 中保存的是　
线程用户态的寄存器上下文(模式上下文).
这里的sp已经是sp_svc了，也就是当前线程的内核栈了
*/
/*

	注意swi指令让cpu进入svc模式,此时sp的值是arm在svc模式下的sp的值，而不是user模式下的sp.
	对于用户进程来说此时的sp也是内核态下的sp
	那么该sp是在何时指定的呢?
1)  在该用户态进程创建过程中sys_clone->do_fork->copy_thread()函数
  实现了thread->cpu_context.sp = (unsigned long)childregs;
  而这个childregs是在上一步sys_clone->do_fork->dup_task_struct中申请的
 2) 当当前进程真正切换到本用户进程时 这个thread->cpu_context.sp 
	在__switch_to中被加载到arm在svc模式下的sp寄存器
	sp的具体指向参见task_pt_regs中定义:(栈顶-8byte)就是sp的值
	栈顶即是:current->stack+THREAD_SIZE;用户每次陷入内核，内核态的栈都指向此;
举例:
[linuxrc]进程:
1 current->stack=0x8053a000(8k字节对齐)
2 当linuxrc进入内核态时sp=0x8053a000+0x2000-8=0x8053BFF8
3 也就是说进程在内核态下的栈是从current->stack+THREAD_SIZE-8
开始向下增长的，并且其中current->stack+THREAD_SIZE-8-72字节
是一个栈帧的长度
普通c函数中使用的栈一般是从current->stack+THREAD_SIZE-8-72
开始向下增长
sp=sp-72 
*/
	sub	sp, sp, #PT_REGS_SIZE
/*
将r0-r12寄存器压入内核栈中
[sp] = r0;[sp+4]=r1 ...
*/
	stmia	sp, {r0 - r12}			@ Calling r0 - r12
/*
	[r8] = rt_regs->pc 
	r8中不是保存的pc指针，而是保存pc指针在栈帧中地址。
*/
 ARM(	add	r8, sp, #S_PC		)
/* 
	将用户态的sp、lr压入内核栈中
	{sp, lr}^中的"^"表示这里的sp和lr是用户态的寄存器
	[r8-4]=lr
	[r8-8]=sp
*/
 ARM(	stmdb	r8, {sp, lr}^		)	@ Calling sp, lr
 THUMB(	mov	r8, sp			)
 THUMB(	store_user_sp_lr r8, r10, S_SP	)	@ calling sp, lr
/*
	sprs_svc中保存的是调用swi指令前的cpsr值，这里将它保存在寄存器r8中
*/
	mrs	saved_psr, spsr			@ called from non-FIQ mode, so ok.
 TRACE(	mov	saved_pc, lr		)
/*
	lr中的值是swi指令的下一行，也就是系统调用的用户态返回地址 
 */
	str	saved_pc, [sp, #S_PC]		@ Save calling PC
/*
	将调用swi指令前的cpsr值压入内核栈中，此时进程还在用户态空间
*/
	str	saved_psr, [sp, #S_PSR]		@ Save CPSR
/*
将r0压入内核栈中
这里r0会存两个位置，一个代表第一个参数，另一个代表返回值
*/
	str	r0, [sp, #S_OLD_R0]		@ Save OLD_R0
#endif
/*
	清除frame pointer，在debugger做栈的回溯的时候，当fp等于0的时候也就意味着到了最外层函数。
	对于kernel而言，来到这里，函数的调用跟踪就结束了，不可能一直回溯到用户空间的函数调用。
*/
	zero_fp
	alignment_trap r10, ip, __cr_alignment
	asm_trace_hardirqs_on save=0
/*
	硬件会关闭irq，通过enable_irq开启本cpu的中断处理
*/
	enable_irq_notrace
	ct_user_exit save=0

	/*
	 * Get the system call number.
	 */

#if defined(CONFIG_OABI_COMPAT)

	/*
	 * If we have CONFIG_OABI_COMPAT then we need to look at the swi
	 * value to determine if it is an EABI or an old ABI call.
	 */
#ifdef CONFIG_ARM_THUMB
	tst	saved_psr, #PSR_T_BIT
	movne	r10, #0				@ no thumb OABI emulation
 USER(	ldreq	r10, [saved_pc, #-4]	)	@ get SWI instruction
#else
 USER(	ldr	r10, [saved_pc, #-4]	)	@ get SWI instruction
#endif
 ARM_BE8(rev	r10, r10)			@ little endian instruction

#elif defined(CONFIG_AEABI)

	/*
	 * Pure EABI user space always put syscall number into scno (r7).
	 */
#elif defined(CONFIG_ARM_THUMB)
	/* Legacy ABI only, possibly thumb mode. */
	tst	saved_psr, #PSR_T_BIT		@ this is SPSR from save_user_regs
/*
	glibc已经将系统调用号保存到r7寄存器了
	取出系统调用号保存到 scno 中
*/
	addne	scno, r7, #__NR_SYSCALL_BASE	@ put OS number in
 USER(	ldreq	scno, [saved_pc, #-4]	)

#else
	/* Legacy ABI only. */
 USER(	ldr	scno, [saved_pc, #-4]	)	@ get SWI instruction
#endif

	/* saved_psr and saved_pc are now dead */

	uaccess_disable tbl
/*
	将 sys_call_table 的地址放入tbl
*/
	adr	tbl, sys_call_table		@ load syscall table pointer

#if defined(CONFIG_OABI_COMPAT)
	/*
	 * If the swi argument is zero, this is an EABI call and we do nothing.
	 *
	 * If this is an old ABI call, get the syscall number into scno and
	 * get the old ABI syscall table address.
	 */
	bics	r10, r10, #0xff000000
	eorne	scno, r10, #__NR_OABI_SYSCALL_BASE
	ldrne	tbl, =sys_oabi_call_table
#elif !defined(CONFIG_AEABI)
	bic	scno, scno, #0xff000000		@ mask off SWI op-code
	eor	scno, scno, #__NR_SYSCALL_BASE	@ check OS number
#endif
	get_thread_info tsk
	/*
	 * Reload the registers that may have been corrupted on entry to
	 * the syscall assembly (by tracing or context tracking.)
	 */
 TRACE(	ldmia	sp, {r0 - r3}		)
/*
我们这里需要模拟一个c函数调用，因此需要在栈上压入系统调用可能存在的第五和第六个参数
（有些系统调用超过4个参数，他们使用r0～r5在swi接口上传递参数）。
*/
local_restart:
/* 
	r10= thread_info->flags 
*/
	ldr	r10, [tsk, #TI_FLAGS]		@ check for syscall tracing
	
/*
	r4 r5入栈
*/
	stmdb	sp!, {r4, r5}			@ push fifth and sixth args

/*
	判断是否在跟踪系统调用
*/
	tst	r10, #_TIF_SYSCALL_WORK		@ are we tracing syscalls?
	bne	__sys_trace

/*
	tbl 中保存 sys_call_table 基地址。
	scno中保存系统调用号。
	此处真正跳转到系统调用中执行。
	执行完后返回到 __ret_fast_syscall
*/
	invoke_syscall tbl, scno, r10, __ret_fast_syscall

	add	r1, sp, #S_OFF
2:	cmp	scno, #(__ARM_NR_BASE - __NR_SYSCALL_BASE)
	eor	r0, scno, #__NR_SYSCALL_BASE	@ put OS number back
	bcs	arm_syscall
	mov	why, #0				@ no longer a real syscall
	b	sys_ni_syscall			@ not private func

#if defined(CONFIG_OABI_COMPAT) || !defined(CONFIG_AEABI)
	/*
	 * We failed to handle a fault trying to access the page
	 * containing the swi instruction, but we're not really in a
	 * position to return -EFAULT. Instead, return back to the
	 * instruction and re-enter the user fault handling path trying
	 * to page it in. This will likely result in sending SEGV to the
	 * current task.
	 */
9001:
	sub	lr, saved_pc, #4
	str	lr, [sp, #S_PC]
	get_thread_info tsk
	b	ret_fast_syscall
#endif
ENDPROC(vector_swi)

	/*
	 * This is the really slow path.  We're going to be doing
	 * context switches, and waiting for our parent to respond.
	 */
/*
	停止traced process & 通知tracer
*/
__sys_trace:
	mov	r1, scno
	add	r0, sp, #S_OFF
	bl	syscall_trace_enter
	mov	scno, r0
	invoke_syscall tbl, scno, r10, __sys_trace_return, reload=1
	cmp	scno, #-1			@ skip the syscall?
	bne	2b
	add	sp, sp, #S_OFF			@ restore stack

__sys_trace_return_nosave:
	enable_irq_notrace
	mov	r0, sp
	bl	syscall_trace_exit
	b	ret_slow_syscall

__sys_trace_return:
	str	r0, [sp, #S_R0 + S_OFF]!	@ save returned r0
	mov	r0, sp
	bl	syscall_trace_exit
	b	ret_slow_syscall

	.align	5
#ifdef CONFIG_ALIGNMENT_TRAP
	.type	__cr_alignment, #object
__cr_alignment:
	.word	cr_alignment
#endif
	.ltorg

	.macro	syscall_table_start, sym
	.equ	__sys_nr, 0
	.type	\sym, #object
ENTRY(\sym)
	.endm

	.macro	syscall, nr, func
	.ifgt	__sys_nr - \nr
	.error	"Duplicated/unorded system call entry"
	.endif
	.rept	\nr - __sys_nr
	.long	sys_ni_syscall
	.endr
	.long	\func
	.equ	__sys_nr, \nr + 1
	.endm

	.macro	syscall_table_end, sym
	.ifgt	__sys_nr - __NR_syscalls
	.error	"System call table too big"
	.endif
	.rept	__NR_syscalls - __sys_nr
	.long	sys_ni_syscall
	.endr
	.size	\sym, . - \sym
	.endm

#define NATIVE(nr, func) syscall nr, func

/*
 * This is the syscall table declaration for native ABI syscalls.
 * With EABI a couple syscalls are obsolete and defined as sys_ni_syscall.
 */
/*
	sys_call_table 跳转表存储的是一系列的函数指针,这些指针就是系统调用函数的指针.
*/
	syscall_table_start sys_call_table
#define COMPAT(nr, native, compat) syscall nr, native
#ifdef CONFIG_AEABI
#include <calls-eabi.S>
#else
#include <calls-oabi.S>
#endif
#undef COMPAT
	syscall_table_end sys_call_table

/*============================================================================
 * Special system call wrappers
 */
@ r0 = syscall number
@ r8 = syscall table
sys_syscall:
		bic	scno, r0, #__NR_OABI_SYSCALL_BASE
		cmp	scno, #__NR_syscall - __NR_SYSCALL_BASE
		cmpne	scno, #NR_syscalls	@ check range
#ifdef CONFIG_CPU_SPECTRE
		movhs	scno, #0
		csdb
#endif
		stmloia	sp, {r5, r6}		@ shuffle args
		movlo	r0, r1
		movlo	r1, r2
		movlo	r2, r3
		movlo	r3, r4
/*
如果参数OK的话，将直接把控制权交给对应的sys_xxx函数。
需要注意的是返回地址的设定，我们无法使用bl这样的汇编指令，
因此只能是手动设定lr寄存器了（badr    lr, ret_fast_syscall ）		
*/
		ldrlo	pc, [tbl, scno, lsl #2]
		b	sys_ni_syscall
ENDPROC(sys_syscall)

sys_sigreturn_wrapper:
		add	r0, sp, #S_OFF
		mov	why, #0		@ prevent syscall restart handling
		b	sys_sigreturn
ENDPROC(sys_sigreturn_wrapper)

sys_rt_sigreturn_wrapper:
		add	r0, sp, #S_OFF
		mov	why, #0		@ prevent syscall restart handling
		b	sys_rt_sigreturn
ENDPROC(sys_rt_sigreturn_wrapper)

sys_statfs64_wrapper:
		teq	r1, #88
		moveq	r1, #84
		b	sys_statfs64
ENDPROC(sys_statfs64_wrapper)

sys_fstatfs64_wrapper:
		teq	r1, #88
		moveq	r1, #84
		b	sys_fstatfs64
ENDPROC(sys_fstatfs64_wrapper)

/*
 * Note: off_4k (r5) is always units of 4K.  If we can't do the requested
 * offset, we return EINVAL.
 */
sys_mmap2:
		str	r5, [sp, #4]
		b	sys_mmap_pgoff
ENDPROC(sys_mmap2)

#ifdef CONFIG_OABI_COMPAT

/*
 * These are syscalls with argument register differences
 */

sys_oabi_pread64:
		stmia	sp, {r3, r4}
		b	sys_pread64
ENDPROC(sys_oabi_pread64)

sys_oabi_pwrite64:
		stmia	sp, {r3, r4}
		b	sys_pwrite64
ENDPROC(sys_oabi_pwrite64)

sys_oabi_truncate64:
		mov	r3, r2
		mov	r2, r1
		b	sys_truncate64
ENDPROC(sys_oabi_truncate64)

sys_oabi_ftruncate64:
		mov	r3, r2
		mov	r2, r1
		b	sys_ftruncate64
ENDPROC(sys_oabi_ftruncate64)

sys_oabi_readahead:
		str	r3, [sp]
		mov	r3, r2
		mov	r2, r1
		b	sys_readahead
ENDPROC(sys_oabi_readahead)

/*
 * Let's declare a second syscall table for old ABI binaries
 * using the compatibility syscall entries.
 */
	syscall_table_start sys_oabi_call_table
#define COMPAT(nr, native, compat) syscall nr, compat
#include <calls-oabi.S>
	syscall_table_end sys_oabi_call_table

#endif

