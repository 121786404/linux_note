/*
 *  linux/arch/i386/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *
 * This file contains the PC-specific time handling details:
 * reading the RTC at bootup, etc..
 * 1994-07-02    Alan Modra
 *	fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 * 1995-03-26    Markus Kuhn
 *      fixed 500 ms bug at call to set_rtc_mmss, fixed DS12887
 *      precision CMOS clock update
 * 1996-05-03    Ingo Molnar
 *      fixed time warps in do_[slow|fast]_gettimeoffset()
 * 1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 * 1998-09-05    (Various)
 *	More robust do_fast_gettimeoffset() algorithm implemented
 *	(works with APM, Cyrix 6x86MX and Centaur C6),
 *	monotonic gettimeofday() with fast_get_timeoffset(),
 *	drift-proof precision TSC calibration on boot
 *	(C. Scott Ananian <cananian@alumni.princeton.edu>, Andrew D.
 *	Balsa <andrebalsa@altern.org>, Philip Gladstone <philip@raptor.com>;
 *	ported from 2.0.35 Jumbo-9 by Michael Krause <m.krause@tu-harburg.de>).
 * 1998-12-16    Andrea Arcangeli
 *	Fixed Jumbo-9 code in 2.1.131: do_gettimeofday was missing 1 jiffy
 *	because was not accounting lost_ticks.
 * 1998-12-24 Copyright (C) 1998  Andrea Arcangeli
 *	Fixed a xtime SMP race (we need the xtime_lock rw spinlock to
 *	serialize accesses to xtime/lost_ticks).
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/sysdev.h>
#include <linux/bcd.h>
#include <linux/efi.h>
#include <linux/mca.h>

#include <asm/io.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/msr.h>
#include <asm/delay.h>
#include <asm/mpspec.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/timer.h>

#include "mach_time.h"

#include <linux/timex.h>
#include <linux/config.h>

#include <asm/hpet.h>

#include <asm/arch_hooks.h>

#include "io_ports.h"

extern spinlock_t i8259A_lock;
int pit_latch_buggy;              /* extern */

#include "do_timer.h"

/**
 * 自系统启动以来产生的系统节拍的真实数目
 */
u64 jiffies_64 = INITIAL_JIFFIES;

EXPORT_SYMBOL(jiffies_64);

unsigned long cpu_khz;	/* Detected as we calibrate the TSC */

extern unsigned long wall_jiffies;

DEFINE_SPINLOCK(rtc_lock);

DEFINE_SPINLOCK(i8253_lock);
EXPORT_SYMBOL(i8253_lock);

/**
 * cur_timer存放了某个定时器对象的地址，该定时器是系统可利用的定时器资源中“最好的”
 * 在内核初始化期间，select_timer函数设置cur_timer指向适当定时器对象的地址。
 * 80x86中，依次选择这些定时器作为系统定时器：timer_hpet,timer_pmtmr,timer_tsc,timer_pit,timer_none
 */
struct timer_opts *cur_timer = &timer_none;

/*
 * This version of gettimeofday has microsecond resolution
 * and better than microsecond precision on fast x86 machines with TSC.
 */
/**
 * 计算1970/01/01到目前为止走过的秒数及前一秒内走过的微秒数.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long seq;
	unsigned long usec, sec;
	unsigned long max_ntp_tick;

	do {
		unsigned long lost;

		/**
		 * 为读获取顺序锁.
		 */
		seq = read_seqbegin(&xtime_lock);

		/**
		 * 调用get_offset方法来获得自上次时钟中断以来所走过的微秒数。
		 */
		usec = cur_timer->get_offset();
		lost = jiffies - wall_jiffies;

		/*
		 * If time_adjust is negative then NTP is slowing the clock
		 * so make sure not to go into next possible interval.
		 * Better to lose some accuracy than have time go backwards..
		 */
		if (unlikely(time_adjust < 0)) {
			max_ntp_tick = (USEC_PER_SEC / HZ) - tickadj;
			usec = min(usec, max_ntp_tick);

			if (lost)
				usec += lost * max_ntp_tick;
		}
		else if (unlikely(lost))/* 自上次时钟中断以来，丢失了时钟中断，就为usec加上相应的延时 */
			usec += lost * (USEC_PER_SEC / HZ);

		sec = xtime.tv_sec;
		usec += (xtime.tv_nsec / 1000);
	} while (read_seqretry(&xtime_lock, seq));/* 读顺序锁的典型用法 */

	/**
	 * 检查微秒字段是否溢出。
	 */
	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	/**
	 * 复制xtime的内容到系统调用参数tv指定的用户空间缓冲区中。
	 */
	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

