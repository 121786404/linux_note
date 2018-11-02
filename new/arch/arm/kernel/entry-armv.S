/*
 *  linux/arch/arm/kernel/entry-armv.S
 *
 *  Copyright (C) 1996,1997,1998 Russell King.
 *  ARM700 fix by Matthew Godbolt (linux-user@willothewisp.demon.co.uk)
 *  nommu support by Hyok S. Choi (hyok.choi@samsung.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Low-level vector interface routines
 *
 *  Note:  there is a StrongARM bug in the STMIA rn, {regs}^ instruction
 *  that causes it to save wrong values...  Be aware!
 */

#include <linux/init.h>

#include <asm/assembler.h>
#include <asm/memory.h>
#include <asm/glue-df.h>
#include <asm/glue-pf.h>
#include <asm/vfpmacros.h>
#ifndef CONFIG_GENERIC_IRQ_MULTI_HANDLER
#include <mach/entry-macro.S>
#endif
#include <asm/thread_notify.h>
#include <asm/unwind.h>
#include <asm/unistd.h>
#include <asm/tls.h>
#include <asm/system_info.h>

#include "entry-header.S"
#include <asm/entry-macro-multi.S>
#include <asm/probes.h>

/*
 * Interrupt handling.
 */
/**
 * 调用C语言中的中断处理程序
 */
	.macro	irq_handler
#ifdef CONFIG_GENERIC_IRQ_MULTI_HANDLER
/*
    CONFIG_MULTI_IRQ_HANDLER:允许每个机器在运行时制定irq处理程序
    对于ARM Soc来说，每一款Soc的芯片设计都不一样，采用的中断控制器以及中断控制器的连接方式也不同，
    有的Soc可能采用GIC-V2的中断控制器，有的则可能采用GIC-V3的中断控制器，也有的厂商自己设计中断控制器
    __gic_init_bases中 set_handle_irq(gic_handle_irq) ---> handle_arch_irq = gic_handle_irq

*/
	ldr	r1, =handle_arch_irq
	mov	r0, sp
	badr	lr, 9997f
	ldr	pc, [r1]
#else
	arch_irq_handler_default
#endif
9997:
	.endm

	.macro	pabt_helper
	@ PABORT handler takes pt_regs in r2, fault address in r4 and psr in r5
