#ifndef _LINUX_NSPROXY_H
#define _LINUX_NSPROXY_H

#include <linux/spinlock.h>
#include <linux/sched.h>

struct mnt_namespace;
struct uts_namespace;
struct ipc_namespace;
struct pid_namespace;
struct cgroup_namespace;
struct fs_struct;

/*
 * A structure to contain pointers to all per-process
 * namespaces - fs (mount), uts, network, sysvipc, etc.
 *
 * The pid namespace is an exception -- it's accessed using
 * task_active_pid_ns.  The pid namespace here is the
 * namespace that children will use.
 *
 * 'count' is the number of tasks holding a reference.
 * The count for each namespace, then, will be the number
 * of nsproxies pointing to it, not the number of tasks.
 *
 * The nsproxy is shared by tasks which share all namespaces.
 * As soon as a single namespace is cloned or unshared, the
 * nsproxy is copied.
 */
 /*
 虚拟化技术需要资源隔离，也就是说，
 不同的虚拟主机（实际上在一台物理主机上）资源是互相不可见的。
 因此，linux kernel增加了若干个name space，例如user name space、PID namespace、IPC namespace、uts namespace、network namespace等。
 以PID namespace为例，原来的linux kernel中，PID唯一的标识了一个process，
 在引入PID namespace之后，不同的namespace可以拥有同样的ID，
 也就是说，标识一个进程的是PID namespace ＋ PID。

 user namespace是linux kernel支持虚拟化之后引入的一个机制，
可以允许系统创建不同的user namespace（之前系统只有一个user namespace）。
user namespace用来管理user ID和group ID的映射。
一个user namespace形成一个container，
该user namespace的user ID和group ID的权限被限定在container内部。
也就是说，某一个user namespace中的root（UID等于0）并非具备任意的权限，
他仅仅是在该user namespace中是privileges的，
在该user namespace之外，该user并非是特权用户。

 Linux Namespaces机制本身就是为了实现 container based virtualizaiton开发的。
 它提供了一套轻量级、高效率的系统资源隔离方案，远比传统的虚拟化技术开销小，不过它也不是完美的，它为内核的开发带来了更多的复杂性，它在隔离性和容错性上跟传统的虚拟化技术比也还有差距。

 */
struct nsproxy {
	atomic_t count;
	/*
    UTS命名空间包含了运行内核的名称、版本、底层体系结构类型等信息。
    UTS是UNIX Timesharing System的简称
	*/
	struct uts_namespace *uts_ns;
	/*
    保存在struct ipc_namespace中的所有与进程间通信（IPC）有关的信息
	*/
	struct ipc_namespace *ipc_ns;
	/*
    已经装载的文件系统的视图，在struct mnt_namespace中给出
	*/
	struct mnt_namespace *mnt_ns;
	/*
    有关进程ID的信息，由struct pid_namespace提供
	*/
	struct pid_namespace *pid_ns_for_children;
	/*
        所有网络相关的命名空间参数
	*/
	struct net 	     *net_ns;
	struct cgroup_namespace *cgroup_ns;
};
extern struct nsproxy init_nsproxy;

/*
 * the namespaces access rules are:
 *
 *  1. only current task is allowed to change tsk->nsproxy pointer or
 *     any pointer on the nsproxy itself.  Current must hold the task_lock
 *     when changing tsk->nsproxy.
 *
 *  2. when accessing (i.e. reading) current task's namespaces - no
 *     precautions should be taken - just dereference the pointers
 *
 *  3. the access to other task namespaces is performed like this
 *     task_lock(task);
 *     nsproxy = task->nsproxy;
 *     if (nsproxy != NULL) {
 *             / *
 *               * work with the namespaces here
 *               * e.g. get the reference on one of them
 *               * /
 *     } / *
 *         * NULL task->nsproxy means that this task is
 *         * almost dead (zombie)
 *         * /
 *     task_unlock(task);
 *
 */

int copy_namespaces(unsigned long flags, struct task_struct *tsk);
void exit_task_namespaces(struct task_struct *tsk);
void switch_task_namespaces(struct task_struct *tsk, struct nsproxy *new);
void free_nsproxy(struct nsproxy *ns);
int unshare_nsproxy_namespaces(unsigned long, struct nsproxy **,
	struct cred *, struct fs_struct *);
int __init nsproxy_cache_init(void);

static inline void put_nsproxy(struct nsproxy *ns)
{
	if (atomic_dec_and_test(&ns->count)) {
		free_nsproxy(ns);
	}
}

static inline void get_nsproxy(struct nsproxy *ns)
{
	atomic_inc(&ns->count);
}

#endif
