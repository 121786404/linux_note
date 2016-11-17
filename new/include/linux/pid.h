#ifndef _LINUX_PID_H
#define _LINUX_PID_H

#include <linux/rcupdate.h>

enum pid_type
{
    /*
    pid是 Linux 中在其命名空间中唯一标识进程而分配给它的一个号码，称做进程ID号，
    简称PID。在使用 fork 或 clone 系统调用时产生的进程均会由内核分配一个新的唯一的PID值
    */
	PIDTYPE_PID,
	/*
    独立的进程可以组成进程组（使用setpgrp系统调用），
    进程组可以简化向所有组内进程发送信号的操作
    例如用管道连接的进程处在同一进程组内。
    进程组ID叫做PGID，进程组内的所有进程都有相同的PGID，等于该组组长的PID
	*/
	PIDTYPE_PGID,
	/*
    几个进程组可以合并成一个会话组（使用setsid系统调用），
    可以用于终端程序设计。会话组中所有进程都有相同的SID,
    保存在task_struct的session成员中
	*/
	PIDTYPE_SID,
	PIDTYPE_MAX
};

/*
 * What is struct pid?
 *
 * A struct pid is the kernel's internal notion of a process identifier.
 * It refers to individual tasks, process groups, and sessions.  While
 * there are processes attached to it the struct pid lives in a hash
 * table, so it and then the processes that it refers to can be found
 * quickly from the numeric pid value.  The attached processes may be
 * quickly accessed by following pointers from struct pid.
 *
 * Storing pid_t values in the kernel and referring to them later has a
 * problem.  The process originally with that pid may have exited and the
 * pid allocator wrapped, and another process could have come along
 * and been assigned that pid.
 *
 * Referring to user space processes by holding a reference to struct
 * task_struct has a problem.  When the user space process exits
 * the now useless task_struct is still kept.  A task_struct plus a
 * stack consumes around 10K of low kernel memory.  More precisely
 * this is THREAD_SIZE + sizeof(struct task_struct).  By comparison
 * a struct pid is about 64 bytes.
 *
 * Holding a reference to struct pid solves both of these problems.
 * It is small so holding a reference does not consume a lot of
 * resources, and since a new struct pid is allocated when the numeric pid
 * value is reused (when pids wrap around) we don't mistakenly refer to new
 * processes.
 */


/*
 * struct upid is used to get the id of the struct pid, as it is
 * seen in particular namespace. Later the struct pid is found with
 * find_pid_ns() using the int nr and struct pid_namespace *ns.
 */
/*
upid 用来关联PID和命名空间的结构体
正是命名空间的作用，使得进程能够分配到不同的命名空间中去，
而且进程在自己所在的命名空间中有着局部的PID，这就是局部PID，
因此各个命名空间的PID有可能重复，也即是一个PID可能为多个进程使用，
*/
struct upid {
	/* Try to keep pid_chain in the same cacheline as nr for find_vpid */
	int nr; // ID具体的值
	struct pid_namespace *ns; // 指向命名空间的指针
	struct hlist_node pid_chain; // 指向PID哈希列表的指针，用于关联对于的PID
};

struct pid
{
	atomic_t count;  // 使用该PID的task的数目
	unsigned int level; // 该PID的命名空间的数目，也就是包含该进程的命名空间的深度
	/* 使用该pid的进程的列表,每个数组项都是一个散列表头,分别对应以下三种类型 
	lists of tasks that use this pid */
	/*
    tasks是一个数组，每个数组项都是一个散列表头，
    对应于一个ID类型,PIDTYPE_PID, PIDTYPE_PGID, PIDTYPE_SID（ PIDTYPE_MAX表示ID类型的数目）
    这样做是必要的，因为一个ID可能用于几个进程。
    所有共享同一给定ID的task_struct实例，都通过该列表连接起来。
	*/
	struct hlist_head tasks[PIDTYPE_MAX];
	struct rcu_head rcu;
	/*
        一个upid的实例数组，每个数组项代表一个命名空间，
        用来表示一个PID可以属于不同的命名空间，
        该元素放在末尾，可以向数组添加附加的项
	*/
	struct upid numbers[1];
};

extern struct pid init_struct_pid;

struct pid_link
{
	struct hlist_node node;
	struct pid *pid;
};

