/*  linux/include/linux/tick.h
 *
 *  This file contains the structure definitions for tick related functions
 *
 */
#ifndef _LINUX_TICK_H
#define _LINUX_TICK_H

#include <linux/clockchips.h>

#ifdef CONFIG_GENERIC_CLOCKEVENTS

enum tick_device_mode {
	TICKDEV_MODE_PERIODIC,
	TICKDEV_MODE_ONESHOT,
};

struct tick_device {
	struct clock_event_device *evtdev;
	enum tick_device_mode mode;
};

enum tick_nohz_mode {
	/* ������ʱ�Ӵ��ڻ״̬��NOHZδ��Ч */
	NOHZ_MODE_INACTIVE,
	/* ��̬tick�����ڵ;���ģʽ */
	NOHZ_MODE_LOWRES,
	/* ��̬tick�����ڸ߾���ģʽ */
	NOHZ_MODE_HIGHRES,
};

/**
 * struct tick_sched - sched tick emulation and no idle tick control/stats
 * @sched_timer:	hrtimer to schedule the periodic tick in high
 *			resolution mode
 * @idle_tick:		Store the last idle tick expiry time when the tick
 *			timer is modified for idle sleeps. This is necessary
 *			to resume the tick timer operation in the timeline
 *			when the CPU returns from idle
 * @tick_stopped:	Indicator that the idle tick has been stopped
 * @idle_jiffies:	jiffies at the entry to idle for idle time accounting
 * @idle_calls:		Total number of idle calls
 * @idle_sleeps:	Number of idle calls, where the sched tick was stopped
 * @idle_entrytime:	Time when the idle call was entered
 * @idle_sleeptime:	Sum of the time slept in idle with sched tick stopped
 * @sleep_length:	Duration of the current idle sleep
 */
/* ��̬ʱ�� */
struct tick_sched {
	/* �߾���ģʽ�£�����������tick�Ķ�ʱ�� */
	struct hrtimer			sched_timer;
	unsigned long			check_clocks;
	/* ����ģʽ */
	enum tick_nohz_mode		nohz_mode;
	/* ����idleǰ����һ��ʱ�ӵĵ���ʱ�� */
	ktime_t				idle_tick;
	/* ������ʱ���Ƿ��Ѿ�ͣ�� */
	int				tick_stopped;
	/* ����IDLEʱ��jiffies */
	unsigned long			idle_jiffies;
	/* ��ͼͣ��������ʱ�ӵĴ��� */
	unsigned long			idle_calls;
	/* ͣ��������ʱ�ӵĴ��� */
	unsigned long			idle_sleeps;
	ktime_t				idle_entrytime;
	/* ��һ�ν�������ʱ�ӵ�׼ȷʱ�� */
	ktime_t				idle_sleeptime;
	/* ����������ʱ�ӵĳ��� */
	ktime_t				sleep_length;
	unsigned long			last_jiffies;
	unsigned long			next_jiffies;
	/* ��һ������ʱ�� */
	ktime_t				idle_expires;
};

extern void __init tick_init(void);
extern int tick_is_oneshot_available(void);
extern struct tick_device *tick_get_device(int cpu);

# ifdef CONFIG_HIGH_RES_TIMERS
extern int tick_init_highres(void);
extern int tick_program_event(ktime_t expires, int force);
extern void tick_setup_sched_timer(void);
extern void tick_cancel_sched_timer(int cpu);
# else
static inline void tick_cancel_sched_timer(int cpu) { }
# endif /* HIGHRES */

# ifdef CONFIG_GENERIC_CLOCKEVENTS_BROADCAST
extern struct tick_device *tick_get_broadcast_device(void);
extern cpumask_t *tick_get_broadcast_mask(void);

#  ifdef CONFIG_TICK_ONESHOT
extern cpumask_t *tick_get_broadcast_oneshot_mask(void);
#  endif

# endif /* BROADCAST */

# ifdef CONFIG_TICK_ONESHOT
extern void tick_clock_notify(void);
extern int tick_check_oneshot_change(int allow_nohz);
extern struct tick_sched *tick_get_tick_sched(int cpu);
# else
static inline void tick_clock_notify(void) { }
static inline int tick_check_oneshot_change(int allow_nohz) { return 0; }
# endif

#else /* CONFIG_GENERIC_CLOCKEVENTS */
static inline void tick_init(void) { }
static inline void tick_cancel_sched_timer(int cpu) { }
static inline void tick_clock_notify(void) { }
static inline int tick_check_oneshot_change(int allow_nohz) { return 0; }
#endif /* !CONFIG_GENERIC_CLOCKEVENTS */

# ifdef CONFIG_NO_HZ
extern void tick_nohz_stop_sched_tick(void);
extern void tick_nohz_restart_sched_tick(void);
extern void tick_nohz_update_jiffies(void);
extern ktime_t tick_nohz_get_sleep_length(void);
# else
static inline void tick_nohz_stop_sched_tick(void) { }
static inline void tick_nohz_restart_sched_tick(void) { }
static inline void tick_nohz_update_jiffies(void) { }
static inline ktime_t tick_nohz_get_sleep_length(void)
{
	ktime_t len = { .tv64 = NSEC_PER_SEC/HZ };

	return len;
}
# endif /* !NO_HZ */

#endif
