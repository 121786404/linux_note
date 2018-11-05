#ifndef _LINUX_MM_TYPES_H
#define _LINUX_MM_TYPES_H

#include <linux/auxvec.h>
#include <linux/types.h>
#include <linux/threads.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/completion.h>
#include <linux/cpumask.h>
#include <linux/uprobes.h>
#include <linux/page-flags-layout.h>
#include <linux/workqueue.h>
#include <asm/page.h>
#include <asm/mmu.h>

#ifndef AT_VECTOR_SIZE_ARCH
#define AT_VECTOR_SIZE_ARCH 0
#endif
#define AT_VECTOR_SIZE (2*(AT_VECTOR_SIZE_ARCH + AT_VECTOR_SIZE_BASE + 1))

struct address_space;
struct mem_cgroup;

#define USE_SPLIT_PTE_PTLOCKS	(NR_CPUS >= CONFIG_SPLIT_PTLOCK_CPUS)
#define USE_SPLIT_PMD_PTLOCKS	(USE_SPLIT_PTE_PTLOCKS && \
		IS_ENABLED(CONFIG_ARCH_ENABLE_SPLIT_PMD_PTLOCK))
#define ALLOC_SPLIT_PTLOCKS	(SPINLOCK_SIZE > BITS_PER_LONG/8)

/*
 * Each physical page in the system has a struct page associated with
 * it to keep track of whatever it is we are using the page for at the
 * moment. Note that we have no way to track which tasks are using
 * a page, though if it is a pagecache page, rmap structures can tell us
 * who is mapping it.
 *
 * The objects in struct page are organized in double word blocks in
 * order to allows us to use atomic double word operations on portions
 * of struct page. That is currently only used by slub but the arrangement
 * allows the use of atomic double word operations on the flags/mapping
 * and lru list pointers also.
 */
/*
因为内核会为每一个物理页帧创建一个struct page的结构体，
因此要保证page结构体足够的小，否则仅struct page就要占用大量的内存。
出于节省内存的考虑，struct page中使用了大量的联合体union

考虑一个例子：一个物理内存页能够通过多个地方的不同页表映射到虚拟地址空间，
内核想要跟踪有多少地方映射了该页，
为此，struct page中有一个计数器用于计算映射的数目。

如果一页用于slab分配器，那么可以确保只有内核会使用该页，而不会有其它地方使用，
因此映射计数信息就是多余的，因此内核可以重新解释该字段，
用来表示该页被细分为多少个小的内存对象使用，联合体就很适用于该问题
*/
struct page {
	/* First double word block */
    /* 
    用来存放页的体系结构无关的标志,linux/page-flags.h
    */
	unsigned long flags;		/* Atomic flags, some possibly
					 * updated asynchronously  描述page的状态和其他信息 */
	union {
    /*
        指向与该页相关的address_space对象

        mapping指定了页帧所在的地址空间, index是页帧在映射内部的偏移量. 
        地址空间是一个非常一般的概念. 例如, 可以用在向内存读取文件时. 
        地址空间用于将文件的内容与装载数据的内存区关联起来. 
        mapping不仅能够保存一个指针, 而且还能包含一些额外的信息, 
        用于判断页是否属于未关联到地址空间的某个匿名内存区.

        1. 如果mapping = 0，说明该page属于交换高速缓存页（swap cache）；
            当需要使用地址空间时会指定交换分区的地址空间swapper_space。
        2. 如果mapping != 0，bit[0] = 0，说明该page属于页缓存或文件映射，
            mapping指向文件的地址空间address_space。
        3. 如果mapping != 0，bit[0] != 0，说明该page为匿名映射，mapping指向struct anon_vma对象。

        通过mapping恢复anon_vma的方法：
            anon_vma = (struct anon_vma *)(mapping - PAGE_MAPPING_ANON)。
    */
		struct address_space *mapping;	/* If low bit clear, points to
						 * inode address_space, or NULL.
						 * If page mapped as anonymous
						 * memory, low bit is set, and
						 * it points to anon_vma object:
						 * see PAGE_MAPPING_ANON below.
						 */
		void *s_mem;			/* slab first object */
		atomic_t compound_mapcount;	/* first tail page */
		/* page_deferred_list().next	 -- second tail page */
	};

