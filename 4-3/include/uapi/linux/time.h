#ifndef _UAPI_LINUX_TIME_H
#define _UAPI_LINUX_TIME_H

#include <linux/types.h>


#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
/**
 * ���뾫�ȵ�ʱ��
 * ����POSIX��׼
 */
struct timespec {
	__kernel_time_t	tv_sec;			/* seconds */
	long		tv_nsec;		/* nanoseconds */
};
#endif

/**
 * ΢�뾫�ȵ�ʱ��
 */
struct timeval {
	__kernel_time_t		tv_sec;		/* seconds */
	__kernel_suseconds_t	tv_usec;	/* microseconds */
};

struct timezone {
	int	tz_minuteswest;	/* minutes west of Greenwich */
	int	tz_dsttime;	/* type of dst correction */
};


/*
 * Names of the interval timers, and structure
 * defining a timer setting:
 */
/**
 * real-time
 * ����CLOCK_REALTIME��ʱ����ʱ����SIGALRM�ź�
 * ��alarm����һ��
 */
#define	ITIMER_REAL		0
/**
 * ֻ�е��ý��̵��û��ռ����ִ�е�ʱ��ż�ʱ
 * ��ʱ����SIGVTALRM�ź�
 */
#define	ITIMER_VIRTUAL		1
/**
 * ֻ�иý���ִ�е�ʱ��ż�ʱ��������ִ���û��ռ���뻹�������ں�ִ�У�����ϵͳ���ã�
 * ��ʱ����SIGPROF�źš�
 */
#define	ITIMER_PROF		2

struct itimerspec {
	struct timespec it_interval;	/* timer period */
	struct timespec it_value;	/* timer expiration */
};

/**
 * getitimer�Ķ�ʱ��ֵ
 */
struct itimerval {
	/**
	 * ���ʱ��
	 */
	struct timeval it_interval;	/* timer interval */
	/**
	 * �´ζ�ʱ����ʼʱ��
	 */
	struct timeval it_value;	/* current value */
};

/*
 * The IDs of the various system clocks (for POSIX.1b interval timers):
 */
/**
 * ��ʵ�����ʱ�ӣ���ǽ��ʱ��
 * ���ԶԸ�ϵͳʱ�ӽ����޸ģ�������������ʱ���ϵ㡣
 * Ҳ����ͨ��NTP�Ը�ʱ�ӽ��е���
 */
#define CLOCK_REALTIME			0
/**
 * ��ʵ�����ʱ�ӣ�����������
 * �����ֶ����������ǿ���ͨ��NTPЭ����е���
 * ���׼�㲻һ����linux epoch
 * һ����ϵͳ������ʱ����趨Ϊ���׼��
 */
#define CLOCK_MONOTONIC			1
/**
 * ���ڽ��̻����߳�ִ��ʱ���������ʱ��
 * �ο�clock_getcpuclockid
 */
#define CLOCK_PROCESS_CPUTIME_ID	2
#define CLOCK_THREAD_CPUTIME_ID		3
/**
 * ��CLOCK_MONOTONIC����
 * ���ǲ�����NTP������е���
 * ����ʱ����Ϊ0
 */
#define CLOCK_MONOTONIC_RAW		4
/**
 * CLOCK_REALTIME_COARSE��CLOCK_MONOTONIC_COARSE�ĸ����CLOCK_REALTIME��CLOCK_MONOTONIC����
 * ���Ǿ����ǱȽϴֵİ汾��
 */
#define CLOCK_REALTIME_COARSE		5
#define CLOCK_MONOTONIC_COARSE		6
/**
 * ��CLOCK_MONOTONIC���ƣ�Ҳ�ǵ�������
 * ��ϵͳ��ʼ����ʱ���趨�Ļ�׼��ֵ��0
 * ����CLOCK_BOOTTIME����ϵͳsuspend��ʱ��
 */
#define CLOCK_BOOTTIME			7
/**
 * ��Ҫ����Alarmtimer������timer�ǻ���RTC��
 */
#define CLOCK_REALTIME_ALARM		8
#define CLOCK_BOOTTIME_ALARM		9
#define CLOCK_SGI_CYCLE			10	/* Hardware specific */
/**
 * ԭ���ӵ�ʱ��
 * �ͻ���UTC��CLOCK_REALTIME���ƣ�����û������
 */
#define CLOCK_TAI			11

#define MAX_CLOCKS			16
#define CLOCKS_MASK			(CLOCK_REALTIME | CLOCK_MONOTONIC)
#define CLOCKS_MONO			CLOCK_MONOTONIC

/*
 * The various flags for setting POSIX.1b interval timers:
 */
#define TIMER_ABSTIME			0x01

#endif /* _UAPI_LINUX_TIME_H */
