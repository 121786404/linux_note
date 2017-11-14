/*  linux/include/linux/clockchips.h
 *
 *  This file contains the structure definitions for clockchips.
 *
 *  If you are not a clockchip, or the time of day code, you should
 *  not be including this file!
 */
#ifndef _LINUX_CLOCKCHIPS_H
#define _LINUX_CLOCKCHIPS_H

#ifdef CONFIG_GENERIC_CLOCKEVENTS

# include <linux/clocksource.h>
# include <linux/cpumask.h>
# include <linux/ktime.h>
# include <linux/notifier.h>

struct clock_event_device;
struct module;

/*
 * Possible states of a clock event device.
 *
 * DETACHED:	Device is not used by clockevents core. Initial state or can be
 *		reached from SHUTDOWN.
 * SHUTDOWN:	Device is powered-off. Can be reached from PERIODIC or ONESHOT.
 * PERIODIC:	Device is programmed to generate events periodically. Can be
 *		reached from DETACHED or SHUTDOWN.
 * ONESHOT:	Device is programmed to generate event only once. Can be reached
 *		from DETACHED or SHUTDOWN.
 * ONESHOT_STOPPED: Device was programmed in ONESHOT mode and is temporarily
 *		    stopped.
 */
enum clock_event_state {
	CLOCK_EVT_STATE_DETACHED,
	CLOCK_EVT_STATE_SHUTDOWN,
	CLOCK_EVT_STATE_PERIODIC,
	CLOCK_EVT_STATE_ONESHOT,
	CLOCK_EVT_STATE_ONESHOT_STOPPED,
};

/*
 * Clock event features
 */
/*
说明该硬件timer可以产生周期性的clock event
如果只是支持周期性的clock event，是无法模拟one shot类型的event。

*/
# define CLOCK_EVT_FEAT_PERIODIC	0x000001
/*
说明只能产生one shot类型的clock event
如果能产生one shot类型的event，那么即便是硬件不支持周期性的clock event，
上层的软件可以通过不断设定next event的方法来模拟周期性的clock event。
*/
# define CLOCK_EVT_FEAT_ONESHOT		0x000002
/*
一般的timer硬件都是用cycle值设定会比较方便，当然，不排除有些奇葩可以直接使用ktime（秒、纳秒），
这时候clock event device的features成员要打上CLOCK_EVT_FEAT_KTIME的标记
*/
# define CLOCK_EVT_FEAT_KTIME		0x000004

/*
 * x86(64) specific (mis)features:
 *
 * - Clockevent source stops in C3 State and needs broadcast support.
 * - Local APIC timer is used as a dummy device.
 */
/*
内核中有一个模块叫做cpuidle framework，当没有任务做的时候，cpu会进入idle状态。
这种CPU的sleep state叫做C-states，有C1/C2…Cn种states（具体多少种和CPU设计相关），
当然不同的状态是在功耗和唤醒时间上进行平衡，CPU睡的越浅，功耗越大，但是能够很快的唤醒。
一般而言，在sleep state的CPU可以被local timer唤醒，但是，当CPU进入某个深度睡眠状态的时候，
停止了local timer的运作，这时候，local timer将无法唤醒CPU了。

为了让系统可以继续运作，传说中tick broadcast framework粉墨登场了。
struct clock_event_device中的broadcast这个callback函数是和clock event广播有关。
在per CPU 的local timer硬件无法正常运作的时候，
需要一个独立于各个CPU的timer硬件来作为broadcast clock event device。
在这种情况下，它可以将clock event广播到所有的CPU core，以此推动各个CPU core上的tick device的运作。
*/
# define CLOCK_EVT_FEAT_C3STOP		0x000008
# define CLOCK_EVT_FEAT_DUMMY		0x000010

/*
 * Core shall set the interrupt affinity dynamically in broadcast mode
 */
# define CLOCK_EVT_FEAT_DYNIRQ		0x000020
# define CLOCK_EVT_FEAT_PERCPU		0x000040

/*
 * Clockevent device is based on a hrtimer for broadcast
 */
