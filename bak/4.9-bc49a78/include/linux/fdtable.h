/*
 * descriptor table internals; you almost certainly want file.h instead.
 */

#ifndef __LINUX_FDTABLE_H
#define __LINUX_FDTABLE_H

#include <linux/posix_types.h>
#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/fs.h>

#include <linux/atomic.h>

/*
 * The default fd array needs to be at least BITS_PER_LONG,
 * as this is the granularity returned by copy_fdset().
 */
#define NR_OPEN_DEFAULT BITS_PER_LONG

struct fdtable {
	unsigned int max_fds;
	struct file __rcu **fd;      /* current fd array */
	unsigned long *close_on_exec;
	unsigned long *open_fds;
	unsigned long *full_fds_bits;
	struct rcu_head rcu;
};

static inline bool close_on_exec(unsigned int fd, const struct fdtable *fdt)
{
	return test_bit(fd, fdt->close_on_exec);
}

static inline bool fd_is_open(unsigned int fd, const struct fdtable *fdt)
{
	return test_bit(fd, fdt->open_fds);
}

/*
 * Open file table structure
 */
struct files_struct {
  /*
   * read mostly part
   */
   /* count为文件表files struct的引用计数 */
	atomic_t count; /*结构的使用计数*/
	bool resize_in_progress;
	wait_queue_head_t resize_wait;

    /*
         为什么有两个fdtable呢?
         这是内核的一种优化策略。fdt为指针, 而fdtab为普通变量。
         一般情况下, fdt是指向fdtab的, 当需要它的时候, 
         才会真正动态申请内存。因为默认大小的文件表足以应付大多数情况, 
         因此这样就可以避免频繁的内存申请。
         这也是内核的常用技巧之一。在创建时, 
         使用普通的变量或者数组, 然后让指针指向它, 作为默认情况使用。
         只有当进程使用量超过默认值时, 才会动态申请内存。
      */
	struct fdtable __rcu *fdt;
	struct fdtable fdtab;
  /*
   * written part on a separate cache line in SMP
   */
    /* 
     使用____cacheline_aligned_in_smp可以保证file_lock是以cache      line 对齐的, 避免了false sharing 
    */
	spinlock_t file_lock ____cacheline_aligned_in_smp;
	/* 用于查找下一个空闲的fd */
	unsigned int next_fd;
	/* 执行exec需要关闭的文件描述符的位图 */
	unsigned long close_on_exec_init[1];
	/* 打开的文件描述符的位图 */
	unsigned long open_fds_init[1];
	unsigned long full_fds_bits_init[1];
	/* 一个固定大小的file结构数组。
	struct file是内核用于文件管理的结构。
	这里使用默认大小的数组, 就是为了可以涵盖大多数情况, 
	避免动态分配 */
	struct file __rcu * fd_array[NR_OPEN_DEFAULT];
};

struct file_operations;
struct vfsmount;
struct dentry;

#define rcu_dereference_check_fdtable(files, fdtfd) \
	rcu_dereference_check((fdtfd), lockdep_is_held(&(files)->file_lock))

#define files_fdtable(files) \
	rcu_dereference_check_fdtable((files), (files)->fdt)

/*
 * The caller must ensure that fd table isn't shared or hold rcu or file lock
 */
static inline struct file *__fcheck_files(struct files_struct *files, unsigned int fd)
{
	struct fdtable *fdt = rcu_dereference_raw(files->fdt);

	if (fd < fdt->max_fds)
		return rcu_dereference_raw(fdt->fd[fd]);
	return NULL;
}

static inline struct file *fcheck_files(struct files_struct *files, unsigned int fd)
{
	RCU_LOCKDEP_WARN(!rcu_read_lock_held() &&
			   !lockdep_is_held(&files->file_lock),
			   "suspicious rcu_dereference_check() usage");
	return __fcheck_files(files, fd);
}

/*
 * Check whether the specified fd has an open file.
 */
#define fcheck(fd)	fcheck_files(current->files, fd)

struct task_struct;

struct files_struct *get_files_struct(struct task_struct *);
void put_files_struct(struct files_struct *fs);
void reset_files_struct(struct files_struct *);
int unshare_files(struct files_struct **);
struct files_struct *dup_fd(struct files_struct *, int *) __latent_entropy;
void do_close_on_exec(struct files_struct *);
int iterate_fd(struct files_struct *, unsigned,
		int (*)(const void *, struct file *, unsigned),
		const void *);

extern int __alloc_fd(struct files_struct *files,
		      unsigned start, unsigned end, unsigned flags);
extern void __fd_install(struct files_struct *files,
		      unsigned int fd, struct file *file);
extern int __close_fd(struct files_struct *files,
		      unsigned int fd);

extern struct kmem_cache *files_cachep;

#endif /* __LINUX_FDTABLE_H */
