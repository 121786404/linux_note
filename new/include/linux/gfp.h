#ifndef __LINUX_GFP_H
#define __LINUX_GFP_H

#include <linux/mmdebug.h>
#include <linux/mmzone.h>
#include <linux/stddef.h>
#include <linux/linkage.h>
#include <linux/topology.h>


/*
 * 以'__'打头的GFP掩码只限于在内存管理组件内部的代码使用，
 * gfp_mask掩码以"GFP_"的形式出现
 */

struct vm_area_struct;

/*
 * In case of changes, please don't forget to update
 * include/trace/events/mmflags.h and tools/perf/builtin-kmem.c
 */
/*
GFP缩写的意思为获取空闲页get free page

常见的几种内存分配场景及使用的标志
    1) 缺页异常分配内存时：
        GFP_HIGHUSER | __GFP_ZERO | __GFP_MOVABLE
        分配可移动的page，并且将page清零
        do_page_fault() -> handle_pte_fault() -> do_anonymous_page() -> alloc_zeroed_user_highpage_movable()
    2) 文件映射分配内存时：
        GFP_HIGHUSER_MOVABLE
        分配可移动的page
        do_page_fault() -> handle_pte_fault() -> do_anonymous_page() -> do_nonlinear_fault() -> __do_fault()
    3) vmalloc分配内存时：
        GFP_KERNEL | __GFP_HIGHMEM
        优先从高端内存中分配
        vmalloc() -> __vmalloc_node_flags(size, -1, GFP_KERNEL | __GFP_HIGHMEM)
*/
/* Plain integer GFP bitmasks. Do not use this directly. */
#define ___GFP_DMA		0x01u
#define ___GFP_HIGHMEM		0x02u
#define ___GFP_DMA32		0x04u
#define ___GFP_MOVABLE		0x08u /* 页是可移动的 */
#define ___GFP_RECLAIMABLE	0x10u /* 页是可回收的 */
#define ___GFP_HIGH		0x20u /* 应该访问紧急分配池？ */
#define ___GFP_IO		0x40u /* 可以启动物理IO？ */
#define ___GFP_FS		0x80u /* 可以调用底层文件系统？ */
#define ___GFP_COLD		0x100u /* 需要非缓存的冷页 */
#define ___GFP_NOWARN		0x200u /* 禁止分配失败警告 */
#define ___GFP_REPEAT		0x400u /* 重试分配，可能失败 */
#define ___GFP_NOFAIL		0x800u /* 一直重试，不会失败 */
#define ___GFP_NORETRY		0x1000u /* 不重试，可能失败 */
#define ___GFP_MEMALLOC		0x2000u /* 使用紧急分配链表 */
#define ___GFP_COMP		0x4000u /* 增加复合页元数据 */
#define ___GFP_ZERO		0x8000u  /* 成功则返回填充字节0的页 */
#define ___GFP_NOMEMALLOC	0x10000u /* 不使用紧急分配链表 */
#define ___GFP_HARDWALL		0x20000u /* 只允许在进程允许运行的CPU所关联的结点分配内存 */
#define ___GFP_THISNODE		0x40000u /* 没有备用结点，没有策略 */
#define ___GFP_ATOMIC		0x80000u /* 用于原子分配，在任何情况下都不能中断  */
#define ___GFP_ACCOUNT		0x100000u
#define ___GFP_NOTRACK		0x200000u
#define ___GFP_DIRECT_RECLAIM	0x400000u
#define ___GFP_WRITE		0x800000u
#define ___GFP_KSWAPD_RECLAIM	0x1000000u
/* If the above are modified, __GFP_BITS_SHIFT may need updating */

/*
 * Physical address zone modifiers (see linux/mmzone.h - low four bits)
 *
 * Do not put any conditional on these. If necessary modify the definitions
 * without the underscores and use them consistently. The definitions here may
 * be used in bit comparisons.
 */
/*
 从ZONE_DMA中分配内存
*/
#define __GFP_DMA	((__force gfp_t)___GFP_DMA)
/*
从ZONE_HIGHMEM活ZONE_NORMAL中分配内存
*/
#define __GFP_HIGHMEM	((__force gfp_t)___GFP_HIGHMEM)
/*
从ZONE_DMA32中分配内存
*/
#define __GFP_DMA32	((__force gfp_t)___GFP_DMA32)
/*
从__GFP_MOVABLE中分配内存
*/
#define __GFP_MOVABLE	((__force gfp_t)___GFP_MOVABLE)  /* ZONE_MOVABLE allowed */
#define GFP_ZONEMASK	(__GFP_DMA|__GFP_HIGHMEM|__GFP_DMA32|__GFP_MOVABLE)

