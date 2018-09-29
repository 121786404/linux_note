/*
 *  linux/drivers/clocksource/arm_arch_timer.c
 *
 *  Copyright (C) 2011 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt)	"arm_arch_timer: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/sched_clock.h>
#include <linux/acpi.h>

#include <asm/arch_timer.h>
#include <asm/virt.h>

#include <clocksource/arm_arch_timer.h>

#define CNTTIDR		0x08
#define CNTTIDR_VIRT(n)	(BIT(1) << ((n) * 4))

#define CNTACR(n)	(0x40 + ((n) * 4))
#define CNTACR_RPCT	BIT(0)
#define CNTACR_RVCT	BIT(1)
#define CNTACR_RFRQ	BIT(2)
#define CNTACR_RVOFF	BIT(3)
#define CNTACR_RWVT	BIT(4)
#define CNTACR_RWPT	BIT(5)

#define CNTVCT_LO	0x08
#define CNTVCT_HI	0x0c
#define CNTFRQ		0x10
#define CNTP_TVAL	0x28
#define CNTP_CTL	0x2c
#define CNTV_TVAL	0x38
#define CNTV_CTL	0x3c

#define ARCH_CP15_TIMER	BIT(0)
#define ARCH_MEM_TIMER	BIT(1)
/*
记录系统中的timer情况
该变量只有两个bit有效，bit 0标识是否有CP15 timer，bit 1标识memory mapped timer是否已经初始化。
如果在调用arch_timer_init之前，ARCH_CP15_TIMER已经置位，
说明之前已经有一个ARM arch timer的device node进行了初始化的动作，
这多半是由于device tree的database中有两个或者多个cp15 timer的节点，这时候，我们初始化一个就OK了。
*/
static unsigned arch_timers_present __initdata;

static void __iomem *arch_counter_base;

struct arch_timer {
	void __iomem *base;
	struct clock_event_device evt;
};

#define to_arch_timer(e) container_of(e, struct arch_timer, evt)

/**
 * system counter的输入频率
 */
static u32 arch_timer_rate;

enum ppi_nr {
/*
    Secure Physical Timer event
*/
	PHYS_SECURE_PPI,
/*
	Non-secure Physical Timer event
*/
	PHYS_NONSECURE_PPI,
/*
	Virtual Timer event
*/
	VIRT_PPI,
/*
	Hypervisor Timer event
*/
	HYP_PPI,
	MAX_TIMER_PPI
};
/*
保存了ARM generic timer使用IRQ number
*/
static int arch_timer_ppi[MAX_TIMER_PPI];

static struct clock_event_device __percpu *arch_timer_evt;

static enum ppi_nr arch_timer_uses_ppi = VIRT_PPI;
static bool arch_timer_c3stop;
/*
标识系统是否使用virtual timer
*/
static bool arch_timer_mem_use_virtual;
static bool arch_counter_suspend_stop;

static bool evtstrm_enable = IS_ENABLED(CONFIG_ARM_ARCH_TIMER_EVTSTREAM);

static int __init early_evtstrm_cfg(char *buf)
{
	return strtobool(buf, &evtstrm_enable);
}
early_param("clocksource.arm_arch_timer.evtstrm", early_evtstrm_cfg);

/*
 * Architected system timer support.
 */

#ifdef CONFIG_FSL_ERRATUM_A008585
/*
 * The number of retries is an arbitrary value well beyond the highest number
 * of iterations the loop has been observed to take.
 */
#define __fsl_a008585_read_reg(reg) ({			\
	u64 _old, _new;					\
	int _retries = 200;				\
							\
	do {						\
		_old = read_sysreg(reg);		\
		_new = read_sysreg(reg);		\
		_retries--;				\
	} while (unlikely(_old != _new) && _retries);	\
							\
	WARN_ON_ONCE(!_retries);			\
	_new;						\
})

static u32 notrace fsl_a008585_read_cntp_tval_el0(void)
{
	return __fsl_a008585_read_reg(cntp_tval_el0);
}

static u32 notrace fsl_a008585_read_cntv_tval_el0(void)
{
	return __fsl_a008585_read_reg(cntv_tval_el0);
}

static u64 notrace fsl_a008585_read_cntvct_el0(void)
{
	return __fsl_a008585_read_reg(cntvct_el0);
}
#endif

#ifdef CONFIG_HISILICON_ERRATUM_161010101
/*
 * Verify whether the value of the second read is larger than the first by
 * less than 32 is the only way to confirm the value is correct, so clear the
 * lower 5 bits to check whether the difference is greater than 32 or not.
 * Theoretically the erratum should not occur more than twice in succession
 * when reading the system counter, but it is possible that some interrupts
 * may lead to more than twice read errors, triggering the warning, so setting
 * the number of retries far beyond the number of iterations the loop has been
 * observed to take.
 */
#define __hisi_161010101_read_reg(reg) ({				\
	u64 _old, _new;						\
	int _retries = 50;					\
								\
	do {							\
		_old = read_sysreg(reg);			\
		_new = read_sysreg(reg);			\
		_retries--;					\
	} while (unlikely((_new - _old) >> 5) && _retries);	\
								\
	WARN_ON_ONCE(!_retries);				\
	_new;							\
})

static u32 notrace hisi_161010101_read_cntp_tval_el0(void)
{
	return __hisi_161010101_read_reg(cntp_tval_el0);
}

static u32 notrace hisi_161010101_read_cntv_tval_el0(void)
{
	return __hisi_161010101_read_reg(cntv_tval_el0);
}

static u64 notrace hisi_161010101_read_cntvct_el0(void)
{
	return __hisi_161010101_read_reg(cntvct_el0);
}
#endif

