/*
 * Generic pidhash and scalable, time-bounded PID allocator
 *
 * (C) 2002-2003 William Irwin, IBM
 * (C) 2004 William Irwin, Oracle
 * (C) 2002-2004 Ingo Molnar, Red Hat
 *
 * pid-structures are backing objects for tasks sharing a given ID to chain
 * against. There is very little to them aside from hashing them and
 * parking tasks using given ID's on a list.
 *
 * The hash is always changed with the tasklist_lock write-acquired,
 * and the hash is only accessed with the tasklist_lock at least
 * read-acquired, so there's no additional SMP locking needed here.
 *
 * We have a list of bitmap pages, which bitmaps represent the PID space.
 * Allocating and freeing PIDs is completely lockless. The worst-case
 * allocation scenario when all but one out of 1 million PIDs possible are
 * allocated already: the scanning of 32 list entries and at most PAGE_SIZE
 * bytes. The typical fastpath is a single successful setbit. Freeing is O(1).
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/hash.h>

#define pid_hashfn(nr) hash_long((unsigned long)nr, pidhash_shift)
/* pid的hash链表,总共最多有PIDTYPE_MAX个元素,
  * pid_hash就像是一个二维表，PIDTYPE_MAX表示有多少行 
  * pidhash_shift表示有2的pidhash_shift次方列，也就是有多少个元素，而元素的个数 
  * 是通过2的n次方来表示的，n就是pidhash_shift 
  */
static struct hlist_head *pid_hash[PIDTYPE_MAX];
static int pidhash_shift;

int pid_max = PID_MAX_DEFAULT;
/* 当前最后一个进程的pid，如果遇到pid_max则会重新循环 */
int last_pid;


/* 当在分配进程id时，如果超过了最大id号，则重新返回到这个地方 */
#define RESERVED_PIDS		300

int pid_max_min = RESERVED_PIDS + 1;
int pid_max_max = PID_MAX_LIMIT;

/* 取整页内存，需要总共多少页内存来标记进程pid  */
#define PIDMAP_ENTRIES		((PID_MAX_LIMIT + 8*PAGE_SIZE - 1)/PAGE_SIZE/8)
/* 获取每页内存中二进制位的数量 */
#define BITS_PER_PAGE		(PAGE_SIZE*8)
/* 一页内存有多少个二进制位 */
#define BITS_PER_PAGE_MASK	(BITS_PER_PAGE-1)
/* 根据页的地址和页内的偏移来生成进程的pid */
#define mk_pid(map, off)	(((map) - pidmap_array)*BITS_PER_PAGE + (off))
#define find_next_offset(map, off)					\
		find_next_zero_bit((map)->page, BITS_PER_PAGE, off)

/*
 * PID-map pages start out as NULL, they get allocated upon
 * first use and are never deallocated. This way a low pid_max
 * value does not cause lots of bitmaps to be allocated, but
 * the scheme scales to up to 4 million PIDs, runtime.
 */
typedef struct pidmap {
	atomic_t nr_free;  /* 一页物理内存当中的二进制位的空闲个数 */
	void *page;       /* 一页物理内存的地址 */
} pidmap_t;

/* 全部初始化没有分配进程pid */
static pidmap_t pidmap_array[PIDMAP_ENTRIES] =
	 { [ 0 ... PIDMAP_ENTRIES-1 ] = { ATOMIC_INIT(BITS_PER_PAGE), NULL } };

static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(pidmap_lock);

/* 释放经常pid对应的内存位图 */
fastcall void free_pidmap(int pid)
{
	pidmap_t *map = pidmap_array + pid / BITS_PER_PAGE;
	int offset = pid & BITS_PER_PAGE_MASK;

	clear_bit(offset, map->page);
	atomic_inc(&map->nr_free);
}