/*
 * Page mobility and placement hints
 *
 * These flags provide hints about how mobile the page is. Pages with similar
 * mobility are placed within the same pageblocks to minimise problems due
 * to external fragmentation.
 *
 * __GFP_MOVABLE (also a zone modifier) indicates that the page can be
 *   moved by page migration during memory compaction or can be reclaimed.
 *
 * __GFP_RECLAIMABLE is used for slab allocations that specify
 *   SLAB_RECLAIM_ACCOUNT and whose pages can be freed via shrinkers.
 *
 * __GFP_WRITE indicates the caller intends to dirty the page. Where possible,
 *   these pages will be spread between local zones to avoid all the dirty
 *   pages being in one zone (fair zone allocation policy).
 *
 * __GFP_HARDWALL enforces the cpuset memory allocation policy.
 *
 * __GFP_THISNODE forces the allocation to be satisified from the requested
 *   node with no fallbacks or placement policy enforcements.
 *
 * __GFP_ACCOUNT causes the allocation to be accounted to kmemcg.
 */
/*
 请求分配可回收的page
*/
#define __GFP_RECLAIMABLE ((__force gfp_t)___GFP_RECLAIMABLE)
#define __GFP_WRITE	((__force gfp_t)___GFP_WRITE)
/*
只在NUMA系统上有意义.
只能在当前进程可运行的cpu关联的内存节点上分配内存，
如果进程可在所有cpu上运行，该标志无意义
*/
#define __GFP_HARDWALL   ((__force gfp_t)___GFP_HARDWALL)
/*
只能在当前节点上分配内存
*/
#define __GFP_THISNODE	((__force gfp_t)___GFP_THISNODE)
#define __GFP_ACCOUNT	((__force gfp_t)___GFP_ACCOUNT)

/*
 * Watermark modifiers -- controls access to emergency reserves
 *
 * __GFP_HIGH indicates that the caller is high-priority and that granting
 *   the request is necessary before the system can make forward progress.
 *   For example, creating an IO context to clean pages.
 *
 * __GFP_ATOMIC indicates that the caller cannot reclaim or sleep and is
 *   high priority. Users are typically interrupt handlers. This may be
 *   used in conjunction with __GFP_HIGH
 *
 * __GFP_MEMALLOC allows access to all memory. This should only be used when
 *   the caller guarantees the allocation will allow more memory to be freed
 *   very shortly e.g. process exiting or swapping. Users either should
 *   be the MM or co-ordinating closely with the VM (e.g. swap over NFS).
 *
 * __GFP_NOMEMALLOC is used to explicitly forbid access to emergency reserves.
 *   This takes precedence over the __GFP_MEMALLOC flag if both are set.
 */
#define __GFP_ATOMIC	((__force gfp_t)___GFP_ATOMIC)
/*
如果请求非常重要, 则设置__GFP_HIGH，即内核急切地需要内存时。
它被允许来消耗甚至被内核保留给紧急状况的最后的内存页
*/
#define __GFP_HIGH	((__force gfp_t)___GFP_HIGH)
#define __GFP_MEMALLOC	((__force gfp_t)___GFP_MEMALLOC)
/* 不使用紧急分配链表 */
#define __GFP_NOMEMALLOC ((__force gfp_t)___GFP_NOMEMALLOC)