#ifdef CONFIG_ARM_ARCH_TIMER_OOL_WORKAROUND
const struct arch_timer_erratum_workaround *timer_unstable_counter_workaround = NULL;
EXPORT_SYMBOL_GPL(timer_unstable_counter_workaround);

DEFINE_STATIC_KEY_FALSE(arch_timer_read_ool_enabled);
EXPORT_SYMBOL_GPL(arch_timer_read_ool_enabled);

static const struct arch_timer_erratum_workaround ool_workarounds[] = {
#ifdef CONFIG_FSL_ERRATUM_A008585
	{
		.id = "fsl,erratum-a008585",
		.read_cntp_tval_el0 = fsl_a008585_read_cntp_tval_el0,
		.read_cntv_tval_el0 = fsl_a008585_read_cntv_tval_el0,
		.read_cntvct_el0 = fsl_a008585_read_cntvct_el0,
	},
#endif
#ifdef CONFIG_HISILICON_ERRATUM_161010101
	{
		.id = "hisilicon,erratum-161010101",
		.read_cntp_tval_el0 = hisi_161010101_read_cntp_tval_el0,
		.read_cntv_tval_el0 = hisi_161010101_read_cntv_tval_el0,
		.read_cntvct_el0 = hisi_161010101_read_cntvct_el0,
	},
#endif
};
#endif /* CONFIG_ARM_ARCH_TIMER_OOL_WORKAROUND */

static __always_inline
void arch_timer_reg_write(int access, enum arch_timer_reg reg, u32 val,
			  struct clock_event_device *clk)
{
	if (access == ARCH_TIMER_MEM_PHYS_ACCESS) {
		struct arch_timer *timer = to_arch_timer(clk);
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			writel_relaxed(val, timer->base + CNTP_CTL);
			break;
		case ARCH_TIMER_REG_TVAL:
			writel_relaxed(val, timer->base + CNTP_TVAL);
			break;
		}
	} else if (access == ARCH_TIMER_MEM_VIRT_ACCESS) {
		struct arch_timer *timer = to_arch_timer(clk);
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			writel_relaxed(val, timer->base + CNTV_CTL);
			break;
		case ARCH_TIMER_REG_TVAL:
			writel_relaxed(val, timer->base + CNTV_TVAL);
			break;
		}
	} else {
		arch_timer_reg_write_cp15(access, reg, val);
	}
}

static __always_inline
u32 arch_timer_reg_read(int access, enum arch_timer_reg reg,
			struct clock_event_device *clk)
{
	u32 val;

	if (access == ARCH_TIMER_MEM_PHYS_ACCESS) {
		struct arch_timer *timer = to_arch_timer(clk);
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			val = readl_relaxed(timer->base + CNTP_CTL);
			break;
		case ARCH_TIMER_REG_TVAL:
			val = readl_relaxed(timer->base + CNTP_TVAL);
			break;
		}
	} else if (access == ARCH_TIMER_MEM_VIRT_ACCESS) {
		struct arch_timer *timer = to_arch_timer(clk);
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			val = readl_relaxed(timer->base + CNTV_CTL);
			break;
		case ARCH_TIMER_REG_TVAL:
			val = readl_relaxed(timer->base + CNTV_TVAL);
			break;
		}
	} else {
		val = arch_timer_reg_read_cp15(access, reg);
	}

	return val;
}

static __always_inline irqreturn_t timer_handler(const int access,
					struct clock_event_device *evt)
{
	unsigned long ctrl;

	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL, evt);
	if (ctrl & ARCH_TIMER_CTRL_IT_STAT) {
		ctrl |= ARCH_TIMER_CTRL_IT_MASK;
		arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl, evt);
		evt->event_handler(evt);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static irqreturn_t arch_timer_handler_virt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_VIRT_ACCESS, evt);
}

static irqreturn_t arch_timer_handler_phys(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_PHYS_ACCESS, evt);
}

static irqreturn_t arch_timer_handler_phys_mem(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_MEM_PHYS_ACCESS, evt);
}

static irqreturn_t arch_timer_handler_virt_mem(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	return timer_handler(ARCH_TIMER_MEM_VIRT_ACCESS, evt);
}

static __always_inline int timer_shutdown(const int access,
					  struct clock_event_device *clk)
{
	unsigned long ctrl;

	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL, clk);
	ctrl &= ~ARCH_TIMER_CTRL_ENABLE;
	arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl, clk);

	return 0;
}

static int arch_timer_shutdown_virt(struct clock_event_device *clk)
{
	return timer_shutdown(ARCH_TIMER_VIRT_ACCESS, clk);
}

static int arch_timer_shutdown_phys(struct clock_event_device *clk)
{
	return timer_shutdown(ARCH_TIMER_PHYS_ACCESS, clk);
}

static int arch_timer_shutdown_virt_mem(struct clock_event_device *clk)
{
	return timer_shutdown(ARCH_TIMER_MEM_VIRT_ACCESS, clk);
}

static int arch_timer_shutdown_phys_mem(struct clock_event_device *clk)
{
	return timer_shutdown(ARCH_TIMER_MEM_PHYS_ACCESS, clk);
}

static __always_inline void set_next_event(const int access, unsigned long evt,
					   struct clock_event_device *clk)
{
	unsigned long ctrl;
	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL, clk);
	ctrl |= ARCH_TIMER_CTRL_ENABLE;
	ctrl &= ~ARCH_TIMER_CTRL_IT_MASK;
	arch_timer_reg_write(access, ARCH_TIMER_REG_TVAL, evt, clk);
	arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl, clk);
}

