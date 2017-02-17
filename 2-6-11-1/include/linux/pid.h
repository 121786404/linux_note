#ifndef _LINUX_PID_H
#define _LINUX_PID_H

enum pid_type
{
	PIDTYPE_PID,
	PIDTYPE_TGID,    /* 代表一组线程组 */
	PIDTYPE_PGID,     /* 代表一组进程描述符 */
	PIDTYPE_SID,        /* 代表一个会话的描述符 */
	PIDTYPE_MAX
};

/* 进程的pid结构 */
struct pid
{
	/* Try to keep pid_chain in the same cacheline as nr for find_pid */
	int nr;           /* 对应进程号 */
	struct hlist_node pid_chain;              /* pid对应的hash链 */
	/* list of pids with the same nr, only one of them is in the hash */
         /* 将通过type和nr映射到pid_hash中同一个位置的struct pid用pid_list连接起来 */
	struct list_head pid_list;       
};

#define pid_task(elem, type) \
	list_entry(elem, struct task_struct, pids[type].pid_list)

/*
 * attach_pid() and detach_pid() must be called with the tasklist_lock
 * write-held.
 */
extern int FASTCALL(attach_pid(struct task_struct *task, enum pid_type type, int nr));

extern void FASTCALL(detach_pid(struct task_struct *task, enum pid_type));

/*
 * look up a PID in the hash table. Must be called with the tasklist_lock
 * held.
 */
extern struct pid *FASTCALL(find_pid(enum pid_type, int));

extern int alloc_pidmap(void);
extern void FASTCALL(free_pidmap(int));
extern void switch_exec_pids(struct task_struct *leader, struct task_struct *thread);

#define do_each_task_pid(who, type, task)				\
	if ((task = find_task_by_pid_type(type, who))) {		\
		prefetch((task)->pids[type].pid_list.next);		\
		do {

#define while_each_task_pid(who, type, task)				\
		} while (task = pid_task((task)->pids[type].pid_list.next,\
						type),			\
			prefetch((task)->pids[type].pid_list.next),	\
			hlist_unhashed(&(task)->pids[type].pid_chain));	\
	}								\

#endif /* _LINUX_PID_H */