static inline struct pid *get_pid(struct pid *pid)
{
	if (pid)
		atomic_inc(&pid->count);
	return pid;
}

extern void put_pid(struct pid *pid);
extern struct task_struct *pid_task(struct pid *pid, enum pid_type);
extern struct task_struct *get_pid_task(struct pid *pid, enum pid_type);

extern struct pid *get_task_pid(struct task_struct *task, enum pid_type type);

/*
 * these helpers must be called with the tasklist_lock write-held.
 */
extern void attach_pid(struct task_struct *task, enum pid_type);
extern void detach_pid(struct task_struct *task, enum pid_type);
extern void change_pid(struct task_struct *task, enum pid_type,
			struct pid *pid);
extern void transfer_pid(struct task_struct *old, struct task_struct *new,
			 enum pid_type);

struct pid_namespace;
extern struct pid_namespace init_pid_ns;

/*
 * look up a PID in the hash table. Must be called with the tasklist_lock
 * or rcu_read_lock() held.
 *
 * find_pid_ns() finds the pid in the namespace specified
 * find_vpid() finds the pid by its virtual id, i.e. in the current namespace
 *
 * see also find_task_by_vpid() set in include/linux/sched.h
 */
extern struct pid *find_pid_ns(int nr, struct pid_namespace *ns);
extern struct pid *find_vpid(int nr);

/*
 * Lookup a PID in the hash table, and return with it's count elevated.
 */
extern struct pid *find_get_pid(int nr);
extern struct pid *find_ge_pid(int nr, struct pid_namespace *);
int next_pidmap(struct pid_namespace *pid_ns, unsigned int last);

extern struct pid *alloc_pid(struct pid_namespace *ns);
extern void free_pid(struct pid *pid);
extern void disable_pid_allocation(struct pid_namespace *ns);

/*
 * ns_of_pid() returns the pid namespace in which the specified pid was
 * allocated.
 *
 * NOTE:
 * 	ns_of_pid() is expected to be called for a process (task) that has
 * 	an attached 'struct pid' (see attach_pid(), detach_pid()) i.e @pid
 * 	is expected to be non-NULL. If @pid is NULL, caller should handle
 * 	the resulting NULL pid-ns.
 */
static inline struct pid_namespace *ns_of_pid(struct pid *pid)
{
	struct pid_namespace *ns = NULL;
	if (pid)
		ns = pid->numbers[pid->level].ns;
	return ns;
}

/*
 * is_child_reaper returns true if the pid is the init process
 * of the current namespace. As this one could be checked before
 * pid_ns->child_reaper is assigned in copy_process, we check
 * with the pid number.
 */
static inline bool is_child_reaper(struct pid *pid)
{
	return pid->numbers[pid->level].nr == 1;
}

/*
 * the helpers to get the pid's id seen from different namespaces
 *
 * pid_nr()    : global id, i.e. the id seen from the init namespace;
 * pid_vnr()   : virtual id, i.e. the id seen from the pid namespace of
 *               current.
 * pid_nr_ns() : id seen from the ns specified.
 *
 * see also task_xid_nr() etc in include/linux/sched.h
 */

static inline pid_t pid_nr(struct pid *pid)
{
	pid_t nr = 0;
	if (pid)
		nr = pid->numbers[0].nr;
	return nr;
}

pid_t pid_nr_ns(struct pid *pid, struct pid_namespace *ns);
pid_t pid_vnr(struct pid *pid);

#define do_each_pid_task(pid, type, task)				\
	do {								\
		if ((pid) != NULL)					\
			hlist_for_each_entry_rcu((task),		\
				&(pid)->tasks[type], pids[type].node) {

			/*
			 * Both old and new leaders may be attached to
			 * the same pid in the middle of de_thread().
			 */
#define while_each_pid_task(pid, type, task)				\
				if (type == PIDTYPE_PID)		\
					break;				\
			}						\
	} while (0)

#define do_each_pid_thread(pid, type, task)				\
	do_each_pid_task(pid, type, task) {				\
		struct task_struct *tg___ = task;			\
		do {

#define while_each_pid_thread(pid, type, task)				\
		} while_each_thread(tg___, task);			\
		task = tg___;						\
	} while_each_pid_task(pid, type, task)
#endif /* _LINUX_PID_H */