#ifdef CONFIG_ARM_ARCH_TIMER_OOL_WORKAROUND
static __always_inline void erratum_set_next_event_generic(const int access,
		unsigned long evt, struct clock_event_device *clk)
{
	unsigned long ctrl;
	u64 cval = evt + arch_counter_get_cntvct();

	ctrl = arch_timer_reg_read(access, ARCH_TIMER_REG_CTRL, clk);
	ctrl |= ARCH_TIMER_CTRL_ENABLE;
	ctrl &= ~ARCH_TIMER_CTRL_IT_MASK;

	if (access == ARCH_TIMER_PHYS_ACCESS)
		write_sysreg(cval, cntp_cval_el0);
	else if (access == ARCH_TIMER_VIRT_ACCESS)
		write_sysreg(cval, cntv_cval_el0);

	arch_timer_reg_write(access, ARCH_TIMER_REG_CTRL, ctrl, clk);
}

static int erratum_set_next_event_virt(unsigned long evt,
					   struct clock_event_device *clk)
{
	erratum_set_next_event_generic(ARCH_TIMER_VIRT_ACCESS, evt, clk);
	return 0;
}

static int erratum_set_next_event_phys(unsigned long evt,
					   struct clock_event_device *clk)
{
	erratum_set_next_event_generic(ARCH_TIMER_PHYS_ACCESS, evt, clk);
	return 0;
}
#endif /* CONFIG_ARM_ARCH_TIMER_OOL_WORKAROUND */

static int arch_timer_set_next_event_virt(unsigned long evt,
					  struct clock_event_device *clk)
{
	set_next_event(ARCH_TIMER_VIRT_ACCESS, evt, clk);
	return 0;
}

static int arch_timer_set_next_event_phys(unsigned long evt,
					  struct clock_event_device *clk)
{
	set_next_event(ARCH_TIMER_PHYS_ACCESS, evt, clk);
	return 0;
}

static int arch_timer_set_next_event_virt_mem(unsigned long evt,
					      struct clock_event_device *clk)
{
	set_next_event(ARCH_TIMER_MEM_VIRT_ACCESS, evt, clk);
	return 0;
}

static int arch_timer_set_next_event_phys_mem(unsigned long evt,
					      struct clock_event_device *clk)
{
	set_next_event(ARCH_TIMER_MEM_PHYS_ACCESS, evt, clk);
	return 0;
}

static void erratum_workaround_set_sne(struct clock_event_device *clk)
{
#ifdef CONFIG_ARM_ARCH_TIMER_OOL_WORKAROUND
	if (!static_branch_unlikely(&arch_timer_read_ool_enabled))
		return;

	if (arch_timer_uses_ppi == VIRT_PPI)
		clk->set_next_event = erratum_set_next_event_virt;
	else
		clk->set_next_event = erratum_set_next_event_phys;
#endif
}

static void __arch_timer_setup(unsigned type,
			       struct clock_event_device *clk)
{
	clk->features = CLOCK_EVT_FEAT_ONESHOT;

	if (type == ARCH_CP15_TIMER) {
		if (arch_timer_c3stop)
			clk->features |= CLOCK_EVT_FEAT_C3STOP;
		clk->name = "arch_sys_timer";
		clk->rating = 450;
		clk->cpumask = cpumask_of(smp_processor_id());
		clk->irq = arch_timer_ppi[arch_timer_uses_ppi];
		switch (arch_timer_uses_ppi) {
		case VIRT_PPI:
			clk->set_state_shutdown = arch_timer_shutdown_virt;
			clk->set_state_oneshot_stopped = arch_timer_shutdown_virt;
			clk->set_next_event = arch_timer_set_next_event_virt;
			break;
		case PHYS_SECURE_PPI:
		case PHYS_NONSECURE_PPI:
		case HYP_PPI:
			clk->set_state_shutdown = arch_timer_shutdown_phys;
			clk->set_state_oneshot_stopped = arch_timer_shutdown_phys;
			clk->set_next_event = arch_timer_set_next_event_phys;
			break;
		default:
			BUG();
		}

		erratum_workaround_set_sne(clk);
	} else {
		clk->features |= CLOCK_EVT_FEAT_DYNIRQ;
		clk->name = "arch_mem_timer";
		clk->rating = 400;
		clk->cpumask = cpu_all_mask;
		if (arch_timer_mem_use_virtual) {
			clk->set_state_shutdown = arch_timer_shutdown_virt_mem;
			clk->set_state_oneshot_stopped = arch_timer_shutdown_virt_mem;
			clk->set_next_event =
				arch_timer_set_next_event_virt_mem;
		} else {
			clk->set_state_shutdown = arch_timer_shutdown_phys_mem;
			clk->set_state_oneshot_stopped = arch_timer_shutdown_phys_mem;
			clk->set_next_event =
				arch_timer_set_next_event_phys_mem;
		}
	}

	clk->set_state_shutdown(clk);

	clockevents_config_and_register(clk, arch_timer_rate, 0xf, 0x7fffffff);
}

static void arch_timer_evtstrm_enable(int divider)
{
	u32 cntkctl = arch_timer_get_cntkctl();

	cntkctl &= ~ARCH_TIMER_EVT_TRIGGER_MASK;
	/* Set the divider and enable virtual event stream */
	cntkctl |= (divider << ARCH_TIMER_EVT_TRIGGER_SHIFT)
			| ARCH_TIMER_VIRT_EVT_EN;
	arch_timer_set_cntkctl(cntkctl);
	elf_hwcap |= HWCAP_EVTSTRM;
#ifdef CONFIG_COMPAT
	compat_elf_hwcap |= COMPAT_HWCAP_EVTSTRM;
#endif
}

