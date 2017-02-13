#ifndef _LINUX_MMZONE_H
#define _LINUX_MMZONE_H

#ifndef __ASSEMBLY__
#ifndef __GENERATING_BOUNDS_H

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/cache.h>
#include <linux/threads.h>
#include <linux/numa.h>
#include <linux/init.h>
#include <linux/seqlock.h>
#include <linux/nodemask.h>
#include <linux/pageblock-flags.h>
#include <linux/page-flags-layout.h>
#include <linux/atomic.h>
#include <asm/page.h>

/* Free memory management - zoned buddy allocator.  */
#ifndef CONFIG_FORCE_MAX_ZONEORDER
#define MAX_ORDER 11 // 一次分配可以请求的页数最大是2^11=2048
#else
#define MAX_ORDER CONFIG_FORCE_MAX_ZONEORDER
#endif
#define MAX_ORDER_NR_PAGES (1 << (MAX_ORDER - 1))

/*
 * PAGE_ALLOC_COSTLY_ORDER is the order at which allocations are deemed
 * costly to service.  That is between allocation orders which should
 * coalesce naturally under reasonable reclaim pressure and those which
 * will not.
 */
/* 内核认为超过8个页算是大的内存分配 */
#define PAGE_ALLOC_COSTLY_ORDER 3

enum {
/*
不可以移动, 内核中大部分数据是这样的
*/
	MIGRATE_UNMOVABLE,
/*
不能够直接移动,但是可以删除,
而内容则可以从某些源重新生成.如文件数据映射的页面则归属此类
*/
	MIGRATE_MOVABLE,
/*
可以移动。分配给用户态程序运行的用户空间页面则为该类.
由于是通过页面映射而得,将其复制到新位置后,
更新映射表项,重新映射,应用程序是不感知的.
*/
	MIGRATE_RECLAIMABLE,
/*
      是一个分界数,小于这个数的都是在pcplist上的
	是per_cpu_pageset, 即用来表示每CPU页框高速缓存的数据结构中的链表的迁移类型数目
*/
	MIGRATE_PCPTYPES,	/* the number of types on the pcp lists */
/*
	= MIGRATE_PCPTYPES, 在罕见的情况下，内核需要分配一个高阶的页面块
	而不能休眠.如果向具有特定可移动性的列表请求分配内存失败，
	这种紧急情况下可从MIGRATE_HIGHATOMIC中分配内存
*/
	MIGRATE_HIGHATOMIC = MIGRATE_PCPTYPES,
#ifdef CONFIG_CMA
	/*
	 * MIGRATE_CMA migration type is designed to mimic the way
	 * ZONE_MOVABLE works.  Only movable pages can be allocated
	 * from MIGRATE_CMA pageblocks and page allocator never
	 * implicitly change migration type of MIGRATE_CMA pageblock.
	 *
	 * The way to use it is to change migratetype of a range of
	 * pageblocks to MIGRATE_CMA which can be done by
	 * __free_pageblock_cma() function.  What is important though
	 * is that a range of pageblocks must be aligned to
	 * MAX_ORDER_NR_PAGES should biggest page be bigger then
	 * a single pageblock.
	 */
/*
     连续内存分配，
     用于避免预留大块内存导致系统可用内存减少而实现的，
     即当驱动不使用内存时，将其分配给用户使用，
     而需要时则通过回收或者迁移的方式将内存腾出来。
*/
	MIGRATE_CMA,
#endif
#ifdef CONFIG_MEMORY_ISOLATION
/*
是一个特殊的虚拟区域, 用于跨越NUMA结点移动物理内存页. 
在大型系统上, 它有益于将物理内存页移动到接近于使用该页最频繁的CPU
*/
	MIGRATE_ISOLATE,	/* can't allocate from here */
#endif
/*
只是表示迁移类型的数目, 也不代表具体的区域
*/
	MIGRATE_TYPES
};

/* In mm/page_alloc.c; keep in sync also with show_migration_types() there */
extern char * const migratetype_names[MIGRATE_TYPES];

#ifdef CONFIG_CMA
#  define is_migrate_cma(migratetype) unlikely((migratetype) == MIGRATE_CMA)
#  define is_migrate_cma_page(_page) (get_pageblock_migratetype(_page) == MIGRATE_CMA)
#else
#  define is_migrate_cma(migratetype) false
#  define is_migrate_cma_page(_page) false
#endif

#define for_each_migratetype_order(order, type) \
	for (order = 0; order < MAX_ORDER; order++) \
		for (type = 0; type < MIGRATE_TYPES; type++)

extern int page_group_by_mobility_disabled;

#define NR_MIGRATETYPE_BITS (PB_migrate_end - PB_migrate + 1)
#define MIGRATETYPE_MASK ((1UL << NR_MIGRATETYPE_BITS) - 1)

#define get_pageblock_migratetype(page)					\
	get_pfnblock_flags_mask(page, page_to_pfn(page),		\
			PB_migrate_end, MIGRATETYPE_MASK)

struct free_area {
/*
是用于连接空闲页的链表. 页链表包含大小相同的连续内存区
*/
	struct list_head	free_list[MIGRATE_TYPES]; 
/*
指定了当前内存区中空闲页块的数目（
对0阶内存区逐页计算，
对1阶内存区计算页对的数目，
对2阶内存区计算4页集合的数目，
依次类推
*/
    unsigned long		nr_free;
};

struct pglist_data;

/*
 * zone->lock and the zone lru_lock are two of the hottest locks in the kernel.
 * So add a wild amount of padding here to ensure that they fall into separate
 * cachelines.  There are very few zone structures in the machine, so space
 * consumption is not a concern here.
*/
#if defined(CONFIG_SMP)
/*
____cacheline_internodealigned_in_smp 展开为
__attribute__((__aligned__(1 << (INTERNODE_CACHE_SHIFT))))
#define INTERNODE_CACHE_SHIFT L1_CACHE_SHIFT
#define L1_CACHE_SHIFT		CONFIG_ARM_L1_CACHE_SHIFT
#define CONFIG_ARM_L1_CACHE_SHIFT 6
表示64对齐
char x[0] 是一个柔性数组，编译器会根据64字节对齐，自动决定大小
*/
struct zone_padding {
	char x[0];
} ____cacheline_internodealigned_in_smp;
#define ZONE_PADDING(name)	struct zone_padding name;
#else
#define ZONE_PADDING(name)
#endif

enum zone_stat_item {
	/* First 128 byte cacheline (assuming 64 bit words) */
	NR_FREE_PAGES,
	NR_ZONE_LRU_BASE, /* Used only for compaction and reclaim retry */
	NR_ZONE_INACTIVE_ANON = NR_ZONE_LRU_BASE,
	NR_ZONE_ACTIVE_ANON,
	NR_ZONE_INACTIVE_FILE,
	NR_ZONE_ACTIVE_FILE,
	NR_ZONE_UNEVICTABLE,
	NR_ZONE_WRITE_PENDING,	/* Count of dirty, writeback and unstable pages */
	NR_MLOCK,		/* mlock()ed pages found and moved off LRU */
	NR_SLAB_RECLAIMABLE,
	NR_SLAB_UNRECLAIMABLE,
	NR_PAGETABLE,		/* used for pagetables */
	NR_KERNEL_STACK_KB,	/* measured in KiB */
	/* Second 128 byte cacheline */
	NR_BOUNCE,
#if IS_ENABLED(CONFIG_ZSMALLOC)
	NR_ZSPAGES,		/* allocated in zsmalloc */
#endif
#ifdef CONFIG_NUMA
	NUMA_HIT,		/* allocated in intended node */
	NUMA_MISS,		/* allocated in non intended node */
	NUMA_FOREIGN,		/* was intended here, hit elsewhere */
	NUMA_INTERLEAVE_HIT,	/* interleaver preferred this zone */
	NUMA_LOCAL,		/* allocation from local node */
	NUMA_OTHER,		/* allocation from other node */
#endif
	NR_FREE_CMA_PAGES,
	NR_VM_ZONE_STAT_ITEMS };