/* 分配一个进程的pid */
int alloc_pidmap(void)
{
	int i, offset, max_scan, pid, last = last_pid;
	pidmap_t *map;

	pid = last + 1;
	if (pid >= pid_max)
		pid = RESERVED_PIDS;
        /* 去pid在内存页中的偏移 */
	offset = pid & BITS_PER_PAGE_MASK;
	map = &pidmap_array[pid/BITS_PER_PAGE];
        /* 确定需要扫描多少页的内存 */
	max_scan = (pid_max + BITS_PER_PAGE - 1)/BITS_PER_PAGE - !offset;
	for (i = 0; i <= max_scan; ++i) {
		/* 此处的unlikely并没有起特别特殊的作用，仅仅是一个宏 */
		if (unlikely(!map->page)) {
			/* 获取一页内存 */
			unsigned long page = get_zeroed_page(GFP_KERNEL);
			/*
			 * Free the page if someone raced with us
			 * installing it:
			 */
			spin_lock(&pidmap_lock);
			if (map->page)
				free_page(page);
			else
				map->page = (void *)page;
			spin_unlock(&pidmap_lock);
			if (unlikely(!map->page))
				break;
		}
                /* 判断内存页中是否还有空闲二进制位 */
		if (likely(atomic_read(&map->nr_free))) {
			do {
                                /* 如果偏移的位没有被使用，则可以直接使用 */
				if (!test_and_set_bit(offset, map->page)) {
                                        /* 减少页空闲位的数量 */
					atomic_dec(&map->nr_free);
                                        /* 设置最后一个进程的pid */
					last_pid = pid;
					return pid;
				}
                                /* 从偏移位置的下一个位置开始寻找，如果没有找到
                                  * 则一直偏移下去，如果当前页偏移完，仍然没有找到 
                                  * 则继续查找下一页，当查到最后一页的最后一位时 
                                  * 还是没找到，则从第一页的第一位继续查找  
                                  */
				offset = find_next_offset(map, offset);
				pid = mk_pid(map, offset);
			/*
			 * find_next_offset() found a bit, the pid from it
			 * is in-bounds, and if we fell back to the last
			 * bitmap block and the final block was the same
			 * as the starting point, pid is before last_pid.
			 */
			} while (offset < BITS_PER_PAGE && pid < pid_max &&
					(i != max_scan || pid < last ||
					    !((last+1) & BITS_PER_PAGE_MASK)));
		}
		if (map < &pidmap_array[(pid_max-1)/BITS_PER_PAGE]) {
			++map;
			offset = 0;
		} else {
			map = &pidmap_array[0];
			offset = RESERVED_PIDS;
			if (unlikely(last == offset))
				break;
		}
		pid = mk_pid(map, offset);
	}
	return -1;
}

/* 通过nr来hash处一个在pid_hash二维数组中
  * 列的位置，type表示行的位置的struct hlist_head 
  * 然后从该hash链表中查找nr相同的那个struct pid结构 
  * 找不到则返回为NULL  
  */
struct pid * fastcall find_pid(enum pid_type type, int nr)
{
	struct hlist_node *elem;
	struct pid *pid;

	/* */
	hlist_for_each_entry(pid, elem,
			&pid_hash[type][pid_hashfn(nr)], pid_chain) {
		if (pid->nr == nr)
			return pid;
	}
	return NULL;
}

int fastcall attach_pid(task_t *task, enum pid_type type, int nr)
{
	struct pid *pid, *task_pid;

	task_pid = &task->pids[type];
	pid = find_pid(type, nr);
	/* 如果找不到，则将对应的struct pid加入到对应的hash链表当中 */
	if (pid == NULL) {
		hlist_add_head(&task_pid->pid_chain,
				&pid_hash[type][pid_hashfn(nr)]);
		/* 初始化struct pid的pid_list双向链表 */
		INIT_LIST_HEAD(&task_pid->pid_list);
	} else {
		/* 清空struct pid的pid_chain对应的hash链表 */
		INIT_HLIST_NODE(&task_pid->pid_chain);
		/* 添加到pid_list双向链表的尾部 */
		list_add_tail(&task_pid->pid_list, &pid->pid_list);
	}
	/* 设置struct pid的nr变量 */
	task_pid->nr = nr;

	return 0;
}

/* 将进程type类型的struct pid从hash链表中移除
  * 同时返回type对应的nr 
  * 返回nr>0表示struct pid在hash链表当中 
  * 否则不在  
  */