	/* Second double word */
	union {
        /*
        在映射的虚拟空间（vma_area）内的偏移；
         一个文件可能只映射一部分，假设映射了1M的空间，
        index指的是在1M空间内的偏移，而不是在整个文件内的偏移

        pgoff_t index是该页描述结构在地址空间radix树page_tree中的对象索引号即页号,
        表示该页在vm_file中的偏移页数, 其类型pgoff_t被定义为unsigned long即一个机器字长.
        */
		pgoff_t index;		/* Our offset within mapping. */
	/* 所在slab的第一个free对象 */
		void *freelist;		/* sl[aou]b first free object */
		/* page_deferred_list().prev	-- second tail page */
	};

	union {
#if defined(CONFIG_HAVE_CMPXCHG_DOUBLE) && \
	defined(CONFIG_HAVE_ALIGNED_STRUCT_PAGE)
		/* Used for cmpxchg_double in slub */
		unsigned long counters;
#else
		/*
		 * Keep _refcount separate from slub cmpxchg_double data.
		 * As the rest of the double word is protected by slab_lock
		 * but _refcount is not.
		 */
		unsigned counters;
#endif
		struct {

			union {
				/*
				 * Count of ptes mapped in mms, to show when
				 * page is mapped & limit reverse map searches.
				 *
				 * Extra information about page type may be
				 * stored here for pages that are never mapped,
				 * in which case the value MUST BE <= -2.
				 * See page-flags.h for more details.
				 */
/*  
            被页表映射的次数，也就是说该page同时被多少个进程共享。还用于限制逆向映射搜索
            初始值为-1，如果只被一个进程的页表映射了，该值为0. 
            如果该page处于伙伴系统中，该值为PAGE_BUDDY_MAPCOUNT_VALUE（-128），
            内核通过判断该值是否为PAGE_BUDDY_MAPCOUNT_VALUE来确定该page是否属于伙伴系统
            _mapcount表示的是映射次数，而_count表示的是使用次数；被映射了不一定在使用，但要使用必须先映射
*/
				atomic_t _mapcount;

				unsigned int active;		/* SLAB */
				struct {			/* SLUB */
						/* 在当前slab中，已经分配的slab数量 */
					unsigned inuse:16;
						/* 在当前slab中，总slab数量 */
					unsigned objects:15;
						/**
						 * 如果为1，表示只能由活动CPU分配SLAB
						 * 其他CPU只能向其释放SLAB
						 */
					unsigned frozen:1;
				};
				int units;			/* SLOB */
			};
			/*
			 * Usage count, *USE WRAPPER FUNCTION* when manual
			 * accounting. See page_ref.h
			 */
/* 
            引用计数，表示内核中引用该page的次数, 
            如果要操作该page, 引用计数会+1, 操作完成-1. 
            当该值为0时, 表示没有引用该page的位置，
            所以该page可以被解除映射，这往往在内存回收时是有用的
*/
			atomic_t _refcount;
		};
	};

	/*
	 * Third double word block
	 *
	 * WARNING: bit 0 of the first word encode PageTail(). That means
	 * the rest users of the storage space MUST NOT use the bit to
	 * avoid collision and false-positive PageTail().
	 */
	union {
/*
链表头，用于在各种链表上维护该页, 以便于按页将不同类别分组, 
主要有3个用途: 伙伴算法, slab分配器, 被用户态使用或被当做页缓存使用

最近、最久未使用struct slab结构指针变量
lru：链表头，主要有3个用途：
1 则page处于伙伴系统中时，用于链接相同阶的伙伴
  （只使用伙伴中的第一个page的lru即可达到目的）。
2 设置PG_slab, 则page属于slab，page->lru.next指向page驻留的的缓存的管理结构，
   page->lru.prec指向保存该page的slab的管理结构。
3 page被用户态使用或被当做页缓存使用时，
   用于将该page连入zone中相应的lru链表，供内存回收时使用。
*/
		struct list_head lru;	/* Pageout list, eg. active_list
					 * protected by zone_lru_lock !
					 * Can be used as a generic list
					 * by the page owner.
					 */
		struct dev_pagemap *pgmap; /* ZONE_DEVICE pages are never on an
					    * lru or handled by a slab
					    * allocator, this points to the
					    * hosting device page map.
					    */
		struct {		/* slub per cpu partial pages */
			struct page *next;	/* Next partial slab */
#ifdef CONFIG_64BIT
			int pages;	/* Nr of partial slabs left */
			int pobjects;	/* Approximate # of objects */
#else
			short int pages;
			short int pobjects;
#endif
		};