enum node_stat_item {
	NR_LRU_BASE,
	NR_INACTIVE_ANON = NR_LRU_BASE, /* must match order of LRU_[IN]ACTIVE */
	NR_ACTIVE_ANON,		/*  "     "     "   "       "         */
	NR_INACTIVE_FILE,	/*  "     "     "   "       "         */
	NR_ACTIVE_FILE,		/*  "     "     "   "       "         */
	NR_UNEVICTABLE,		/*  "     "     "   "       "         */
	NR_ISOLATED_ANON,	/* Temporary isolated pages from anon lru */
	NR_ISOLATED_FILE,	/* Temporary isolated pages from file lru */
	NR_PAGES_SCANNED,	/* pages scanned since last reclaim */
	WORKINGSET_REFAULT,
	WORKINGSET_ACTIVATE,
	WORKINGSET_NODERECLAIM,
	NR_ANON_MAPPED,	/* Mapped anonymous pages */
	NR_FILE_MAPPED,	/* pagecache pages mapped into pagetables.
			   only modified from process context */
	NR_FILE_PAGES,
	NR_FILE_DIRTY,
	NR_WRITEBACK,
	NR_WRITEBACK_TEMP,	/* Writeback using temporary buffers */
	NR_SHMEM,		/* shmem pages (included tmpfs/GEM pages) */
	NR_SHMEM_THPS,
	NR_SHMEM_PMDMAPPED,
	NR_ANON_THPS,
	NR_UNSTABLE_NFS,	/* NFS unstable pages */
	NR_VMSCAN_WRITE,
	NR_VMSCAN_IMMEDIATE,	/* Prioritise for reclaim when writeback ends */
	NR_DIRTIED,		/* page dirtyings since bootup */
	NR_WRITTEN,		/* page writings since bootup */
	NR_VM_NODE_STAT_ITEMS
};

/*
 * We do arithmetic on the LRU lists in various places in the code,
 * so it is important to keep the active lists LRU_ACTIVE higher in
 * the array than the corresponding inactive lists, and to keep
 * the *_FILE lists LRU_FILE higher than the corresponding _ANON lists.
 *
 * This has to be kept in sync with the statistics in zone_stat_item
 * above and the descriptions in vmstat_text in mm/vmstat.c
 */
/*
 LRU = Least Recently Used
*/
#define LRU_BASE 0
#define LRU_ACTIVE 1
#define LRU_FILE 2
/*
Active/inactive memory是针对用户进程所占用的内存而言的，
内核占用的内存（包括slab）不在其中。

如果Inactive list很大，表明在必要时可以回收的页面很多；

*/
enum lru_list {
	LRU_INACTIVE_ANON = LRU_BASE,
	/*
      anonymous pages 与文件无关的内存（比如进程的堆栈，用malloc申请的内存）
      anonymous pages 在发生换页时，是对交换区进行读/写操作
	*/
	LRU_ACTIVE_ANON = LRU_BASE + LRU_ACTIVE,  
	LRU_INACTIVE_FILE = LRU_BASE + LRU_FILE,
    /*
        File-backed pages 与文件关联的内存（比如程序文件、数据文件所对应的内存页）
        File-backed pages 在发生换页(page-in或page-out)时，是从它对应的文件读入或写出
      */
	LRU_ACTIVE_FILE = LRU_BASE + LRU_FILE + LRU_ACTIVE, 
	LRU_UNEVICTABLE,
	NR_LRU_LISTS
};

#define for_each_lru(lru) for (lru = 0; lru < NR_LRU_LISTS; lru++)

#define for_each_evictable_lru(lru) for (lru = 0; lru <= LRU_ACTIVE_FILE; lru++)

static inline int is_file_lru(enum lru_list lru)
{
	return (lru == LRU_INACTIVE_FILE || lru == LRU_ACTIVE_FILE);
}

static inline int is_active_lru(enum lru_list lru)
{
	return (lru == LRU_ACTIVE_ANON || lru == LRU_ACTIVE_FILE);
}

struct zone_reclaim_stat {
	/*
	 * The pageout code in vmscan.c keeps track of how many of the
	 * mem/swap backed and file backed pages are referenced.
	 * The higher the rotated/scanned ratio, the more valuable
	 * that cache is.
	 *
	 * The anon LRU stats live in [0], file LRU stats in [1]
	 */
	unsigned long		recent_rotated[2];
	unsigned long		recent_scanned[2];
};

struct lruvec {
	struct list_head		lists[NR_LRU_LISTS];
	/**
          * 页面回收状态。
          */
	struct zone_reclaim_stat	reclaim_stat;
	/* Evictions & activations on the inactive file list */
	atomic_long_t			inactive_age;
#ifdef CONFIG_MEMCG
	struct pglist_data *pgdat;
#endif
};

/* Mask used at gathering information at once (see memcontrol.c) */
#define LRU_ALL_FILE (BIT(LRU_INACTIVE_FILE) | BIT(LRU_ACTIVE_FILE))
#define LRU_ALL_ANON (BIT(LRU_INACTIVE_ANON) | BIT(LRU_ACTIVE_ANON))
#define LRU_ALL	     ((1 << NR_LRU_LISTS) - 1)

/* Isolate clean file */
#define ISOLATE_CLEAN		((__force isolate_mode_t)0x1)
/* Isolate unmapped file */
#define ISOLATE_UNMAPPED	((__force isolate_mode_t)0x2)
/* Isolate for asynchronous migration */
#define ISOLATE_ASYNC_MIGRATE	((__force isolate_mode_t)0x4)
/* Isolate unevictable pages */
#define ISOLATE_UNEVICTABLE	((__force isolate_mode_t)0x8)

/* LRU Isolation modes. */
typedef unsigned __bitwise isolate_mode_t;

enum zone_watermarks {
/*
当空闲页面的数量达到page_min所标定的数量的时候， 
说明页面数非常紧张, 分配页面的动作和kswapd线程同步运行.
WMARK_MIN所表示的page的数量值，是在内存初始化的过程中调用free_area_init_core
中计算的。这个数值是根据zone中的page的数量除以一个>1的系数来确定的。
通常是这样初始化的ZoneSizeInPages/12
*/
	WMARK_MIN,
/*
当空闲页面的数量达到WMARK_LOW所标定的数量的时候，
说明页面刚开始紧张, 则kswapd线程将被唤醒，并开始释放回收页面
*/
	WMARK_LOW,
/*
当空闲页面的数量达到page_high所标定的数量的时候， 
说明内存页面数充足, 不需要回收, kswapd线程将重新休眠，
通常这个数值是page_min的3倍
*/
	WMARK_HIGH,
	NR_WMARK
};

#define min_wmark_pages(z) (z->watermark[WMARK_MIN])
#define low_wmark_pages(z) (z->watermark[WMARK_LOW])
#define high_wmark_pages(z) (z->watermark[WMARK_HIGH])
/*
每个zone都会有一个这个结构用于缓存页的分配, 
中文一般就叫高速缓存.
*/
struct per_cpu_pages {
	//当前数量
	int count;		/* number of pages in the list */
	//高水线，高于此数将内存还给伙伴系统
	int high;		/* high watermark, emptying needed */
	//一次性添加和删除的页面数量
	int batch;		/* chunk size for buddy add/remove */

	/* Lists of pages, one per migrate type stored on the pcp-lists */
	//缓存的页链表
	struct list_head lists[MIGRATE_PCPTYPES];
};

/**
 * 每个zone上面的pageset缓存页。
 */
struct per_cpu_pageset {
	struct per_cpu_pages pcp;
#ifdef CONFIG_NUMA
	s8 expire;
#endif
#ifdef CONFIG_SMP
	s8 stat_threshold;
	s8 vm_stat_diff[NR_VM_ZONE_STAT_ITEMS];
#endif
};

struct per_cpu_nodestat {
	s8 stat_threshold;
	s8 vm_node_stat_diff[NR_VM_NODE_STAT_ITEMS];
};

#endif /* !__GENERATING_BOUNDS.H */

