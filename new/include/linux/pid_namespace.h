/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PID_NS_H
#define _LINUX_PID_NS_H

#include <linux/sched.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <linux/workqueue.h>
#include <linux/threads.h>
#include <linux/nsproxy.h>
#include <linux/kref.h>
#include <linux/ns_common.h>
#include <linux/idr.h>


struct fs_pin;

enum { /* definitions for pid_namespace's hide_pid field */
	HIDEPID_OFF	  = 0,
	HIDEPID_NO_ACCESS = 1,
	HIDEPID_INVISIBLE = 2,
};

struct pid_namespace {
    /*
        被nsproxy 引用计数
    */
	struct kref kref;
	struct idr idr;
	struct rcu_head rcu;
	unsigned int pid_allocated;
	/*
	当前命名空间的init进程，每个命名空间都有一个作用相当于全局init进程的进程
	*/
	struct task_struct *child_reaper;
	/*
      为命名空间分配pid 的slab 缓存
	*/
	struct kmem_cache *pid_cachep;
	/*
	当前命名空间的等级，初始命名空间的level为0，
	它的子命名空间level为1，依次递增，
	而且子命名空间对父命名空间是可见的
	从给定的level设置，内核即可推断进程会关联到多少个ID
	*/
	unsigned int level;
	/*父命名空间*/
	struct pid_namespace *parent;
#ifdef CONFIG_PROC_FS
	struct vfsmount *proc_mnt;
	struct dentry *proc_self;
	struct dentry *proc_thread_self;
#endif
#ifdef CONFIG_BSD_PROCESS_ACCT
	struct fs_pin *bacct;
#endif
	struct user_namespace *user_ns;
	struct ucounts *ucounts;
	struct work_struct proc_work;
	kgid_t pid_gid;
	int hide_pid;
	int reboot;	/* group exit code if this pidns was rebooted */
	struct ns_common ns;
} __randomize_layout;

extern struct pid_namespace init_pid_ns;

#define PIDNS_ADDING (1U << 31)

#ifdef CONFIG_PID_NS
static inline struct pid_namespace *get_pid_ns(struct pid_namespace *ns)
{
	if (ns != &init_pid_ns)
		kref_get(&ns->kref);
	return ns;
}

extern struct pid_namespace *copy_pid_ns(unsigned long flags,
	struct user_namespace *user_ns, struct pid_namespace *ns);
extern void zap_pid_ns_processes(struct pid_namespace *pid_ns);
extern int reboot_pid_ns(struct pid_namespace *pid_ns, int cmd);
extern void put_pid_ns(struct pid_namespace *ns);

#else /* !CONFIG_PID_NS */
#include <linux/err.h>

static inline struct pid_namespace *get_pid_ns(struct pid_namespace *ns)
{
	return ns;
}

static inline struct pid_namespace *copy_pid_ns(unsigned long flags,
	struct user_namespace *user_ns, struct pid_namespace *ns)
{
	if (flags & CLONE_NEWPID)
		ns = ERR_PTR(-EINVAL);
	return ns;
}

static inline void put_pid_ns(struct pid_namespace *ns)
{
}

static inline void zap_pid_ns_processes(struct pid_namespace *ns)
{
	BUG();
}

static inline int reboot_pid_ns(struct pid_namespace *pid_ns, int cmd)
{
	return 0;
}
#endif /* CONFIG_PID_NS */

extern struct pid_namespace *task_active_pid_ns(struct task_struct *tsk);
void pidhash_init(void);
void pid_idr_init(void);

#endif /* _LINUX_PID_NS_H */