		struct rcu_head rcu_head;	/* Used by SLAB
						 * when destroying via RCU
						 */
		/* Tail pages of compound page */
		/* 内核可以将多个毗连的页合并为较大的复合页（compound page）*/
		struct {
			unsigned long compound_head; /* If bit zero is set */

			/* First tail page only */
#ifdef CONFIG_64BIT
			/*
			 * On 64 bit system we have enough space in struct page
			 * to encode compound_dtor and compound_order with
			 * unsigned int. It can help compiler generate better or
			 * smaller code on some archtectures.
			 */
			unsigned int compound_dtor;
			unsigned int compound_order;
#else
			unsigned short int compound_dtor;
			unsigned short int compound_order;
#endif
		};

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && USE_SPLIT_PMD_PTLOCKS
		struct {
			unsigned long __pad;	/* do not overlay pmd_huge_pte
						 * with compound_head to avoid
						 * possible bit 0 collision.
						 */
			pgtable_t pmd_huge_pte; /* protected by page->ptl */
		};
#endif
	};

	/* Remainder is not double word aligned */
	union {
	    /* 
        private私有数据指针, 由应用场景确定其具体的含义：
        如果设置了PG_private标志，则private字段指向struct buffer_heads
        如果设置了PG_compound，则指向struct page
        如果设置了PG_swapcache标志，private存储了该page在交换分区中对应的位置信息swp_entry_t。
        如果_mapcount = PAGE_BUDDY_MAPCOUNT_VALUE，说明该page位于伙伴系统，private存储该伙伴的阶
	    */
		unsigned long private;		/* Mapping-private opaque data:
					 	 * usually used for buffer_heads
						 * if PagePrivate set; used for
						 * swp_entry_t if PageSwapCache;
						 * indicates order in the buddy
						 * system if PG_buddy is set.
						 */
#if USE_SPLIT_PTE_PTLOCKS
#if ALLOC_SPLIT_PTLOCKS
		spinlock_t *ptl;
#else
		spinlock_t ptl;
#endif
#endif
		/* 指向本页关联的SLAB描述符 */
		struct kmem_cache *slab_cache;	/* SL[AU]B: Pointer to slab */
	};

#ifdef CONFIG_MEMCG
	struct mem_cgroup *mem_cgroup;
#endif

	/*
	 * On machines where all RAM is mapped into kernel address space,
	 * we can simply calculate the virtual address. On machines with
	 * highmem some memory is mapped into kernel virtual memory
	 * dynamically, so we need a place to store that address.
	 * Note that this field could be 16 bits on x86 ... ;)
	 *
	 * Architectures with slow multiplication can define
	 * WANT_PAGE_VIRTUAL in asm/page.h
	 */
#if defined(WANT_PAGE_VIRTUAL)
/*
对于如果物理内存可以直接映射内核的系统, 
我们可以之间映射出虚拟地址与物理地址的管理, 
但是对于需要使用高端内存区域的页, 
即无法直接映射到内核的虚拟地址空间, 
因此需要用virtual保存该页的虚拟地址
*/
	void *virtual;			/* Kernel virtual address (NULL if
					   not kmapped, ie. highmem) */
#endif /* WANT_PAGE_VIRTUAL */

#ifdef CONFIG_KMEMCHECK
	/*
	 * kmemcheck wants to track the status of each byte in a page; this
	 * is a pointer to such a status block. NULL if not tracked.
	 */
	void *shadow;
#endif

#ifdef LAST_CPUPID_NOT_IN_PAGE_FLAGS
	int _last_cpupid;
#endif
}
/*
 * The struct page can be forced to be double word aligned so that atomic ops
 * on double words work. The SLUB allocator can make use of such a feature.
 */
#ifdef CONFIG_HAVE_ALIGNED_STRUCT_PAGE
	__aligned(2 * sizeof(unsigned long))
#endif
;

struct page_frag {
	struct page *page;
#if (BITS_PER_LONG > 32) || (PAGE_SIZE >= 65536)
	__u32 offset;
	__u32 size;
#else
	__u16 offset;
	__u16 size;
#endif
};

