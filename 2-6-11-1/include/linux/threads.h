#ifndef _LINUX_THREADS_H
#define _LINUX_THREADS_H

#include <linux/config.h>

/*
 * The default limit for the nr of threads is now in
 * /proc/sys/kernel/threads-max.
 */
 
/*
 * Maximum supported processors that can run under SMP.  This value is
 * set via configure setting.  The maximum is equal to the size of the
 * bitmasks used on that platform, i.e. 32 or 64.  Setting this smaller
 * saves quite a bit of memory.
 */

/* 根据CONFIG_SMP宏来判断系统中到底有多少个处理器，默认情况下就是一个 */
#ifdef CONFIG_SMP
#define NR_CPUS		CONFIG_NR_CPUS
#else
#define NR_CPUS		1
#endif

#define MIN_THREADS_LEFT_FOR_ROOT 4

/*
 * This controls the default maximum pid allocated to a process
 */
/* 设置可以给进程分配的最大进程号 
 */
#define PID_MAX_DEFAULT 0x8000

/*
 * A maximum of 4 million PIDs should be enough for a while:
 */
/* 根据不同情况来确定系统中可以分配给进程的最大序号 */
#define PID_MAX_LIMIT (sizeof(long) > 4 ? 4*1024*1024 : PID_MAX_DEFAULT)

#endif
