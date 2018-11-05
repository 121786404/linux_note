#ifndef __ASM_GENERIC_MMAN_COMMON_H
#define __ASM_GENERIC_MMAN_COMMON_H

/*
 Author: Michael S. Tsirkin <mst@mellanox.co.il>, Mellanox Technologies Ltd.
 Based on: asm-xxx/mman.h
*/
/*
表示映射的页面是可以读取的
*/
#define PROT_READ	0x1		/* page can be read */
/*
表示映射的页面是可以写入的
*/
#define PROT_WRITE	0x2		/* page can be written */
/*
表示映射的页面是可以执行的
*/
#define PROT_EXEC	0x4		/* page can be executed */
#define PROT_SEM	0x8		/* page may be used for atomic ops */
/*
表示映射的页面是不可访问的
*/
#define PROT_NONE	0x0		/* page can not be accessed */
#define PROT_GROWSDOWN	0x01000000	/* mprotect flag: extend change to start of growsdown vma */
#define PROT_GROWSUP	0x02000000	/* mprotect flag: extend change to end of growsup vma */
/*
创建一个共享映射的区域。多个进程可以通过共享映射方式来映射一个文件，
这样其他进程也可以看到映射内容的改变，修改后的内容会同步到磁盘文件中
*/
#define MAP_SHARED	0x01		/* Share changes */
/*
创建一个私有的写时复制的映射。多个进程可以通过私有映射的方式来映射一个文件，
这样其他进程不会看到映射内容的改变，修改后的内容也不会同步到磁盘文件中
*/
#define MAP_PRIVATE	0x02		/* Changes are private */
#define MAP_TYPE	0x0f		/* Mask for type of mapping */
/*
使用参数addr 创建映射，如果在内核中无法映射指定的地址addr，
那mmap 会返回失败，参数addr 要求按页对齐。
如果addr 和length 指定的进程地址空间和已有的VMA 区域重叠，
那么内核会调用do_munmapO函数把这段重叠区域销毁，然后重新映射新的内容
*/
#define MAP_FIXED	0x10		/* Interpret addr exactly */
/*
创建一个匿名映射，即没有关联到文件的映射
*/
#define MAP_ANONYMOUS	0x20		/* don't use a file */
#ifdef CONFIG_MMAP_ALLOW_UNINITIALIZED
# define MAP_UNINITIALIZED 0x4000000	/* For anonymous mmap, memory could be uninitialized */
#else
# define MAP_UNINITIALIZED 0x0		/* Don't support this flag */
#endif

/*
 * Flags for mlock
 */
#define MLOCK_ONFAULT	0x01		/* Lock pages in range after they are faulted in, do not prefault */

#define MS_ASYNC	1		/* sync memory asynchronously */
#define MS_INVALIDATE	2		/* invalidate the caches */
#define MS_SYNC		4		/* synchronous memory sync */

#define MADV_NORMAL	0		/* no further special treatment */
#define MADV_RANDOM	1		/* expect random page references */
/*
MADV_SEQUENTIAL 适合多媒体文件预读的场景，但是内核默认的预读功能也能很好的工作。
对于问题2. 能够有效提高流媒体服务IO 性能的方法是增大内核的默认预读窗口，
现在内核默认预读的大小是128KB，可以通过"b1ockdev →etra" 命令来修改。
*/
#define MADV_SEQUENTIAL	2		/* expect sequential page references */
/*
立刻启动磁盘 IO 进行预读，仅预读指定的长度，因此在读取新的文件区域时，
要重新调用 MADV_WILLNEED ，显然它不适合流媒体服务的场景。
MADV_WILLNEED 适合内核很难预测接下来要预读哪些内容的场景，例如随机读。
*/
#define MADV_WILLNEED	3		/* will need these pages */
#define MADV_DONTNEED	4		/* don't need these pages */

/* common parameters: try to keep these consistent across architectures */
#define MADV_FREE	8		/* free pages only if memory pressure */
#define MADV_REMOVE	9		/* remove these pages & resources */
#define MADV_DONTFORK	10		/* don't inherit across fork */
#define MADV_DOFORK	11		/* do inherit across fork */
#define MADV_HWPOISON	100		/* poison a page for testing */
#define MADV_SOFT_OFFLINE 101		/* soft offline page for testing */

#define MADV_MERGEABLE   12		/* KSM may merge identical pages */
#define MADV_UNMERGEABLE 13		/* KSM may not merge identical pages */

#define MADV_HUGEPAGE	14		/* Worth backing with hugepages */
#define MADV_NOHUGEPAGE	15		/* Not worth backing with hugepages */

#define MADV_DONTDUMP   16		/* Explicity exclude from the core dump,
					   overrides the coredump filter bits */
#define MADV_DODUMP	17		/* Clear the MADV_DONTDUMP flag */

/* compatibility flags */
#define MAP_FILE	0

/*
 * When MAP_HUGETLB is set bits [26:31] encode the log2 of the huge page size.
 * This gives us 6 bits, which is enough until someone invents 128 bit address
 * spaces.
 *
 * Assume these are all power of twos.
 * When 0 use the default page size.
 */
#define MAP_HUGE_SHIFT	26
#define MAP_HUGE_MASK	0x3f

#define PKEY_DISABLE_ACCESS	0x1
#define PKEY_DISABLE_WRITE	0x2
#define PKEY_ACCESS_MASK	(PKEY_DISABLE_ACCESS |\
				 PKEY_DISABLE_WRITE)

#endif /* __ASM_GENERIC_MMAN_COMMON_H */