#define PAGE_FRAG_CACHE_MAX_SIZE	__ALIGN_MASK(32768, ~PAGE_MASK)
#define PAGE_FRAG_CACHE_MAX_ORDER	get_order(PAGE_FRAG_CACHE_MAX_SIZE)

struct page_frag_cache {
	void * va;
#if (PAGE_SIZE < PAGE_FRAG_CACHE_MAX_SIZE)
	__u16 offset;
	__u16 size;
#else
	__u32 offset;
#endif
	/* we maintain a pagecount bias, so that we dont dirty cache line
	 * containing page->_refcount every time we allocate a fragment.
	 */
	unsigned int		pagecnt_bias;
	bool pfmemalloc;
};

typedef unsigned long vm_flags_t;

/*
 * A region containing a mapping of a non-memory backed file under NOMMU
 * conditions.  These are held in a global tree and are pinned by the VMAs that
 * map parts of them.
 */
struct vm_region {
	struct rb_node	vm_rb;		/* link in global region tree */
	vm_flags_t	vm_flags;	/* VMA vm_flags */
	unsigned long	vm_start;	/* start address of region */
	unsigned long	vm_end;		/* region initialised to here */
	unsigned long	vm_top;		/* region allocated to here */
	unsigned long	vm_pgoff;	/* the offset in vm_file corresponding to vm_start */
	struct file	*vm_file;	/* the backing file or NULL */

	int		vm_usage;	/* region usage count (access under nommu_region_sem) */
	bool		vm_icache_flushed : 1; /* true if the icache has been flushed for
						* this region */
};

#ifdef CONFIG_USERFAULTFD
#define NULL_VM_UFFD_CTX ((struct vm_userfaultfd_ctx) { NULL, })
struct vm_userfaultfd_ctx {
	struct userfaultfd_ctx *ctx;
};
#else /* CONFIG_USERFAULTFD */
#define NULL_VM_UFFD_CTX ((struct vm_userfaultfd_ctx) {})
struct vm_userfaultfd_ctx {};
#endif /* CONFIG_USERFAULTFD */

/*
 * This struct defines a memory VMM memory area. There is one of these
 * per VM-area/task.  A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
/*
vm_area_struct结构体描述了指定地址空间内连续区间上的一个独立内存范围. 
内核将每个内存区域作为一个单独的内存对象管理,
 每个内存区域都拥有一致的属性, 比如访问权限等. 另外相应的操作也都一致. 

按照这样的方式, 每一个VMA就可以代表不同类型的内存区域
(比如内存映射文件或者进程用户空间栈). 
这种管理方式类似于使用CFS层面向对象的方法
*/
struct vm_area_struct {
	/* The first cache line has the info for VMA tree walking. */

/*
vm_end - vm_start的大小便是区间的长度. 
即内存区域就在[vm_start, vm_end)之中
*/
	unsigned long vm_start;		/* Our start address within vm_mm. */
	unsigned long vm_end;		/* The first byte after our end address
					   within vm_mm. */

	/* linked list of VM areas per task, sorted by address */
	/* 该vma的在一个进程的vma链表中的前驱vma和后驱vma指针，
          链表中的vma都是按地址来排序的*/
	struct vm_area_struct *vm_next, *vm_prev;
    /* 红黑树中对应的节点 */ 
	struct rb_node vm_rb;

	/*
	 * Largest free memory gap in bytes to the left of this VMA.
	 * Either between this VMA and vma->vm_prev, or between one of the
	 * VMAs below us in the VMA rbtree and its ->vm_prev. This helps
	 * get_unmapped_area find a free area of the right size.
	 */
	unsigned long rb_subtree_gap;

	/* Second cache line starts here. */

    /* 所属的内存描述符 */
	struct mm_struct *vm_mm;	/* The address space we belong to. */
    /* vma的访问权限 */ 
	pgprot_t vm_page_prot;		/* Access permissions of this VMA. */
    /* 标识集 */
	unsigned long vm_flags;		/* Flags, see mm.h. */

	/*
	 * For areas with an address space and backing store,
	 * linkage into the address_space->i_mmap interval tree.
	 */
	struct {
		struct rb_node rb;
		unsigned long rb_subtree_last;
	} shared;