EXPORT_SYMBOL(do_gettimeofday);

int do_settimeofday(struct timespec *tv)
{
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);
	/*
	 * This is revolting. We need to set "xtime" correctly. However, the
	 * value in this location is the value at the most recent update of
	 * wall time.  Discover what correction gettimeofday() would have
	 * made, and then undo it!
	 */
	nsec -= cur_timer->get_offset() * NSEC_PER_USEC;
	nsec -= (jiffies - wall_jiffies) * TICK_NSEC;

	wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

	set_normalized_timespec(&xtime, sec, nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	write_sequnlock_irq(&xtime_lock);
	clock_was_set();
	return 0;
}

EXPORT_SYMBOL(do_settimeofday);

static int set_rtc_mmss(unsigned long nowtime)
{
	int retval;

	/* gets recalled with irq locally disabled */
	spin_lock(&rtc_lock);
	if (efi_enabled)
		retval = efi_set_rtc_mmss(nowtime);
	else
		retval = mach_set_rtc_mmss(nowtime);
	spin_unlock(&rtc_lock);

	return retval;
}

/* last time the cmos clock got updated */
static long last_rtc_update;

int timer_ack;

/* monotonic_clock(): returns # of nanoseconds passed since time_init()
 *		Note: This function is required to return accurate
 *		time even in the absence of multiple timer ticks.
 */
unsigned long long monotonic_clock(void)
{
	return cur_timer->monotonic_clock();
}
EXPORT_SYMBOL(monotonic_clock);

#if defined(CONFIG_SMP) && defined(CONFIG_FRAME_POINTER)
unsigned long profile_pc(struct pt_regs *regs)
{
	unsigned long pc = instruction_pointer(regs);

	if (in_lock_functions(pc))
		return *(unsigned long *)(regs->ebp + 4);

	return pc;
}
EXPORT_SYMBOL(profile_pc);
#endif

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
/**
 * 时钟中断的主处理函数。
 */
static inline void do_timer_interrupt(int irq, void *dev_id,
					struct pt_regs *regs)
{
#ifdef CONFIG_X86_IO_APIC
	if (timer_ack) {
		/*
		 * Subtle, when I/O APICs are used we have to ack timer IRQ
		 * manually to reset the IRR bit for do_slow_gettimeoffset().
		 * This will also deassert NMI lines for the watchdog if run
		 * on an 82489DX-based system.
		 */
		spin_lock(&i8259A_lock);
		outb(0x0c, PIC_MASTER_OCW3);
		/* Ack the IRQ; AEOI will end it automatically. */
		inb(PIC_MASTER_POLL);
		spin_unlock(&i8259A_lock);
	}
#endif

	do_timer_interrupt_hook(regs);