static void arch_timer_configure_evtstream(void)
{
	int evt_stream_div, pos;

	/* Find the closest power of two to the divisor */
	evt_stream_div = arch_timer_rate / ARCH_TIMER_EVT_STREAM_FREQ;
	pos = fls(evt_stream_div);
	if (pos > 1 && !(evt_stream_div & (1 << (pos - 2))))
		pos--;
	/* enable event stream */
	arch_timer_evtstrm_enable(min(pos, 15));
}

static void arch_counter_set_user_access(void)
{
	u32 cntkctl = arch_timer_get_cntkctl();

	/* Disable user access to the timers and the physical counter */
	/* Also disable virtual event stream */
	cntkctl &= ~(ARCH_TIMER_USR_PT_ACCESS_EN
			| ARCH_TIMER_USR_VT_ACCESS_EN
			| ARCH_TIMER_VIRT_EVT_EN
			| ARCH_TIMER_USR_PCT_ACCESS_EN);

	/* Enable user access to the virtual counter */
	cntkctl |= ARCH_TIMER_USR_VCT_ACCESS_EN;

	arch_timer_set_cntkctl(cntkctl);
}

static bool arch_timer_has_nonsecure_ppi(void)
{
	return (arch_timer_uses_ppi == PHYS_SECURE_PPI &&
		arch_timer_ppi[PHYS_NONSECURE_PPI]);
}

static u32 check_ppi_trigger(int irq)
{
	u32 flags = irq_get_trigger_type(irq);

	if (flags != IRQF_TRIGGER_HIGH && flags != IRQF_TRIGGER_LOW) {
		pr_warn("WARNING: Invalid trigger for IRQ%d, assuming level low\n", irq);
		pr_warn("WARNING: Please fix your firmware\n");
		flags = IRQF_TRIGGER_LOW;
	}

	return flags;
}

static int arch_timer_starting_cpu(unsigned int cpu)
{
	struct clock_event_device *clk = this_cpu_ptr(arch_timer_evt);
	u32 flags;
	/**
	 * 初始化clock event device并注册到系统
	 */
	__arch_timer_setup(ARCH_CP15_TIMER, clk);

	flags = check_ppi_trigger(arch_timer_ppi[arch_timer_uses_ppi]);
	enable_percpu_irq(arch_timer_ppi[arch_timer_uses_ppi], flags);
	/* 打开定时器中断 */
	if (arch_timer_has_nonsecure_ppi()) {
		flags = check_ppi_trigger(arch_timer_ppi[PHYS_NONSECURE_PPI]);
		enable_percpu_irq(arch_timer_ppi[PHYS_NONSECURE_PPI], flags);
	}

	/* 禁止用户态访问counter寄存器 */
	arch_counter_set_user_access();
	if (evtstrm_enable)/* 默认不打开这个东东 */
		arch_timer_configure_evtstream();

	return 0;
}

/*
确定system counter的输入clock频率
*/
static void
arch_timer_detect_rate(void __iomem *cntbase, struct device_node *np)
{
	/* Who has more than one independent system counter? */
	/* 有人已经初始化了??? */
	if (arch_timer_rate)
		return;

	/*
	 * Try to determine the frequency from the device tree or CNTFRQ,
	 * if ACPI is enabled, get the frequency from CNTFRQ ONLY.
	 */
/*
system counter的输入频率，基本上，这个数据有两个可能的来源
（a）device tree node中的clock-frequency属性
（b）寄存器CNTFRQ

我们优先考虑从clock-frequency属性中获取该数据，
*/
	if (!acpi_disabled ||
		/* DT里面强制指定了 */
	    of_property_read_u32(np, "clock-frequency", &arch_timer_rate)) {
        /*
        如果device node中没有定义该属性，那么从CNTFRQ寄存器中读取,        访问CNTFRQ寄存器有两种形态，
        */
/*
        如果给出了cntbase的值，说明是memory mapped的方式来访问CNTFRQ寄存器（直接使用readl_relaxed函数）。
*/
		if (cntbase)/* memory mapped的方式 */
			/**
			 * 访问CNTFRQ寄存器
			 * 直接使用readl_relaxed函数
			 */
			arch_timer_rate = readl_relaxed(cntbase + CNTFRQ);
/*
		如果cntbase是NULL的话，说明是CP15 timer，
        可以通过协处理器来获取该值（调用arch_timer_get_cntfrq函数）。
*/
		else/* CP15 timer */
			/**
			 * 通过协处理器来获取该值
			 * 调用arch_timer_get_cntfrq函数
			 */
			arch_timer_rate = arch_timer_get_cntfrq();
	}

	/* Check the timer frequency. */
	if (arch_timer_rate == 0)
		pr_warn("Architected timer frequency not available\n");
}

static void arch_timer_banner(unsigned type)
{
	pr_info("Architected %s%s%s timer(s) running at %lu.%02luMHz (%s%s%s).\n",
		     type & ARCH_CP15_TIMER ? "cp15" : "",
		     type == (ARCH_CP15_TIMER | ARCH_MEM_TIMER) ?  " and " : "",
		     type & ARCH_MEM_TIMER ? "mmio" : "",
		     (unsigned long)arch_timer_rate / 1000000,
		     (unsigned long)(arch_timer_rate / 10000) % 100,
		     type & ARCH_CP15_TIMER ?
		     (arch_timer_uses_ppi == VIRT_PPI) ? "virt" : "phys" :
			"",
		     type == (ARCH_CP15_TIMER | ARCH_MEM_TIMER) ?  "/" : "",
		     type & ARCH_MEM_TIMER ?
			arch_timer_mem_use_virtual ? "virt" : "phys" :
			"");
}

u32 arch_timer_get_rate(void)
{
	return arch_timer_rate;
}