#ifdef MULTI_PABORT
	ldr	ip, .LCprocfns
	mov	lr, pc
	ldr	pc, [ip, #PROCESSOR_PABT_FUNC]
#else
	bl	CPU_PABORT_HANDLER
#endif
	.endm

	.macro	dabt_helper

	@
	@ Call the processor-specific abort handler:
	@
	@  r2 - pt_regs
	@  r4 - aborted context pc
	@  r5 - aborted context psr
	@
	@ The abort handler must return the aborted address in r0, and
	@ the fault status register in r1.  r9 must be preserved.
	@
#ifdef MULTI_DABORT
	ldr	ip, .LCprocfns
	mov	lr, pc
	ldr	pc, [ip, #PROCESSOR_DABT_FUNC]
#else
	bl	CPU_DABORT_HANDLER
#endif
	.endm

	.section	.entry.text,"ax",%progbits

/*
 * Invalid mode handlers
 */
	.macro	inv_entry, reason
	sub	sp, sp, #PT_REGS_SIZE
 ARM(	stmib	sp, {r1 - lr}		)
 THUMB(	stmia	sp, {r0 - r12}		)
 THUMB(	str	sp, [sp, #S_SP]		)
 THUMB(	str	lr, [sp, #S_LR]		)
	mov	r1, #\reason
	.endm

__pabt_invalid:
	inv_entry BAD_PREFETCH
	b	common_invalid
ENDPROC(__pabt_invalid)

__dabt_invalid:
	inv_entry BAD_DATA
	b	common_invalid
ENDPROC(__dabt_invalid)

__irq_invalid:
	inv_entry BAD_IRQ
	b	common_invalid
ENDPROC(__irq_invalid)

__und_invalid:
	inv_entry BAD_UNDEFINSTR

	@
	@ XXX fall through to common_invalid
	@

@
@ common_invalid - generic code for failed exception (re-entrant version of handlers)
@
common_invalid:
	zero_fp

	ldmia	r0, {r4 - r6}
	add	r0, sp, #S_PC		@ here for interlock avoidance
	mov	r7, #-1			@  ""   ""    ""        ""
	str	r4, [sp]		@ save preserved r0
	stmia	r0, {r5 - r7}		@ lr_<exception>,
					@ cpsr_<exception>, "old_r0"

	mov	r0, sp
	b	bad_mode
ENDPROC(__und_invalid)

/*
 * SVC mode handlers
 */

#if defined(CONFIG_AEABI) && (__LINUX_ARM_ARCH__ >= 5)
#define SPFIX(code...) code
#else
#define SPFIX(code...)
#endif

	.macro	svc_entry, stack_hole=0, trace=1, uaccess=1
 UNWIND(.fnstart		)
 UNWIND(.save {r0 - pc}		)
/*
 sp减去SVC_REGS_SIZE   - 4，指向 ARM_r1
*/
	sub	sp, sp, #(SVC_REGS_SIZE + \stack_hole - 4)
#ifdef CONFIG_THUMB2_KERNEL
 SPFIX(	str	r0, [sp]	)	@ temporarily saved
 SPFIX(	mov	r0, sp		)
 SPFIX(	tst	r0, #4		)	@ test original stack alignment
 SPFIX(	ldr	r0, [sp]	)	@ restored
#else
 SPFIX(	tst	sp, #4		)
#endif
 SPFIX(	subeq	sp, sp, #4	)
/*
进入__irq_svc时已经变成SVC模式，当前的栈时当前进程（发生中断的进程）的内核栈
*/
	stmia	sp, {r1 - r12}
/*
    r0还保存这irq模式的栈指针，irq模式的栈空间分别保存这r0、lr_irq和cpsr_irq的内容
    通过ldmia把irq模式的栈空间复制到SVC模式的r3,r4,r5寄存器中
*/
	ldmia	r0, {r3 - r5}
/*
    r7指向ARM_sp地址处	
*/
	add	r7, sp, #S_SP - 4	@ here for interlock avoidance
/*
    r6寄存器赋值-1
*/
	mov	r6, #-1			@  ""  ""      ""       ""
/*
    将CPSR_svc的内容赋值到r2
*/
	add	r2, sp, #(SVC_REGS_SIZE + \stack_hole - 4)
 SPFIX(	addeq	r2, r2, #4	)
/*
    刚才已把IRQ模式的r0内容复制到r3，现在重新复制到SVC模式的r0
*/
	str	r3, [sp, #-4]!		@ save the "real" r0 copied
					@ from the exception stack
/*
    把SVC模式的lr_svc复制到r3
*/
	mov	r3, lr

	@
	@ We are now ready to fill in the remaining blanks on the stack:
	@
	@  r2 - sp_svc
	@  r3 - lr_svc
	@  r4 - lr_<exception>, already fixed up for correct return/restart
	@  r5 - spsr_<exception>
	@  r6 - orig_r0 (see pt_regs definition in ptrace.h)
	@
/*
	这时r2=SPSR_svc,r3=lr_svc,r4=LR_irq,r5=CPSR_irq,r6=-1
	通过stmia将这些寄存器保存在栈中的ARM_sp,ARM_lr,ARM_pc,ARM_cpsr,ARM_ORIG_r0中
	为什么ARM_ORIG_r0要赋值位-1呢？
	这是ARM处理器在传递参数时的约定。ARM处理器通过通用寄存器来传递参数。
	系统调用和中断都属于处理器异常范畴，系统调用通常通过r0-r3来传递参数，返回值放入r0，可能会把r0传递
	的参数覆盖，因此ARM的寄存器中定义了两份r0，ARM_ORIG_r0 在系统调用时传递系统调用号，在返回时用做返回值。
	中断处理不需要使用 ARM_ORIG_r0，所以赋值-1表示非系统调用
*/
	stmia	r7, {r2 - r6}

	get_thread_info tsk
	ldr	r0, [tsk, #TI_ADDR_LIMIT]
	mov	r1, #TASK_SIZE
	str	r1, [tsk, #TI_ADDR_LIMIT]
	str	r0, [sp, #SVC_ADDR_LIMIT]

	uaccess_save r0
	.if \uaccess
	uaccess_disable r0
	.endif

	.if \trace
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_off
#endif
	.endif
	.endm

	.align	5
__dabt_svc:
	svc_entry uaccess=0
	mov	r2, sp
	dabt_helper
 THUMB(	ldr	r5, [sp, #S_PSR]	)	@ potentially updated CPSR
	svc_exit r5				@ return from exception
 UNWIND(.fnend		)
ENDPROC(__dabt_svc)

	.align	5
/**
 * svc模式下的中断入口
 */
__irq_svc:
/**
 * 将中断现场保存在内核栈中
 */
	svc_entry
/**
 * 执行中断处理
 */
	irq_handler
/*
    如果开启内核抢占，中断返回时回去检查是否可以抢占发生中断时的进程
*/
#ifdef CONFIG_PREEMPT
/**
 * 从r9中取当前线程的 threadinfo
 */

/**
 * 读抢占计数
 */
	ldr	r8, [tsk, #TI_PREEMPT]		@ get preempt count
	ldr	r0, [tsk, #TI_FLAGS]		@ get flags
/*
	
*/
	teq	r8, #0				@ if preempt count != 0
	movne	r0, #0				@ force flags to 0
/**
 * 如果需要抢占调度，则进行抢占。
 */
	tst	r0, #_TIF_NEED_RESCHED
	blne	svc_preempt
#endif

/**
 * 这里的恢复过程有点不一样，返回到上一层异常。
 */
	svc_exit r5, irq = 1			@ return from exception
 UNWIND(.fnend		)
ENDPROC(__irq_svc)

	.ltorg

#ifdef CONFIG_PREEMPT
svc_preempt:
	mov	r8, lr
1:	bl	preempt_schedule_irq		@ irq en/disable is done inside
	ldr	r0, [tsk, #TI_FLAGS]		@ get new tasks TI_FLAGS
	tst	r0, #_TIF_NEED_RESCHED
	reteq	r8				@ go again
	b	1b
#endif

__und_fault:
	@ Correct the PC such that it is pointing at the instruction
	@ which caused the fault.  If the faulting instruction was ARM
	@ the PC will be pointing at the next instruction, and have to
	@ subtract 4.  Otherwise, it is Thumb, and the PC will be
	@ pointing at the second half of the Thumb instruction.  We
	@ have to subtract 2.
	ldr	r2, [r0, #S_PC]
	sub	r2, r2, r1
	str	r2, [r0, #S_PC]
	b	do_undefinstr
ENDPROC(__und_fault)

	.align	5
__und_svc:
#ifdef CONFIG_KPROBES
	@ If a kprobe is about to simulate a "stmdb sp..." instruction,
	@ it obviously needs free stack space which then will belong to
	@ the saved context.
	svc_entry MAX_STACK_SIZE
#else
	svc_entry
#endif
	@
	@ call emulation code, which returns using r9 if it has emulated
	@ the instruction, or the more conventional lr if we are to treat
	@ this as a real undefined instruction
	@
	@  r0 - instruction
	@
#ifndef CONFIG_THUMB2_KERNEL
	ldr	r0, [r4, #-4]
#else
	mov	r1, #2
	ldrh	r0, [r4, #-2]			@ Thumb instruction at LR - 2
	cmp	r0, #0xe800			@ 32-bit instruction if xx >= 0
	blo	__und_svc_fault
	ldrh	r9, [r4]			@ bottom 16 bits
	add	r4, r4, #2
	str	r4, [sp, #S_PC]
	orr	r0, r9, r0, lsl #16
#endif
	badr	r9, __und_svc_finish
	mov	r2, r4
	bl	call_fpe

	mov	r1, #4				@ PC correction to apply
__und_svc_fault:
	mov	r0, sp				@ struct pt_regs *regs
	bl	__und_fault

__und_svc_finish:
	get_thread_info tsk
	ldr	r5, [sp, #S_PSR]		@ Get SVC cpsr
	svc_exit r5				@ return from exception
 UNWIND(.fnend		)
ENDPROC(__und_svc)

	.align	5
__pabt_svc:
	svc_entry
	mov	r2, sp				@ regs
	pabt_helper
	svc_exit r5				@ return from exception
 UNWIND(.fnend		)
ENDPROC(__pabt_svc)

	.align	5
__fiq_svc:
	svc_entry trace=0
	mov	r0, sp				@ struct pt_regs *regs
	bl	handle_fiq_as_nmi
	svc_exit_via_fiq
 UNWIND(.fnend		)
ENDPROC(__fiq_svc)

	.align	5
.LCcralign:
	.word	cr_alignment
#ifdef MULTI_DABORT
.LCprocfns:
	.word	processor
#endif
.LCfp:
	.word	fp_enter

/*
 * Abort mode handlers
 */

@
@ Taking a FIQ in abort mode is similar to taking a FIQ in SVC mode
@ and reuses the same macros. However in abort mode we must also
@ save/restore lr_abt and spsr_abt to make nested aborts safe.
@
	.align 5
__fiq_abt:
	svc_entry trace=0

 ARM(	msr	cpsr_c, #ABT_MODE | PSR_I_BIT | PSR_F_BIT )
 THUMB( mov	r0, #ABT_MODE | PSR_I_BIT | PSR_F_BIT )
 THUMB( msr	cpsr_c, r0 )
	mov	r1, lr		@ Save lr_abt
	mrs	r2, spsr	@ Save spsr_abt, abort is now safe
 ARM(	msr	cpsr_c, #SVC_MODE | PSR_I_BIT | PSR_F_BIT )
 THUMB( mov	r0, #SVC_MODE | PSR_I_BIT | PSR_F_BIT )
 THUMB( msr	cpsr_c, r0 )
	stmfd	sp!, {r1 - r2}

	add	r0, sp, #8			@ struct pt_regs *regs
	bl	handle_fiq_as_nmi

	ldmfd	sp!, {r1 - r2}
 ARM(	msr	cpsr_c, #ABT_MODE | PSR_I_BIT | PSR_F_BIT )
 THUMB( mov	r0, #ABT_MODE | PSR_I_BIT | PSR_F_BIT )
 THUMB( msr	cpsr_c, r0 )
	mov	lr, r1		@ Restore lr_abt, abort is unsafe
	msr	spsr_cxsf, r2	@ Restore spsr_abt
 ARM(	msr	cpsr_c, #SVC_MODE | PSR_I_BIT | PSR_F_BIT )
 THUMB( mov	r0, #SVC_MODE | PSR_I_BIT | PSR_F_BIT )
 THUMB( msr	cpsr_c, r0 )

	svc_exit_via_fiq
 UNWIND(.fnend		)
ENDPROC(__fiq_abt)

/*
 * User mode handlers
 *
 * EABI note: sp_svc is always 64-bit aligned here, so should PT_REGS_SIZE
 */

#if defined(CONFIG_AEABI) && (__LINUX_ARM_ARCH__ >= 5) && (PT_REGS_SIZE & 7)
#error "sizeof(struct pt_regs) must be a multiple of 8"
#endif

	.macro	usr_entry, trace=1, uaccess=1
 UNWIND(.fnstart	)
 UNWIND(.cantunwind	)	@ don't unwind the user space
	sub	sp, sp, #PT_REGS_SIZE
 ARM(	stmib	sp, {r1 - r12}	)
 THUMB(	stmia	sp, {r0 - r12}	)

 ATRAP(	mrc	p15, 0, r7, c1, c0, 0)
 ATRAP(	ldr	r8, .LCcralign)

	ldmia	r0, {r3 - r5}
	add	r0, sp, #S_PC		@ here for interlock avoidance
	mov	r6, #-1			@  ""  ""     ""        ""

	str	r3, [sp]		@ save the "real" r0 copied
					@ from the exception stack

 ATRAP(	ldr	r8, [r8, #0])

	@
	@ We are now ready to fill in the remaining blanks on the stack:
	@
	@  r4 - lr_<exception>, already fixed up for correct return/restart
	@  r5 - spsr_<exception>
	@  r6 - orig_r0 (see pt_regs definition in ptrace.h)
	@
	@ Also, separately save sp_usr and lr_usr
	@
	stmia	r0, {r4 - r6}
 ARM(	stmdb	r0, {sp, lr}^			)
 THUMB(	store_user_sp_lr r0, r1, S_SP - S_PC	)

	.if \uaccess
	uaccess_disable ip
	.endif

	@ Enable the alignment trap while in kernel mode
 ATRAP(	teq	r8, r7)
 ATRAP( mcrne	p15, 0, r8, c1, c0, 0)

	@
	@ Clear FP to mark the first stack frame
	@
	zero_fp

	.if	\trace
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_off
#endif
	ct_user_exit save = 0
	.endif
	.endm

	.macro	kuser_cmpxchg_check
#if !defined(CONFIG_CPU_32v6K) && defined(CONFIG_KUSER_HELPERS)
#ifndef CONFIG_MMU
#warning "NPTL on non MMU needs fixing"
#else
	@ Make sure our user space atomic helper is restarted
	@ if it was interrupted in a critical region.  Here we
	@ perform a quick test inline since it should be false
	@ 99.9999% of the time.  The rest is done out of line.
	cmp	r4, #TASK_SIZE
	blhs	kuser_cmpxchg64_fixup
#endif
#endif
	.endm

	.align	5
__dabt_usr:
	usr_entry uaccess=0
	kuser_cmpxchg_check
	mov	r2, sp
	dabt_helper
	b	ret_from_exception
 UNWIND(.fnend		)
ENDPROC(__dabt_usr)

	.align	5
/**
 * usr模式下的中断入口
 */
__irq_usr:
/**
 * 将用户模式的上下文保存到svc模式的栈中
 */
	usr_entry
/**
 * 如果在用户态下处理原子操作，此时需要特殊处理。
 */
	kuser_cmpxchg_check
/**
 * 处理中断
 */
	irq_handler
/**
 * 从r9中取得当前进程的threadinfo
 */
	get_thread_info tsk
/**
 * 返回用户态
 */
	mov	why, #0
	b	ret_to_user_from_irq
 UNWIND(.fnend		)
ENDPROC(__irq_usr)

	.ltorg

	.align	5
__und_usr:
	usr_entry uaccess=0

	mov	r2, r4
	mov	r3, r5

	@ r2 = regs->ARM_pc, which is either 2 or 4 bytes ahead of the
	@      faulting instruction depending on Thumb mode.
	@ r3 = regs->ARM_cpsr
	@
	@ The emulation code returns using r9 if it has emulated the
	@ instruction, or the more conventional lr if we are to treat
	@ this as a real undefined instruction
	@
	badr	r9, ret_from_exception

	@ IRQs must be enabled before attempting to read the instruction from
	@ user space since that could cause a page/translation fault if the
	@ page table was modified by another CPU.
	enable_irq

	tst	r3, #PSR_T_BIT			@ Thumb mode?
	bne	__und_usr_thumb
	sub	r4, r2, #4			@ ARM instr at LR - 4
1:	ldrt	r0, [r4]
 ARM_BE8(rev	r0, r0)				@ little endian instruction

	uaccess_disable ip

	@ r0 = 32-bit ARM instruction which caused the exception
	@ r2 = PC value for the following instruction (:= regs->ARM_pc)
	@ r4 = PC value for the faulting instruction
	@ lr = 32-bit undefined instruction function
	badr	lr, __und_usr_fault_32
	b	call_fpe

__und_usr_thumb:
	@ Thumb instruction
	sub	r4, r2, #2			@ First half of thumb instr at LR - 2
#if CONFIG_ARM_THUMB && __LINUX_ARM_ARCH__ >= 6 && CONFIG_CPU_V7
/*
 * Thumb-2 instruction handling.  Note that because pre-v6 and >= v6 platforms
 * can never be supported in a single kernel, this code is not applicable at
 * all when __LINUX_ARM_ARCH__ < 6.  This allows simplifying assumptions to be
 * made about .arch directives.
 */
#if __LINUX_ARM_ARCH__ < 7
/* If the target CPU may not be Thumb-2-capable, a run-time check is needed: */
#define NEED_CPU_ARCHITECTURE
	ldr	r5, .LCcpu_architecture
	ldr	r5, [r5]
	cmp	r5, #CPU_ARCH_ARMv7
	blo	__und_usr_fault_16		@ 16bit undefined instruction
/*
 * The following code won't get run unless the running CPU really is v7, so
 * coding round the lack of ldrht on older arches is pointless.  Temporarily
 * override the assembler target arch with the minimum required instead:
 */
	.arch	armv6t2
#endif
2:	ldrht	r5, [r4]
ARM_BE8(rev16	r5, r5)				@ little endian instruction
	cmp	r5, #0xe800			@ 32bit instruction if xx != 0
	blo	__und_usr_fault_16_pan		@ 16bit undefined instruction
3:	ldrht	r0, [r2]
ARM_BE8(rev16	r0, r0)				@ little endian instruction
	uaccess_disable ip
	add	r2, r2, #2			@ r2 is PC + 2, make it PC + 4
	str	r2, [sp, #S_PC]			@ it's a 2x16bit instr, update
	orr	r0, r0, r5, lsl #16
	badr	lr, __und_usr_fault_32
	@ r0 = the two 16-bit Thumb instructions which caused the exception
	@ r2 = PC value for the following Thumb instruction (:= regs->ARM_pc)
	@ r4 = PC value for the first 16-bit Thumb instruction
	@ lr = 32bit undefined instruction function

#if __LINUX_ARM_ARCH__ < 7
/* If the target arch was overridden, change it back: */
#ifdef CONFIG_CPU_32v6K
	.arch	armv6k
#else
	.arch	armv6
#endif
#endif /* __LINUX_ARM_ARCH__ < 7 */
#else /* !(CONFIG_ARM_THUMB && __LINUX_ARM_ARCH__ >= 6 && CONFIG_CPU_V7) */
	b	__und_usr_fault_16
#endif
 UNWIND(.fnend)
ENDPROC(__und_usr)

/*
 * The out of line fixup for the ldrt instructions above.
 */
	.pushsection .text.fixup, "ax"
	.align	2
4:	str     r4, [sp, #S_PC]			@ retry current instruction
	ret	r9
	.popsection
	.pushsection __ex_table,"a"
	.long	1b, 4b
#if CONFIG_ARM_THUMB && __LINUX_ARM_ARCH__ >= 6 && CONFIG_CPU_V7
	.long	2b, 4b
	.long	3b, 4b
#endif
	.popsection

/*
 * Check whether the instruction is a co-processor instruction.
 * If yes, we need to call the relevant co-processor handler.
 *
 * Note that we don't do a full check here for the co-processor
 * instructions; all instructions with bit 27 set are well
 * defined.  The only instructions that should fault are the
 * co-processor instructions.  However, we have to watch out
 * for the ARM6/ARM7 SWI bug.
 *
 * NEON is a special case that has to be handled here. Not all
 * NEON instructions are co-processor instructions, so we have
 * to make a special case of checking for them. Plus, there's
 * five groups of them, so we have a table of mask/opcode pairs
 * to check against, and if any match then we branch off into the
 * NEON handler code.
 *
 * Emulators may wish to make use of the following registers:
 *  r0  = instruction opcode (32-bit ARM or two 16-bit Thumb)
 *  r2  = PC value to resume execution after successful emulation
 *  r9  = normal "successful" return address
 *  r10 = this threads thread_info structure
 *  lr  = unrecognised instruction return address
 * IRQs enabled, FIQs enabled.
 */
	@
	@ Fall-through from Thumb-2 __und_usr
	@
#ifdef CONFIG_NEON
	get_thread_info r10			@ get current thread
	adr	r6, .LCneon_thumb_opcodes
	b	2f
#endif
call_fpe:
	get_thread_info r10			@ get current thread
#ifdef CONFIG_NEON
	adr	r6, .LCneon_arm_opcodes
2:	ldr	r5, [r6], #4			@ mask value
	ldr	r7, [r6], #4			@ opcode bits matching in mask
	cmp	r5, #0				@ end mask?
	beq	1f
	and	r8, r0, r5
	cmp	r8, r7				@ NEON instruction?
	bne	2b
	mov	r7, #1
	strb	r7, [r10, #TI_USED_CP + 10]	@ mark CP#10 as used
	strb	r7, [r10, #TI_USED_CP + 11]	@ mark CP#11 as used
	b	do_vfp				@ let VFP handler handle this
1:
#endif
	tst	r0, #0x08000000			@ only CDP/CPRT/LDC/STC have bit 27
	tstne	r0, #0x04000000			@ bit 26 set on both ARM and Thumb-2
	reteq	lr
	and	r8, r0, #0x00000f00		@ mask out CP number
 THUMB(	lsr	r8, r8, #8		)
	mov	r7, #1
	add	r6, r10, #TI_USED_CP
 ARM(	strb	r7, [r6, r8, lsr #8]	)	@ set appropriate used_cp[]
 THUMB(	strb	r7, [r6, r8]		)	@ set appropriate used_cp[]
#ifdef CONFIG_IWMMXT
	@ Test if we need to give access to iWMMXt coprocessors
	ldr	r5, [r10, #TI_FLAGS]
	rsbs	r7, r8, #(1 << 8)		@ CP 0 or 1 only
	movcss	r7, r5, lsr #(TIF_USING_IWMMXT + 1)
	bcs	iwmmxt_task_enable
#endif
 ARM(	add	pc, pc, r8, lsr #6	)
 THUMB(	lsl	r8, r8, #2		)
 THUMB(	add	pc, r8			)
	nop

	ret.w	lr				@ CP#0
	W(b)	do_fpe				@ CP#1 (FPE)
	W(b)	do_fpe				@ CP#2 (FPE)
	ret.w	lr				@ CP#3
#ifdef CONFIG_CRUNCH
	b	crunch_task_enable		@ CP#4 (MaverickCrunch)
	b	crunch_task_enable		@ CP#5 (MaverickCrunch)
	b	crunch_task_enable		@ CP#6 (MaverickCrunch)
#else
	ret.w	lr				@ CP#4
	ret.w	lr				@ CP#5
	ret.w	lr				@ CP#6
#endif
	ret.w	lr				@ CP#7
	ret.w	lr				@ CP#8
	ret.w	lr				@ CP#9
#ifdef CONFIG_VFP
	W(b)	do_vfp				@ CP#10 (VFP)
	W(b)	do_vfp				@ CP#11 (VFP)
#else
	ret.w	lr				@ CP#10 (VFP)
	ret.w	lr				@ CP#11 (VFP)
#endif
	ret.w	lr				@ CP#12
	ret.w	lr				@ CP#13
	ret.w	lr				@ CP#14 (Debug)
	ret.w	lr				@ CP#15 (Control)

#ifdef NEED_CPU_ARCHITECTURE
	.align	2
.LCcpu_architecture:
	.word	__cpu_architecture
#endif

#ifdef CONFIG_NEON
	.align	6

.LCneon_arm_opcodes:
	.word	0xfe000000			@ mask
	.word	0xf2000000			@ opcode

	.word	0xff100000			@ mask
	.word	0xf4000000			@ opcode

	.word	0x00000000			@ mask
	.word	0x00000000			@ opcode

.LCneon_thumb_opcodes:
	.word	0xef000000			@ mask
	.word	0xef000000			@ opcode

	.word	0xff100000			@ mask
	.word	0xf9000000			@ opcode

	.word	0x00000000			@ mask
	.word	0x00000000			@ opcode
#endif

do_fpe:
	ldr	r4, .LCfp
	add	r10, r10, #TI_FPSTATE		@ r10 = workspace
	ldr	pc, [r4]			@ Call FP module USR entry point

/*
 * The FP module is called with these registers set:
 *  r0  = instruction
 *  r2  = PC+4
 *  r9  = normal "successful" return address
 *  r10 = FP workspace
 *  lr  = unrecognised FP instruction return address
 */

	.pushsection .data
	.align	2
ENTRY(fp_enter)
	.word	no_fp
	.popsection

ENTRY(no_fp)
	ret	lr
ENDPROC(no_fp)

__und_usr_fault_32:
	mov	r1, #4
	b	1f
__und_usr_fault_16_pan:
	uaccess_disable ip
__und_usr_fault_16:
	mov	r1, #2
1:	mov	r0, sp
	badr	lr, ret_from_exception
	b	__und_fault
ENDPROC(__und_usr_fault_32)
ENDPROC(__und_usr_fault_16)

	.align	5
__pabt_usr:
	usr_entry
	mov	r2, sp				@ regs
	pabt_helper
 UNWIND(.fnend		)
	/* fall through */
/*
 * This is the return code to user mode for abort handlers
 */
ENTRY(ret_from_exception)
 UNWIND(.fnstart	)
 UNWIND(.cantunwind	)
	get_thread_info tsk
	mov	why, #0
	b	ret_to_user
 UNWIND(.fnend		)
ENDPROC(__pabt_usr)
ENDPROC(ret_from_exception)

	.align	5
__fiq_usr:
	usr_entry trace=0
	kuser_cmpxchg_check
	mov	r0, sp				@ struct pt_regs *regs
	bl	handle_fiq_as_nmi
	get_thread_info tsk
	restore_user_regs fast = 0, offset = 0
 UNWIND(.fnend		)
ENDPROC(__fiq_usr)

/*
 * Register switch for ARMv3 and ARMv4 processors
 * r0 = previous task_struct, r1 = previous thread_info, r2 = next thread_info
 * previous and next are guaranteed not to be the same.
 */
/*
该函数的实质就是将移出进程的寄存器上下文保存到移出进程的thread_info->cpu_context结构体中。
并且将移入进程的thread_info->cpu_context结构体中的值restore到cpu的寄存器中，
从而实现堆栈，pc指针和cpu通用寄存器都切换到新的进程上开始执行。

由于switch_to是一个函数，所以对他的寄存器的保存，同样也遵循arm的APCS标准，但又跟APCS又不完全一样。

跟APCS不一样的地方表现在：
保存移出进程的寄存器上下时，保存了lr的地址，而并未保存pc值，
而此时的lr寄存器的地址即为context_switch函数中的switch_to的下一行，即barrier()函数的开始地址。

跟APCS相同的地方表现在:
r0-r3都未保存，因为根据APCS约束，这4个寄存器都是用来存函数的参数，可以不入栈保存。
r4-r10，根据APCS约束，由于这些寄存器会被编译器用来暂存变量的值，因为会被修改，需要入栈保存和出栈恢复。
我们知道在中断处理过程中，linux arm是有将cpsr寄存器也做了保存，而为什么在上下文切换时，却不需要保存呢？
因为中断发生是随机不确定的，他可能在任何指令的后面产生中断，而cpsr只会影响到中断指令的下面的指令的执行，
如果被中断时的指令的下一条指令的执行是依赖于cpsr的，如果中断处理程序修改了cpsr值，而又没保存，会导致中断返回时，
在执行下条指令时，会出现逻辑错误。而上下文切换的返回地址和接下来要执行的指令是确定的，
只要上下文切换回来的指令不受cpsr值得影响(不依赖cpsr)，则就可以不保存cpsr寄存器的值.

**注意**: 
__switch_to函数用户切换线程上下文，而vector_swi用来切换模式上下文.
*/
ENTRY(__switch_to)
 UNWIND(.fnstart	)
 UNWIND(.cantunwind	)
/*
    ip就是上一个线程的thread_info里面的cpu_context的地址
*/
	add	ip, r1, #TI_CPU_SAVE
/*
r1是被切换出去的线程的thread_info, 
ip(r12)寄存器中保存切换出去线程的cpu上下文    
add    ip, r1, #TI_CPU_SAVE    
保存被切换出去的线程的寄存器到自己的线程栈中，保存在它自己的thread_info的cpu_context里
*/
 ARM(	stmia	ip!, {r4 - sl, fp, sp, lr} )	@ Store most regs on stack
 THUMB(	stmia	ip!, {r4 - sl, fp}	   )	@ Store most regs on stack
 THUMB(	str	sp, [ip], #4		   )
 THUMB(	str	lr, [ip], #4		   )
/*
r2是将要切换到运行的线程的thread_info, 
r4,r5中保存将要运行线程thread_info中的tp_value
*/
	ldr	r4, [r2, #TI_TP_VALUE]
	ldr	r5, [r2, #TI_TP_VALUE + 4]
#ifdef CONFIG_CPU_USE_DOMAINS
/*
r6存着下一个线程的DOMAIN属性
*/
	mrc	p15, 0, r6, c3, c0, 0		@ Get domain register
	str	r6, [r1, #TI_CPU_DOMAIN]	@ Save old domain register
	ldr	r6, [r2, #TI_CPU_DOMAIN]
#endif
	switch_tls r1, r4, r5, r3, r7
#if defined(CONFIG_STACKPROTECTOR) && !defined(CONFIG_SMP)
/*
下一个线程的task_struct
*/
	ldr	r7, [r2, #TI_TASK]
/*
r8里面是__stack_chk_guard地址
*/
	ldr	r8, =__stack_chk_guard
	.if (TSK_STACK_CANARY > IMM12_MASK)
	add	r7, r7, #TSK_STACK_CANARY & ~IMM12_MASK
	.endif
	ldr	r7, [r7, #TSK_STACK_CANARY & IMM12_MASK]
#endif
#ifdef CONFIG_CPU_USE_DOMAINS
/*
设置domain寄存器
*/
	mcr	p15, 0, r6, c3, c0, 0		@ Set domain register
#endif
/*
r5里面是上一个线程的task_struct
*/
	mov	r5, r0
/*
r4就是下一个线程的thread_info里面的cpu_context的地址
*/
	add	r4, r2, #TI_CPU_SAVE
/*
下面的thread_notify_head通知链，下面例子说明 
*/
	ldr	r0, =thread_notify_head
	mov	r1, #THREAD_NOTIFY_SWITCH
	bl	atomic_notifier_call_chain
#if defined(CONFIG_STACKPROTECTOR) && !defined(CONFIG_SMP)
	str	r7, [r8]
#endif
/*
ip指向线程的thread_info里面的cpu_context的地址 
*/
 THUMB(	mov	ip, r4			   )
/*
r0从新指向上一个线程的task_struct
*/
	mov	r0, r5
/*
这里就是恢复现场,pc对应了下个进程的cpu_context->pc从上面看到这个cpu_context->pc就是 
之前保存现场的lr,就是下个线程要执行的地方。 

__switch_to最后一句将子进程thread_info中的cpu_context装载到CPU的寄存器里。
这里的pc值就是copy_thread()中设置的ret_from_fork了。
*/
 ARM(	ldmia	r4, {r4 - sl, fp, sp, pc}  )	@ Load all regs saved previously
 THUMB(	ldmia	ip!, {r4 - sl, fp}	   )	@ Load all regs saved previously
 THUMB(	ldr	sp, [ip], #4		   )
 THUMB(	ldr	pc, [ip]		   )
 UNWIND(.fnend		)
ENDPROC(__switch_to)

	__INIT

/*
 * User helpers.
 *
 * Each segment is 32-byte aligned and will be moved to the top of the high
 * vector page.  New segments (if ever needed) must be added in front of
 * existing ones.  This mechanism should be used only for things that are
 * really small and justified, and not be abused freely.
 *
 * See Documentation/arm/kernel_user_helpers.txt for formal definitions.
 */
 THUMB(	.arm	)

	.macro	usr_ret, reg
#ifdef CONFIG_ARM_THUMB
	bx	\reg
#else
	ret	\reg
#endif
	.endm

	.macro	kuser_pad, sym, size
	.if	(. - \sym) & 3
	.rept	4 - (. - \sym) & 3
	.byte	0
	.endr
	.endif
	.rept	(\size - (. - \sym)) / 4
	.word	0xe7fddef1
	.endr
	.endm

#ifdef CONFIG_KUSER_HELPERS
	.align	5
	.globl	__kuser_helper_start
__kuser_helper_start:

/*
 * Due to the length of some sequences, __kuser_cmpxchg64 spans 2 regular
 * kuser "slots", therefore 0xffff0f80 is not used as a valid entry point.
 */

__kuser_cmpxchg64:				@ 0xffff0f60

#if defined(CONFIG_CPU_32v6K)

	stmfd	sp!, {r4, r5, r6, r7}
	ldrd	r4, r5, [r0]			@ load old val
	ldrd	r6, r7, [r1]			@ load new val
	smp_dmb	arm
1:	ldrexd	r0, r1, [r2]			@ load current val
	eors	r3, r0, r4			@ compare with oldval (1)
	eoreqs	r3, r1, r5			@ compare with oldval (2)
	strexdeq r3, r6, r7, [r2]		@ store newval if eq
	teqeq	r3, #1				@ success?
	beq	1b				@ if no then retry
	smp_dmb	arm
	rsbs	r0, r3, #0			@ set returned val and C flag
	ldmfd	sp!, {r4, r5, r6, r7}
	usr_ret	lr

#elif !defined(CONFIG_SMP)

#ifdef CONFIG_MMU

	/*
	 * The only thing that can break atomicity in this cmpxchg64
	 * implementation is either an IRQ or a data abort exception
	 * causing another process/thread to be scheduled in the middle of
	 * the critical sequence.  The same strategy as for cmpxchg is used.
	 */
	stmfd	sp!, {r4, r5, r6, lr}
	ldmia	r0, {r4, r5}			@ load old val
	ldmia	r1, {r6, lr}			@ load new val
1:	ldmia	r2, {r0, r1}			@ load current val
	eors	r3, r0, r4			@ compare with oldval (1)
	eoreqs	r3, r1, r5			@ compare with oldval (2)
2:	stmeqia	r2, {r6, lr}			@ store newval if eq
	rsbs	r0, r3, #0			@ set return val and C flag
	ldmfd	sp!, {r4, r5, r6, pc}

	.text
kuser_cmpxchg64_fixup:
	@ Called from kuser_cmpxchg_fixup.
	@ r4 = address of interrupted insn (must be preserved).
	@ sp = saved regs. r7 and r8 are clobbered.
	@ 1b = first critical insn, 2b = last critical insn.
	@ If r4 >= 1b and r4 <= 2b then saved pc_usr is set to 1b.
	mov	r7, #0xffff0fff
	sub	r7, r7, #(0xffff0fff - (0xffff0f60 + (1b - __kuser_cmpxchg64)))
	subs	r8, r4, r7
	rsbcss	r8, r8, #(2b - 1b)
	strcs	r7, [sp, #S_PC]
#if __LINUX_ARM_ARCH__ < 6
	bcc	kuser_cmpxchg32_fixup
#endif
	ret	lr
	.previous

#else
#warning "NPTL on non MMU needs fixing"
	mov	r0, #-1
	adds	r0, r0, #0
	usr_ret	lr
#endif

#else
#error "incoherent kernel configuration"
#endif

	kuser_pad __kuser_cmpxchg64, 64

__kuser_memory_barrier:				@ 0xffff0fa0
	smp_dmb	arm
	usr_ret	lr

	kuser_pad __kuser_memory_barrier, 32

__kuser_cmpxchg:				@ 0xffff0fc0

#if __LINUX_ARM_ARCH__ < 6

#ifdef CONFIG_MMU

	/*
	 * The only thing that can break atomicity in this cmpxchg
	 * implementation is either an IRQ or a data abort exception
	 * causing another process/thread to be scheduled in the middle
	 * of the critical sequence.  To prevent this, code is added to
	 * the IRQ and data abort exception handlers to set the pc back
	 * to the beginning of the critical section if it is found to be
	 * within that critical section (see kuser_cmpxchg_fixup).
	 */
1:	ldr	r3, [r2]			@ load current val
	subs	r3, r3, r0			@ compare with oldval
2:	streq	r1, [r2]			@ store newval if eq
	rsbs	r0, r3, #0			@ set return val and C flag
	usr_ret	lr

	.text
kuser_cmpxchg32_fixup:
	@ Called from kuser_cmpxchg_check macro.
	@ r4 = address of interrupted insn (must be preserved).
	@ sp = saved regs. r7 and r8 are clobbered.
	@ 1b = first critical insn, 2b = last critical insn.
	@ If r4 >= 1b and r4 <= 2b then saved pc_usr is set to 1b.
	mov	r7, #0xffff0fff
	sub	r7, r7, #(0xffff0fff - (0xffff0fc0 + (1b - __kuser_cmpxchg)))
	subs	r8, r4, r7
	rsbcss	r8, r8, #(2b - 1b)
	strcs	r7, [sp, #S_PC]
	ret	lr
	.previous

#else
#warning "NPTL on non MMU needs fixing"
	mov	r0, #-1
	adds	r0, r0, #0
	usr_ret	lr
#endif

#else

	smp_dmb	arm
1:	ldrex	r3, [r2]
	subs	r3, r3, r0
	strexeq	r3, r1, [r2]
	teqeq	r3, #1
	beq	1b
	rsbs	r0, r3, #0
	/* beware -- each __kuser slot must be 8 instructions max */
	ALT_SMP(b	__kuser_memory_barrier)
	ALT_UP(usr_ret	lr)

#endif

	kuser_pad __kuser_cmpxchg, 32

__kuser_get_tls:				@ 0xffff0fe0
	ldr	r0, [pc, #(16 - 8)]	@ read TLS, set in kuser_get_tls_init
	usr_ret	lr
	mrc	p15, 0, r0, c13, c0, 3	@ 0xffff0fe8 hardware TLS code
	kuser_pad __kuser_get_tls, 16
	.rep	3
	.word	0			@ 0xffff0ff0 software TLS value, then
	.endr				@ pad up to __kuser_helper_version

__kuser_helper_version:				@ 0xffff0ffc
	.word	((__kuser_helper_end - __kuser_helper_start) >> 5)

	.globl	__kuser_helper_end
__kuser_helper_end:

#endif

 THUMB(	.thumb	)

/*
 * Vector stubs.
 *
 * This code is copied to 0xffff1000 so we can use branches in the
 * vectors, rather than ldr's.  Note that this code must not exceed
 * a page size.
 *
 * Common stub entry macro:
 *   Enter in IRQ mode, spsr = SVC/USR CPSR, lr = SVC/USR PC
 *
 * SP points to a minimal amount of processor-private memory, the address
 * of which is copied into r0 for the mode specific abort handler.
 */
/*
  * 生成通用中断、异常处理代码的宏。
  * correction用于调整lr的值。这是因为进入异常时，pc指针是发生异常时的指针后面8个字节或者12个字节处。
  * 不同的异常需要跳转到不同的返回地址。有的需要重新执行指令，有的则需要跳到下一条指令处。
    vector_stub	irq, IRQ_MODE, 4
    vector_stub	dabt, ABT_MODE, 8
    vector_stub	pabt, ABT_MODE, 4
    vector_stub	und, UND_MODE
    vector_stub	fiq, FIQ_MODE, 4
    
 */
	.macro	vector_stub, name, mode, correction=0
	/* 将异常入口强制进行32字节对齐，32字节是一个缓存行的大小。这应当是出于性能的考虑。 */
	.align	5

vector_\name:
	/* 需要调整返回值，则递减lr寄存器 */
	.if \correction
/*
	由于ARM处理器流水线架构的原因，进入中断响应之前，pc被装入LR_irq,
	此时pc指向中断点加8，所以减去4才是中断返回地址
*/
	sub	lr, lr, #\correction
	.endif

	@
	@ Save r0, lr_<exception> (parent PC) and spsr_<exception>
	@ (parent CPSR)
	@

 	/**
     * 将r0,lr保存到堆栈中。这里并没有移动堆栈指针。
     * 这是因为:每种处理器模式都有自己的堆栈。接下来系统会切换到svc模式，将堆栈切换到每个任务的系统堆栈去。
     * 执行后，[sp] = r0, [sp+4]=lr，这里保存r0和lr是因为后面要使用这两个寄存器，即这两个寄存器会被破坏。
     * irq模式栈空间只有12字节。中断和异常并不会保存所有寄存器。
     */
	stmia	sp, {r0, lr}		@ save r0, lr
	/**
     * spsr是异常发生前的状态寄存器，退出异常后，需要根据它恢复现场，因此需要将它保存起来。
     * 首先将它装载到lr寄存器，再将它存储到[sp+8]处。
    */
	mrs	lr, spsr
	str	lr, [sp, #8]		@ save spsr

	@
	@ Prepare for SVC32 mode.  IRQs remain disabled.
	@
/**
    仍然禁用中断
    
    准备将处理器模式设置为SVC32模式。这样，当前堆栈也会切换到SVC32模式下的堆栈。
    eor 异或指令修改CPSR寄存器的控制域为SVC模式，使得中断处理在SVC模式下进行

    msr 将立即数或通用寄存器的内容加载到程序状态寄存器 (PSR) 的指定字段中
    fields 指定要移动的 SPSR 或 CPSR 字段。fields 可以是以下项中的一个
    
	c 控制字段掩码字节， PSR[7:0] （特权模式）
    x 扩展字段掩码字节， PSR[15:8] （特权模式）
    s 状态字段掩码字节， PSR[23:16] （特权模式）
    f 标记字段掩码字节， PSR[31:24] （特权模式）
*/
	mrs	r0, cpsr
	eor	r0, r0, #(\mode ^ SVC_MODE | PSR_ISETSTATE)

	msr	spsr_cxsf, r0

	@
	@ the branch table must immediately follow this code
	@
	/* 
	lr中保存了发生中断时的SPSR（低4位分别表示CPU运行在USR、FIQ、IRQ、SVC模式），
	与0x0f and后，可以得到SPSR M域的值（中断发生在User Space时位0，发生在Kernel Mode时为3） 
	*/
	and	lr, lr, #0x0f
	/* 1f就是宏生成的代码后面的跳转表，这里根据异常前的处理器模式，决定跳转到哪一个处理代码 */
 THUMB(	adr	r0, 1f			)
 THUMB(	ldr	lr, [r0, lr, lsl #2]	)
/*
    通过r0寄存器把irq模式的栈指针sp指向的内容传递给即将跳转的函数
*/
	mov	r0, sp
/**
 * pc应该正好指向随后的label，加上CPU模式，lr中正好保存了异常入口函数。
 * 根据中断发生点所在的模式给lr赋值，__irq_usr或者 __irq_svc
 */
 ARM(	ldr	lr, [pc, lr, lsl #2]	)
 /**
  * 将lr的值加载到pc，实现跳转功能。
  * 这里的mov带s后缀，表示需要根据结果设置cpsr标志位。
  * 但这里目标寄存器时pc时，又有了特殊的作用，那就是将spsr寄存器的内容
  * 设置到cpsr寄存器中，从而实现模式切换到SVC模式
  * 相当于CPU在中断模式之间好似打个照面就退出了。
  * 从ARM处理器角度来看，中断响应已经结束，但是从linux内核来看，
  * 只是进入SVC开始响应中断
  */
	movs	pc, lr			@ branch to handler in SVC mode
ENDPROC(vector_\name)

	.align	2
	@ handler addresses follow this label
1:
	.endm

	.section .stubs, "ax", %progbits
	@ This must be the first word
	.word	vector_swi

vector_rst:
 ARM(	swi	SYS_ERROR0	) /* reset异常，简单的调用SYS_ERROR0系统调用即可 */
 THUMB(	svc	#0		) /* 应该不会运行到这里，呵呵，这仅仅是我的猜想 */
 THUMB(	nop			)
	b	vector_und

/*
 * Interrupt dispatcher
 */
	vector_stub	irq, IRQ_MODE, 4

/**
 * 各种模式下的中断入口
 * linux只使用了usr和svc下的中断，其他模式均为错误模式
 */
	.long	__irq_usr			@  0  (USR_26 / USR_32)
	.long	__irq_invalid			@  1  (FIQ_26 / FIQ_32)
	.long	__irq_invalid			@  2  (IRQ_26 / IRQ_32)
	.long	__irq_svc			@  3  (SVC_26 / SVC_32)
	.long	__irq_invalid			@  4
	.long	__irq_invalid			@  5
	.long	__irq_invalid			@  6
	.long	__irq_invalid			@  7
	.long	__irq_invalid			@  8
	.long	__irq_invalid			@  9
	.long	__irq_invalid			@  a
	.long	__irq_invalid			@  b
	.long	__irq_invalid			@  c
	.long	__irq_invalid			@  d
	.long	__irq_invalid			@  e
	.long	__irq_invalid			@  f

/*
 * Data abort dispatcher
 * Enter in ABT mode, spsr = USR CPSR, lr = USR PC
 */
	vector_stub	dabt, ABT_MODE, 8

/**
 * 各种模式下的dabt入口
 */
	.long	__dabt_usr			@  0  (USR_26 / USR_32)
	.long	__dabt_invalid			@  1  (FIQ_26 / FIQ_32)
	.long	__dabt_invalid			@  2  (IRQ_26 / IRQ_32)
	.long	__dabt_svc			@  3  (SVC_26 / SVC_32)
	.long	__dabt_invalid			@  4
	.long	__dabt_invalid			@  5
	.long	__dabt_invalid			@  6
	.long	__dabt_invalid			@  7
	.long	__dabt_invalid			@  8
	.long	__dabt_invalid			@  9
	.long	__dabt_invalid			@  a
	.long	__dabt_invalid			@  b
	.long	__dabt_invalid			@  c
	.long	__dabt_invalid			@  d
	.long	__dabt_invalid			@  e
	.long	__dabt_invalid			@  f

/*
 * Prefetch abort dispatcher
 * Enter in ABT mode, spsr = USR CPSR, lr = USR PC
 */
	vector_stub	pabt, ABT_MODE, 4

	.long	__pabt_usr			@  0 (USR_26 / USR_32)
	.long	__pabt_invalid			@  1 (FIQ_26 / FIQ_32)
	.long	__pabt_invalid			@  2 (IRQ_26 / IRQ_32)
	.long	__pabt_svc			@  3 (SVC_26 / SVC_32)
	.long	__pabt_invalid			@  4
	.long	__pabt_invalid			@  5
	.long	__pabt_invalid			@  6
	.long	__pabt_invalid			@  7
	.long	__pabt_invalid			@  8
	.long	__pabt_invalid			@  9
	.long	__pabt_invalid			@  a
	.long	__pabt_invalid			@  b
	.long	__pabt_invalid			@  c
	.long	__pabt_invalid			@  d
	.long	__pabt_invalid			@  e
	.long	__pabt_invalid			@  f

/*
 * Undef instr entry dispatcher
 * Enter in UND mode, spsr = SVC/USR CPSR, lr = SVC/USR PC
 */
	vector_stub	und, UND_MODE

	.long	__und_usr			@  0 (USR_26 / USR_32)
	.long	__und_invalid			@  1 (FIQ_26 / FIQ_32)
	.long	__und_invalid			@  2 (IRQ_26 / IRQ_32)
	.long	__und_svc			@  3 (SVC_26 / SVC_32)
	.long	__und_invalid			@  4
	.long	__und_invalid			@  5
	.long	__und_invalid			@  6
	.long	__und_invalid			@  7
	.long	__und_invalid			@  8
	.long	__und_invalid			@  9
	.long	__und_invalid			@  a
	.long	__und_invalid			@  b
	.long	__und_invalid			@  c
	.long	__und_invalid			@  d
	.long	__und_invalid			@  e
	.long	__und_invalid			@  f

	.align	5

/*=============================================================================
 * Address exception handler
 *-----------------------------------------------------------------------------
 * These aren't too critical.
 * (they're not supposed to happen, and won't happen in 32-bit data mode).
 */

vector_addrexcptn:
	b	vector_addrexcptn

/*=============================================================================
 * FIQ "NMI" handler
 *-----------------------------------------------------------------------------
 * Handle a FIQ using the SVC stack allowing FIQ act like NMI on x86
 * systems.
 */
	vector_stub	fiq, FIQ_MODE, 4

	.long	__fiq_usr			@  0  (USR_26 / USR_32)
	.long	__fiq_svc			@  1  (FIQ_26 / FIQ_32)
	.long	__fiq_svc			@  2  (IRQ_26 / IRQ_32)
	.long	__fiq_svc			@  3  (SVC_26 / SVC_32)
	.long	__fiq_svc			@  4
	.long	__fiq_svc			@  5
	.long	__fiq_svc			@  6
	.long	__fiq_abt			@  7
	.long	__fiq_svc			@  8
	.long	__fiq_svc			@  9
	.long	__fiq_svc			@  a
	.long	__fiq_svc			@  b
	.long	__fiq_svc			@  c
	.long	__fiq_svc			@  d
	.long	__fiq_svc			@  e
	.long	__fiq_svc			@  f

	.globl	vector_fiq

	.section .vectors, "ax", %progbits
/**
 * 复制到向量地址表中的代码，各种中断和异常的处理入口。
 * 这些代码由 vector_stub 宏生成
 * ARM异常向量表可以放在0x0地址(normal vectors) 也可以放在0xffff0000处(high vectors)
 * Linux使用high vectors,因为0x0属于用户空间，同时也避免空指针误操作
 */
.L__vectors_start:
	W(b)	vector_rst /* reset异常 */
	W(b)	vector_und /* 未定义指令 */
	/*
由于系统调用异常的代码编译在其他文件中，其入口地址与异常向量相隔较远，使用b指令无法跳转过去。 
因此将其地址存放到LCvswi中，并从内存地址中加载其入口地址。这样，系统调用的速度稍微慢一点
	*/
	W(ldr)	pc, .L__vectors_start + 0x1000 /* 系统调用 */
	W(b)	vector_pabt /* 指令预取异常 */
	W(b)	vector_dabt /* 数据访问中止异常 */
	W(b)	vector_addrexcptn /* 保留 */
	W(b)	vector_irq /* 中断 */
	W(b)	vector_fiq /* 快速中断 */

	.data
	.align	2

	.globl	cr_alignment
cr_alignment:
	.space	4