	/*
	 * A file's MAP_PRIVATE vma can be in both i_mmap tree and anon_vma
	 * list, after a COW of one of the file pages.	A MAP_SHARED vma
	 * can only be in the i_mmap tree.  An anonymous MAP_PRIVATE, stack
	 * or brk vma (with NULL file) can only be in an anon_vma list.
	 */
	 /*anno_vma_node和annon_vma用于管理源自匿名映射的共享页*/ 
	struct list_head anon_vma_chain; /* Serialized by mmap_sem &
					  * page_table_lock */
	struct anon_vma *anon_vma;	/* Serialized by page_table_lock */

	/* Function pointers to deal with this struct. */
	/*该vma上的各种标准操作函数指针集*/ 
	const struct vm_operations_struct *vm_ops;

	/* Information about our backing store: */
	 /* 映射文件的偏移量，以PAGE_SIZE为单位 */ 
	unsigned long vm_pgoff;		/* Offset (within vm_file) in PAGE_SIZE
					   units */
	/* 映射的文件，没有则为NULL */  
	struct file * vm_file;		/* File we map to (can be NULL). */
	void * vm_private_data;		/* was vm_pte (shared mem) */

#ifndef CONFIG_MMU
	struct vm_region *vm_region;	/* NOMMU mapping region */
#endif
#ifdef CONFIG_NUMA
	struct mempolicy *vm_policy;	/* NUMA policy for the VMA */
#endif
	struct vm_userfaultfd_ctx vm_userfaultfd_ctx;
};

struct core_thread {
	struct task_struct *task;
	struct core_thread *next;
};

struct core_state {
	atomic_t nr_threads;
	struct core_thread dumper;
	struct completion startup;
};

enum {
	MM_FILEPAGES,	/* Resident file mapping pages */
	MM_ANONPAGES,	/* Resident anonymous pages */
	MM_SWAPENTS,	/* Anonymous swap entries */
	MM_SHMEMPAGES,	/* Resident shared memory pages */
	NR_MM_COUNTERS
};

#if USE_SPLIT_PTE_PTLOCKS && defined(CONFIG_MMU)
#define SPLIT_RSS_COUNTING
/* per-thread cached information, */
struct task_rss_stat {
	int events;	/* for synchronization threshold */
	int count[NR_MM_COUNTERS];
};
#endif /* USE_SPLIT_PTE_PTLOCKS */

struct mm_rss_stat {
	atomic_long_t count[NR_MM_COUNTERS];
};

struct kioctx_table;
/*
3G进程空间的数据抽象
*/
struct mm_struct {
/*
而由于进程往往不会整个3G空间都使用，而是使用不同的离散虚存区域，
并且每个离散区域的使用都不一样，
比如说栈所在的区域和代码段所在的区域的使用就不一样，
从而导致各个区域的属性、需要的管理操作都不一致，
所以就将各个虚存区域提炼出来单独管理，
然后就再将所有虚存区域组成一颗红黑树交由mm_struct统一管理。
*/
	struct vm_area_struct *mmap;		/* list of VMAs */
/*
	l指向进程所有虚存区域构成的红黑树的根节点
*/
	struct rb_root mm_rb;
	u32 vmacache_seqnum;                   /* per-thread vmacache */
#ifdef CONFIG_MMU
	unsigned long (*get_unmapped_area) (struct file *filp,
				unsigned long addr, unsigned long len,
				unsigned long pgoff, unsigned long flags);
#endif
/*
虚拟地址空间中用于内存映射的起始地址, 
可调用get_unmapped_area在mmap区域中为新映射找到适当的位置
*/
	unsigned long mmap_base;		/* base of mmap area */
	unsigned long mmap_legacy_base;         /* base of mmap area in bottom-up allocations */
/*
对应进程的地址空间长度. 
对本机应用程序来说, 该值通常是TASK_SIZE. 
但64位体系结构与前辈处理器通常是二进制兼容的. 
如果在64位计算机上执行32位二进制代码, 
则task_size描述了该二进制代码实际可见的地址空间长度.
*/
	unsigned long task_size;		/* size of task vm space */
	unsigned long highest_vm_end;		/* highest vma end address */
/*
     一级页表基址，
     当内核运行这个进程时，它就将pgd存放在TTBRx寄存器内 
*/
	pgd_t * pgd;
/*
	共享mm_struct数据结构的轻量级进程的个数
*/
	atomic_t mm_users;			/* How many users with user space? */
/*
	内存描述符的主使计数器
*/
	atomic_t mm_count;			/* How many references to "struct mm_struct" (users count as 1) */
    // 页表所占(物理〉空间
	atomic_long_t nr_ptes;			/* PTE page table pages */
#if CONFIG_PGTABLE_LEVELS > 2
	atomic_long_t nr_pmds;			/* PMD page table pages */
#endif
    // 虚拟区间的个数
	int map_count;				/* number of VMAs */
    // 保护任务页表和 mm->rss
	spinlock_t page_table_lock;		/* Protects page tables and some counters */
	// 对mmap操作的互斥信号量
	struct rw_semaphore mmap_sem;
    // 所有活动（active）mm的链表
	struct list_head mmlist;		/* List of maybe swapped mm's.	These are globally strung
						 * together off init_mm.mmlist, and are protected
						 * by mmlist_lock
						 */