static fastcall int __detach_pid(task_t *task, enum pid_type type)
{
	struct pid *pid, *pid_next;
	int nr = 0;

	pid = &task->pids[type];
	/* 如果进程是在hash链表当中，则将其删除  */
	if (!hlist_unhashed(&pid->pid_chain)) {
		hlist_del(&pid->pid_chain);

		if (list_empty(&pid->pid_list))
			nr = pid->nr;
		else {
			pid_next = list_entry(pid->pid_list.next,
						struct pid, pid_list);
			/* insert next pid from pid_list to hash */
			hlist_add_head(&pid_next->pid_chain,
				&pid_hash[type][pid_hashfn(pid_next->nr)]);
		}
	}

	list_del(&pid->pid_list);
	pid->nr = 0;

	return nr;
}

void fastcall detach_pid(task_t *task, enum pid_type type)
{
	int tmp, nr;

	nr = __detach_pid(task, type);
	if (!nr)
		return;

	for (tmp = PIDTYPE_MAX; --tmp >= 0; )
		if (tmp != type && find_pid(tmp, nr))
			return;

	/* 释放nr进程号占用的位 */
	free_pidmap(nr);
}

/* 根据条件来查找进程的pcb */
task_t *find_task_by_pid_type(int type, int nr)
{
	struct pid *pid;

	pid = find_pid(type, nr);
	if (!pid)
		return NULL;

	/* 返回对应进程的pid  */
	return pid_task(&pid->pid_list, type);
}

EXPORT_SYMBOL(find_task_by_pid_type);

/*
 * This function switches the PIDs if a non-leader thread calls
 * sys_execve() - this must be done without releasing the PID.
 * (which a detach_pid() would eventually do.)
 */
void switch_exec_pids(task_t *leader, task_t *thread)
{
	__detach_pid(leader, PIDTYPE_PID);
	__detach_pid(leader, PIDTYPE_TGID);
	__detach_pid(leader, PIDTYPE_PGID);
	__detach_pid(leader, PIDTYPE_SID);

	__detach_pid(thread, PIDTYPE_PID);
	__detach_pid(thread, PIDTYPE_TGID);

	leader->pid = leader->tgid = thread->pid;
	thread->pid = thread->tgid;

	attach_pid(thread, PIDTYPE_PID, thread->pid);
	attach_pid(thread, PIDTYPE_TGID, thread->tgid);
	attach_pid(thread, PIDTYPE_PGID, thread->signal->pgrp);
	attach_pid(thread, PIDTYPE_SID, thread->signal->session);
	list_add_tail(&thread->tasks, &init_task.tasks);

	attach_pid(leader, PIDTYPE_PID, leader->pid);
	attach_pid(leader, PIDTYPE_TGID, leader->tgid);
	attach_pid(leader, PIDTYPE_PGID, leader->signal->pgrp);
	attach_pid(leader, PIDTYPE_SID, leader->signal->session);
}

/*
 * The pid hash table is scaled according to the amount of memory in the
 * machine.  From a minimum of 16 slots up to 4096 slots at one gigabyte or
 * more.
 */
/* 初始化整个pid_hash */
void __init pidhash_init(void)
{
	int i, j, pidhash_size;
	unsigned long megabytes = nr_kernel_pages >> (20 - PAGE_SHIFT);

	pidhash_shift = max(4, fls(megabytes * 4));
	pidhash_shift = min(12, pidhash_shift);
	pidhash_size = 1 << pidhash_shift;

	printk("PID hash table entries: %d (order: %d, %Zd bytes)\n",
		pidhash_size, pidhash_shift,
		PIDTYPE_MAX * pidhash_size * sizeof(struct hlist_head));

	for (i = 0; i < PIDTYPE_MAX; i++) {
		pid_hash[i] = alloc_bootmem(pidhash_size *
					sizeof(*(pid_hash[i])));
		if (!pid_hash[i])
			panic("Could not alloc pidhash!\n");
		for (j = 0; j < pidhash_size; j++)
			INIT_HLIST_HEAD(&pid_hash[i][j]);
	}
}

/* 初始化pid的map数组 */
void __init pidmap_init(void)
{
	int i;

	/* 使用第一页的第一个位 */
	pidmap_array->page = (void *)get_zeroed_page(GFP_KERNEL);
	set_bit(0, pidmap_array->page);
	atomic_dec(&pidmap_array->nr_free);

	/*
	 * Allocate PID 0, and hash it via all PID types:
	 */

	for (i = 0; i < PIDTYPE_MAX; i++)
		attach_pid(current, i, 0);
}
