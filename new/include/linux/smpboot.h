/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SMPBOOT_H
#define _LINUX_SMPBOOT_H

#include <linux/types.h>

struct task_struct;
/* Cookie handed to the thread_fn*/
struct smpboot_thread_data;

/**
 * struct smp_hotplug_thread - CPU hotplug related thread descriptor
 * @store:		Pointer to per cpu storage for the task pointers
 * @list:		List head for core management
 * @thread_should_run:	Check whether the thread should run or not. Called with
 *			preemption disabled.
 * @thread_fn:		The associated thread function
 * @create:		Optional setup function, called when the thread gets
 *			created (Not called from the thread context)
 * @setup:		Optional setup function, called when the thread gets
 *			operational the first time
 * @cleanup:		Optional cleanup function, called when the thread
 *			should stop (module exit)
 * @park:		Optional park function, called when the thread is
 *			parked (cpu offline)
 * @unpark:		Optional unpark function, called when the thread is
 *			unparked (cpu online)
 * @selfparking:	Thread is not parked by the park function.
 * @thread_comm:	The base name of the thread
 */
struct smp_hotplug_thread {
	struct task_struct __percpu	**store;
	struct list_head		list;
	int				(*thread_should_run)(unsigned int cpu);
	void				(*thread_fn)(unsigned int cpu);
	void				(*create)(unsigned int cpu);
	void				(*setup)(unsigned int cpu);
	void				(*cleanup)(unsigned int cpu, bool online);
	void				(*park)(unsigned int cpu);
	void				(*unpark)(unsigned int cpu);
	bool				selfparking;
	const char			*thread_comm;
};

/*
创建 per_cpu 内核进程，所谓的 per_cpu 进程是指需要在每个 
online cpu 上创建线程

既然 per_cpu 进程是和 cpu 绑定的，那么在 cpu hotplug 的时候，
设置进程的 cpu 亲和力 set_cpus_allowed_ptr()。

缺点是进程绑定的 cpu 如果被 down 掉，
进程会迁移到其他 cpu 继续执行。

为了克服上述方案的缺点，适配 per_cpu 进程的 cpu hotplug 操作，
设计了 kthread_park()/kthread_unpark() 机制。
*/

int smpboot_register_percpu_thread(struct smp_hotplug_thread *plug_thread);

void smpboot_unregister_percpu_thread(struct smp_hotplug_thread *plug_thread);

#endif