/*
 * Reclaim modifiers
 *
 * __GFP_IO can start physical IO.
 *
 * __GFP_FS can call down to the low-level FS. Clearing the flag avoids the
 *   allocator recursing into the filesystem which might already be holding
 *   locks.
 *
 * __GFP_DIRECT_RECLAIM indicates that the caller may enter direct reclaim.
 *   This flag can be cleared to avoid unnecessary delays when a fallback
 *   option is available.
 *
 * __GFP_KSWAPD_RECLAIM indicates that the caller wants to wake kswapd when
 *   the low watermark is reached and have it reclaim pages until the high
 *   watermark is reached. A caller may wish to clear this flag when fallback
 *   options are available and the reclaim is likely to disrupt the system. The
 *   canonical example is THP allocation where a fallback is cheap but
 *   reclaim/compaction may cause indirect stalls.
 *
 * __GFP_RECLAIM is shorthand to allow/forbid both direct and kswapd reclaim.
 *
 * __GFP_REPEAT: Try hard to allocate the memory, but the allocation attempt
 *   _might_ fail.  This depends upon the particular VM implementation.
 *
 * __GFP_NOFAIL: The VM implementation _must_ retry infinitely: the caller
 *   cannot handle allocation failures. New users should be evaluated carefully
 *   (and the flag should be used only when there is no reasonable failure
 *   policy) but it is definitely preferable to use the flag rather than
 *   opencode endless loop around allocator.
 *
 * __GFP_NORETRY: The VM implementation must not retry indefinitely and will
 *   return NULL when direct reclaim and memory compaction have failed to allow
 *   the allocation to succeed.  The OOM killer is not called with the current
 *   implementation.
 */
/*
内存分配的过程中可进行IO操作，
也就是说分配过程中如果需要换出页，
必须设置该标志，才能将换出的页写入磁盘
*/
#define __GFP_IO	((__force gfp_t)___GFP_IO)
/*
允许内核执行VFS操作. 在与VFS层有联系的内核子系统中必须禁用,
因为这可能引起循环递归调用
*/
#define __GFP_FS	((__force gfp_t)___GFP_FS)
#define __GFP_DIRECT_RECLAIM	((__force gfp_t)___GFP_DIRECT_RECLAIM) /* Caller can reclaim */
#define __GFP_KSWAPD_RECLAIM	((__force gfp_t)___GFP_KSWAPD_RECLAIM) /* kswapd can wake */
#define __GFP_RECLAIM ((__force gfp_t)(___GFP_DIRECT_RECLAIM|___GFP_KSWAPD_RECLAIM))
/*
在分配失败后自动重试，但在尝试若干次之后会停止
*/
#define __GFP_REPEAT	((__force gfp_t)___GFP_REPEAT)
/*
在分配失败后一直重试，直至成功
*/
#define __GFP_NOFAIL	((__force gfp_t)___GFP_NOFAIL)
/*
在分配失败后不重试，因此可能分配失败
*/
#define __GFP_NORETRY	((__force gfp_t)___GFP_NORETRY)

/*
 * Action modifiers
 *
 * __GFP_COLD indicates that the caller does not expect to be used in the near
 *   future. Where possible, a cache-cold page will be returned.
 *
 * __GFP_NOWARN suppresses allocation failure reports.
 *
 * __GFP_COMP address compound page metadata.
 *
 * __GFP_ZERO returns a zeroed page on success.
 *
 * __GFP_NOTRACK avoids tracking with kmemcheck.
 *
 * __GFP_NOTRACK_FALSE_POSITIVE is an alias of __GFP_NOTRACK. It's a means of
 *   distinguishing in the source between false positives and allocations that
 *   cannot be supported (e.g. page tables).
 *
 * __GFP_OTHER_NODE is for allocations that are on a remote node but that
 *   should not be accounted for as a remote allocation in vmstat. A
 *   typical user would be khugepaged collapsing a huge page on a remote
 *   node.
 */
/*
 如果需要分配不在CPU高速缓存中的“冷”页时，则设置__GFP_COLD
 它在一段时间没被使用. 它对分配页作 DMA 读是有用的,
 此时在处理器缓冲中出现是无用的.
*/
#define __GFP_COLD	((__force gfp_t)___GFP_COLD)
/*
在分配失败时禁止内核故障警告。在极少数场合该标志有用
*/
#define __GFP_NOWARN	((__force gfp_t)___GFP_NOWARN)
/*
添加混合页元素, 在hugetlb的代码内部使用
*/
#define __GFP_COMP	((__force gfp_t)___GFP_COMP)
/* 申请全部填充为0的page */
#define __GFP_ZERO	((__force gfp_t)___GFP_ZERO)
/* 不对分配的内存进行跟踪 */
#define __GFP_NOTRACK	((__force gfp_t)___GFP_NOTRACK)
#define __GFP_NOTRACK_FALSE_POSITIVE (__GFP_NOTRACK)

/* Room for N __GFP_FOO bits */
#define __GFP_BITS_SHIFT 25
#define __GFP_BITS_MASK ((__force gfp_t)((1 << __GFP_BITS_SHIFT) - 1))