static u64 arch_counter_get_cntvct_mem(void)
{
	u32 vct_lo, vct_hi, tmp_hi;

	do {
		vct_hi = readl_relaxed(arch_counter_base + CNTVCT_HI);
		vct_lo = readl_relaxed(arch_counter_base + CNTVCT_LO);
		tmp_hi = readl_relaxed(arch_counter_base + CNTVCT_HI);
	} while (vct_hi != tmp_hi);

	return ((u64) vct_hi << 32) | vct_lo;
}

/*
 * Default to cp15 based access because arm64 uses this function for
 * sched_clock() before DT is probed and the cp15 method is guaranteed
 * to exist on arm64. arm doesn't use this before DT is probed so even
 * if we don't have the cp15 accessors we won't have a problem.
 */
u64 (*arch_timer_read_counter)(void) = arch_counter_get_cntvct;

static u64 arch_counter_read(struct clocksource *cs)
{
	return arch_timer_read_counter();
}

static u64 arch_counter_read_cc(const struct cyclecounter *cc)
{
	return arch_timer_read_counter();
}

/**
 * Arm通用ClockSource设备
 */
static struct clocksource clocksource_counter = {
	.name	= "arch_sys_counter",
	/* 很高的精度，一般就选择它了 */
	.rating	= 400,
	/* 读cycles的回调 */
	.read	= arch_counter_read,
	.mask	= CLOCKSOURCE_MASK(56),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static struct cyclecounter cyclecounter __ro_after_init = {
	.read	= arch_counter_read_cc,
	.mask	= CLOCKSOURCE_MASK(56),
};

static struct arch_timer_kvm_info arch_timer_kvm_info;

struct arch_timer_kvm_info *arch_timer_get_kvm_info(void)
{
	return &arch_timer_kvm_info;
}

/**
 * 向时间子系统注册设备
 */
static void __init arch_counter_register(unsigned type)
{
	u64 start_count;

	/* Register the CP15 based counter if we have one */
	/**
	 * ARM generic timer的clock source，其read函数被设定成arch_counter_read
	 * 该函数会调用arch_timer_read_counter 函数
	 * 而这个函数指针会在初始化的时候根据timer的类型进行设定。
	 */
	if (type & ARCH_CP15_TIMER) {
		if (IS_ENABLED(CONFIG_ARM64) || arch_timer_uses_ppi == VIRT_PPI)
			arch_timer_read_counter = arch_counter_get_cntvct;
		else
			arch_timer_read_counter = arch_counter_get_cntpct;

		clocksource_counter.archdata.vdso_direct = true;

#ifdef CONFIG_ARM_ARCH_TIMER_OOL_WORKAROUND
		/*
		 * Don't use the vdso fastpath if errata require using
		 * the out-of-line counter accessor.
		 */
		if (static_branch_unlikely(&arch_timer_read_ool_enabled))
			clocksource_counter.archdata.vdso_direct = false;
#endif
	} else {
		arch_timer_read_counter = arch_counter_get_cntvct_mem;
	}

	if (!arch_counter_suspend_stop)
		clocksource_counter.flags |= CLOCK_SOURCE_SUSPEND_NONSTOP;
	start_count = arch_timer_read_counter();
	/**
	 * 向系统注册一个clock soure
	 * 并给出counter的工作频率作为传入的参数。
	 */
	clocksource_register_hz(&clocksource_counter, arch_timer_rate);
	cyclecounter.mult = clocksource_counter.mult;
	cyclecounter.shift = clocksource_counter.shift;
	/**
	 * 注册一个timercounter
	 * 可以通过arch_timer_get_timecounter获取timercounter
	 * 并可以调用timecounter_read获取一个纳秒值
	 */
	timecounter_init(&arch_timer_kvm_info.timecounter,
			 &cyclecounter, start_count);

	/* 56 bits minimum, so we assume worst case rollover */
	/**
	 * 注册sched clock
	 */
	sched_clock_register(arch_timer_read_counter, 56, arch_timer_rate);
}

static void arch_timer_stop(struct clock_event_device *clk)
{
	pr_debug("arch_timer_teardown disable IRQ%d cpu #%d\n",
		 clk->irq, smp_processor_id());

	disable_percpu_irq(arch_timer_ppi[arch_timer_uses_ppi]);
	if (arch_timer_has_nonsecure_ppi())
		disable_percpu_irq(arch_timer_ppi[PHYS_NONSECURE_PPI]);

	clk->set_state_shutdown(clk);
}

static int arch_timer_dying_cpu(unsigned int cpu)
{
	struct clock_event_device *clk = this_cpu_ptr(arch_timer_evt);

	arch_timer_stop(clk);
	return 0;
}

#ifdef CONFIG_CPU_PM
static unsigned int saved_cntkctl;
static int arch_timer_cpu_pm_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	if (action == CPU_PM_ENTER)
		saved_cntkctl = arch_timer_get_cntkctl();
	else if (action == CPU_PM_ENTER_FAILED || action == CPU_PM_EXIT)
		arch_timer_set_cntkctl(saved_cntkctl);
	return NOTIFY_OK;
}

static struct notifier_block arch_timer_cpu_pm_notifier = {
	.notifier_call = arch_timer_cpu_pm_notify,
};

static int __init arch_timer_cpu_pm_init(void)
{
	return cpu_pm_register_notifier(&arch_timer_cpu_pm_notifier);
}

static void __init arch_timer_cpu_pm_deinit(void)
{
	WARN_ON(cpu_pm_unregister_notifier(&arch_timer_cpu_pm_notifier));
}

#else
static int __init arch_timer_cpu_pm_init(void)
{
	return 0;
}

static void __init arch_timer_cpu_pm_deinit(void)
{
}
#endif