	unsigned long hiwater_rss;	/* High-watermark of RSS usage */
	unsigned long hiwater_vm;	/* High-water virtual memory usage */

	unsigned long total_vm;		/* Total pages mapped */
	// 任务已经锁住的物理内存的大小。锁住的物理内存不能交换到硬盘
	unsigned long locked_vm;	/* Pages that have PG_mlocked set */
	unsigned long pinned_vm;	/* Refcount permanently increased */
	unsigned long data_vm;		/* VM_WRITE & ~VM_SHARED & ~VM_STACK */
	unsigned long exec_vm;		/* VM_EXEC & ~VM_WRITE & ~VM_STACK */
    // 任务在用户态的栈的大小
	unsigned long stack_vm;		/* VM_STACK */
	unsigned long def_flags;
/*
可执行代码占用的虚拟地址空间区域, 其开始和结束分别通过 start_code和end_code标记.

start_data和end_data标记了包含已初始化数据的区域. 请注意, 
在ELF二进制文件映射到地址空间中之后,这些区域的长度不再改变
*/
	unsigned long start_code, end_code, start_data, end_data;
/*
堆的起始地址保存在start_brk, brk表示堆区域当前的结束地址. 
尽管堆的起始地址在进程生命周期中是不变的, 但堆的长度会发生变化,
因而brk的值也会变.
*/
	unsigned long start_brk, brk, start_stack;
/*
参数列表和环境变量的位置分别由arg_start和 arg_end、env_start和env_end描述. 
两个区域都位于栈中最高的区域
*/
	unsigned long arg_start, arg_end, env_start, env_end;

	unsigned long saved_auxv[AT_VECTOR_SIZE]; /* for /proc/PID/auxv */

	/*
	 * Special counters, in some configurations protected by the
	 * page_table_lock, in other configurations by being atomic.
	 */
	struct mm_rss_stat rss_stat;

	struct linux_binfmt *binfmt;
    /*用于懒惰TLB交换的位掩码*/ 
	cpumask_var_t cpu_vm_mask_var;

	/* Architecture-specific MM context */
	// 存放着当前进程使用的段起始地址
	mm_context_t context;

	unsigned long flags; /* Must use atomic bitops to access the bits */

	struct core_state *core_state; /* coredumping support */
#ifdef CONFIG_AIO
	spinlock_t			ioctx_lock;
	struct kioctx_table __rcu	*ioctx_table;
#endif
#ifdef CONFIG_MEMCG
	/*
	 * "owner" points to a task that is regarded as the canonical
	 * user/owner of this mm. All of the following must be true in
	 * order for it to be changed:
	 *
	 * current == mm->owner
	 * current->mm != mm
	 * new_owner->mm == mm
	 * new_owner->alloc_lock is held
	 */
	struct task_struct __rcu *owner;
#endif
	struct user_namespace *user_ns;