/*
实际的计算机体系结构有硬件的诸多限制, 这限制了页框可以使用的方式. 
尤其是, Linux内核必须处理80x86体系结构的两种硬件约束.
ISA总线的直接内存存储DMA处理器有一个严格的限制 : 他们只能对RAM的前16MB进行寻址
在具有大容量RAM的现代32位计算机中, CPU不能直接访问所有的物理地址, 
因为线性地址空间太小, 内核不可能直接映射所有物理内存到线性地址空间, 
因此Linux内核对不同区域的内存需要采用不同的管理方式和映射方式, 
因此内核将物理地址或者成用zone_t表示的不同地址区域

传统X86_32位系统中, 前16M划分给ZONE_DMA, 
该区域包含的页框可以由老式的基于ISAS的设备通过DMA使用”直接内存访问(DMA)”,
 ZONE_DMA和ZONE_NORMAL区域包含了内存的常规页框, 
通过把他们线性的映射到现行地址的第4个GB, 内核就可以直接进行访问, 
相反ZONE_HIGHME包含的内存页不能由内核直接访问, 
尽管他们也线性地映射到了现行地址空间的第4个GB. 


64位系统中, 并不需要高端内存, 因为AM64的linux采用4级页表，
支持的最大物理内存为64TB, 对于虚拟地址空间的划分，
将0x0000,0000,0000,0000 – 0x0000,7fff,ffff,f000这128T地址用于用户空间；
而0xffff,8000,0000,0000以上的128T为系统空间地址, 
这远大于当前我们系统中的内存空间, 
因此所有的物理地址都可以直接映射到内核中, 
不需要高端内存的特殊映射.

对于Arm 实际上只有ZONE_NORMAL和ZONE_MOVABLE 
*/
enum zone_type {
#ifdef CONFIG_ZONE_DMA
	/*
	 * ZONE_DMA is used when there are devices that are not able
	 * to do DMA to all of addressable memory (ZONE_NORMAL). Then we
	 * carve out the portion of memory that is needed for these devices.
	 * The range is arch specific.
	 * 当有些设备不能使用所有的ZONE_NORMAL区域中的内存空间作DMA访问时，
	 * 就可以使用ZONE_DMA所表示的内存区域，于是我么把这部分空间划分出来
	 * 专门用作DMA访问的内存空间。
	 * 该区域的空间访问是处理器体系结构相关的
	 *
	 * Some examples(一些例子)
	 *
	 * Architecture(体系架构) Limit(限制)
	 * ---------------------------
	 * parisc, ia64, sparc	<4G
	 * s390			<2G
	 * arm			Various
	 * alpha		Unlimited or 0-16MB.
	 *
	 * i386, x86_64 and multiple other arches
	 * 			<16M.
	 */
/*
标记了适合DMA的内存域. 该区域的长度依赖于处理器类型.
 这是由于古老的ISA设备强加的边界. 
但是为了兼容性, 现代的计算机也可能受此影响

ARM所有地址都可以进行DMA，所以该值可以很大，
或者干脆不定义DMA类型的内存域。
而在IA-32的处理器上，一般定义为16M
*/
	ZONE_DMA,
#endif
#ifdef CONFIG_ZONE_DMA32
	/*
	 * x86_64 needs two ZONE_DMAs because it supports devices that are
	 * only able to do DMA to the lower 16M but also 32 bit devices that
	 * can only do DMA areas below 4G.
	 * X86_64架构因为除了支持只能使用低于16MB空间的DMA设备外，
	 * 还支持可以访问4GB以下空间的32位DMA设备，所以需要两个ZONE_DMA
	 * 内存区域
	 */
/*
标记了使用32位地址字可寻址, 适合DMA的内存域. 显然, 
只有在53位系统中ZONE_DMA32才和ZONE_DMA有区别, 
在32位系统中, 本区域是空的, 即长度为0MB, 在Alpha和AMD64系统上, 
该内存的长度可能是从0到4GB

只在64位系统上有效，为一些32位外设DMA时分配内存。
如果物理内存大于4G，该值为4G，
否则与实际的物理内存大小相同。
*/
	ZONE_DMA32,
#endif
	/*
	 * Normal addressable memory is in ZONE_NORMAL. DMA operations can be
	 * performed on pages in ZONE_NORMAL if the DMA devices support
	 * transfers to all addressable memory.
	 * 常规内存访问区域由ZONE_NORMAL标识，如果DMA设备可以在次区域
	 * 作内存访问，也可以使用本区域
	 */
/*
标记了可直接映射到内存段的普通内存域. 
这是在所有体系结构上保证会存在的唯一内存区域, 
但无法保证该地址范围对应了实际的物理地址. 

例如, 如果AMD64系统只有两2G内存, 那么所有的内存都属于ZONE_DMA32范围, 
而ZONE_NORMAL则为空

在64位系统上，如果物理内存小于4G，该内存域为空。
而在32位系统上，该值最大为896M
*/
	ZONE_NORMAL,
#ifdef CONFIG_HIGHMEM
	/*
	 * A memory area that is only addressable by the kernel through
	 * mapping portions into its own address space. This is for example
	 * used by i386 to allow the kernel to address the memory beyond
	 * 900MB. The kernel will set up special mappings (page
	 * table entries on i386) for each page that the kernel needs to
	 * access.
	 * 高端内存区域用ZONE_HIGHMEM标识，该区域无法从内核虚拟地址空间直接
	 * 做线性映射，所以为访问该区域必须经内核作特殊的页映射，比如在i386
	 * 体系上，内核空间1GB，除去其他一些开销，能对物理地址进行线性映射的
	 * 空间大约只有896MB，此时高于896MB以上的物理地址空间就叫ZONE_HIGHMEM
	 * 区域
	 */
/*
标记了超出内核虚拟地址空间的物理内存段, 
因此这段地址不能被内核直接映射

32位系统中, Linux内核虚拟地址空间只有1G, 
而0~895M这个986MB被用于DMA和直接映射, 剩余的物理内存被成为高端内存. 
那内核是如何借助剩余128MB高端内存地址空间是如何实现访问可以所有物理内存？
当内核想访问高于896MB物理地址内存时，从0xF8000000 ~ 0xFFFFFFFF地址空间范围
内找一段相应大小空闲的逻辑地址空间，借用一会。
借用这段逻辑地址空间，建立映射到想访问的那段物理内存（即填充内核PTE页面表），
临时用一会，用完后归还。这样别人也可以借用这段地址空间访问其他物理内存，
实现了使用有限的地址空间，访问所有所有物理内存
*/
	ZONE_HIGHMEM,
#endif
/*
伪内存域
在防止物理内存碎片的机制memory migration中需要使用该内存域. 
可以将虚拟地址对应的物理地址进行迁移
*/
	ZONE_MOVABLE,
#ifdef CONFIG_ZONE_DEVICE
/*
为支持热插拔设备而分配的Non Volatile Memory非易失性内存
*/
	ZONE_DEVICE,
#endif
	__MAX_NR_ZONES

};

#ifndef __GENERATING_BOUNDS_H
/*
为什么要按cache line进行对齐呢？
因为zone结构体会被频繁访问到，在多核环境下，
会有多个CPU同时访问同一个结构体，
为了避免彼此间的干扰，必须对每次访问进行加锁。

那么是不是要对整个zone加锁呢？一把锁锁住zone中所有的成员？
这应该不是最有效的方法。我们知道zone结构体很大，
访问zone最频繁的有两个两个场景，一是页面分配，二是页面回收，
这两种场景各自访问结构体zone中不同的成员。
如果一把锁锁住zone中所有成员的话，
当一个cpu正在处理页面分配的事情，
另一个cpu想要进行页面回收的处理，
却因为第一个cpu锁住了zone中所有成员而只好等待。
第二个CPU明知道此时可以“安全的”进行回收处理
（因为第一个CPU不会访问与回收处理相关的成员）
却也只能干着急。可见，我们应该将锁再细分。
zone中定义了两把锁，lock以及lru_lock，lock与页面分配有关，
lru_lock与页面回收有关。定义了两把锁，自然，
每把锁所控制的成员最好跟锁呆在一起，
最好是跟锁在同一个cache line中。
当CPU对某个锁进行上锁处理时，CPU会将锁从内存加载到cache中，
因为是以cache line为单位进行加载，
所以与锁紧挨着的成员也被加载到同一cache line中。
CPU上完锁想要访问相关的成员时，这些成员已经在cache line中了。

增加padding，会额外占用一些字节，
但内核中zone结构体的实例并不会太多(UMA下只有三个)，
相比效率的提升，损失这点空间是值得的

*/
struct zone {
	/* Read-mostly fields */

	/* zone watermarks, access with *_wmark_pages(zone) macros */
    /*
    内核线程kswapd检测到不同的水线值会进行不同的处理
    如果空闲页多于pages_high = watermark[WMARK_HIGH], 内存域状态理想，不需要进行内存回收
    如果空闲页低于pages_low = watermark[WMARK_LOW], 开始进行内存回收，将page换出到硬盘.
    如果空闲页低于pages_min = watermark[WMARK_MIN], 内存回收的压力很重，因为内存域中的可用page数已经很少了，必须加快进行内存回收
    */
	unsigned long watermark[NR_WMARK];

	unsigned long nr_reserved_highatomic;

