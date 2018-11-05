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

struct pidmap {
       atomic_t nr_free;  // 表示还能分配的pid的数量
       void *page;        // 指向的是存放pid的物理页
};

#define BITS_PER_PAGE		(PAGE_SIZE * 8)
#define BITS_PER_PAGE_MASK	(BITS_PER_PAGE-1)
#define PIDMAP_ENTRIES		((PID_MAX_LIMIT+BITS_PER_PAGE-1)/BITS_PER_PAGE)

struct fs_pin;

struct pid_namespace {
    /*
        被nsproxy 引用计数
    */
	struct kref kref;
	/*
    pidmap结构体表示分配pid的位图。当需要分配一个新的pid时只需查找位图，
    找到bit为0的位置并置1，然后更新统计数据域（nr_free)
	*/
	struct pidmap pidmap[PIDMAP_ENTRIES];
	struct rcu_head rcu;
	/*
    上一次分配的pid
	*/
	int last_pid;
	unsigned int nr_hashed;
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
};

extern struct pid_namespace init_pid_ns;

#define PIDNS_HASH_ADDING (1U << 31)

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
void pidmap_init(void);

#endif /* _LINUX_PID_NS_H */