static int __init arch_timer_register(void)
{
	int err;
	int ppi;

	/* 配一个类型是struct clock_event_device的per cpu变量 */
	arch_timer_evt = alloc_percpu(struct clock_event_device);
	if (!arch_timer_evt) {/* 哦豁，没内存了 */
		err = -ENOMEM;
		goto out;
	}

	ppi = arch_timer_ppi[arch_timer_uses_ppi];
	switch (arch_timer_uses_ppi) {
	case VIRT_PPI:
		/**
		 * 注册虚拟Timer中断
		 */
		err = request_percpu_irq(ppi, arch_timer_handler_virt,
					 "arch_timer", arch_timer_evt);
		break;
	case PHYS_SECURE_PPI:
	case PHYS_NONSECURE_PPI:
		/**
		 * 注册物理Timer中断
		 */
		err = request_percpu_irq(ppi, arch_timer_handler_phys,
					 "arch_timer", arch_timer_evt);
		if (!err && arch_timer_ppi[PHYS_NONSECURE_PPI]) {
			ppi = arch_timer_ppi[PHYS_NONSECURE_PPI];
			err = request_percpu_irq(ppi, arch_timer_handler_phys,
						 "arch_timer", arch_timer_evt);
			if (err)
				free_percpu_irq(arch_timer_ppi[PHYS_SECURE_PPI],
						arch_timer_evt);
		}
		break;
	case HYP_PPI:
		err = request_percpu_irq(ppi, arch_timer_handler_phys,
					 "arch_timer", arch_timer_evt);
		break;
	default:
		BUG();
	}

	if (err) {
		pr_err("arch_timer: can't register interrupt %d (%d)\n",
		       ppi, err);
		goto out_free;
	}


	/**
	 * 注册一个回调函数
	 * 在processor进入和退出low power state的时候会调用该回调函数
	 * 以进行电源管理相关的处理
	 */
	err = arch_timer_cpu_pm_init();
	if (err)
		goto out_unreg_notify;


	/* Register and immediately configure the timer on the boot CPU */
	err = cpuhp_setup_state(CPUHP_AP_ARM_ARCH_TIMER_STARTING,
				"clockevents/arm/arch_timer:starting",
	/**
	 * 初始化BSP上的timer硬件对应的clock event device
	 * 并调用clockevents_register_device函数
	 * 将该clock event device注册到linux kernel的时间子系统中
	 */
				arch_timer_starting_cpu, arch_timer_dying_cpu);
	if (err)
		goto out_unreg_cpupm;
	return 0;

out_unreg_cpupm:
	arch_timer_cpu_pm_deinit();

out_unreg_notify:
	free_percpu_irq(arch_timer_ppi[arch_timer_uses_ppi], arch_timer_evt);
	if (arch_timer_has_nonsecure_ppi())
		free_percpu_irq(arch_timer_ppi[PHYS_NONSECURE_PPI],
				arch_timer_evt);

out_free:
	free_percpu(arch_timer_evt);
out:
	return err;
}

static int __init arch_timer_mem_register(void __iomem *base, unsigned int irq)
{
	int ret;
	irq_handler_t func;
	struct arch_timer *t;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return -ENOMEM;

	t->base = base;
	t->evt.irq = irq;
	__arch_timer_setup(ARCH_MEM_TIMER, &t->evt);

	if (arch_timer_mem_use_virtual)
		func = arch_timer_handler_virt_mem;
	else
		func = arch_timer_handler_phys_mem;

	ret = request_irq(irq, func, IRQF_TIMER, "arch_mem_timer", &t->evt);
	if (ret) {
		pr_err("arch_timer: Failed to request mem timer irq\n");
		kfree(t);
	}

	return ret;
}

static const struct of_device_id arch_timer_of_match[] __initconst = {
	{ .compatible   = "arm,armv7-timer",    },
	{ .compatible   = "arm,armv8-timer",    },
	{},
};

static const struct of_device_id arch_timer_mem_of_match[] __initconst = {
	{ .compatible   = "arm,armv7-timer-mem", },
	{},
};

static bool __init
arch_timer_needs_probing(int type, const struct of_device_id *matches)
{
	struct device_node *dn;
	bool needs_probing = false;

	dn = of_find_matching_node(NULL, matches);
	if (dn && of_device_is_available(dn) && !(arch_timers_present & type))
		needs_probing = true;
	of_node_put(dn);

	return needs_probing;
}

static int __init arch_timer_common_init(void)
{
	unsigned mask = ARCH_CP15_TIMER | ARCH_MEM_TIMER;

	/* Wait until both nodes are probed if we have two timers */
	/* 等两个Timer都初始化完再执行后面的初始化 */
	if ((arch_timers_present & mask) != mask) {
		if (arch_timer_needs_probing(ARCH_MEM_TIMER, arch_timer_mem_of_match))
			return 0;
		if (arch_timer_needs_probing(ARCH_CP15_TIMER, arch_timer_of_match))
			return 0;
	}

	/**
	 * 输出ARM generic timer的相关信息到控制台
	 */
	arch_timer_banner(arch_timers_present);
	/**
	 * 向linux kernel的时间子系统注册clock source、timer counter、shed clock设备
	 */
	arch_counter_register(arch_timers_present);
	/**
	 * 注册delay timer
	 */
	return arch_timer_arch_init();
}