	/*
	 * We don't know if the memory that we're going to allocate will be
	 * freeable or/and it will be released eventually, so to avoid totally
	 * wasting several GB of ram we must reserve some of the lower zone
	 * memory (otherwise we risk to run OOM on the lower zones despite
	 * there being tons of freeable ram on the higher zones).  This array is
	 * recalculated at runtime if the sysctl_lowmem_reserve_ratio sysctl
	 * changes.
	 */
    /*
      为各个内存域指定的保留内存，用于分配不能失败的关键性内存分配 
      当高端内存、normal内存区域中无法分配到内存时，需要从normal、DMA区域中分配内存。
      为了避免DMA区域被消耗光，需要额外保留一些内存供驱动使用。
      该字段就是指从上级内存区退到回内存区时，需要额外保留的内存数量。

      内核为内存域定义了一个“价值”的层次结构，
      按分配的“廉价度”依次为：ZONE_HIGHMEM > ZONE_NORMAL > ZONE_DMA。
      高端内存域是最廉价的，因为内核没有任何部分依赖于从该zone中分配内存，
      如果高端内存用尽，对内核没有任何副作用，这也是优先分配高端内存的原因。
      普通内存域有所不同，因为所有的内核数据都保存在该区域，
      如果用尽内核将面临紧急情况，甚至崩溃。
      DMA内存域是最昂贵的，因为它不但数量少很容易被用尽，
      而且被用于与外设进行DMA交互，一旦用尽则失去了与外设交互的能力。
      因此内核在进行内存分配时，优先从高端内存进行分配，
      其次是普通内存，最后才是DMA内存
      */
	long lowmem_reserve[MAX_NR_ZONES];

#ifdef CONFIG_NUMA
    /* 该zone所属的node的node id */
	int node;  
#endif
    /*  指向这个zone所在的pglist_data对象  */
    /**
         * 管理区所属性的节点。通过此字段可以找到同一节点的其他的管理区。
      */
	struct pglist_data	*zone_pgdat;
	//每CPU的页面缓存。
    /*
        内核经常请求和释放单个页框. 为了提升性能,
        每个内存管理区都定义了一个Per-CPU 的页面高速缓存. 
        所有”每CPU高速缓存”包含一些预先分配的页框, 
        他们被定义满足本地CPU发出的单一内存请求

        这个数组用于实现每个CPU的热/冷页帧列表。
        内核使用这些列表来保存可用于满足实现的“新鲜”页。
        但冷热页帧对应的高速缓存状态不同：
        有些页帧很可能在高速缓存中，因此可以快速访问，故称之为热的；
        未缓存的页帧与此相对，称之为冷的。
        
        page管理的数据结构对象，内部有一个page的列表(list)来管理。
        每个CPU维护一个page list，避免自旋锁的冲突。
        这个数组的大小和NR_CPUS(CPU的数量）有关，这个值是编译的时候确定的

         为什么是per-cpu? 
         因为每个cpu都可从当前zone中分配内存，而pageset本身实现的一个功能就是批量申请和释放
         修改为per-cpu可减小多个cpu在申请内存时的所竞争
      */
    /**
      * 每CPU的页面缓存。
      * 当分配单个页面时，首先从该缓存中分配页面。这样可以:
      *              避免使用全局的锁
      *              避免同一个页面反复被不同的CPU分配，引起缓存行的失效。
      *              避免将管理区中的大块分割成碎片。
      */
	struct per_cpu_pageset __percpu *pageset;

#ifndef CONFIG_SPARSEMEM
	/*
	 * Flags for a pageblock_nr_pages block. See pageblock-flags.h.
	 * In SPARSEMEM, this map is stored in struct mem_section
	 */
/*
    在内存释放时，为了能够快速的将内存块返回到正确的迁移链表上，
    内核提供了一个专门的字段pageblock_flags，
    用来跟踪包含pageblock_nr_pages个页的内存块的迁移类型，
    在启用大页的情况下pageblock_nr_pages与大页大小（2M）相同，
    否则pageblock_nr_pages=2^(MAX_ORDER-1)=2^10个page。

    pageblock_flags是一个比特位空间，每3位（保证能表示5种迁移类型）对应一个内存块；
    在使能SPARSEMEM（稀疏内存模型）的系统上，pageblock_flags被定义在struct mem_section中，
    否则定义在struct zone中。

    几点说明：
    1) pageblock_flags的空间是根据内存大小，在内存初始化时就分配好的，
        它的比特位总是可用的，与page是否由伙伴系统管理无关。
    2) 函数set_pageblock_migratetype(page, migratetype)用于设置以page为首的内存块的迁移类型，
        而get_pageblock_migratetype(page)用于获取以page为首的内存块的迁移类型。
    3) 所有页最开始都被标记为可移动的。
    4) 分配内存时，如果需要从备用列表中申请，内核优先申请更大的内存块。
        比如计划申请1个page的不可移动内存块，优先查找是否存在2^10个page的可回收内存块，
        如果不存在再查找是否存在2^9个page的可回收内存块，直到2^0。
        这样做是为了尽量避免向可回收和可移动内存块中引入碎片。
*/
	unsigned long		*pageblock_flags;
#endif /* CONFIG_SPARSEMEM */

	/* zone_start_pfn == zone_start_paddr >> PAGE_SHIFT */
    // 只内存域的第一个页帧
/*
和node_start_pfn的含义一样。
这个成员是用于表示zone中的开始那个page在物理内存中的位置的present_pages， 
spanned_pages: 和node中的类似的成员含义一样
*/
	unsigned long		zone_start_pfn;

	/*
	 * spanned_pages is the total pages spanned by the zone, including
	 * holes, which is calculated as:
	 * 	spanned_pages = zone_end_pfn - zone_start_pfn;
	 *
	 * present_pages is physical pages existing within the zone, which
	 * is calculated as:
	 *	present_pages = spanned_pages - absent_pages(pages in holes);
	 *
	 * managed_pages is present pages managed by the buddy system, which
	 * is calculated as (reserved_pages includes pages allocated by the
	 * bootmem allocator):
	 *	managed_pages = present_pages - reserved_pages;
	 *
	 * So present_pages may be used by memory hotplug or memory power
	 * management logic to figure out unmanaged pages by checking
	 * (present_pages - managed_pages). And managed_pages should be used
	 * by page allocator and vm scanner to calculate all kinds of watermarks
	 * and thresholds.
	 *
	 * Locking rules:
	 *
	 * zone_start_pfn and spanned_pages are protected by span_seqlock.
	 * It is a seqlock because it has to be read outside of zone->lock,
	 * and it is done in the main allocator path.  But, it is written
	 * quite infrequently.
	 *
	 * The span_seq lock is declared along with zone->lock because it is
	 * frequently read in proximity to zone->lock.  It's good to
	 * give them a chance of being in the same cacheline.
	 *
	 * Write access to present_pages at runtime should be protected by
	 * mem_hotplug_begin/end(). Any reader who can't tolerant drift of
	 * present_pages should get_online_mems() to get a stable value.
	 *
	 * Read access to managed_pages should be safe because it's unsigned
	 * long. Write access to zone->managed_pages and totalram_pages are
	 * protected by managed_page_count_lock at runtime. Idealy only
	 * adjust_managed_page_count() should be used instead of directly
	 * touching zone->managed_pages and totalram_pages.
	 */
	//该zone管理的页面数量
	unsigned long		managed_pages;
    /*  总页数，包含空洞  */
	unsigned long		spanned_pages;
	/*  可用页数，不包含空洞  */
	unsigned long		present_pages;  
    // zone的名字，字符串表示： “DMA”，”Normal” 和”HighMem”
	const char		*name;

#ifdef CONFIG_MEMORY_ISOLATION
	/*
	 * Number of isolated pageblock. It is used to solve incorrect
	 * freepage counting problem due to racy retrieving migratetype
	 * of pageblock. Protected by zone->lock.
	 */
	unsigned long		nr_isolate_pageblock;
#endif

#ifdef CONFIG_MEMORY_HOTPLUG
	/* see spanned/present_pages for more description */
	/**
      * 用于保护spanned/present_pages等变量。这些变量几乎不会发生变化，除非发生了内存热插拨操作。
      * 这几个变量并不被lock字段保护。并且主要用于读，因此使用读写锁。
      */
	seqlock_t		span_seqlock;
#endif

	int initialized;

	/* Write-intensive fields used from the page allocator */
	ZONE_PADDING(_pad1_)

	/* free areas of different sizes */
    /*  页面使用状态的信息，以每个bit标识对应的page是否可以分配
           是用于伙伴系统的，每个数组元素指向对应阶也表的数组开头
           以下是供页帧回收扫描器(page reclaim scanner)访问的字段
           scanner会跟据页帧的活动情况对内存域中使用的页进行编目
           如果页帧被频繁访问，则是活动的，相反则是不活动的，
           在需要换出页帧时，这样的信息是很重要的：  
      */
    /**
      * 伙伴系统的主要变量。这个数组定义了11个队列，每个队列中的元素都是大小为2^n的页面块。
      */
	struct free_area	free_area[MAX_ORDER];

	/* zone flags, see below */
    /* 描述当前内存的状态, 参见下面的enum zone_flags结构 */
	unsigned long		flags;

	/* Primarily protects free_area */
    /* 对zone并发访问的保护的自旋锁  */
  /**
      * 该锁用于保护伙伴系统数据结构。即保护free_area相关数据。
      */
	spinlock_t		lock;