/*
 * Useful GFP flag combinations that are commonly used. It is recommended
 * that subsystems start with one of these combinations and then set/clear
 * __GFP_FOO flags as necessary.
 *
 * GFP_ATOMIC users can not sleep and need the allocation to succeed. A lower
 *   watermark is applied to allow access to "atomic reserves"
 *
 * GFP_KERNEL is typical for kernel-internal allocations. The caller requires
 *   ZONE_NORMAL or a lower zone for direct access but can direct reclaim.
 *
 * GFP_KERNEL_ACCOUNT is the same as GFP_KERNEL, except the allocation is
 *   accounted to kmemcg.
 *
 * GFP_NOWAIT is for kernel allocations that should not stall for direct
 *   reclaim, start physical IO or use any filesystem callback.
 *
 * GFP_NOIO will use direct reclaim to discard clean pages or slab pages
 *   that do not require the starting of any physical IO.
 *
 * GFP_NOFS will use direct reclaim but will not use any filesystem interfaces.
 *
 * GFP_USER is for userspace allocations that also need to be directly
 *   accessibly by the kernel or hardware. It is typically used by hardware
 *   for buffers that are mapped to userspace (e.g. graphics) that hardware
 *   still must DMA to. cpuset limits are enforced for these allocations.
 *
 * GFP_DMA exists for historical reasons and should be avoided where possible.
 *   The flags indicates that the caller requires that the lowest zone be
 *   used (ZONE_DMA or 16M on x86-64). Ideally, this would be removed but
 *   it would require careful auditing as some users really require it and
 *   others use the flag to avoid lowmem reserves in ZONE_DMA and treat the
 *   lowest zone as a type of emergency reserve.
 *
 * GFP_DMA32 is similar to GFP_DMA except that the caller requires a 32-bit
 *   address.
 *
 * GFP_HIGHUSER is for userspace allocations that may be mapped to userspace,
 *   do not need to be directly accessible by the kernel but that cannot
 *   move once in use. An example may be a hardware allocation that maps
 *   data directly into userspace but has no addressing limitations.
 *
 * GFP_HIGHUSER_MOVABLE is for userspace allocations that the kernel does not
 *   need direct access to but can use kmap() when access is required. They
 *   are expected to be movable via page reclaim or page migration. Typically,
 *   pages on the LRU would also be allocated with GFP_HIGHUSER_MOVABLE.
 *
 * GFP_TRANSHUGE and GFP_TRANSHUGE_LIGHT are used for THP allocations. They are
 *   compound allocations that will generally fail quickly if memory is not
 *   available and will not wake kswapd/kcompactd on failure. The _LIGHT
 *   version does not attempt reclaim/compaction at all and is by default used
 *   in page fault path, while the non-light is used by khugepaged.
 */
/*
Get Free Pages = GFP
*/
/*
 用于原子分配，不能中断, 不能睡眠，不能有IO和VFS操作
 可能使用紧急分配链表中的内存,
 这个标志用在中断处理程序, 下半部,
 持有自旋锁以及其他不能睡眠的地方
*/
#define GFP_ATOMIC	(__GFP_HIGH|__GFP_ATOMIC|__GFP_KSWAPD_RECLAIM)
/*
* 当内存不足时，允许通过文件系统、IO操作将页面换出以释放内存空间。
* 因此这时分配函数必须是可重入的
* 一般的系统调用代码中，都使用此分配标志。
* 这也是最常见的分配标志
*/
#define GFP_KERNEL	(__GFP_RECLAIM | __GFP_IO | __GFP_FS)
#define GFP_KERNEL_ACCOUNT (GFP_KERNEL | __GFP_ACCOUNT)
/*
与GFP_ATOMIC类似, 不同之处在于, 调用不会退给紧急内存池,
这就增加了内存分配失败的可能性
*/
#define GFP_NOWAIT	(__GFP_KSWAPD_RECLAIM)
/*
不允许IO操作。
当内存不足时，可能需要将内存换出到外部设备，这需要IO操作。
但是在进行IO操作的过程，需要申请内存时必须指定此标志，以免死锁。
*/
#define GFP_NOIO	(__GFP_RECLAIM)
/*
不允许文件系统操作。
在文件系统代码中，申请内存需要有此标志。也是为了避免死锁
*/
#define GFP_NOFS	(__GFP_RECLAIM | __GFP_IO)
/**
 * 类似于GFP_KERNEL，并且在内存不足时，还允许进行内存回收。
 */