# define CLOCK_EVT_FEAT_HRTIMER		0x000080

/**
 * struct clock_event_device - clock event device descriptor
 * @event_handler:	Assigned by the framework to be called by the low
 *			level handler of the event source
 * @set_next_event:	set next event function using a clocksource delta
 * @set_next_ktime:	set next event function using a direct ktime value
 * @next_event:		local storage for the next event in oneshot mode
 * @max_delta_ns:	maximum delta value in ns
 * @min_delta_ns:	minimum delta value in ns
 * @mult:		nanosecond to cycles multiplier
 * @shift:		nanoseconds to cycles divisor (power of two)
 * @state_use_accessors:current state of the device, assigned by the core code
 * @features:		features
 * @retries:		number of forced programming retries
 * @set_state_periodic:	switch state to periodic
 * @set_state_oneshot:	switch state to oneshot
 * @set_state_oneshot_stopped: switch state to oneshot_stopped
 * @set_state_shutdown:	switch state to shutdown
 * @tick_resume:	resume clkevt device
 * @broadcast:		function to broadcast events
 * @min_delta_ticks:	minimum delta value in ticks stored for reconfiguration
 * @max_delta_ticks:	maximum delta value in ticks stored for reconfiguration
 * @name:		ptr to clock event name
 * @rating:		variable to rate clock event devices
 * @irq:		IRQ number (only for non CPU local devices)
 * @bound_on:		Bound on CPU
 * @cpumask:		cpumask to indicate for which CPUs this device works
 * @list:		list head for the management code
 * @owner:		module reference
 */
struct clock_event_device {
	void			(*event_handler)(struct clock_event_device *);
	int			(*set_next_event)(unsigned long evt, struct clock_event_device *);
	int			(*set_next_ktime)(ktime_t expires, struct clock_event_device *);
/*
	下一次要触发event的时间点信息
*/
	ktime_t			next_event;
/*
	设定next event触发的最大时间值。这个最大值是和硬件counter的bit数目有关：
	如果一个硬件timer最大能表示60秒的时间长度，那么设定65秒后触发clock event是没有意义的，
	因为这时候counter会溢出，如果强行设定那么硬件实际会在5秒后触发event。
	在注册clock event device的时候设定这个参数
*/
	u64			max_delta_ns;
/*
    在逻辑思维的世界里，你可以想像任意小的时间片段，但是在实现面，这个要收到timer硬件能力的限制。
    一个输入频率是1Hz的timer硬件，其最小时间粒度就是1秒，如何产生0.1秒的clock event呢？
    因此，所谓立刻产出也就是在硬件允许的最小的时间点上产生event。
    在注册clock event device的时候设定这个参数
*/
	u64			min_delta_ns;
	u32			mult;
	u32			shift;
	enum clock_event_state	state_use_accessors;
/*
    底层的硬件驱动知道自己的能力，可以通过struct clock_event_device中的features成员宣称自己的功能：
*/
	unsigned int		features;
	unsigned long		retries;

	int			(*set_state_periodic)(struct clock_event_device *);
	int			(*set_state_oneshot)(struct clock_event_device *);
	int			(*set_state_oneshot_stopped)(struct clock_event_device *);
	int			(*set_state_shutdown)(struct clock_event_device *);
	int			(*tick_resume)(struct clock_event_device *);

	void			(*broadcast)(const struct cpumask *mask);
	void			(*suspend)(struct clock_event_device *);
	void			(*resume)(struct clock_event_device *);

    /*
         底层的硬件驱动需要设定max_delta_ticks和min_delta_ticks。需要注意的是这里的tick不是system tick，
         而是输入硬件timer的clock tick，其实就是可以设定最大和最小cycle数目。
         这个cycle数目除以freq就是时间值，单位是秒
    */
	unsigned long		min_delta_ticks;
	unsigned long		max_delta_ticks;

