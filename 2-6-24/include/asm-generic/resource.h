#ifndef _ASM_GENERIC_RESOURCE_H
#define _ASM_GENERIC_RESOURCE_H

/*
 * Resource limit IDs
 *
 * ( Compatibility detail: there are architectures that have
 *   a different rlimit ID order in the 5-9 range and want
 *   to keep that order for binary compatibility. The reasons
 *   are historic and all new rlimits are identical across all
 *   arches. If an arch has such special order for some rlimits
 *   then it defines them prior including asm-generic/resource.h. )
 */

/* �������������CPUʱ�� */
#define RLIMIT_CPU		0	/* CPU time in sec */
/* ���������ļ����� */
#define RLIMIT_FSIZE		1	/* Maximum filesize */
/* ���ݶε���󳤶� */
#define RLIMIT_DATA		2	/* max data size */
/* �û�ջ����󳤶� */
#define RLIMIT_STACK		3	/* max stack size */
/* �ڴ�ת���ļ�����󳤶� */
#define RLIMIT_CORE		4	/* max core file size */

#ifndef RLIMIT_RSS
/* ���פ�ڴ泤�ȣ�Ŀǰδ�� */
# define RLIMIT_RSS		5	/* max resident set size */
#endif

#ifndef RLIMIT_NPROC
/* �û�����ӵ�е���������Ŀ��Ĭ��Ϊmax_threads/2��max_threadsֵ�ɿ����ڴ������ */
# define RLIMIT_NPROC		6	/* max number of processes */
#endif

#ifndef RLIMIT_NOFILE
/* ���ļ��������ƣ�Ĭ��Ϊ1024 */
# define RLIMIT_NOFILE		7	/* max number of open files */
#endif

#ifndef RLIMIT_MEMLOCK
/* ���ɻ���ҳ�������Ŀ */
# define RLIMIT_MEMLOCK		8	/* max locked-in-memory address space */
#endif

#ifndef RLIMIT_AS
/* ���̵�ַ�ռ����� */
# define RLIMIT_AS		9	/* address space limit */
#endif

/* �ļ����������Ŀ */
#define RLIMIT_LOCKS		10	/* maximum file locks held */
/* δ���źŵ�������� */
#define RLIMIT_SIGPENDING	11	/* max number of pending signals */
/* POSIX��Ϣ���е�������������ֽ�Ϊ��λ */
#define RLIMIT_MSGQUEUE		12	/* maximum bytes in POSIX mqueues */
/* ��ʵʱ���̵�������ȼ� */
#define RLIMIT_NICE		13	/* max nice prio allowed to raise to
					   0-39 for nice level 19 .. -20 */
/* ʵʱ���̵�������ȼ� */
#define RLIMIT_RTPRIO		14	/* maximum realtime priority */

#define RLIM_NLIMITS		15

/*
 * SuS says limits have to be unsigned.
 * Which makes a ton more sense anyway.
 *
 * Some architectures override this (for compatibility reasons):
 */
#ifndef RLIM_INFINITY
/* ��Ӧ����Դδ������ */
# define RLIM_INFINITY		(~0UL)
#endif

/*
 * RLIMIT_STACK default maximum - some architectures override it:
 */
#ifndef _STK_LIM_MAX
# define _STK_LIM_MAX		RLIM_INFINITY
#endif

#ifdef __KERNEL__

/*
 * boot-time rlimit defaults for the init task:
 */
/**
 * INIT���̵���Դ����
 */
#define INIT_RLIMITS							\
{									\
	[RLIMIT_CPU]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_FSIZE]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_DATA]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_STACK]		= {       _STK_LIM,   _STK_LIM_MAX },	\
	[RLIMIT_CORE]		= {              0,  RLIM_INFINITY },	\
	[RLIMIT_RSS]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_NPROC]		= {              0,              0 },	\
	[RLIMIT_NOFILE]		= {       INR_OPEN,       INR_OPEN },	\
	[RLIMIT_MEMLOCK]	= {    MLOCK_LIMIT,    MLOCK_LIMIT },	\
	[RLIMIT_AS]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_LOCKS]		= {  RLIM_INFINITY,  RLIM_INFINITY },	\
	[RLIMIT_SIGPENDING]	= { 		0,	       0 },	\
	[RLIMIT_MSGQUEUE]	= {   MQ_BYTES_MAX,   MQ_BYTES_MAX },	\
	[RLIMIT_NICE]		= { 0, 0 },				\
	[RLIMIT_RTPRIO]		= { 0, 0 },				\
}

#endif	/* __KERNEL__ */

#endif