#define GFP_TEMPORARY	(__GFP_RECLAIM | __GFP_IO | __GFP_FS | \
			 __GFP_RECLAIMABLE)
/**
 * 分配用于用户进程的页面。与GFP_KERNEL相比，
 增加__GFP_HARDWALL 只能从进程可运行的node上分配内存。
 */
#define GFP_USER	(__GFP_RECLAIM | __GFP_IO | __GFP_FS | __GFP_HARDWALL)
/*
用于分配适用于DMA的内存, 当前是__GFP_DMA的同义词,
GFP_DMA32也是__GFP_GMA32的同义词
*/
#define GFP_DMA		__GFP_DMA
#define GFP_DMA32	__GFP_DMA32
/*
GFP_USER的一个扩展, 分配用于用户进程的页面，
优先从高端zone中分配内存
*/
#define GFP_HIGHUSER	(GFP_USER | __GFP_HIGHMEM)
/*
用途类似于GFP_HIGHUSER，但分配将从虚拟内存域ZONE_MOVABLE进行
*/
#define GFP_HIGHUSER_MOVABLE	(GFP_HIGHUSER | __GFP_MOVABLE)
#define GFP_TRANSHUGE_LIGHT	((GFP_HIGHUSER_MOVABLE | __GFP_COMP | \
			 __GFP_NOMEMALLOC | __GFP_NOWARN) & ~__GFP_RECLAIM)
/**
 * 分配的页面用于透明巨页。
 */
#define GFP_TRANSHUGE	(GFP_TRANSHUGE_LIGHT | __GFP_DIRECT_RECLAIM)

/* Convert GFP flags to their corresponding migrate type */
#define GFP_MOVABLE_MASK (__GFP_RECLAIMABLE|__GFP_MOVABLE)
#define GFP_MOVABLE_SHIFT 3

// 转换分配标志及对应的迁移类型
/*
把 gfp_flags 分配掩码转换成 MTGRATE_TYPES，
例如分配掩码为 GFP_KERNEL，那么 MIGRATE_TYPES 是MIGRATE_UNMOVABLE
如果分配掩码为 GFP_HIGHUSER_MOVABLE，那么 MIGRATE_TYPES 是MIGRATE_MOVABLE
*/
static inline int gfpflags_to_migratetype(const gfp_t gfp_flags)
{
	VM_WARN_ON((gfp_flags & GFP_MOVABLE_MASK) == GFP_MOVABLE_MASK);
	BUILD_BUG_ON((1UL << GFP_MOVABLE_SHIFT) != ___GFP_MOVABLE);
	BUILD_BUG_ON((___GFP_MOVABLE >> GFP_MOVABLE_SHIFT) != MIGRATE_MOVABLE);

	if (unlikely(page_group_by_mobility_disabled))
		return MIGRATE_UNMOVABLE;

	/* Group based on mobility */
	return (gfp_flags & GFP_MOVABLE_MASK) >> GFP_MOVABLE_SHIFT;
}
#undef GFP_MOVABLE_MASK
#undef GFP_MOVABLE_SHIFT

static inline bool gfpflags_allow_blocking(const gfp_t gfp_flags)
{
	return !!(gfp_flags & __GFP_DIRECT_RECLAIM);
}

#ifdef CONFIG_HIGHMEM
#define OPT_ZONE_HIGHMEM ZONE_HIGHMEM/*在ZONE_HIGHMEM标识的内存区域中查找空闲页*/
#else
#define OPT_ZONE_HIGHMEM ZONE_NORMAL
#endif

#ifdef CONFIG_ZONE_DMA
#define OPT_ZONE_DMA ZONE_DMA
#else
#define OPT_ZONE_DMA ZONE_NORMAL
#endif

#ifdef CONFIG_ZONE_DMA32
#define OPT_ZONE_DMA32 ZONE_DMA32
#else
#define OPT_ZONE_DMA32 ZONE_NORMAL
#endif

