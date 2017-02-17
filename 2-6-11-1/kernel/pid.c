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
/* pid��hash����,�ܹ������PIDTYPE_MAX��Ԫ��,
  * pid_hash������һ����ά��PIDTYPE_MAX��ʾ�ж����� 
  * pidhash_shift��ʾ��2��pidhash_shift�η��У�Ҳ�����ж��ٸ�Ԫ�أ���Ԫ�صĸ��� 
  * ��ͨ��2��n�η�����ʾ�ģ�n����pidhash_shift 
  */
static struct hlist_head *pid_hash[PIDTYPE_MAX];
static int pidhash_shift;

int pid_max = PID_MAX_DEFAULT;
/* ��ǰ���һ�����̵�pid���������pid_max�������ѭ�� */
int last_pid;


/* ���ڷ������idʱ��������������id�ţ������·��ص�����ط� */
#define RESERVED_PIDS		300

int pid_max_min = RESERVED_PIDS + 1;
int pid_max_max = PID_MAX_LIMIT;

/* ȡ��ҳ�ڴ棬��Ҫ�ܹ�����ҳ�ڴ�����ǽ���pid  */
#define PIDMAP_ENTRIES		((PID_MAX_LIMIT + 8*PAGE_SIZE - 1)/PAGE_SIZE/8)
/* ��ȡÿҳ�ڴ��ж�����λ������ */
#define BITS_PER_PAGE		(PAGE_SIZE*8)
/* һҳ�ڴ��ж��ٸ�������λ */
#define BITS_PER_PAGE_MASK	(BITS_PER_PAGE-1)
/* ����ҳ�ĵ�ַ��ҳ�ڵ�ƫ�������ɽ��̵�pid */
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
	atomic_t nr_free;  /* һҳ�����ڴ浱�еĶ�����λ�Ŀ��и��� */
	void *page;       /* һҳ�����ڴ�ĵ�ַ */
} pidmap_t;

/* ȫ����ʼ��û�з������pid */
static pidmap_t pidmap_array[PIDMAP_ENTRIES] =
	 { [ 0 ... PIDMAP_ENTRIES-1 ] = { ATOMIC_INIT(BITS_PER_PAGE), NULL } };

static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(pidmap_lock);

/* �ͷž���pid��Ӧ���ڴ�λͼ */
fastcall void free_pidmap(int pid)
{
	pidmap_t *map = pidmap_array + pid / BITS_PER_PAGE;
	int offset = pid & BITS_PER_PAGE_MASK;

	clear_bit(offset, map->page);
	atomic_inc(&map->nr_free);
}

/* ����һ�����̵�pid */
int alloc_pidmap(void)
{
	int i, offset, max_scan, pid, last = last_pid;
	pidmap_t *map;

	pid = last + 1;
	if (pid >= pid_max)
		pid = RESERVED_PIDS;
        /* ȥpid���ڴ�ҳ�е�ƫ�� */
	offset = pid & BITS_PER_PAGE_MASK;
	map = &pidmap_array[pid/BITS_PER_PAGE];
        /* ȷ����Ҫɨ�����ҳ���ڴ� */
	max_scan = (pid_max + BITS_PER_PAGE - 1)/BITS_PER_PAGE - !offset;
	for (i = 0; i <= max_scan; ++i) {
		/* �˴���unlikely��û�����ر���������ã�������һ���� */
		if (unlikely(!map->page)) {
			/* ��ȡһҳ�ڴ� */
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
                /* �ж��ڴ�ҳ���Ƿ��п��ж�����λ */
		if (likely(atomic_read(&map->nr_free))) {
			do {
                                /* ���ƫ�Ƶ�λû�б�ʹ�ã������ֱ��ʹ�� */
				if (!test_and_set_bit(offset, map->page)) {
                                        /* ����ҳ����λ������ */
					atomic_dec(&map->nr_free);
                                        /* �������һ�����̵�pid */
					last_pid = pid;
					return pid;
				}
                                /* ��ƫ��λ�õ���һ��λ�ÿ�ʼѰ�ң����û���ҵ�
                                  * ��һֱƫ����ȥ�������ǰҳƫ���꣬��Ȼû���ҵ� 
                                  * �����������һҳ�����鵽���һҳ�����һλʱ 
                                  * ����û�ҵ�����ӵ�һҳ�ĵ�һλ��������  
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

/* ͨ��nr��hash��һ����pid_hash��ά������
  * �е�λ�ã�type��ʾ�е�λ�õ�struct hlist_head 
  * Ȼ��Ӹ�hash�����в���nr��ͬ���Ǹ�struct pid�ṹ 
  * �Ҳ����򷵻�ΪNULL  
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
	/* ����Ҳ������򽫶�Ӧ��struct pid���뵽��Ӧ��hash������ */
	if (pid == NULL) {
		hlist_add_head(&task_pid->pid_chain,
				&pid_hash[type][pid_hashfn(nr)]);
		/* ��ʼ��struct pid��pid_list˫������ */
		INIT_LIST_HEAD(&task_pid->pid_list);
	} else {
		/* ���struct pid��pid_chain��Ӧ��hash���� */
		INIT_HLIST_NODE(&task_pid->pid_chain);
		/* ��ӵ�pid_list˫�������β�� */
		list_add_tail(&task_pid->pid_list, &pid->pid_list);
	}
	/* ����struct pid��nr���� */
	task_pid->nr = nr;

	return 0;
}

/* ������type���͵�struct pid��hash�������Ƴ�
  * ͬʱ����type��Ӧ��nr 
  * ����nr>0��ʾstruct pid��hash������ 
  * ������  
  */
static fastcall int __detach_pid(task_t *task, enum pid_type type)
{
	struct pid *pid, *pid_next;
	int nr = 0;

	pid = &task->pids[type];
	/* �����������hash�����У�����ɾ��  */
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

	/* �ͷ�nr���̺�ռ�õ�λ */
	free_pidmap(nr);
}

/* �������������ҽ��̵�pcb */
task_t *find_task_by_pid_type(int type, int nr)
{
	struct pid *pid;

	pid = find_pid(type, nr);
	if (!pid)
		return NULL;

	/* ���ض�Ӧ���̵�pid  */
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
/* ��ʼ������pid_hash */
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

/* ��ʼ��pid��map���� */
void __init pidmap_init(void)
{
	int i;

	/* ʹ�õ�һҳ�ĵ�һ��λ */
	pidmap_array->page = (void *)get_zeroed_page(GFP_KERNEL);
	set_bit(0, pidmap_array->page);
	atomic_dec(&pidmap_array->nr_free);

	/*
	 * Allocate PID 0, and hash it via all PID types:
	 */

	for (i = 0; i < PIDTYPE_MAX; i++)
		attach_pid(current, i, 0);
}