	/* Write-intensive fields used by compaction and vmstats. */
	ZONE_PADDING(_pad2_)

	/*
	 * When free pages are below this point, additional steps are taken
	 * when reading the number of free pages to avoid per-cpu counter
	 * drift allowing watermarks to be breached
       在空闲页的数目少于这个点percpu_drift_mark的时候
       当读取和空闲页数一样的内存页时，系统会采取额外的工作，
       防止单CPU页数漂移，从而导致水印被破坏。
      */
	unsigned long percpu_drift_mark;

#if defined CONFIG_COMPACTION || defined CONFIG_CMA
	/* pfn where compaction free scanner should start */
	unsigned long		compact_cached_free_pfn;
	/* pfn where async and sync compaction migration scanner should start */
	unsigned long		compact_cached_migrate_pfn[2];
#endif

#ifdef CONFIG_COMPACTION
	/*
	 * On compaction failure, 1<<compact_defer_shift compactions
	 * are skipped before trying again. The number attempted since
	 * last failure is tracked with compact_considered.
	 */
    /**
      * 这两个字段用于内存紧缩。其目的是为了避免内存外碎片。
      * 通过页面迁移，将已经分配的页面合并到一起，
      * 未分配页面合并到一起，这样可用页面将形成大的块，减少外碎片。
      * 这两个标志用于判断是否需要在本管理区进行内在紧缩。
      */
	unsigned int		compact_considered;
	unsigned int		compact_defer_shift;
	int			compact_order_failed;
#endif

#if defined CONFIG_COMPACTION || defined CONFIG_CMA
	/* Set to true when the PG_migrate_skip bits should be cleared */
	bool			compact_blockskip_flush;
#endif

	bool			contiguous;

	ZONE_PADDING(_pad3_)
	/* Zone statistics 内存域的统计信息, 参见后面的enum zone_stat_item结构*/
	atomic_long_t		vm_stat[NR_VM_ZONE_STAT_ITEMS];
} ____cacheline_internodealigned_in_smp;

enum pgdat_flags {
/* 标识当前区域中有很多脏页 */
	PGDAT_CONGESTED,		/* pgdat has many dirty pages backed by
					 * a congested BDI
					 */
/* 用于标识最近的一次页面扫描中, LRU算法发现了很多脏的页面 */
	PGDAT_DIRTY,			/* reclaim scanning has recently found
					 * many dirty file pages at the tail
					 * of the LRU.
					 */
/* 最近的回收扫描发现有很多页在写回 */
	PGDAT_WRITEBACK,		/* reclaim scanning has recently found
					 * many pages under writeback
					 */
/*
防止并发回收, 在SMP上系统, 多个CPU可能试图并发的回收亿i个内存域.
 PGDAT_RECLAIM_LOCKED标志可防止这种情况: 
如果一个CPU在回收某个内存域, 则设置该标识. 这防止了其他CPU的尝试
*/
	PGDAT_RECLAIM_LOCKED,		/* prevents concurrent reclaim */
};

static inline unsigned long zone_end_pfn(const struct zone *zone)
{
	return zone->zone_start_pfn + zone->spanned_pages;
}

static inline bool zone_spans_pfn(const struct zone *zone, unsigned long pfn)
{
	return zone->zone_start_pfn <= pfn && pfn < zone_end_pfn(zone);
}

static inline bool zone_is_initialized(struct zone *zone)
{
	return zone->initialized;
}

static inline bool zone_is_empty(struct zone *zone)
{
	return zone->spanned_pages == 0;
}

/*
 * The "priority" of VM scanning is how much of the queues we will scan in one
 * go. A value of 12 for DEF_PRIORITY implies that we will scan 1/4096th of the
 * queues ("queue_length >> 12") during an aging round.
 */
#define DEF_PRIORITY 12

/* Maximum number of zones on a zonelist */
#define MAX_ZONES_PER_ZONELIST (MAX_NUMNODES * MAX_NR_ZONES)

enum {
	ZONELIST_FALLBACK,	/* zonelist with fallback */
#ifdef CONFIG_NUMA
	/*
	 * The NUMA zonelists are doubled because we need zonelists that
	 * restrict the allocations to a single node for __GFP_THISNODE.
	 */
	ZONELIST_NOFALLBACK,	/* zonelist without fallback (__GFP_THISNODE) */
#endif
	MAX_ZONELISTS
};

/*
 * This struct contains information about a zone in a zonelist. It is stored
 * here to avoid dereferences into large structures and lookups of tables
 */
/**
 * 对zone的引用
 */
struct zoneref {
	//实际引用的zone指针
	struct zone *zone;	/* Pointer to actual zone */
	//在引用数组中的索引
	int zone_idx;		/* zone_idx(zoneref->zone) */
};

/*
 * One allocation request operates on a zonelist. A zonelist
 * is a list of zones, the first one is the 'goal' of the
 * allocation, the other zones are fallback zones, in decreasing
 * priority.
 *
 * To speed the reading of the zonelist, the zonerefs contain the zone index
 * of the entry being read. Helper functions to access information given
 * a struct zoneref are
 *
 * zonelist_zone()	- Return the struct zone * for an entry in _zonerefs
 * zonelist_zone_idx()	- Return the index of the zone for an entry
 * zonelist_node_idx()	- Return the index of the node for an entry
 */
/**
 * 用于内存分配的zone列表
 */
struct zonelist {
	/**
	 * 最大节点数*每个节点的zone数量，对所有zone进行引用。
	 */
	struct zoneref _zonerefs[MAX_ZONES_PER_ZONELIST + 1];
};

#ifndef CONFIG_DISCONTIGMEM
/* The array of struct pages - for discontigmem use pgdat->lmem_map */
/* 全局变量，存放所有物理页page对象的指针*/
extern struct page *mem_map;
#endif

/*
 * The pg_data_t structure is used in machines with CONFIG_DISCONTIGMEM
 * (mostly NUMA machines?) to denote a higher-level memory zone than the
 * zone denotes.
 *
 * On NUMA machines, each NUMA node would have a pg_data_t to describe
 * it's memory layout.
 *
 * Memory statistics and page replacement data structures are maintained on a
 * per-zone basis.
 */
struct bootmem_data;
/**
 * 内存区域，在非连续内存模型中，代表一块内存bank
 * 在NUMA系统中，一般表示一个NUMA节点。
 */
