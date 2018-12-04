/*
 *  linux/arch/arm/mm/context.c
 *
 *  Copyright (C) 2002-2003 Deep Blue Solutions Ltd, all rights reserved.
 *  Copyright (C) 2012 ARM Limited
 *
 *  Author: Will Deacon <will.deacon@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/percpu.h>

#include <asm/mmu_context.h>
#include <asm/smp_plat.h>
#include <asm/thread_notify.h>
#include <asm/tlbflush.h>
#include <asm/proc-fns.h>

/*
 * On ARMv6, we have the following structure in the Context ID:
 *
 * 31                         7          0
 * +-------------------------+-----------+
 * |      process ID         |   ASID    |
 * +-------------------------+-----------+
 * |              context ID             |
 * +-------------------------------------+
 *
 * The ASID is used to tag entries in the CPU caches and TLBs.
 * The context ID is used by debuggers and trace logic, and
 * should be unique within all running processes.
 *
 * In big endian operation, the two 32 bit words are swapped if accessed
 * by non-64-bit operations.
 */
#define ASID_FIRST_VERSION	(1ULL << ASID_BITS)
#define NUM_USER_ASIDS		ASID_FIRST_VERSION

static DEFINE_RAW_SPINLOCK(cpu_asid_lock);
/*
arm支持ASID的概念，理论上进程切换不需要TLB的操作，不过由于HW asid的编址空间有限，

asid generation从 ASID_FIRST_VERSION 开始，每当HW asid溢出后，asid generation会累加。

*/
static atomic64_t asid_generation = ATOMIC64_INIT(ASID_FIRST_VERSION);
static DECLARE_BITMAP(asid_map, NUM_USER_ASIDS);

static DEFINE_PER_CPU(atomic64_t, active_asids);
static DEFINE_PER_CPU(u64, reserved_asids);
static cpumask_t tlb_flush_pending;

#ifdef CONFIG_ARM_ERRATA_798181
void a15_erratum_get_cpumask(int this_cpu, struct mm_struct *mm,
			     cpumask_t *mask)
{
	int cpu;
	unsigned long flags;
	u64 context_id, asid;

	raw_spin_lock_irqsave(&cpu_asid_lock, flags);
	context_id = mm->context.id.counter;
	for_each_online_cpu(cpu) {
		if (cpu == this_cpu)
			continue;
		/*
		 * We only need to send an IPI if the other CPUs are
		 * running the same ASID as the one being invalidated.
		 */
		asid = per_cpu(active_asids, cpu).counter;
		if (asid == 0)
			asid = per_cpu(reserved_asids, cpu);
		if (context_id == asid)
			cpumask_set_cpu(cpu, mask);
	}
	raw_spin_unlock_irqrestore(&cpu_asid_lock, flags);
}
#endif

#ifdef CONFIG_ARM_LPAE
/*
 * With LPAE, the ASID and page tables are updated atomicly, so there is
 * no need for a reserved set of tables (the active ASID tracking prevents
 * any issues across a rollover).
 */
#define cpu_set_reserved_ttbr0()
#else
static void cpu_set_reserved_ttbr0(void)
{
	u32 ttb;
	/*
	 * Copy TTBR1 into TTBR0.
	 * This points at swapper_pg_dir, which contains only global
	 * entries so any speculative walks are perfectly safe.
	 */
	asm volatile(
	"	mrc	p15, 0, %0, c2, c0, 1		@ read TTBR1\n"
	"	mcr	p15, 0, %0, c2, c0, 0		@ set TTBR0\n"
	: "=r" (ttb));
	isb();
}
#endif

#ifdef CONFIG_PID_IN_CONTEXTIDR
static int contextidr_notifier(struct notifier_block *unused, unsigned long cmd,
			       void *t)
{
	u32 contextidr;
	pid_t pid;
	struct thread_info *thread = t;

	if (cmd != THREAD_NOTIFY_SWITCH)
		return NOTIFY_DONE;

	pid = task_pid_nr(thread->task) << ASID_BITS;
	asm volatile(
	"	mrc	p15, 0, %0, c13, c0, 1\n"
	"	and	%0, %0, %2\n"
	"	orr	%0, %0, %1\n"
	"	mcr	p15, 0, %0, c13, c0, 1\n"
	: "=r" (contextidr), "+r" (pid)
	: "I" (~ASID_MASK));
	isb();

	return NOTIFY_OK;
}

static struct notifier_block contextidr_notifier_block = {
	.notifier_call = contextidr_notifier,
};

static int __init contextidr_notifier_init(void)
{
	return thread_register_notifier(&contextidr_notifier_block);
}
arch_initcall(contextidr_notifier_init);
#endif

static void flush_context(unsigned int cpu)
{
	int i;
	u64 asid;

	/* Update the list of reserved ASIDs and the ASID bitmap. */
/*
    控制HW asid的asid_map被全部清零了，
*/
	bitmap_clear(asid_map, 0, NUM_USER_ASIDS);
/*
    所有cpu上的所有的 old generation 需要被flush掉，
*/
	for_each_possible_cpu(i) {
		asid = atomic64_xchg(&per_cpu(active_asids, i), 0);
		/*
		 * If this CPU has already been through a
		 * rollover, but hasn't run another task in
		 * the meantime, we must preserve its reserved
		 * ASID, as this is the only trace we have of
		 * the process it is still running.
		 */
		if (asid == 0)
			asid = per_cpu(reserved_asids, i);
		__set_bit(asid & ~ASID_MASK, asid_map);
		per_cpu(reserved_asids, i) = asid;
	}

	/* Queue a TLB invalidate and flush the I-cache if necessary. */
	cpumask_setall(&tlb_flush_pending);

	if (icache_is_vivt_asid_tagged())
		__flush_icache_all();
}