	const char		*name;
	int			rating;
	int			irq;
	int			bound_on;
/*
    在multi core的环境下，底层driver在调用该接口函数注册clock event设备之前就需要设定cpumask成员，
    毕竟一个timer硬件附着在哪一个cpu上底层硬件最清楚
    指明该设备为哪一个CPU工作，
*/
	const struct cpumask	*cpumask;
	struct list_head	list;
	struct module		*owner;
} ____cacheline_aligned;

/* Helpers to verify state of a clockevent device */
static inline bool clockevent_state_detached(struct clock_event_device *dev)
{
	return dev->state_use_accessors == CLOCK_EVT_STATE_DETACHED;
}

static inline bool clockevent_state_shutdown(struct clock_event_device *dev)
{
	return dev->state_use_accessors == CLOCK_EVT_STATE_SHUTDOWN;
}

static inline bool clockevent_state_periodic(struct clock_event_device *dev)
{
	return dev->state_use_accessors == CLOCK_EVT_STATE_PERIODIC;
}

static inline bool clockevent_state_oneshot(struct clock_event_device *dev)
{
	return dev->state_use_accessors == CLOCK_EVT_STATE_ONESHOT;
}

static inline bool clockevent_state_oneshot_stopped(struct clock_event_device *dev)
{
	return dev->state_use_accessors == CLOCK_EVT_STATE_ONESHOT_STOPPED;
}

/*
 * Calculate a multiplication factor for scaled math, which is used to convert
 * nanoseconds based values to clock ticks:
 *
 * clock_ticks = (nanoseconds * factor) >> shift.
 *
 * div_sc is the rearranged equation to calculate a factor from a given clock
 * ticks / nanoseconds ratio:
 *
 * factor = (clock_ticks << shift) / nanoseconds
 */
static inline unsigned long
div_sc(unsigned long ticks, unsigned long nsec, int shift)
{
	u64 tmp = ((u64)ticks) << shift;

	do_div(tmp, nsec);

	return (unsigned long) tmp;
}

/* Clock event layer functions */
extern u64 clockevent_delta2ns(unsigned long latch, struct clock_event_device *evt);
extern void clockevents_register_device(struct clock_event_device *dev);
extern int clockevents_unbind_device(struct clock_event_device *ced, int cpu);

extern void clockevents_config(struct clock_event_device *dev, u32 freq);
extern void clockevents_config_and_register(struct clock_event_device *dev,
					    u32 freq, unsigned long min_delta,
					    unsigned long max_delta);

extern int clockevents_update_freq(struct clock_event_device *ce, u32 freq);

static inline void
clockevents_calc_mult_shift(struct clock_event_device *ce, u32 freq, u32 maxsec)
{
	return clocks_calc_mult_shift(&ce->mult, &ce->shift, NSEC_PER_SEC, freq, maxsec);
}

extern void clockevents_suspend(void);
extern void clockevents_resume(void);

# ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
#  ifdef CONFIG_ARCH_HAS_TICK_BROADCAST
extern void tick_broadcast(const struct cpumask *mask);
#  else
#   define tick_broadcast	NULL
#  endif
extern int tick_receive_broadcast(void);
# endif

# if defined(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST) && defined(CONFIG_TICK_ONESHOT)
extern void tick_setup_hrtimer_broadcast(void);
extern int tick_check_broadcast_expired(void);
# else
static inline int tick_check_broadcast_expired(void) { return 0; }
static inline void tick_setup_hrtimer_broadcast(void) { }
# endif

#else /* !CONFIG_GENERIC_CLOCKEVENTS: */

static inline void clockevents_suspend(void) { }
static inline void clockevents_resume(void) { }
static inline int tick_check_broadcast_expired(void) { return 0; }
static inline void tick_setup_hrtimer_broadcast(void) { }

#endif /* !CONFIG_GENERIC_CLOCKEVENTS */

#define CLOCKEVENT_OF_DECLARE(name, compat, fn) \
	OF_DECLARE_1_RET(clkevt, name, compat, fn)

#ifdef CONFIG_CLKEVT_PROBE
extern int clockevent_probe(void);
#els
static inline int clockevent_probe(void) { return 0; }
#endif

#endif /* _LINUX_CLOCKCHIPS_H */