typedef struct pglist_data {
    /*  
    包含了结点中各内存域的数据结构 , 可能的区域类型用zone_type表示
    每个Node划分为不同的zone，分别为ZONE_DMA，ZONE_NORMAL，ZONE_HIGHMEM
    */
	struct zone node_zones[MAX_NR_ZONES];
	/*
     node_zonelists定义了一个zone搜索列表（每一项代表一个zone），
     当从某个node的某个zone申请内存失败后，会搜索该列表，
     查找一个合适的zone继续分配内存

    当调用free_area_init_core()时，
    由mm/page_alloc.c文件中的build_zonelists()函数设置
	*/
	struct zonelist node_zonelists[MAX_ZONELISTS];
    /*  
    当前节点中不同内存域zone的数量，1到3个之间。
    并不是所有的node都有3个zone的，比如一个CPU簇就可能没有ZONE_DMA区域 
    */
	int nr_zones;
#ifdef CONFIG_FLAT_NODE_MEM_MAP	/* means !SPARSEMEM */
    /*  
        平坦型的内存模型中，它指向本节点第一个页面的描述符
         node中的第一个page，它可以指向mem_map中的任何一个page，
        指向page实例数组的指针，用于描述该节点所拥有的的物理内存页，

        linux为每个物理页分配了一个struct page的管理结构体，
        并形成了一个结构体数组，node_mem_map即为数组的指针；
        pfn_to_page和page_to_pfn都借助该数组实现
     */
	struct page *node_mem_map;
#ifdef CONFIG_PAGE_EXTENSION
	struct page_ext *node_page_ext;
#endif
#endif
#ifndef CONFIG_NO_BOOTMEM
    /*  在系统启动boot期间，内存管理子系统初始化之前，
           内核页需要使用内存（另外，还需要保留部分内存用于初始化内存管理子系统）
           为解决这个问题，内核使用了自举内存分配器 
           此结构用于这个阶段的内存管理  */
	struct bootmem_data *bdata;
#endif
#ifdef CONFIG_MEMORY_HOTPLUG
	/*
	 * Must be held any time you expect node_start_pfn, node_present_pages
	 * or node_spanned_pages stay constant.  Holding this will also
	 * guarantee that any pfn_valid() stays that way.
	 *
	 * pgdat_resize_lock() and pgdat_resize_unlock() are provided to
	 * manipulate node_size_lock without checking for CONFIG_MEMORY_HOTPLUG.
	 *
	 * Nests above zone->lock and zone->span_seqlock
	 */
    /*
        当系统支持内存热插拨时，用于保护本结构中的与节点大小相关的字段。
        哪调用node_start_pfn，node_present_pages，node_spanned_pages相关的代码时，需要使用该锁。
      */
	spinlock_t node_size_lock;
#endif
    /*
        起始页面帧号，指出该节点在全局mem_map中的偏移
        系统中所有的页帧是依次编号的，
        每个页帧的号码都是全局唯一的（不只是结点内唯一）  
        
        pfn是page frame number的缩写。
        这个成员是用于表示node中的开始那个page在物理内存中的位置的。
        是当前NUMA节点的第一个页帧的编号，系统中所有的页帧是依次进行编号的，
        这个字段代表的是当前节点的页帧的起始值，对于UMA系统，
        只有一个节点，所以该值总是0
     */
	unsigned long node_start_pfn;
/*
	node中的真正可以使用的page数量 
*/
	unsigned long node_present_pages; /* total number of physical pages  */
/*
     node中page数量(包含空洞)
*/
	unsigned long node_spanned_pages; /* total size of physical page  range, including holes*/
	int node_id; /*  全局结点ID，系统中的NUMA结点都从0开始编号  */
	/*  node的等待队列，交换守护列队进程的等待列表*/
	wait_queue_head_t kswapd_wait; 
	wait_queue_head_t pfmemalloc_wait;
	/* 指向负责回收该node中内存的swap内核线程kswapd，
	一个node对应一个kswapd */
	struct task_struct *kswapd;	/* Protected by   mem_hotplug_begin/end() */
    /*  
        定义需要释放的区域的长度  
     */
	int kswapd_order;              
    /**
        * 临时变量，用于内存回收时使用。记录上一次回收时，扫描的最后一个区域。
      */
	enum zone_type kswapd_classzone_idx;

#ifdef CONFIG_COMPACTION
	int kcompactd_max_order;
	enum zone_type kcompactd_classzone_idx;
	wait_queue_head_t kcompactd_wait;
	struct task_struct *kcompactd;
#endif
#ifdef CONFIG_NUMA_BALANCING
	/* Lock serializing the migrate rate limiting window */
	spinlock_t numabalancing_migrate_lock;

	/* Rate limiting time interval */
	unsigned long numabalancing_migrate_next_window;

	/* Number of pages migrated during the rate limiting time interval */
	unsigned long numabalancing_migrate_nr_pages;
#endif
	/*
	 * This is a per-node reserve of pages that are not available
	 * to userspace allocations.
	 */
    // 每个区域保留的不能被用户空间分配的页面数目
	unsigned long		totalreserve_pages;

#ifdef CONFIG_NUMA
	/*
	 * zone reclaim becomes active if more unmapped pages exist.
	 对应的page个数大于这两个值时，开始进行回收 
	*/
	unsigned long		min_unmapped_pages;
	/**
      * 当管理区中，用于slab的可回收页大于此值时，将回收slab中的缓存页。
      */
	unsigned long		min_slab_pages;
#endif /* CONFIG_NUMA */

	/* Write-intensive fields used by page reclaim */
	 /**
      * 填充的未用字段，确保后面的字段是缓存行对齐的。
      */
	ZONE_PADDING(_pad1_)
	/**
      * lru相关的字段用于内存回收。这个字段用于保护这几个回收相关的字段。
      * lru用于确定哪些字段是活跃的，哪些不是活跃的，并据此确定应当被写回到磁盘以释放内存。
      * LRU(最近最少使用算法)活动以及非活动链表使用的自旋锁  
      */
	spinlock_t		lru_lock; 

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
	/*
	 * If memory initialisation on large machines is deferred then this
	 * is the first PFN that needs to be initialised.
	 */
	unsigned long first_deferred_pfn;
#endif /* CONFIG_DEFERRED_STRUCT_PAGE_INIT */

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	spinlock_t split_queue_lock;
	struct list_head split_queue;
	unsigned long split_queue_len;
#endif

	/* Fields commonly accessed by the page reclaim scanner */
	struct lruvec		lruvec;

	/*
	 * The target ratio of ACTIVE_ANON to INACTIVE_ANON pages on
	 * this node's LRU.  Maintained by the pageout code.
	 */
     /**
          * 将匿名活动页向匿名非活动页中移动的比率。
       */
	unsigned int inactive_ratio;

	unsigned long		flags;

	ZONE_PADDING(_pad2_)

	/* Per-node vmstats */
	struct per_cpu_nodestat __percpu *per_cpu_nodestats;
	/**
      * 一些统计信息，如可用页面数。
      */
	atomic_long_t		vm_stat[NR_VM_NODE_STAT_ITEMS];
} pg_data_t;

#define node_present_pages(nid)	(NODE_DATA(nid)->node_present_pages)
#define node_spanned_pages(nid)	(NODE_DATA(nid)->node_spanned_pages)
#ifdef CONFIG_FLAT_NODE_MEM_MAP
#define pgdat_page_nr(pgdat, pagenr)	((pgdat)->node_mem_map + (pagenr))
#else
#define pgdat_page_nr(pgdat, pagenr)	pfn_to_page((pgdat)->node_start_pfn + (pagenr))
#endif
#define nid_page_nr(nid, pagenr) 	pgdat_page_nr(NODE_DATA(nid),(pagenr))

#define node_start_pfn(nid)	(NODE_DATA(nid)->node_start_pfn)
#define node_end_pfn(nid) pgdat_end_pfn(NODE_DATA(nid))
static inline spinlock_t *zone_lru_lock(struct zone *zone)
{
	return &zone->zone_pgdat->lru_lock;
}

static inline struct lruvec *node_lruvec(struct pglist_data *pgdat)
{
	return &pgdat->lruvec;
}

static inline unsigned long pgdat_end_pfn(pg_data_t *pgdat)
{
	return pgdat->node_start_pfn + pgdat->node_spanned_pages;
}

static inline bool pgdat_is_empty(pg_data_t *pgdat)
{
	return !pgdat->node_start_pfn && !pgdat->node_spanned_pages;
}

static inline int zone_id(const struct zone *zone)
{
	struct pglist_data *pgdat = zone->zone_pgdat;

	return zone - pgdat->node_zones;
}

#ifdef CONFIG_ZONE_DEVICE
static inline bool is_dev_zone(const struct zone *zone)
{
	return zone_id(zone) == ZONE_DEVICE;
}
#else
static inline bool is_dev_zone(const struct zone *zone)
{
	return false;
}
#endif

#include <linux/memory_hotplug.h>

extern struct mutex zonelists_mutex;
void build_all_zonelists(pg_data_t *pgdat, struct zone *zone);
void wakeup_kswapd(struct zone *zone, int order, enum zone_type classzone_idx);
bool __zone_watermark_ok(struct zone *z, unsigned int order, unsigned long mark,
			 int classzone_idx, unsigned int alloc_flags,
			 long free_pages);
bool zone_watermark_ok(struct zone *z, unsigned int order,
		unsigned long mark, int classzone_idx,
		unsigned int alloc_flags);
bool zone_watermark_ok_safe(struct zone *z, unsigned int order,
		unsigned long mark, int classzone_idx);
enum memmap_context {
	MEMMAP_EARLY,
	MEMMAP_HOTPLUG,
};
extern int init_currently_empty_zone(struct zone *zone, unsigned long start_pfn,
				     unsigned long size);

extern void lruvec_init(struct lruvec *lruvec);

static inline struct pglist_data *lruvec_pgdat(struct lruvec *lruvec)
{
#ifdef CONFIG_MEMCG
	return lruvec->pgdat;
#else
	return container_of(lruvec, struct pglist_data, lruvec);
#endif
}

extern unsigned long lruvec_lru_size(struct lruvec *lruvec, enum lru_list lru);

#ifdef CONFIG_HAVE_MEMORY_PRESENT
void memory_present(int nid, unsigned long start, unsigned long end);
#else
static inline void memory_present(int nid, unsigned long start, unsigned long end) {}
#endif

#ifdef CONFIG_HAVE_MEMORYLESS_NODES
int local_memory_node(int node_id);
#else
static inline int local_memory_node(int node_id) { return node_id; };
#endif

#ifdef CONFIG_NEED_NODE_MEMMAP_SIZE
unsigned long __init node_memmap_size_bytes(int, unsigned long, unsigned long);
#endif

/*
 * zone_idx() returns 0 for the ZONE_DMA zone, 1 for the ZONE_NORMAL zone, etc.
 */