/*
 * GFP_ZONE_TABLE is a word size bitstring that is used for looking up the
 * zone to use given the lowest 4 bits of gfp_t. Entries are ZONE_SHIFT long
 * and there are 16 of them to cover all possible combinations of
 * __GFP_DMA, __GFP_DMA32, __GFP_MOVABLE and __GFP_HIGHMEM.
 *
 * The zone fallback order is MOVABLE=>HIGHMEM=>NORMAL=>DMA32=>DMA.
 * But GFP_MOVABLE is not only a zone specifier but also an allocation
 * policy. Therefore __GFP_MOVABLE plus another zone selector is valid.
 * Only 1 bit of the lowest 3 bits (DMA,DMA32,HIGHMEM) can be set to "1".
 *
 *       bit       result
 *       =================
 *       0x0    => NORMAL
 *       0x1    => DMA or NORMAL
 *       0x2    => HIGHMEM or NORMAL
 *       0x3    => BAD (DMA+HIGHMEM)
 *       0x4    => DMA32 or DMA or NORMAL
 *       0x5    => BAD (DMA+DMA32)
 *       0x6    => BAD (HIGHMEM+DMA32)
 *       0x7    => BAD (HIGHMEM+DMA32+DMA)
 *       0x8    => NORMAL (MOVABLE+0)
 *       0x9    => DMA or NORMAL (MOVABLE+DMA)
 *       0xa    => MOVABLE (Movable is valid only if HIGHMEM is set too)
 *       0xb    => BAD (MOVABLE+HIGHMEM+DMA)
 *       0xc    => DMA32 (MOVABLE+DMA32)
 *       0xd    => BAD (MOVABLE+DMA32+DMA)
 *       0xe    => BAD (MOVABLE+DMA32+HIGHMEM)
 *       0xf    => BAD (MOVABLE+DMA32+HIGHMEM+DMA)
 *
 * GFP_ZONES_SHIFT must be <= 2 on 32 bit platforms.
 */

#if defined(CONFIG_ZONE_DEVICE) && (MAX_NR_ZONES-1) <= 4
/* ZONE_DEVICE is not a valid GFP zone specifier */
#define GFP_ZONES_SHIFT 2
#else
#define GFP_ZONES_SHIFT ZONES_SHIFT
#endif

#if 16 * GFP_ZONES_SHIFT > BITS_PER_LONG
#error GFP_ZONES_SHIFT too large to create GFP_ZONE_TABLE integer
#endif

#define GFP_ZONE_TABLE ( \
	(ZONE_NORMAL << 0 * GFP_ZONES_SHIFT)				       \
	| (OPT_ZONE_DMA << ___GFP_DMA * GFP_ZONES_SHIFT)		       \
	| (OPT_ZONE_HIGHMEM << ___GFP_HIGHMEM * GFP_ZONES_SHIFT)	       \
	| (OPT_ZONE_DMA32 << ___GFP_DMA32 * GFP_ZONES_SHIFT)		       \
	| (ZONE_NORMAL << ___GFP_MOVABLE * GFP_ZONES_SHIFT)		       \
	| (OPT_ZONE_DMA << (___GFP_MOVABLE | ___GFP_DMA) * GFP_ZONES_SHIFT)    \
	| (ZONE_MOVABLE << (___GFP_MOVABLE | ___GFP_HIGHMEM) * GFP_ZONES_SHIFT)\
	| (OPT_ZONE_DMA32 << (___GFP_MOVABLE | ___GFP_DMA32) * GFP_ZONES_SHIFT)\
)

/*
 * GFP_ZONE_BAD is a bitmap for all combinations of __GFP_DMA, __GFP_DMA32
 * __GFP_HIGHMEM and __GFP_MOVABLE that are not permitted. One flag per
 * entry starting with bit 0. Bit is set if the combination is not
 * allowed.
 */
#define GFP_ZONE_BAD ( \
	1 << (___GFP_DMA | ___GFP_HIGHMEM)				      \
	| 1 << (___GFP_DMA | ___GFP_DMA32)				      \
	| 1 << (___GFP_DMA32 | ___GFP_HIGHMEM)				      \
	| 1 << (___GFP_DMA | ___GFP_DMA32 | ___GFP_HIGHMEM)		      \
	| 1 << (___GFP_MOVABLE | ___GFP_HIGHMEM | ___GFP_DMA)		      \
	| 1 << (___GFP_MOVABLE | ___GFP_DMA32 | ___GFP_DMA)		      \
	| 1 << (___GFP_MOVABLE | ___GFP_DMA32 | ___GFP_HIGHMEM)		      \
	| 1 << (___GFP_MOVABLE | ___GFP_DMA32 | ___GFP_DMA | ___GFP_HIGHMEM)  \
)