static int __init arch_timer_init(void)
{
	int ret;
	/*
	 * If HYP mode is available, we know that the physical timer
	 * has been configured to be accessible from PL1. Use it, so
	 * that a guest can use the virtual timer instead.
	 *
	 * If no interrupt provided for virtual timer, we'll have to
	 * stick to the physical timer. It'd better be accessible...
	 *
	 * On ARMv8.1 with VH extensions, the kernel runs in HYP. VHE
	 * accesses to CNTP_*_EL1 registers are silently redirected to
	 * their CNTHP_*_EL2 counterparts, and use a different PPI
	 * number.
	 */
/*
	 如果没有定义virtual timer的中断（arch_timer_ppi[VIRT_PPI]==0），那么我们只能是使用physical timer的，
	 * 系统运行于hyp模式
	 * 或者没有定义virtual timer的中断
	 * 都必须用物理TIMER中断
	 。
	 ok，既然使用physical timer，那么需要定义physical timer中断，
	 包括secure和non-secure physical timer event PPI。
	 只要有一个没有定义，那么就出错退出了。
     如果系统支持虚拟化，那么CPU会处于HYP mode，这时候，我们也是应该使用physical timer的，
     virtual timer是guest OS需要访问的
*/
	if (is_hyp_mode_available() || !arch_timer_ppi[VIRT_PPI]) {
		bool has_ppi;

		if (is_kernel_in_hyp_mode()) {
		    // 设定arch_timer_use_virtual这个全局变量为false
			arch_timer_uses_ppi = HYP_PPI;
			has_ppi = !!arch_timer_ppi[HYP_PPI];
		} else {
			arch_timer_uses_ppi = PHYS_SECURE_PPI;
		/**
		 * 如果没有虚拟中断，就用物理中断
		 * 那么secure和non-secure physical timer event PPI就必须定义
		 */
			has_ppi = (!!arch_timer_ppi[PHYS_SECURE_PPI] ||
				   !!arch_timer_ppi[PHYS_NONSECURE_PPI]);
		}

		if (!has_ppi) {
			pr_warn("arch_timer: No interrupt available, giving up\n");
			return -EINVAL;
		}
	}

	/**
	 * 向Linux时间子系统注册设备
	 */
	ret = arch_timer_register();
	if (ret)
		return ret;
	/**
	 * CP 15 Timer和Mem Timer的通用处理
	 */
	ret = arch_timer_common_init();
	if (ret)
		return ret;

	arch_timer_kvm_info.virtual_irq = arch_timer_ppi[VIRT_PPI];
	
	return 0;
}

/**
 * CP 15 TIMER的初始化
 */
static int __init arch_timer_of_init(struct device_node *np)
{
	int i;

	/**
	 * 之前已经有一个ARM arch timer的device node进行了初始化的动作
	 */
	if (arch_timers_present & ARCH_CP15_TIMER) {
		/**
		 * 多半是由于device tree的database中有两个或者多个cp15 timer的节点，这时候
		 * 我们初始化一个就OK了
		 */
		pr_warn("arch_timer: multiple nodes in dt, skipping\n");
		return 0;
	}

	arch_timers_present |= ARCH_CP15_TIMER;
	/* 分配IRQ */
	for (i = PHYS_SECURE_PPI; i < MAX_TIMER_PPI; i++)
		/**
		 * irq_of_parse_and_map对该device node中的interrupt属性进行分析
		 * 并分配IRQ number，建立HW interrupt ID和该IRQ number的映射
		 */
		arch_timer_ppi[i] = irq_of_parse_and_map(np, i);

	/**
	 * 确定system counter的输入clock频率
	 */
	arch_timer_detect_rate(NULL, np);

	arch_timer_c3stop = !of_property_read_bool(np, "always-on");

#ifdef CONFIG_ARM_ARCH_TIMER_OOL_WORKAROUND
	for (i = 0; i < ARRAY_SIZE(ool_workarounds); i++) {
		if (of_property_read_bool(np, ool_workarounds[i].id)) {
			timer_unstable_counter_workaround = &ool_workarounds[i];
			static_branch_enable(&arch_timer_read_ool_enabled);
			pr_info("arch_timer: Enabling workaround for %s\n",
				timer_unstable_counter_workaround->id);
			break;
		}
	}
#endif

	/*
	 * If we cannot rely on firmware initializing the timer registers then
	 * we should use the physical timers instead.
	 */
	if (IS_ENABLED(CONFIG_ARM) &&
	    of_property_read_bool(np, "arm,cpu-registers-not-fw-configured"))
		arch_timer_uses_ppi = PHYS_SECURE_PPI;

	/* On some systems, the counter stops ticking when in suspend. */
	arch_counter_suspend_stop = of_property_read_bool(np,
							 "arm,no-tick-in-suspend");

	return arch_timer_init();
}

/*
初始化了一个struct of_device_id 的静态常量，并放置在 __clksrc_of_table section中。

static const struct of_device_id __of_table_arm_twd_a9
	__used __section(__clksrc_of_table)
	 = { .compatible = "arm,cortex-a9-twd-timer",
	     .data =  twd_local_timer_of_register  }

不论是v7还是v8，其初始化函数都是一个arch_timer_init
*/
/*
通过协处理器CP15访问timer的寄存器，我们称之CP15 timer。
arch_timer_init是for CP15 timer类型的驱动进行初始化的。
*/
CLOCKSOURCE_OF_DECLARE(armv7_arch_timer, "arm,armv7-timer", arch_timer_of_init);
CLOCKSOURCE_OF_DECLARE(armv8_arch_timer, "arm,armv8-timer", arch_timer_of_init);