#define zone_idx(zone)		((zone) - (zone)->zone_pgdat->node_zones)

/*
 * Returns true if a zone has pages managed by the buddy allocator.
 * All the reclaim decisions have to use this function rather than
 * populated_zone(). If the whole zone is reserved then we can easily
 * end up with populated_zone() && !managed_zone().
 */
static inline bool managed_zone(struct zone *zone)
{
	return zone->managed_pages;
}

/* Returns true if a zone has memory */
static inline bool populated_zone(struct zone *zone)
{
	return zone->present_pages;
}

extern int movable_zone;

#ifdef CONFIG_HIGHMEM
static inline int zone_movable_is_highmem(void)
{
#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
	return movable_zone == ZONE_HIGHMEM;
#else
	return (ZONE_MOVABLE - 1) == ZONE_HIGHMEM;
#endif
}
#endif

static inline int is_highmem_idx(enum zone_type idx)
{
#ifdef CONFIG_HIGHMEM
	return (idx == ZONE_HIGHMEM ||
		(idx == ZONE_MOVABLE && zone_movable_is_highmem()));
#else
	return 0;
#endif
}

/**
 * is_highmem - helper function to quickly check if a struct zone is a 
 *              highmem zone or not.  This is an attempt to keep references
 *              to ZONE_{DMA/NORMAL/HIGHMEM/etc} in general code to a minimum.
 * @zone - pointer to struct zone variable
 */
static inline int is_highmem(struct zone *zone)
{
#ifdef CONFIG_HIGHMEM
	return is_highmem_idx(zone_idx(zone));
#else
	return 0;
#endif
}

/* These two functions are used to setup the per zone pages min values */
struct ctl_table;
int min_free_kbytes_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
int watermark_scale_factor_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
extern int sysctl_lowmem_reserve_ratio[MAX_NR_ZONES-1];
int lowmem_reserve_ratio_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
int percpu_pagelist_fraction_sysctl_handler(struct ctl_table *, int,
					void __user *, size_t *, loff_t *);
int sysctl_min_unmapped_ratio_sysctl_handler(struct ctl_table *, int,
			void __user *, size_t *, loff_t *);
int sysctl_min_slab_ratio_sysctl_handler(struct ctl_table *, int,
			void __user *, size_t *, loff_t *);

extern int numa_zonelist_order_handler(struct ctl_table *, int,
			void __user *, size_t *, loff_t *);
extern char numa_zonelist_order[];
#define NUMA_ZONELIST_ORDER_LEN 16	/* string buffer size */

#ifndef CONFIG_NEED_MULTIPLE_NODES

/*
UMA结构的机器中, 只有一个node结点即contig_page_data, 
此时NODE_DATA直接指向了全局的contig_page_data, 而与node的编号nid无关
*/
extern struct pglist_data contig_page_data;
#define NODE_DATA(nid)		(&contig_page_data)
#define NODE_MEM_MAP(nid)	mem_map

#else /* CONFIG_NEED_MULTIPLE_NODES */

#include <asm/mmzone.h>

#endif /* !CONFIG_NEED_MULTIPLE_NODES */

extern struct pglist_data *first_online_pgdat(void);
extern struct pglist_data *next_online_pgdat(struct pglist_data *pgdat);
extern struct zone *next_zone(struct zone *zone);

/**
 * for_each_online_pgdat - helper macro to iterate over all online nodes
 * @pgdat - pointer to a pg_data_t variable
 */
#define for_each_online_pgdat(pgdat)			\
	for (pgdat = first_online_pgdat();		\
	     pgdat;					\
	     pgdat = next_online_pgdat(pgdat))
/**
 * for_each_zone - helper macro to iterate over all memory zones
 * @zone - pointer to struct zone variable
 *
 * The user only needs to declare the zone variable, for_each_zone
 * fills it in.
 */
#define for_each_zone(zone)			        \
	for (zone = (first_online_pgdat())->node_zones; \
	     zone;					\
	     zone = next_zone(zone))

#define for_each_populated_zone(zone)		        \
	for (zone = (first_online_pgdat())->node_zones; \
	     zone;					\
	     zone = next_zone(zone))			\
		if (!populated_zone(zone))		\
			; /* do nothing */		\
		else

static inline struct zone *zonelist_zone(struct zoneref *zoneref)
{
	return zoneref->zone;
}

static inline int zonelist_zone_idx(struct zoneref *zoneref)
{
	return zoneref->zone_idx;
}

static inline int zonelist_node_idx(struct zoneref *zoneref)
{
#ifdef CONFIG_NUMA
	/* zone_to_nid not available in this context */
	return zoneref->zone->node;
#else
	return 0;
#endif /* CONFIG_NUMA */
}

struct zoneref *__next_zones_zonelist(struct zoneref *z,
					enum zone_type highest_zoneidx,
					nodemask_t *nodes);

/**
 * next_zones_zonelist - Returns the next zone at or below highest_zoneidx within the allowed nodemask using a cursor within a zonelist as a starting point
 * @z - The cursor used as a starting point for the search
 * @highest_zoneidx - The zone index of the highest zone to return
 * @nodes - An optional nodemask to filter the zonelist with
 *
 * This function returns the next zone at or below a given zone index that is
 * within the allowed nodemask using a cursor as the starting point for the
 * search. The zoneref returned is a cursor that represents the current zone
 * being examined. It should be advanced by one before calling
 * next_zones_zonelist again.
 */
static __always_inline struct zoneref *next_zones_zonelist(struct zoneref *z,
					enum zone_type highest_zoneidx,
					nodemask_t *nodes)
{
	if (likely(!nodes && zonelist_zone_idx(z) <= highest_zoneidx))
		return z;
	return __next_zones_zonelist(z, highest_zoneidx, nodes);
}

/**
 * first_zones_zonelist - Returns the first zone at or below highest_zoneidx within the allowed nodemask in a zonelist
 * @zonelist - The zonelist to search for a suitable zone
 * @highest_zoneidx - The zone index of the highest zone to return
 * @nodes - An optional nodemask to filter the zonelist with
 * @zone - The first suitable zone found is returned via this parameter
 *
 * This function returns the first zone at or below a given zone index that is
 * within the allowed nodemask. The zoneref returned is a cursor that can be
 * used to iterate the zonelist with next_zones_zonelist by advancing it by
 * one before calling.
 */
static inline struct zoneref *first_zones_zonelist(struct zonelist *zonelist,
					enum zone_type highest_zoneidx,
					nodemask_t *nodes)
{
	return next_zones_zonelist(zonelist->_zonerefs,
							highest_zoneidx, nodes);
}

/**
 * for_each_zone_zonelist_nodemask - helper macro to iterate over valid zones in a zonelist at or below a given zone index and within a nodemask
 * @zone - The current zone in the iterator
 * @z - The current pointer within zonelist->zones being iterated
 * @zlist - The zonelist being iterated
 * @highidx - The zone index of the highest zone to return
 * @nodemask - Nodemask allowed by the allocator
 *
 * This iterator iterates though all zones at or below a given zone index and
 * within a given nodemask
 */
#define for_each_zone_zonelist_nodemask(zone, z, zlist, highidx, nodemask) \
	for (z = first_zones_zonelist(zlist, highidx, nodemask), zone = zonelist_zone(z);	\
		zone;							\
		z = next_zones_zonelist(++z, highidx, nodemask),	\
			zone = zonelist_zone(z))

#define for_next_zone_zonelist_nodemask(zone, z, zlist, highidx, nodemask) \
	for (zone = z->zone;	\
		zone;							\
		z = next_zones_zonelist(++z, highidx, nodemask),	\
			zone = zonelist_zone(z))


/**
 * for_each_zone_zonelist - helper macro to iterate over valid zones in a zonelist at or below a given zone index
 * @zone - The current zone in the iterator
 * @z - The current pointer within zonelist->zones being iterated
 * @zlist - The zonelist being iterated
 * @highidx - The zone index of the highest zone to return
 *
 * This iterator iterates though all zones at or below a given zone index.
 */
#define for_each_zone_zonelist(zone, z, zlist, highidx) \
	for_each_zone_zonelist_nodemask(zone, z, zlist, highidx, NULL)

#ifdef CONFIG_SPARSEMEM
#include <asm/sparsemem.h>
#endif

#if !defined(CONFIG_HAVE_ARCH_EARLY_PFN_TO_NID) && \
	!defined(CONFIG_HAVE_MEMBLOCK_NODE_MAP)
static inline unsigned long early_pfn_to_nid(unsigned long pfn)
{
	return 0;
}
#endif

#ifdef CONFIG_FLATMEM
#define pfn_to_nid(pfn)		(0)
#endif

#ifdef CONFIG_SPARSEMEM