/* 根据gfp_mask制定在囊二域中分配物理页面
 * 如果没有在gfp_mask中明确制定__GFP_DMA或者__GFP_HIGHMEM,那么默认在ZONE_NORMAL中分配物理页
 * 如果ZONE_NORMAL中现有空闲页不足以满足当前的分配，那么页分配器会到ZONE_DMA域中查找空闲页，而不会到ZONE_HIGHMEM中查找

   从分配掩码中计算出zone 的zoneidx ，并存放在 high_zoneidx 成员中。
 */
static inline enum zone_type gfp_zone(gfp_t flags)
{
	enum zone_type z;
	int bit = (__force int) (flags & GFP_ZONEMASK);

	z = (GFP_ZONE_TABLE >> (bit * GFP_ZONES_SHIFT)) &
					 ((1 << GFP_ZONES_SHIFT) - 1);
	VM_BUG_ON((GFP_ZONE_BAD >> bit) & 1);
	return z;
}

/*
 * There is only one page-allocator function, and two main namespaces to
 * it. The alloc_page*() variants return 'struct page *' and as such
 * can allocate highmem pages, the *get*page*() variants return
 * virtual kernel addresses to the allocated page(s).
 */

static inline int gfp_zonelist(gfp_t flags)
{
#ifdef CONFIG_NUMA
	if (unlikely(flags & __GFP_THISNODE))
		return ZONELIST_NOFALLBACK;
#endif
	return ZONELIST_FALLBACK;
}

/*
 * We get the zone list from the current node and the gfp_mask.
 * This zone list contains a maximum of MAXNODES*MAX_NR_ZONES zones.
 * There are two zonelists per node, one for all zones with memory and
 * one containing just zones from the node the zonelist belongs to.
 *
 * For the normal case of non-DISCONTIGMEM systems the NODE_DATA() gets
 * optimized to &contig_page_data at compile-time.
 */
static inline struct zonelist *node_zonelist(int nid, gfp_t flags)
{
	return NODE_DATA(nid)->node_zonelists + gfp_zonelist(flags);
}

#ifndef HAVE_ARCH_FREE_PAGE
static inline void arch_free_page(struct page *page, int order) { }
#endif
#ifndef HAVE_ARCH_ALLOC_PAGE
static inline void arch_alloc_page(struct page *page, int order) { }
#endif

struct page *
__alloc_pages_nodemask(gfp_t gfp_mask, unsigned int order,
		       struct zonelist *zonelist, nodemask_t *nodemask);

static inline struct page *
__alloc_pages(gfp_t gfp_mask, unsigned int order,
		struct zonelist *zonelist)
{
	return __alloc_pages_nodemask(gfp_mask, order, zonelist, NULL);
}

/*
 * Allocate pages, preferring the node given as nid. The node must be valid and
 * online. For more general interface, see alloc_pages_node().
 */
static inline struct page *
__alloc_pages_node(int nid, gfp_t gfp_mask, unsigned int order)
{
	VM_BUG_ON(nid < 0 || nid >= MAX_NUMNODES);
	VM_WARN_ON(!node_online(nid));

	return __alloc_pages(gfp_mask, order, node_zonelist(nid, gfp_mask));
}

/*
 * Allocate pages, preferring the node given as nid. When nid == NUMA_NO_NODE,
 * prefer the current CPU's closest node. Otherwise node must be valid and
 * online.
 */
static inline struct page *alloc_pages_node(int nid, gfp_t gfp_mask,
						unsigned int order)
{
	if (nid == NUMA_NO_NODE)
		nid = numa_mem_id();

	return __alloc_pages_node(nid, gfp_mask, order);
}

#ifdef CONFIG_NUMA
extern struct page *alloc_pages_current(gfp_t gfp_mask, unsigned order);

static inline struct page *
alloc_pages(gfp_t gfp_mask, unsigned int order)
{
	return alloc_pages_current(gfp_mask, order);
}
extern struct page *alloc_pages_vma(gfp_t gfp_mask, int order,
			struct vm_area_struct *vma, unsigned long addr,
			int node, bool hugepage);
#define alloc_hugepage_vma(gfp_mask, vma, addr, order)	\
	alloc_pages_vma(gfp_mask, order, vma, addr, numa_node_id(), true)