static bool check_update_reserved_asid(u64 asid, u64 newasid)
{
	int cpu;
	bool hit = false;

	/*
	 * Iterate over the set of reserved ASIDs looking for a match.
	 * If we find one, then we can update our mm to use newasid
	 * (i.e. the same ASID in the current generation) but we can't
	 * exit the loop early, since we need to ensure that all copies
	 * of the old ASID are updated to reflect the mm. Failure to do
	 * so could result in us missing the reserved ASID in a future
	 * generation.
	 */
	for_each_possible_cpu(cpu) {
		if (per_cpu(reserved_asids, cpu) == asid) {
			hit = true;
			per_cpu(reserved_asids, cpu) = newasid;
		}
	}

	return hit;
}

static u64 new_context(struct mm_struct *mm, unsigned int cpu)
{
	static u32 cur_idx = 1;
	u64 asid = atomic64_read(&mm->context.id);
	u64 generation = atomic64_read(&asid_generation);

/*
    如果asid不等于0那么说明这个mm之前就已经分配过software asid了，
    那么new context就将software asid中的旧的generation更新为当前的generation
*/
	if (asid != 0) {
		u64 newasid = generation | (asid & ~ASID_MASK);

		/*
		 * If our current ASID was active during a rollover, we
		 * can continue to use it and this was just a false alarm.
		 */
		if (check_update_reserved_asid(asid, newasid))
			return newasid;

		/*
		 * We had a valid ASID in a previous life, so try to re-use
		 * it if possible.,
		 */
		asid &= ~ASID_MASK;
		if (!__test_and_set_bit(asid, asid_map))
			return newasid;
	}

	/*
	 * Allocate a free ASID. If we can't find one, take a note of the
	 * currently active ASIDs and mark the TLBs as requiring flushes.
	 * We always count from ASID #1, as we reserve ASID #0 to switch
	 * via TTBR0 and to avoid speculative page table walks from hitting
	 * in any partial walk caches, which could be populated from
	 * overlapping level-1 descriptors used to map both the module
	 * area and the userspace stack.
	 */
/*
    如果asid等于0，说明我们的确是需要分配一个新的HW asid，
*/
	asid = find_next_zero_bit(asid_map, NUM_USER_ASIDS, cur_idx);
	if (asid == NUM_USER_ASIDS) {
/*
        如果找不到一个空闲的HW asid，说明HW asid已经用光了，这是只能提升generation了。
*/
		generation = atomic64_add_return(ASID_FIRST_VERSION,
						 &asid_generation);
/*
        因为系统已经准备进入 new generation 了。
        顺便一提的是这里 generation 变量已经被赋值为 new generation了   
*/
		flush_context(cpu);
/*
        new generation中HW asid的分配		
*/
		asid = find_next_zero_bit(asid_map, NUM_USER_ASIDS, 1);
	}
/*
    找到一个空闲的HW asid
*/
	__set_bit(asid, asid_map);
	cur_idx = asid;
	cpumask_clear(mm_cpumask(mm));
/*
    返回 software asid（当前generation＋新分配的hw asid）  
*/
	return asid | generation;
}

void check_and_switch_context(struct mm_struct *mm, struct task_struct *tsk)
{
	unsigned long flags;
	unsigned int cpu = smp_processor_id();
	u64 asid;

	if (unlikely(mm->context.vmalloc_seq != init_mm.context.vmalloc_seq))
		__check_vmalloc_seq(mm);

	/*
	 * We cannot update the pgd and the ASID atomicly with classic
	 * MMU, so switch exclusively to global mappings to avoid
	 * speculative page table walking with the wrong TTBR.
	 */
	cpu_set_reserved_ttbr0();
/*
    符获取该地址空间的 software asid。
*/
	asid = atomic64_read(&mm->context.id);
/*
    当要切入的mm的software asid仍然处于当前这一批次（generation）的ASID的时候，
    切换中不需要任何的TLB操作，可以直接调用 cpu_switch_mm 进行地址空间的切换，
    当然，也会顺便设定active_asids这个percpu变量	
*/
	if (!((asid ^ atomic64_read(&asid_generation)) >> ASID_BITS)
	    && atomic64_xchg(&per_cpu(active_asids, cpu), asid))
		goto switch_mm_fastpath;

	raw_spin_lock_irqsave(&cpu_asid_lock, flags);
	/* Check that our ASID belongs to the current generation. */
	asid = atomic64_read(&mm->context.id);
/*
	如果要切入的进程和当前的asid generation不一致，
	那么说明该地址空间需要一个新的software asid了，
	更准确的说是需要推进到new generation了。
*/
	if ((asid ^ atomic64_read(&asid_generation)) >> ASID_BITS) {
/*
        分配一个新的context ID
*/
		asid = new_context(mm, cpu);
/*
        设定到mm->context.id中		
*/
		atomic64_set(&mm->context.id, asid);
	}
/*
    各个cpu在切入新一代的asid空间的时候会调用 local_flush_tlb_all 将本地tlb flush掉
*/
	if (cpumask_test_and_clear_cpu(cpu, &tlb_flush_pending)) {
		local_flush_bp_all();
		local_flush_tlb_all();
	}

	atomic64_set(&per_cpu(active_asids, cpu), asid);
	cpumask_set_cpu(cpu, mm_cpumask(mm));
	raw_spin_unlock_irqrestore(&cpu_asid_lock, flags);

switch_mm_fastpath:
	cpu_switch_mm(mm->pgd, mm);
}