static int __init arch_timer_mem_init(struct device_node *np)
{
	struct device_node *frame, *best_frame = NULL;
	void __iomem *cntctlbase, *base;
	unsigned int irq, ret = -EINVAL;
	u32 cnttidr;

	arch_timers_present |= ARCH_MEM_TIMER;
	cntctlbase = of_iomap(np, 0);
	if (!cntctlbase) {
		pr_err("arch_timer: Can't find CNTCTLBase\n");
		return -ENXIO;
	}

	cnttidr = readl_relaxed(cntctlbase + CNTTIDR);

	/*
	 * Try to find a virtual capable frame. Otherwise fall back to a
	 * physical capable frame.
	 */
	for_each_available_child_of_node(np, frame) {
		int n;
		u32 cntacr;

		if (of_property_read_u32(frame, "frame-number", &n)) {
			pr_err("arch_timer: Missing frame-number\n");
			of_node_put(frame);
			goto out;
		}

		/* Try enabling everything, and see what sticks */
		cntacr = CNTACR_RFRQ | CNTACR_RWPT | CNTACR_RPCT |
			 CNTACR_RWVT | CNTACR_RVOFF | CNTACR_RVCT;
		writel_relaxed(cntacr, cntctlbase + CNTACR(n));
		cntacr = readl_relaxed(cntctlbase + CNTACR(n));

		if ((cnttidr & CNTTIDR_VIRT(n)) &&
		    !(~cntacr & (CNTACR_RWVT | CNTACR_RVCT))) {
			of_node_put(best_frame);
			best_frame = frame;
			arch_timer_mem_use_virtual = true;
			break;
		}

		if (~cntacr & (CNTACR_RWPT | CNTACR_RPCT))
			continue;

		of_node_put(best_frame);
		best_frame = of_node_get(frame);
	}

	ret= -ENXIO;
	base = arch_counter_base = of_io_request_and_map(best_frame, 0,
							 "arch_mem_timer");
	if (IS_ERR(base)) {
		pr_err("arch_timer: Can't map frame's registers\n");
		goto out;
	}

	if (arch_timer_mem_use_virtual)
		irq = irq_of_parse_and_map(best_frame, 1);
	else
		irq = irq_of_parse_and_map(best_frame, 0);

	ret = -EINVAL;
	if (!irq) {
		pr_err("arch_timer: Frame missing %s irq",
		       arch_timer_mem_use_virtual ? "virt" : "phys");
		goto out;
	}

	arch_timer_detect_rate(base, np);
	ret = arch_timer_mem_register(base, irq);
	if (ret)
		goto out;

	return arch_timer_common_init();
out:
	iounmap(cntctlbase);
	of_node_put(best_frame);
	return ret;
}

/*
通过寄存器接口访问timer，也就是说，generic timer的控制寄存器被memory map到CPU的地址空间，
这种我们称之memory mapped timer
arch_timer_mem_init是for memory mapped timer类型的驱动初始化的

其实最开始的时候，driver只支持CP15 type的timer访问形态，毕竟这种方式比memory mapped register的访问速度要更快一些。
但是，这种方式不能控制system level的counter硬件部分（只能使用memory mapped IO形式访问），因此功能受限。
比如：system counter可以提供一组frequency table，
可以让软件设定当然counter的输入频率以及每个clock下counter增加的数目。
这样的设定可以让system counter的硬件在不同的输入频率下工作，有更好的电源管理特性。

此外，有些系统不支持协处理的访问，这种情况下又想给系统增加ARM generic timer的功能，
这时候必须使用memory mapped register的方式来访问ARM generic timer的所有硬件block
（包括system counter和per cpu的timer）。这时候，在访问timer硬件的时候虽然性能不佳，但总是好过功能丧失
*/
CLOCKSOURCE_OF_DECLARE(armv7_arch_timer_mem, "arm,armv7-timer-mem",
		       arch_timer_mem_init);

#ifdef CONFIG_ACPI
static int __init map_generic_timer_interrupt(u32 interrupt, u32 flags)
{
	int trigger, polarity;

	if (!interrupt)
		return 0;

	trigger = (flags & ACPI_GTDT_INTERRUPT_MODE) ? ACPI_EDGE_SENSITIVE
			: ACPI_LEVEL_SENSITIVE;

	polarity = (flags & ACPI_GTDT_INTERRUPT_POLARITY) ? ACPI_ACTIVE_LOW
			: ACPI_ACTIVE_HIGH;

	return acpi_register_gsi(NULL, interrupt, trigger, polarity);
}

/* Initialize per-processor generic timer */
static int __init arch_timer_acpi_init(struct acpi_table_header *table)
{
	struct acpi_table_gtdt *gtdt;

	if (arch_timers_present & ARCH_CP15_TIMER) {
		pr_warn("arch_timer: already initialized, skipping\n");
		return -EINVAL;
	}

	gtdt = container_of(table, struct acpi_table_gtdt, header);

	arch_timers_present |= ARCH_CP15_TIMER;

	arch_timer_ppi[PHYS_SECURE_PPI] =
		map_generic_timer_interrupt(gtdt->secure_el1_interrupt,
		gtdt->secure_el1_flags);

	arch_timer_ppi[PHYS_NONSECURE_PPI] =
		map_generic_timer_interrupt(gtdt->non_secure_el1_interrupt,
		gtdt->non_secure_el1_flags);

	arch_timer_ppi[VIRT_PPI] =
		map_generic_timer_interrupt(gtdt->virtual_timer_interrupt,
		gtdt->virtual_timer_flags);

	arch_timer_ppi[HYP_PPI] =
		map_generic_timer_interrupt(gtdt->non_secure_el2_interrupt,
		gtdt->non_secure_el2_flags);

	/* Get the frequency from CNTFRQ */
	arch_timer_detect_rate(NULL, NULL);

	/* Always-on capability */
	arch_timer_c3stop = !(gtdt->non_secure_el1_flags & ACPI_GTDT_ALWAYS_ON);

	arch_timer_init();
	return 0;
}
CLOCKSOURCE_ACPI_DECLARE(arch_timer, ACPI_SIG_GTDT, arch_timer_acpi_init);
#endif