	/*
	 * If we have an externally synchronized Linux clock, then update
	 * CMOS clock accordingly every ~11 minutes. Set_rtc_mmss() has to be
	 * called as close as possible to 500 ms before the new second starts.
	 */
	/**
	 * 如果使用外部时钟来同步系统时钟，则每隔11分钟调用set_rtc_mmss函数来调整实时时钟。
	 * 这个特性用来帮助网络中的系统同步它们的时钟。（参见adjtimex系统调用）
	 */
	if ((time_status & STA_UNSYNC) == 0 &&
	    xtime.tv_sec > last_rtc_update + 660 &&
	    (xtime.tv_nsec / 1000)
			>= USEC_AFTER - ((unsigned) TICK_SIZE) / 2 &&
	    (xtime.tv_nsec / 1000)
			<= USEC_BEFORE + ((unsigned) TICK_SIZE) / 2) {
		/* horrible...FIXME */
		if (efi_enabled) {
	 		if (efi_set_rtc_mmss(xtime.tv_sec) == 0)
				last_rtc_update = xtime.tv_sec;
			else
				last_rtc_update = xtime.tv_sec - 600;
		} else if (set_rtc_mmss(xtime.tv_sec) == 0)
			last_rtc_update = xtime.tv_sec;
		else
			last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */
	}

	if (MCA_bus) {
		/* The PS/2 uses level-triggered interrupts.  You can't
		turn them off, nor would you want to (any attempt to
		enable edge-triggered interrupts usually gets intercepted by a
		special hardware circuit).  Hence we have to acknowledge
		the timer interrupt.  Through some incredibly stupid
		design idea, the reset for IRQ 0 is done by setting the
		high bit of the PPI port B (0x61).  Note that some PS/2s,
		notably the 55SX, work fine if this is removed.  */

		irq = inb_p( 0x61 );	/* read the current state */
		outb_p( irq|0x80, 0x61 );	/* reset the IRQ */
	}
}

/*
 * This is the same as the above, except we _also_ save the current
 * Time Stamp Counter value at the time of the timer interrupt, so that
 * we later on can estimate the time of day more exactly.
 */
/**
 * PIT或者HPET的中断服务例程。
 */
irqreturn_t timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	/*
	 * Here we are in the timer irq handler. We just have irqs locally
	 * disabled but we don't know if the timer_bh is running on the other
	 * CPU. We need to avoid to SMP race with it. NOTE: we don' t need
	 * the irq version of write_lock because as just said we have irq
	 * locally disabled. -arca
	 */
	/**
	 * 使用顺序锁来保护定时相关的内核变量。
	 * 由于顺序锁并不会阻塞，而且要保护的数据都是一些简单数据，没有指针数据。所以用在这里是安全的。
	 */
	write_seqlock(&xtime_lock);

	/**
	 * 如果有HPET、pmtmr、tsc对象，那么首先检查是否自上次处理以来，有丢失的中断。
	 * 应该说，丢失中断的情况虽然存在，但是极难遇到。
	 * 如果有丢失的中断，就更新一下jiffies_64。
	 * 然后，记录下HPET、pmtmr、tsc的当前值。
	 * 如果只有timer_pit，那么，mark_offset什么都不会做。
	 */
	cur_timer->mark_offset();

 	/**
 	 * 调用do_timer_interrupt，执行时钟中断的主体方法。
 	 */
	do_timer_interrupt(irq, NULL, regs);

	/**
	 * 释放顺序锁。
	 */
	write_sequnlock(&xtime_lock);
	/**
	 * 返回IRQ_HANDLED，表示中断已经被处理。
	 * 这是必须的，否则linux可能会认为遇到了有问题的主板，从而屏蔽相应的中断线。
	 */
	return IRQ_HANDLED;
}

/* not static: needed by APM */
unsigned long get_cmos_time(void)
{
	unsigned long retval;

	spin_lock(&rtc_lock);

	if (efi_enabled)
		retval = efi_get_time();
	else
		retval = mach_get_cmos_time();

	spin_unlock(&rtc_lock);

	return retval;
}

static long clock_cmos_diff, sleep_start;

static int timer_suspend(struct sys_device *dev, u32 state)
{
	/*
	 * Estimate time zone so that set_time can update the clock
	 */
	clock_cmos_diff = -get_cmos_time();
	clock_cmos_diff += get_seconds();
	sleep_start = get_cmos_time();
	return 0;
}