	/* store ref to file /proc/<pid>/exe symlink points to */
	struct file __rcu *exe_file;
#ifdef CONFIG_MMU_NOTIFIER
	struct mmu_notifier_mm *mmu_notifier_mm;
#endif
#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && !USE_SPLIT_PMD_PTLOCKS
	pgtable_t pmd_huge_pte; /* protected by page_table_lock */
#endif
#ifdef CONFIG_CPUMASK_OFFSTACK
	struct cpumask cpumask_allocation;
#endif
#ifdef CONFIG_NUMA_BALANCING
	/*
	 * numa_next_scan is the next time that the PTEs will be marked
	 * pte_numa. NUMA hinting faults will gather statistics and migrate
	 * pages to new nodes if necessary.
	 */
	unsigned long numa_next_scan;

	/* Restart point for scanning and setting pte_numa */
	unsigned long numa_scan_offset;

	/* numa_scan_seq prevents two threads setting pte_numa */
	int numa_scan_seq;
#endif
#if defined(CONFIG_NUMA_BALANCING) || defined(CONFIG_COMPACTION)
	/*
	 * An operation with batched TLB flushing is going on. Anything that
	 * can move process memory needs to flush the TLB when moving a
	 * PROT_NONE or PROT_NUMA mapped page.
	 */
	bool tlb_flush_pending;
#endif
	struct uprobes_state uprobes_state;
#ifdef CONFIG_HUGETLB_PAGE
	atomic_long_t hugetlb_usage;
#endif
	struct work_struct async_put_work;
};

static inline void mm_init_cpumask(struct mm_struct *mm)
{
#ifdef CONFIG_CPUMASK_OFFSTACK
	mm->cpu_vm_mask_var = &mm->cpumask_allocation;
#endif
	cpumask_clear(mm->cpu_vm_mask_var);
}

/* Future-safe accessor for struct mm_struct's cpu_vm_mask. */
static inline cpumask_t *mm_cpumask(struct mm_struct *mm)
{
	return mm->cpu_vm_mask_var;
}

#if defined(CONFIG_NUMA_BALANCING) || defined(CONFIG_COMPACTION)
/*
 * Memory barriers to keep this state in sync are graciously provided by
 * the page table locks, outside of which no page table modifications happen.
 * The barriers below prevent the compiler from re-ordering the instructions
 * around the memory barriers that are already present in the code.
 */
static inline bool mm_tlb_flush_pending(struct mm_struct *mm)
{
	barrier();
	return mm->tlb_flush_pending;
}
static inline void set_tlb_flush_pending(struct mm_struct *mm)
{
	mm->tlb_flush_pending = true;

	/*
	 * Guarantee that the tlb_flush_pending store does not leak into the
	 * critical section updating the page tables
	 */
	smp_mb__before_spinlock();
}
/* Clearing is done after a TLB flush, which also provides a barrier. */
static inline void clear_tlb_flush_pending(struct mm_struct *mm)
{
	barrier();
	mm->tlb_flush_pending = false;
}
#else
static inline bool mm_tlb_flush_pending(struct mm_struct *mm)
{
	return false;
}
static inline void set_tlb_flush_pending(struct mm_struct *mm)
{
}
static inline void clear_tlb_flush_pending(struct mm_struct *mm)
{
}
#endif

struct vm_fault;

struct vm_special_mapping {
	const char *name;	/* The name, e.g. "[vdso]". */

	/*
	 * If .fault is not provided, this points to a
	 * NULL-terminated array of pages that back the special mapping.
	 *
	 * This must not be NULL unless .fault is provided.
	 */
	struct page **pages;

	/*
	 * If non-NULL, then this is called to resolve page faults
	 * on the special mapping.  If used, .pages is not checked.
	 */
	int (*fault)(const struct vm_special_mapping *sm,
		     struct vm_area_struct *vma,
		     struct vm_fault *vmf);

	int (*mremap)(const struct vm_special_mapping *sm,
		     struct vm_area_struct *new_vma);
};

enum tlb_flush_reason {
	TLB_FLUSH_ON_TASK_SWITCH,
	TLB_REMOTE_SHOOTDOWN,
	TLB_LOCAL_SHOOTDOWN,
	TLB_LOCAL_MM_SHOOTDOWN,
	TLB_REMOTE_SEND_IPI,
	NR_TLB_FLUSH_REASONS,
};

 /*
  * A swap entry has to fit into a "unsigned long", as the entry is hidden
  * in the "index" field of the swapper address space.
  */
typedef struct {
	unsigned long val;
} swp_entry_t;

#endif /* _LINUX_MM_TYPES_H */