#else
/*页面分配器,分配2的order次方个连续的物理页面并返回起始页的 page实例*/
#define alloc_pages(gfp_mask, order) \
		alloc_pages_node(numa_node_id(), gfp_mask, order)
#define alloc_pages_vma(gfp_mask, order, vma, addr, node, false)\
	alloc_pages(gfp_mask, order)
#define alloc_hugepage_vma(gfp_mask, vma, addr, order)	\
	alloc_pages(gfp_mask, order)
#endif

/*只用于分配一个物理页面，alloc_page()是order=0时alloc_pages的简化形式，
只分配单个页面,如果系统没有足够的空间满足alloc_page的分配，函数将返回NULL*/
#define alloc_page(gfp_mask) alloc_pages(gfp_mask, 0)
#define alloc_page_vma(gfp_mask, vma, addr)			\
	alloc_pages_vma(gfp_mask, 0, vma, addr, numa_node_id(), false)
#define alloc_page_vma_node(gfp_mask, vma, addr, node)		\
	alloc_pages_vma(gfp_mask, 0, vma, addr, node, false)
/*该函数不能在高端内存分配页面*/
extern unsigned long __get_free_pages(gfp_t gfp_mask, unsigned int order);
extern unsigned long get_zeroed_page(gfp_t gfp_mask);

void *alloc_pages_exact(size_t size, gfp_t gfp_mask);
void free_pages_exact(void *virt, size_t size);
void * __meminit alloc_pages_exact_nid(int nid, size_t size, gfp_t gfp_mask);

/* 如果只想分配单个物理页面可以用这个函数，他是order=0时
 * __get_free_pages的简化形式*/
#define __get_free_page(gfp_mask) \
		__get_free_pages((gfp_mask), 0)

/*用于从ZONE_DMA区域中分配物理页，返回页面所在线性地址*/
#define __get_dma_pages(gfp_mask, order) \
		__get_free_pages((gfp_mask) | GFP_DMA, (order))

extern void __free_pages(struct page *page, unsigned int order);
extern void free_pages(unsigned long addr, unsigned int order);
extern void free_hot_cold_page(struct page *page, bool cold);
extern void free_hot_cold_page_list(struct list_head *list, bool cold);

struct page_frag_cache;
extern void __page_frag_cache_drain(struct page *page, unsigned int count);
extern void *page_frag_alloc(struct page_frag_cache *nc,
			     unsigned int fragsz, gfp_t gfp_mask);
extern void page_frag_free(void *addr);

#define __free_page(page) __free_pages((page), 0)
/**
 * 释放页面的主函数
 */
#define free_page(addr) free_pages((addr), 0)

void page_alloc_init(void);
void drain_zone_pages(struct zone *zone, struct per_cpu_pages *pcp);
void drain_all_pages(struct zone *zone);
void drain_local_pages(struct zone *zone);

void page_alloc_init_late(void);

/*
 * gfp_allowed_mask is set to GFP_BOOT_MASK during early boot to restrict what
 * GFP flags are used before interrupts are enabled. Once interrupts are
 * enabled, it is set to __GFP_BITS_MASK while the system is running. During
 * hibernation, it is used by PM to avoid I/O during memory allocation while
 * devices are suspended.
 */
extern gfp_t gfp_allowed_mask;

/* Returns true if the gfp_mask allows use of ALLOC_NO_WATERMARK */
bool gfp_pfmemalloc_allowed(gfp_t gfp_mask);

extern void pm_restrict_gfp_mask(void);
extern void pm_restore_gfp_mask(void);

#ifdef CONFIG_PM_SLEEP
extern bool pm_suspended_storage(void);
#else
static inline bool pm_suspended_storage(void)
{
	return false;
}
#endif /* CONFIG_PM_SLEEP */

#if (defined(CONFIG_MEMORY_ISOLATION) && defined(CONFIG_COMPACTION)) || defined(CONFIG_CMA)
/* The below functions must be run on a range from a single zone. */
extern int alloc_contig_range(unsigned long start, unsigned long end,
			      unsigned migratetype);
extern void free_contig_range(unsigned long pfn, unsigned nr_pages);
#endif

#ifdef CONFIG_CMA
/* CMA stuff */
extern void init_cma_reserved_pageblock(struct page *page);
#endif

#endif /* __LINUX_GFP_H */