static int timer_resume(struct sys_device *dev)
{
	unsigned long flags;
	unsigned long sec;
	unsigned long sleep_length;

#ifdef CONFIG_HPET_TIMER
	if (is_hpet_enabled())
		hpet_reenable();
#endif
	sec = get_cmos_time() + clock_cmos_diff;
	sleep_length = (get_cmos_time() - sleep_start) * HZ;
	write_seqlock_irqsave(&xtime_lock, flags);
	xtime.tv_sec = sec;
	xtime.tv_nsec = 0;
	write_sequnlock_irqrestore(&xtime_lock, flags);
	jiffies += sleep_length;
	wall_jiffies += sleep_length;
	return 0;
}

static struct sysdev_class timer_sysclass = {
	.resume = timer_resume,
	.suspend = timer_suspend,
	set_kset_name("timer"),
};


/* XXX this driverfs stuff should probably go elsewhere later -john */
static struct sys_device device_timer = {
	.id	= 0,
	.cls	= &timer_sysclass,
};

static int time_init_device(void)
{
	int error = sysdev_class_register(&timer_sysclass);
	if (!error)
		error = sysdev_register(&device_timer);
	return error;
}

device_initcall(time_init_device);

#ifdef CONFIG_HPET_TIMER
extern void (*late_time_init)(void);
/* Duplicate of time_init() below, with hpet_enable part added */
void __init hpet_time_init(void)
{
	xtime.tv_sec = get_cmos_time();
	xtime.tv_nsec = (INITIAL_JIFFIES % HZ) * (NSEC_PER_SEC / HZ);
	set_normalized_timespec(&wall_to_monotonic,
		-xtime.tv_sec, -xtime.tv_nsec);

	if (hpet_enable() >= 0) {
		printk("Using HPET for base-timer\n");
	}

	cur_timer = select_timer();
	printk(KERN_INFO "Using %s for high-res timesource\n",cur_timer->name);

	time_init_hook();
}
#endif

/**
 * 系统初始化时，用来建立计时体系结构。请注意对HPET的不同处理。
 */
void __init time_init(void)
{
#ifdef CONFIG_HPET_TIMER
	/**
	 * 由于HPET寄存器是由内存映射的，而time_init是在mem_init之前运行的。
	 * 所以，对HPET的初始化必须放在mem_init之后。
	 */
	if (is_hpet_capable()) {
		/*
		 * HPET initialization needs to do memory-mapped io. So, let
		 * us do a late initialization after mem_init().
		 */
		/**
		 * 延后执行hpet_time_init，这样，在mem_init后，hpet_time_init就可以调用由内存映射的HPET寄存器。
		 */
		late_time_init = hpet_time_init;
		return;
	}
#endif
	/**
	 * 初始化xtime变量。利用get_cmos_time从实时时钟上读取自UTC午夜以来经过的秒数。
	 */
	xtime.tv_sec = get_cmos_time();
	/**
	 * 设置xtime的tv_nsec字段。注意这个字段的值与jiffies的初始值的关系。这样二者才能同步。
	 */
	xtime.tv_nsec = (INITIAL_JIFFIES % HZ) * (NSEC_PER_SEC / HZ);
	/**
	 * 初始化wall_to_monotonic。
	 */
	set_normalized_timespec(&wall_to_monotonic,
		-xtime.tv_sec, -xtime.tv_nsec);

	/**
	 * 从可用资源中选择一个“最好的”定时器资源，并将cur_timer指向它。
	 */
	cur_timer = select_timer();
	printk(KERN_INFO "Using %s for high-res timesource\n",cur_timer->name);

	/**
	 * 间接调用setup_irq(0, &irq0);来创建与irq0相应的中断门。
	 * irq0连接着系统时钟中断源（PIT或者HPET）。
	 * 从现在开始，timer_interrupt将会在每个节拍到来时被调用。
	 */
	time_init_hook();
}