/*
 * SECTION_SHIFT    		#bits space required to store a section #
 *
 * PA_SECTION_SHIFT		physical address to/from section number
 * PFN_SECTION_SHIFT		pfn to/from section number
 */
#define PA_SECTION_SHIFT	(SECTION_SIZE_BITS)
#define PFN_SECTION_SHIFT	(SECTION_SIZE_BITS - PAGE_SHIFT)

#define NR_MEM_SECTIONS		(1UL << SECTIONS_SHIFT)

#define PAGES_PER_SECTION       (1UL << PFN_SECTION_SHIFT)
#define PAGE_SECTION_MASK	(~(PAGES_PER_SECTION-1))

#define SECTION_BLOCKFLAGS_BITS \
	((1UL << (PFN_SECTION_SHIFT - pageblock_order)) * NR_PAGEBLOCK_BITS)

#if (MAX_ORDER - 1 + PAGE_SHIFT) > SECTION_SIZE_BITS
#error Allocator MAX_ORDER exceeds SECTION_SIZE
#endif

#define pfn_to_section_nr(pfn) ((pfn) >> PFN_SECTION_SHIFT)
#define section_nr_to_pfn(sec) ((sec) << PFN_SECTION_SHIFT)

#define SECTION_ALIGN_UP(pfn)	(((pfn) + PAGES_PER_SECTION - 1) & PAGE_SECTION_MASK)
#define SECTION_ALIGN_DOWN(pfn)	((pfn) & PAGE_SECTION_MASK)

struct page;
struct page_ext;
struct mem_section {
	/*
	 * This is, logically, a pointer to an array of struct
	 * pages.  However, it is stored with some other magic.
	 * (see sparse.c::sparse_init_one_section())
	 *
	 * Additionally during early boot we encode node id of
	 * the location of the section here to guide allocation.
	 * (see sparse.c::memory_present())
	 *
	 * Making it a UL at least makes someone do a cast
	 * before using it wrong.
	 */
	unsigned long section_mem_map;

	/* See declaration of similar field in struct zone */
	/**
      * 本管理区里的页面标志数组。
      */
	unsigned long *pageblock_flags;
#ifdef CONFIG_PAGE_EXTENSION
	/*
	 * If SPARSEMEM, pgdat doesn't have page_ext pointer. We use
	 * section. (see page_ext.h about this.)
	 */
	struct page_ext *page_ext;
	unsigned long pad;
#endif
	/*
	 * WARNING: mem_section must be a power-of-2 in size for the
	 * calculation and use of SECTION_ROOT_MASK to make sense.
	 */
};

#ifdef CONFIG_SPARSEMEM_EXTREME
#define SECTIONS_PER_ROOT       (PAGE_SIZE / sizeof (struct mem_section))
#else
#define SECTIONS_PER_ROOT	1
#endif

#define SECTION_NR_TO_ROOT(sec)	((sec) / SECTIONS_PER_ROOT)
#define NR_SECTION_ROOTS	DIV_ROUND_UP(NR_MEM_SECTIONS, SECTIONS_PER_ROOT)
#define SECTION_ROOT_MASK	(SECTIONS_PER_ROOT - 1)

#ifdef CONFIG_SPARSEMEM_EXTREME
extern struct mem_section *mem_section[NR_SECTION_ROOTS];
#else
extern struct mem_section mem_section[NR_SECTION_ROOTS][SECTIONS_PER_ROOT];
#endif

static inline struct mem_section *__nr_to_section(unsigned long nr)
{
	if (!mem_section[SECTION_NR_TO_ROOT(nr)])
		return NULL;
	return &mem_section[SECTION_NR_TO_ROOT(nr)][nr & SECTION_ROOT_MASK];
}
extern int __section_nr(struct mem_section* ms);
extern unsigned long usemap_size(void);

/*
 * We use the lower bits of the mem_map pointer to store
 * a little bit of information.  There should be at least
 * 3 bits here due to 32-bit alignment.
 */
#define	SECTION_MARKED_PRESENT	(1UL<<0)
#define SECTION_HAS_MEM_MAP	(1UL<<1)
#define SECTION_MAP_LAST_BIT	(1UL<<2)
#define SECTION_MAP_MASK	(~(SECTION_MAP_LAST_BIT-1))
#define SECTION_NID_SHIFT	2

static inline struct page *__section_mem_map_addr(struct mem_section *section)
{
	unsigned long map = section->section_mem_map;
	map &= SECTION_MAP_MASK;
	return (struct page *)map;
}

static inline int present_section(struct mem_section *section)
{
	return (section && (section->section_mem_map & SECTION_MARKED_PRESENT));
}

static inline int present_section_nr(unsigned long nr)
{
	return present_section(__nr_to_section(nr));
}

static inline int valid_section(struct mem_section *section)
{
	return (section && (section->section_mem_map & SECTION_HAS_MEM_MAP));
}

static inline int valid_section_nr(unsigned long nr)
{
	return valid_section(__nr_to_section(nr));
}

static inline struct mem_section *__pfn_to_section(unsigned long pfn)
{
	return __nr_to_section(pfn_to_section_nr(pfn));
}

#ifndef CONFIG_HAVE_ARCH_PFN_VALID
static inline int pfn_valid(unsigned long pfn)
{
	if (pfn_to_section_nr(pfn) >= NR_MEM_SECTIONS)
		return 0;
	return valid_section(__nr_to_section(pfn_to_section_nr(pfn)));
}
#endif

static inline int pfn_present(unsigned long pfn)
{
	if (pfn_to_section_nr(pfn) >= NR_MEM_SECTIONS)
		return 0;
	return present_section(__nr_to_section(pfn_to_section_nr(pfn)));
}

/*
 * These are _only_ used during initialisation, therefore they
 * can use __initdata ...  They could have names to indicate
 * this restriction.
 */
#ifdef CONFIG_NUMA
#define pfn_to_nid(pfn)							\
({									\
	unsigned long __pfn_to_nid_pfn = (pfn);				\
	page_to_nid(pfn_to_page(__pfn_to_nid_pfn));			\
})
#else
#define pfn_to_nid(pfn)		(0)
#endif

#define early_pfn_valid(pfn)	pfn_valid(pfn)
void sparse_init(void);
#else
#define sparse_init()	do {} while (0)
#define sparse_index_init(_sec, _nid)  do {} while (0)
#endif /* CONFIG_SPARSEMEM */

/*
 * During memory init memblocks map pfns to nids. The search is expensive and
 * this caches recent lookups. The implementation of __early_pfn_to_nid
 * may treat start/end as pfns or sections.
 */
struct mminit_pfnnid_cache {
	unsigned long last_start;
	unsigned long last_end;
	int last_nid;
};

#ifndef early_pfn_valid
#define early_pfn_valid(pfn)	(1)
#endif

void memory_present(int nid, unsigned long start, unsigned long end);
unsigned long __init node_memmap_size_bytes(int, unsigned long, unsigned long);

/*
 * If it is possible to have holes within a MAX_ORDER_NR_PAGES, then we
 * need to check pfn validility within that MAX_ORDER_NR_PAGES block.
 * pfn_valid_within() should be used in this case; we optimise this away
 * when we have no holes within a MAX_ORDER_NR_PAGES block.
 */
#ifdef CONFIG_HOLES_IN_ZONE
#define pfn_valid_within(pfn) pfn_valid(pfn)
#else
#define pfn_valid_within(pfn) (1)
#endif

#ifdef CONFIG_ARCH_HAS_HOLES_MEMORYMODEL
/*
 * pfn_valid() is meant to be able to tell if a given PFN has valid memmap
 * associated with it or not. In FLATMEM, it is expected that holes always
 * have valid memmap as long as there is valid PFNs either side of the hole.
 * In SPARSEMEM, it is assumed that a valid section has a memmap for the
 * entire section.
 *
 * However, an ARM, and maybe other embedded architectures in the future
 * free memmap backing holes to save memory on the assumption the memmap is
 * never used. The page_zone linkages are then broken even though pfn_valid()
 * returns true. A walker of the full memmap must then do this additional
 * check to ensure the memmap they are looking at is sane by making sure
 * the zone and PFN linkages are still valid. This is expensive, but walkers
 * of the full memmap are extremely rare.
 */
bool memmap_valid_within(unsigned long pfn,
					struct page *page, struct zone *zone);
#else
static inline bool memmap_valid_within(unsigned long pfn,
					struct page *page, struct zone *zone)
{
	return true;
}
#endif /* CONFIG_ARCH_HAS_HOLES_MEMORYMODEL */

#endif /* !__GENERATING_BOUNDS.H */
#endif /* !__ASSEMBLY__ */
#endif /* _LINUX_MMZONE_H */
