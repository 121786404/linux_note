/*
 *  linux/mm/page_alloc.c
 *
 *  Manages the free list, the system allocates free pages here.
 *  Note that kmalloc() lives in slab.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *  Reshaped it to be a zoned allocator, Ingo Molnar, Red Hat, 1999
 *  Discontiguous memory support, Kanoj Sarcar, SGI, Nov 1999
 *  Zone balancing, Kanoj Sarcar, SGI, Jan 2000
 *  Per cpu hot/cold page lists, bulk allocation, Martin J. Bligh, Sept 2002
 *          (lots of bits borrowed from Ingo Molnar & Andrew Morton)
 */

#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/jiffies.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/kmemcheck.h>
#include <linux/kasan.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/pagevec.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/oom.h>
#include <linux/notifier.h>
#include <linux/topology.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/memory_hotplug.h>
#include <linux/nodemask.h>
#include <linux/vmalloc.h>
#include <linux/vmstat.h>
#include <linux/mempolicy.h>
#include <linux/memremap.h>
#include <linux/stop_machine.h>
#include <linux/sort.h>
#include <linux/pfn.h>
#include <linux/backing-dev.h>
#include <linux/fault-inject.h>
#include <linux/page-isolation.h>
#include <linux/page_ext.h>
#include <linux/debugobjects.h>
#include <linux/kmemleak.h>
#include <linux/compaction.h>
#include <trace/events/kmem.h>
#include <trace/events/oom.h>
#include <linux/prefetch.h>
#include <linux/mm_inline.h>
#include <linux/migrate.h>
#include <linux/page_ext.h>
#include <linux/hugetlb.h>
#include <linux/sched/rt.h>
#include <linux/page_owner.h>
#include <linux/kthread.h>
#include <linux/memcontrol.h>

#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/div64.h>
#include "internal.h"

/* prevent >1 _updater_ of zone percpu pageset ->high and ->batch fields */
static DEFINE_MUTEX(pcp_batch_high_lock);
#define MIN_PERCPU_PAGELIST_FRACTION	(8)

#ifdef CONFIG_USE_PERCPU_NUMA_NODE_ID
DEFINE_PER_CPU(int, numa_node);
EXPORT_PER_CPU_SYMBOL(numa_node);
#endif

#ifdef CONFIG_HAVE_MEMORYLESS_NODES
/*
 * N.B., Do NOT reference the '_numa_mem_' per cpu variable directly.
 * It will not be defined when CONFIG_HAVE_MEMORYLESS_NODES is not defined.
 * Use the accessor functions set_numa_mem(), numa_mem_id() and cpu_to_mem()
 * defined in <linux/topology.h>.
 */
DEFINE_PER_CPU(int, _numa_mem_);		/* Kernel "local memory" node */
EXPORT_PER_CPU_SYMBOL(_numa_mem_);
int _node_numa_mem_[MAX_NUMNODES];
#endif

#ifdef CONFIG_GCC_PLUGIN_LATENT_ENTROPY
volatile unsigned long latent_entropy __latent_entropy;
EXPORT_SYMBOL(latent_entropy);
#endif

/*
 * Array of node states.
 */
/*
 * node_states数组中的每一个nodemask_t类型的位图中，当某一位设置时，
 * 其所在的索引是一个结点ID。这里将每个元素的成员设置为1UL，
 * 是因为系统中至少有一个结点，也就是说肯定存在结点ID为0的结点。
 */
nodemask_t node_states[NR_NODE_STATES] __read_mostly = {
	[N_POSSIBLE] = NODE_MASK_ALL,
	[N_ONLINE] = { { [0] = 1UL } },
#ifndef CONFIG_NUMA
	[N_NORMAL_MEMORY] = { { [0] = 1UL } },
#ifdef CONFIG_HIGHMEM
	[N_HIGH_MEMORY] = { { [0] = 1UL } },
#endif
#ifdef CONFIG_MOVABLE_NODE
	[N_MEMORY] = { { [0] = 1UL } },
#endif
	[N_CPU] = { { [0] = 1UL } },
#endif	/* NUMA */
};
EXPORT_SYMBOL(node_states);

/* Protect totalram_pages and zone->managed_pages */
static DEFINE_SPINLOCK(managed_page_count_lock);

unsigned long totalram_pages __read_mostly;
unsigned long totalreserve_pages __read_mostly;
unsigned long totalcma_pages __read_mostly;

int percpu_pagelist_fraction;
gfp_t gfp_allowed_mask __read_mostly = GFP_BOOT_MASK;

/*
 * A cached value of the page's pageblock's migratetype, used when the page is
 * put on a pcplist. Used to avoid the pageblock migratetype lookup when
 * freeing from pcplists in most cases, at the cost of possibly becoming stale.
 * Also the migratetype set in the page does not necessarily match the pcplist
 * index, e.g. page might have MIGRATE_CMA set but be on a pcplist with any
 * other index - this ensures that it will be put on the correct CMA freelist.
 */
static inline int get_pcppage_migratetype(struct page *page)
{
	return page->index;
}

static inline void set_pcppage_migratetype(struct page *page, int migratetype)
{
	page->index = migratetype;
}

#ifdef CONFIG_PM_SLEEP
/*
 * The following functions are used by the suspend/hibernate code to temporarily
 * change gfp_allowed_mask in order to avoid using I/O during memory allocations
 * while devices are suspended.  To avoid races with the suspend/hibernate code,
 * they should always be called with pm_mutex held (gfp_allowed_mask also should
 * only be modified with pm_mutex held, unless the suspend/hibernate code is
 * guaranteed not to run in parallel with that modification).
 */

static gfp_t saved_gfp_mask;

void pm_restore_gfp_mask(void)
{
	WARN_ON(!mutex_is_locked(&pm_mutex));
	if (saved_gfp_mask) {
		gfp_allowed_mask = saved_gfp_mask;
		saved_gfp_mask = 0;
	}
}

void pm_restrict_gfp_mask(void)
{
	WARN_ON(!mutex_is_locked(&pm_mutex));
	WARN_ON(saved_gfp_mask);
	saved_gfp_mask = gfp_allowed_mask;
	gfp_allowed_mask &= ~(__GFP_IO | __GFP_FS);
}

bool pm_suspended_storage(void)
{
	if ((gfp_allowed_mask & (__GFP_IO | __GFP_FS)) == (__GFP_IO | __GFP_FS))
		return false;
	return true;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_HUGETLB_PAGE_SIZE_VARIABLE
unsigned int pageblock_order __read_mostly;
#endif

static void __free_pages_ok(struct page *page, unsigned int order);

/*
 * results with 256, 32 in the lowmem_reserve sysctl:
 *	1G machine -> (16M dma, 800M-16M normal, 1G-800M high)
 *	1G machine -> (16M dma, 784M normal, 224M high)
 *	NORMAL allocation will leave 784M/256 of ram reserved in the ZONE_DMA
 *	HIGHMEM allocation will leave 224M/32 of ram reserved in ZONE_NORMAL
 *	HIGHMEM allocation will leave (224M+784M)/256 of ram reserved in ZONE_DMA
 *
 * TBD: should special case ZONE_DMA32 machines here - in those we normally
 * don't need any ZONE_NORMAL reservation
 */
int sysctl_lowmem_reserve_ratio[MAX_NR_ZONES-1] = {
#ifdef CONFIG_ZONE_DMA
	 256,
#endif
#ifdef CONFIG_ZONE_DMA32
	 256,
#endif
#ifdef CONFIG_HIGHMEM
	 32,
#endif
	 32,
};

EXPORT_SYMBOL(totalram_pages);

static char * const zone_names[MAX_NR_ZONES] = {
#ifdef CONFIG_ZONE_DMA
	 "DMA",
#endif
#ifdef CONFIG_ZONE_DMA32
	 "DMA32",
#endif
	 "Normal",
#ifdef CONFIG_HIGHMEM
	 "HighMem",
#endif
	 "Movable",
#ifdef CONFIG_ZONE_DEVICE
	 "Device",
#endif
};

char * const migratetype_names[MIGRATE_TYPES] = {
	"Unmovable",
	"Movable",
	"Reclaimable",
	"HighAtomic",
#ifdef CONFIG_CMA
	"CMA",
#endif
#ifdef CONFIG_MEMORY_ISOLATION
	"Isolate",
#endif
};

compound_page_dtor * const compound_page_dtors[] = {
	NULL,
	free_compound_page,
#ifdef CONFIG_HUGETLB_PAGE
	free_huge_page,
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	free_transhuge_page,
#endif
};

/**
 *
 * 该值随可用内存的大小而非线性增长，并保存在全局变量min_free_kbytes中。
 * 用户层可通过文件/proc/sys/vm/min_free_kbytes来读取和修改该设置。
 *
 * 内核保留内存池大小。
 * 一般等于sqrt(16*内核直接映射内存大小) 最小不低于128K，最多不超过64M

 参数规定了可用于诸如中断处理程序发起的紧急分配等的内存池的大小。
 在更大规模的系统上， 该参数会影响可用于中断时间的内存与可用于运行时分配的内存比率。
 在更小的内存系统上， 该参数被保持为较小值以避免分配失败。
 普遍共识是这个低水印参数用于确定何时拒绝用户空间分配以及何时激发页面替换场景。
 有可能不将该值设置为任何较大的物理内存比例。
 如果用户空间操作是内存子系统主要消耗者，尤其适用
 */
int min_free_kbytes = 1024;
int user_min_free_kbytes = -1;
int watermark_scale_factor = 10;
/*
 * 统计所有一致映射的页(应该是指内核中用到的)
 */
static unsigned long __meminitdata nr_kernel_pages;//统计所有一致映射的页
/*
 * 包括高端内存页在内
 */
static unsigned long __meminitdata nr_all_pages;
static unsigned long __meminitdata dma_reserve;

#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
/*
各个内存域可使用的最低内存页帧编号
*/
static unsigned long __meminitdata arch_zone_lowest_possible_pfn[MAX_NR_ZONES];
/*
各个内存域可使用的最高内存页帧编号
*/
static unsigned long __meminitdata arch_zone_highest_possible_pfn[MAX_NR_ZONES];
/*
 * kernelcore参数(应该是在grub.conf中)用来指定用于不可移动分配的内存
 * 数量，即用于既不能回收也不能迁移的内存数量。剩余的内存用于可移动分配。
 * 在分析该参数之后，结果保存在全局变量required_kernelcore中。
 */
static unsigned long __initdata required_kernelcore;
/*
 * 参数moveablecore控制用于可移动内存分配的内存数量。
 * required_kernelcore的大小会根据此计算。如果有人同时
 * 指定两个参数，内核会按前述方法计算出required_kernelcore
 * 的值，并取指定值和计算值中较大的一个。
 */
static unsigned long __initdata required_movablecore;
static unsigned long __meminitdata zone_movable_pfn[MAX_NUMNODES];
static bool mirrored_kernelcore;

/* movable_zone is the "real" zone pages in ZONE_MOVABLE are taken from */
int movable_zone;
EXPORT_SYMBOL(movable_zone);
#endif /* CONFIG_HAVE_MEMBLOCK_NODE_MAP */

#if MAX_NUMNODES > 1
int nr_node_ids __read_mostly = MAX_NUMNODES;
int nr_online_nodes __read_mostly = 1;
EXPORT_SYMBOL(nr_node_ids);
EXPORT_SYMBOL(nr_online_nodes);
#endif

int page_group_by_mobility_disabled __read_mostly;

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
static inline void reset_deferred_meminit(pg_data_t *pgdat)
{
	pgdat->first_deferred_pfn = ULONG_MAX;
}

/* Returns true if the struct page for the pfn is uninitialised */
static inline bool __meminit early_page_uninitialised(unsigned long pfn)
{
	int nid = early_pfn_to_nid(pfn);

	if (node_online(nid) && pfn >= NODE_DATA(nid)->first_deferred_pfn)
		return true;

	return false;
}

/*
 * Returns false when the remaining initialisation should be deferred until
 * later in the boot cycle when it can be parallelised.
 */
static inline bool update_defer_init(pg_data_t *pgdat,
				unsigned long pfn, unsigned long zone_end,
				unsigned long *nr_initialised)
{
	unsigned long max_initialise;

	/* Always populate low zones for address-contrained allocations */
	if (zone_end < pgdat_end_pfn(pgdat))
		return true;
	/*
	 * Initialise at least 2G of a node but also take into account that
	 * two large system hashes that can take up 1GB for 0.25TB/node.
	 */
	max_initialise = max(2UL << (30 - PAGE_SHIFT),
		(pgdat->node_spanned_pages >> 8));

	(*nr_initialised)++;
	if ((*nr_initialised > max_initialise) &&
	    (pfn & (PAGES_PER_SECTION - 1)) == 0) {
		pgdat->first_deferred_pfn = pfn;
		return false;
	}

	return true;
}
#else
static inline void reset_deferred_meminit(pg_data_t *pgdat)
{
}

static inline bool early_page_uninitialised(unsigned long pfn)
{
	return false;
}

static inline bool update_defer_init(pg_data_t *pgdat,
				unsigned long pfn, unsigned long zone_end,
				unsigned long *nr_initialised)
{
	return true;
}
#endif

/* Return a pointer to the bitmap storing bits affecting a block of pages */
static inline unsigned long *get_pageblock_bitmap(struct page *page,
							unsigned long pfn)
{
#ifdef CONFIG_SPARSEMEM
	return __pfn_to_section(pfn)->pageblock_flags;
#else
	return page_zone(page)->pageblock_flags;
#endif /* CONFIG_SPARSEMEM */
}

static inline int pfn_to_bitidx(struct page *page, unsigned long pfn)
{
#ifdef CONFIG_SPARSEMEM
	pfn &= (PAGES_PER_SECTION-1);
	return (pfn >> pageblock_order) * NR_PAGEBLOCK_BITS;
#else
	pfn = pfn - round_down(page_zone(page)->zone_start_pfn, pageblock_nr_pages);
	return (pfn >> pageblock_order) * NR_PAGEBLOCK_BITS;
#endif /* CONFIG_SPARSEMEM */
}

/**
 * get_pfnblock_flags_mask - Return the requested group of flags for the pageblock_nr_pages block of pages
 * @page: The page within the block of interest
 * @pfn: The target page frame number
 * @end_bitidx: The last bit of interest to retrieve
 * @mask: mask of bits that the caller is interested in
 *
 * Return: pageblock_bits flags
 */
static __always_inline unsigned long __get_pfnblock_flags_mask(struct page *page,
					unsigned long pfn,
					unsigned long end_bitidx,
					unsigned long mask)
{
	unsigned long *bitmap;
	unsigned long bitidx, word_bitidx;
	unsigned long word;

	bitmap = get_pageblock_bitmap(page, pfn);
	bitidx = pfn_to_bitidx(page, pfn);
	word_bitidx = bitidx / BITS_PER_LONG;
	bitidx &= (BITS_PER_LONG-1);

	word = bitmap[word_bitidx];
	bitidx += end_bitidx;
	return (word >> (BITS_PER_LONG - bitidx - 1)) & mask;
}

unsigned long get_pfnblock_flags_mask(struct page *page, unsigned long pfn,
					unsigned long end_bitidx,
					unsigned long mask)
{
	return __get_pfnblock_flags_mask(page, pfn, end_bitidx, mask);
}

static __always_inline int get_pfnblock_migratetype(struct page *page, unsigned long pfn)
{
	return __get_pfnblock_flags_mask(page, pfn, PB_migrate_end, MIGRATETYPE_MASK);
}

/**
 * set_pfnblock_flags_mask - Set the requested group of flags for a pageblock_nr_pages block of pages
 * @page: The page within the block of interest
 * @flags: The flags to set
 * @pfn: The target page frame number
 * @end_bitidx: The last bit of interest
 * @mask: mask of bits that the caller is interested in
 */
void set_pfnblock_flags_mask(struct page *page, unsigned long flags,
					unsigned long pfn,
					unsigned long end_bitidx,
					unsigned long mask)
{
	unsigned long *bitmap;
	unsigned long bitidx, word_bitidx;
	unsigned long old_word, word;

	BUILD_BUG_ON(NR_PAGEBLOCK_BITS != 4);

    /*
        得到位图的起始地址，即zone->pageblock_flags
    */
	bitmap = get_pageblock_bitmap(page, pfn);
	/*
	    得到pfn对应的位图区域的偏移量
	*/
	bitidx = pfn_to_bitidx(page, pfn);
	word_bitidx = bitidx / BITS_PER_LONG;
	bitidx &= (BITS_PER_LONG-1);

	VM_BUG_ON_PAGE(!zone_spans_pfn(page_zone(page), pfn), page);

	bitidx += end_bitidx;
	mask <<= (BITS_PER_LONG - bitidx - 1);
	flags <<= (BITS_PER_LONG - bitidx - 1);

	word = READ_ONCE(bitmap[word_bitidx]);
	for (;;) {
		old_word = cmpxchg(&bitmap[word_bitidx], word, (word & ~mask) | flags);
		if (word == old_word)
			break;
		word = old_word;
	}
}

/*
设置以page为首的一个内存区的迁移类型
*/
void set_pageblock_migratetype(struct page *page, int migratetype)
{
	if (unlikely(page_group_by_mobility_disabled &&
		     migratetype < MIGRATE_PCPTYPES))
		migratetype = MIGRATE_UNMOVABLE;

	set_pageblock_flags_group(page, (unsigned long)migratetype,
					PB_migrate, PB_migrate_end);
}

#ifdef CONFIG_DEBUG_VM
static int page_outside_zone_boundaries(struct zone *zone, struct page *page)
{
	int ret = 0;
	unsigned seq;
	unsigned long pfn = page_to_pfn(page);
	unsigned long sp, start_pfn;

	do {
		seq = zone_span_seqbegin(zone);
		start_pfn = zone->zone_start_pfn;
		sp = zone->spanned_pages;
		if (!zone_spans_pfn(zone, pfn))
			ret = 1;
	} while (zone_span_seqretry(zone, seq));

	if (ret)
		pr_err("page 0x%lx outside node %d zone %s [ 0x%lx - 0x%lx ]\n",
			pfn, zone_to_nid(zone), zone->name,
			start_pfn, start_pfn + sp);

	return ret;
}

static int page_is_consistent(struct zone *zone, struct page *page)
{
	if (!pfn_valid_within(page_to_pfn(page)))
		return 0;
	if (zone != page_zone(page))
		return 0;

	return 1;
}
/*
 * Temporary debugging check for pages not lying within a given zone.
 */
static int bad_range(struct zone *zone, struct page *page)
{
	if (page_outside_zone_boundaries(zone, page))
		return 1;
	if (!page_is_consistent(zone, page))
		return 1;

	return 0;
}
#else
static inline int bad_range(struct zone *zone, struct page *page)
{
	return 0;
}
#endif

static void bad_page(struct page *page, const char *reason,
		unsigned long bad_flags)
{
	static unsigned long resume;
	static unsigned long nr_shown;
	static unsigned long nr_unshown;

	/*
	 * Allow a burst of 60 reports, then keep quiet for that minute;
	 * or allow a steady drip of one report per second.
	 */
	if (nr_shown == 60) {
		if (time_before(jiffies, resume)) {
			nr_unshown++;
			goto out;
		}
		if (nr_unshown) {
			pr_alert(
			      "BUG: Bad page state: %lu messages suppressed\n",
				nr_unshown);
			nr_unshown = 0;
		}
		nr_shown = 0;
	}
	if (nr_shown++ == 0)
		resume = jiffies + 60 * HZ;

	pr_alert("BUG: Bad page state in process %s  pfn:%05lx\n",
		current->comm, page_to_pfn(page));
	__dump_page(page, reason);
	bad_flags &= page->flags;
	if (bad_flags)
		pr_alert("bad because of flags: %#lx(%pGp)\n",
						bad_flags, &bad_flags);
	dump_page_owner(page);

	print_modules();
	dump_stack();
out:
	/* Leave bad fields for debug, except PageBuddy could make trouble */
	page_mapcount_reset(page); /* remove PageBuddy */
	add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE);
}

/*
 * Higher-order pages are called "compound pages".  They are structured thusly:
 *
 * The first PAGE_SIZE page is called the "head page" and have PG_head set.
 *
 * The remaining PAGE_SIZE pages are called "tail pages". PageTail() is encoded
 * in bit 0 of page->compound_head. The rest of bits is pointer to head page.
 *
 * The first tail page's ->compound_dtor holds the offset in array of compound
 * page destructors. See compound_page_dtors.
 *
 * The first tail page's ->compound_order holds the order of allocation.
 * This usage means that zero-order pages may not be compound.
 */

void free_compound_page(struct page *page)
{
	__free_pages_ok(page, compound_order(page));
}

/**
 * 形成复合页。所有页，包括首页的private成员指向首页。
 * 第一个尾页的LRU链表lru.next保存析构函数，分配阶保存在lru.prev。
 */
void prep_compound_page(struct page *page, unsigned int order)
{
	int i;
	int nr_pages = 1 << order;

	set_compound_page_dtor(page, COMPOUND_PAGE_DTOR);
	set_compound_order(page, order);
	__SetPageHead(page);
	for (i = 1; i < nr_pages; i++) {
		struct page *p = page + i;
		set_page_count(p, 0);
		p->mapping = TAIL_MAPPING;
		set_compound_head(p, page);
	}
	atomic_set(compound_mapcount_ptr(page), -1);
}

#ifdef CONFIG_DEBUG_PAGEALLOC
unsigned int _debug_guardpage_minorder;
bool _debug_pagealloc_enabled __read_mostly
			= IS_ENABLED(CONFIG_DEBUG_PAGEALLOC_ENABLE_DEFAULT);
EXPORT_SYMBOL(_debug_pagealloc_enabled);
bool _debug_guardpage_enabled __read_mostly;

static int __init early_debug_pagealloc(char *buf)
{
	if (!buf)
		return -EINVAL;
	return kstrtobool(buf, &_debug_pagealloc_enabled);
}
early_param("debug_pagealloc", early_debug_pagealloc);

static bool need_debug_guardpage(void)
{
	/* If we don't use debug_pagealloc, we don't need guard page */
	if (!debug_pagealloc_enabled())
		return false;

	if (!debug_guardpage_minorder())
		return false;

	return true;
}

static void init_debug_guardpage(void)
{
	if (!debug_pagealloc_enabled())
		return;

	if (!debug_guardpage_minorder())
		return;

	_debug_guardpage_enabled = true;
}

struct page_ext_operations debug_guardpage_ops = {
	.need = need_debug_guardpage,
	.init = init_debug_guardpage,
};

static int __init debug_guardpage_minorder_setup(char *buf)
{
	unsigned long res;

	if (kstrtoul(buf, 10, &res) < 0 ||  res > MAX_ORDER / 2) {
		pr_err("Bad debug_guardpage_minorder value\n");
		return 0;
	}
	_debug_guardpage_minorder = res;
	pr_info("Setting debug_guardpage_minorder to %lu\n", res);
	return 0;
}
early_param("debug_guardpage_minorder", debug_guardpage_minorder_setup);

static inline bool set_page_guard(struct zone *zone, struct page *page,
				unsigned int order, int migratetype)
{
	struct page_ext *page_ext;

	if (!debug_guardpage_enabled())
		return false;

	if (order >= debug_guardpage_minorder())
		return false;

	page_ext = lookup_page_ext(page);
	if (unlikely(!page_ext))
		return false;

	__set_bit(PAGE_EXT_DEBUG_GUARD, &page_ext->flags);

	INIT_LIST_HEAD(&page->lru);
	set_page_private(page, order);
	/* Guard pages are not available for any usage */
	__mod_zone_freepage_state(zone, -(1 << order), migratetype);

	return true;
}

static inline void clear_page_guard(struct zone *zone, struct page *page,
				unsigned int order, int migratetype)
{
	struct page_ext *page_ext;

	if (!debug_guardpage_enabled())
		return;

	page_ext = lookup_page_ext(page);
	if (unlikely(!page_ext))
		return;

	__clear_bit(PAGE_EXT_DEBUG_GUARD, &page_ext->flags);

	set_page_private(page, 0);
	if (!is_migrate_isolate(migratetype))
		__mod_zone_freepage_state(zone, (1 << order), migratetype);
}
#else
struct page_ext_operations debug_guardpage_ops;
static inline bool set_page_guard(struct zone *zone, struct page *page,
			unsigned int order, int migratetype) { return false; }
static inline void clear_page_guard(struct zone *zone, struct page *page,
				unsigned int order, int migratetype) {}
#endif

/*
 * 对于回收到伙伴系统的内存区，该函数将第一个struct page实例的private标志设置为当前
 * 分配阶，并设置页的PG_buddy标志位。该标志表示内存块由伙伴系统管理。
 */
static inline void set_page_order(struct page *page, unsigned int order)
{
	set_page_private(page, order);
	__SetPageBuddy(page);
}

/*
 * 从页标志删除PG_buddy位，表示该页不再
 * 包含于伙伴系统中，并将struct page的private成员设置为0.
 */
static inline void rmv_page_order(struct page *page)
{
	__ClearPageBuddy(page);
	set_page_private(page, 0);
}

/*
 * This function checks whether a page is free && is the buddy
 * we can do coalesce a page and its buddy if
 * (a) the buddy is not in a hole (check before calling!) &&
 * (b) the buddy is in the buddy system &&
 * (c) a page and its buddy have the same order &&
 * (d) a page and its buddy are in the same zone.
 *
 * For recording whether a page is in the buddy system, we set ->_mapcount
 * PAGE_BUDDY_MAPCOUNT_VALUE.
 * Setting, clearing, and testing _mapcount PAGE_BUDDY_MAPCOUNT_VALUE is
 * serialized by zone->lock.
 *
 * For recording page's order, we use page_private(page).
 */
/*
 * 根据伙伴的页索引信息，并不足以作出判断。内核还必须确保属于伙伴的所有页都是
 * 空闲的，并包含在伙伴系统中，以便进行合并。
 */
static inline int page_is_buddy(struct page *page, struct page *buddy,
							unsigned int order)
{
	if (page_is_guard(buddy) && page_order(buddy) == order) {
		/* 判断两个页面是否位于同一个内存域中 */
		if (page_zone_id(page) != page_zone_id(buddy))
			return 0;

		VM_BUG_ON_PAGE(page_count(buddy) != 0, buddy);

		return 1;
	}

	/* 判断页面是否位于伙伴系统中。如果是，表示其为空闲页，同时判断二者的阶数是否相等 */
	if (PageBuddy(buddy) && page_order(buddy) == order) {
		/*
		 * zone check is done late to avoid uselessly
		 * calculating zone/node ids for pages that could
		 * never merge.
		 */

        /* 页的内存域是否相同 */
		if (page_zone_id(page) != page_zone_id(buddy))
			return 0;

		VM_BUG_ON_PAGE(page_count(buddy) != 0, buddy);

		return 1;
	}
	return 0;
}

/*
 * Freeing function for a buddy system allocator.
 *
 * The concept of a buddy system is to maintain direct-mapped table
 * (containing bit values) for memory blocks of various "orders".
 * The bottom level table contains the map for the smallest allocatable
 * units of memory (here, pages), and each level above it describes
 * pairs of units from the levels below, hence, "buddies".
 * At a high level, all that happens here is marking the table entry
 * at the bottom level available, and propagating the changes upward
 * as necessary, plus some accounting needed to play nicely with other
 * parts of the VM system.
 * At each level, we keep a list of pages, which are heads of continuous
 * free pages of length of (1 << order) and marked with _mapcount
 * PAGE_BUDDY_MAPCOUNT_VALUE. Page's order is recorded in page_private(page)
 * field.
 * So when we are allocating or freeing one, we can derive the state of the
 * other.  That is, if we allocate a small block, and both were
 * free, the remainder of the region must be split into blocks.
 * If a block is freed, and its buddy is also free, then this
 * triggers coalescing into a block of larger size.
 *
 * -- nyc
 */
/*  把page加回到free_list里面
从order开始迭代不断寻找自己的buddy
然后层层递归把低层的free_area中的page块向高层移动,
最后有一个优化, 内核认为刚释放的page很可能相关的buddy页也会释放,
所以把刚释放的页放到末尾让他更晚被
分配出去从而得到更多的机会被合并成大块
*/
/*
 * 按照伙伴系统的策略释放页框。
 * page-被释放块中所包含的第一个页框描述符的地址。
 * zone-管理区描述符的地址。
 * order-块大小的对数。
 * base-纯粹由于效率的原因而引入。其实可以从其他三个参数计算得出。
 * 该函数假定调用者已经禁止本地中断并获得了自旋锁。
 *
 * 该函数时内存释放功能的基础。相关的内存区被添加到伙伴系统中
 * 适当的free_area列表。在释放伙伴对时，该函数将其合并为一个
 * 连续内存区，放置到高一阶的free_area列表中。如果还能合并
 * 一个进一步的伙伴对，那么也进行合并，转移到更高阶的列表。
 * 该过程会一直重复下去，直至所有可能的伙伴对都已经合并，
 * 并将改变尽可能向上传播。
 */
static inline void __free_one_page(struct page *page,
		unsigned long pfn,
		struct zone *zone, unsigned int order,
		int migratetype)
{
	/**
	 * page_idx包含块中第一个页框的下标。
	 * 这是相对于管理区中的第一个页框而言的。
	 */
	unsigned long combined_pfn;
	unsigned long uninitialized_var(buddy_pfn);
	struct page *buddy;
	unsigned int max_order;

    /* 获取最大的阶数 */
	max_order = min_t(unsigned int, MAX_ORDER, pageblock_order + 1);
/*
检查wait_table是否存在来表示zone是否初始化过 里面
用到了双感叹号表示强制1或者0
*/
	VM_BUG_ON(!zone_is_initialized(zone));
	VM_BUG_ON_PAGE(page->flags & PAGE_FLAGS_CHECK_AT_PREP, page);

	VM_BUG_ON(migratetype == -1);

	/* 是isolate类型 */
	if (likely(!is_migrate_isolate(migratetype)))
		__mod_zone_freepage_state(zone, 1 << order, migratetype);

    /* 如果被释放的页不是所释放阶的第一个页，则说明参数有误 */
	VM_BUG_ON_PAGE(pfn & ((1 << order) - 1), page);
    /* 检查页面是否处于zone之中 */
	VM_BUG_ON_PAGE(bad_range(zone, page), page);

continue_merging:
    /* 从页的阶开始遍历，直到最高阶-1 */
    /* 释放页以后，当前页面可能与前后的空闲页组成更大的
          空闲页面，直到放到最大阶的伙伴系统中 */
	while (order < max_order - 1) {
	    /* 找到与当前页属于同一个阶的伙伴页面索引 */
		buddy_pfn = __find_buddy_pfn(pfn, order);
		/* 根据相对距离得到buddy page */
		buddy = page + (buddy_pfn - pfn);

		if (!pfn_valid_within(buddy_pfn))
			goto done_merging;
		/* 判断预期的伙伴页面是否与当前页处于同一个管理区，
		并且是否处于空闲状态。
		如果不是，则不能与该页合并为大的伙伴，退出 */
		if (!page_is_buddy(page, buddy, order))
			goto done_merging;
		/*
		 * Our buddy is free or it is CONFIG_DEBUG_PAGEALLOC guard page,
		 * merge with it and move up one order.
		 */
		if (page_is_guard(buddy)) {
			clear_page_guard(zone, buddy, order, migratetype);
		} else {
		/* 把buddy从free_area当中移除 */
		/* 伙伴是空闲的，合并并且向上一个order移动 */
			list_del(&buddy->lru);
            /* 同时减少当前阶中的空闲页计数 */
			zone->free_area[order].nr_free--;
			/* 去除伙伴页的标志，因为要将它合并到其他块中了 */
			rmv_page_order(buddy);
		}
		/* 继续找下一个伙伴页，注意，order变化了 */
		combined_pfn = buddy_pfn & pfn;
		/* 将当前页与伙伴页合并后，新的页面起始地址 */
		page = page + (combined_pfn - pfn);
		pfn = combined_pfn;
		/**
		 * 将合并了的块再与它的伙伴进行合并。
		 */
		order++;
	}
	if (max_order < MAX_ORDER) {
		/* If we are here, it means order is >= pageblock_order.
		 * We want to prevent merge between freepages on isolate
		 * pageblock and normal pageblock. Without this, pageblock
		 * isolation could cause incorrect freepage or CMA accounting.
		 *
		 * We don't want to hit this code for the more frequent
		 * low-order merging.
		 */
		 /* 在这里说明已经超出了pageblock的order,
		 可能在不同的migratetype的block边界了,
		 这里再检查一次是不是isolate,  不允许节点间迁移的page,
		 如果是的话就要结束合并的迭代, 不然继续向上合并 */
		if (unlikely(has_isolate_pageblock(zone))) {
			int buddy_mt;

			buddy_pfn = __find_buddy_pfn(pfn, order);
			buddy = page + (buddy_pfn - pfn);
			buddy_mt = get_pageblock_migratetype(buddy);

			if (migratetype != buddy_mt
					&& (is_migrate_isolate(migratetype) ||
						is_migrate_isolate(buddy_mt)))
				goto done_merging;
		}
		max_order++;
		goto continue_merging;
	}

done_merging:
    /* 结束合并自后设置合并之后的阶数 */
	/* 将最终形成的大块加入到伙伴系统的空闲链表中 */
	/**
	 * 伙伴不能与当前块合并。
	 * 将块插入适当的链表，并以块大小的order更新第一个页框的private字段。
	 */
	 /* 设置伙伴页中第一个空闲页的阶 */
	set_page_order(page, order);

	/*
	 * If this is not the largest possible page, check if the buddy
	 * of the next-highest order is free. If it is, it's possible
	 * that pages are being freed that will coalesce soon. In case,
	 * that is happening, add the free page to the tail of the list
	 * so it's less likely to be used soon and more likely to be merged
	 * as a higher order page
	 */
	 /* 这个条件判断没有合并到最大块,
	 内核认为很有可能接下来这个块和其他free的块合并,
	 从减少碎片的角度来说会更倾向于放到链表的末尾,
	 让这个块的free状态保持久一点, 更有机会被合并成更大的块

      如果当前合并后的页不是最大阶的，那么将当前空闲页放到伙伴链表的最后。
      这样，它将不会被很快被分配，更有可能与更高阶页面进行合并。
     */
	if ((order < MAX_ORDER-2) && pfn_valid_within(buddy_pfn)) {
		struct page *higher_page, *higher_buddy;
		/* 合并后的idx */
		/* 计算更高阶的页面索引及页面地址 */
		combined_pfn = buddy_pfn & pfn;
		/* 合并后的page */
		higher_page = page + (combined_pfn - pfn);
		/* 向上寻找buddy */
		buddy_pfn = __find_buddy_pfn(combined_pfn, order + 1);
		higher_buddy = higher_page + (buddy_pfn - combined_pfn);
		/* 更高阶的页面是空闲的，属于伙伴系统 */
		if (page_is_buddy(higher_page, higher_buddy, order + 1)) {
		/* page->lru队列增加到free_area相应的列表中 */
		/* 将当前页面合并到空闲链表的最后，尽量避免将它分配出去 */
			list_add_tail(&page->lru,
				&zone->free_area[order].free_list[migratetype]);
			goto out;
		}
	}
    /* 把page加入到对应的order当中 */
    /* 更高阶的页面已经分配出去，那么将当前页面放到链表前面 */
	list_add(&page->lru, &zone->free_area[order].free_list[migratetype]);
out:
    /* 将当前阶的空闲计数加1 */
	zone->free_area[order].nr_free++;
}

/*
 * A bad page could be due to a number of fields. Instead of multiple branches,
 * try and check multiple fields with one check. The caller must do a detailed
 * check if necessary.
 */
static inline bool page_expected_state(struct page *page,
					unsigned long check_flags)
{
	if (unlikely(atomic_read(&page->_mapcount) != -1))
		return false;

	if (unlikely((unsigned long)page->mapping |
			page_ref_count(page) |
#ifdef CONFIG_MEMCG
			(unsigned long)page->mem_cgroup |
#endif
			(page->flags & check_flags)))
		return false;

	return true;
}

static void free_pages_check_bad(struct page *page)
{
	const char *bad_reason;
	unsigned long bad_flags;

	bad_reason = NULL;
	bad_flags = 0;

	if (unlikely(atomic_read(&page->_mapcount) != -1))
		bad_reason = "nonzero mapcount";
	if (unlikely(page->mapping != NULL))
		bad_reason = "non-NULL mapping";
/*
		引用不为0
*/
	if (unlikely(page_ref_count(page) != 0))
		bad_reason = "nonzero _refcount";
	if (unlikely(page->flags & PAGE_FLAGS_CHECK_AT_FREE)) {
		bad_reason = "PAGE_FLAGS_CHECK_AT_FREE flag(s) set";
		bad_flags = PAGE_FLAGS_CHECK_AT_FREE;
	}
#ifdef CONFIG_MEMCG
	if (unlikely(page->mem_cgroup))
		bad_reason = "page still charged to cgroup";
#endif
	bad_page(page, bad_reason, bad_flags);
}

static inline int free_pages_check(struct page *page)
{
	if (likely(page_expected_state(page, PAGE_FLAGS_CHECK_AT_FREE)))
		return 0;

	/* Something has gone sideways, find it */
	free_pages_check_bad(page);
	return 1;
}

static int free_tail_pages_check(struct page *head_page, struct page *page)
{
	int ret = 1;

	/*
	 * We rely page->lru.next never has bit 0 set, unless the page
	 * is PageTail(). Let's make sure that's true even for poisoned ->lru.
	 */
	BUILD_BUG_ON((unsigned long)LIST_POISON1 & 1);

	if (!IS_ENABLED(CONFIG_DEBUG_VM)) {
		ret = 0;
		goto out;
	}
	switch (page - head_page) {
	case 1:
		/* the first tail page: ->mapping is compound_mapcount() */
		if (unlikely(compound_mapcount(page))) {
			bad_page(page, "nonzero compound_mapcount", 0);
			goto out;
		}
		break;
	case 2:
		/*
		 * the second tail page: ->mapping is
		 * page_deferred_list().next -- ignore value.
		 */
		break;
	default:
		if (page->mapping != TAIL_MAPPING) {
			bad_page(page, "corrupted mapping in tail page", 0);
			goto out;
		}
		break;
	}
	if (unlikely(!PageTail(page))) {
		bad_page(page, "PageTail not set", 0);
		goto out;
	}
	if (unlikely(compound_head(page) != head_page)) {
		bad_page(page, "compound_head not consistent", 0);
		goto out;
	}
	ret = 0;
out:
	page->mapping = NULL;
	clear_compound_head(page);
	return ret;
}

static __always_inline bool free_pages_prepare(struct page *page,
					unsigned int order, bool check_free)
{
	int bad = 0;

	VM_BUG_ON_PAGE(PageTail(page), page);

	trace_mm_page_free(page, order);
	kmemcheck_free_shadow(page, order);

	/*
	 * Check tail pages before head page information is cleared to
	 * avoid checking PageCompound for order-0 pages.
	 */
	if (unlikely(order)) {
		bool compound = PageCompound(page);
		int i;

		VM_BUG_ON_PAGE(compound && compound_order(page) != order, page);

		if (compound)
			ClearPageDoubleMap(page);
		for (i = 1; i < (1 << order); i++) {
			if (compound)
				bad += free_tail_pages_check(page, page + i);
			if (unlikely(free_pages_check(page + i))) {
				bad++;
				continue;
			}
			(page + i)->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
		}
	}
	if (PageMappingFlags(page))
		page->mapping = NULL;
	if (memcg_kmem_enabled() && PageKmemcg(page))
		memcg_kmem_uncharge(page, order);
	if (check_free)
		bad += free_pages_check(page);
	if (bad)
		return false;

	page_cpupid_reset_last(page);
	page->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
	reset_page_owner(page, order);

	if (!PageHighMem(page)) {
		debug_check_no_locks_freed(page_address(page),
					   PAGE_SIZE << order);
		debug_check_no_obj_freed(page_address(page),
					   PAGE_SIZE << order);
	}
	arch_free_page(page, order);
	kernel_poison_pages(page, 1 << order, 0);
	kernel_map_pages(page, 1 << order, 0);
	kasan_free_pages(page, order);

	return true;
}

#ifdef CONFIG_DEBUG_VM
static inline bool free_pcp_prepare(struct page *page)
{
	return free_pages_prepare(page, 0, true);
}

static inline bool bulkfree_pcp_prepare(struct page *page)
{
	return false;
}
#else
static bool free_pcp_prepare(struct page *page)
{
	return free_pages_prepare(page, 0, false);
}

static bool bulkfree_pcp_prepare(struct page *page)
{
	return free_pages_check(page);
}
#endif /* CONFIG_DEBUG_VM */

/*
 * Frees a number of pages from the PCP lists
 * Assumes all pages on list are in same zone, and of same order.
 * count is the number of pages to free.
 *
 * If the zone was previously in an "all pages pinned" state then look to
 * see if this freeing clears that state.
 *
 * And clear the zone's pages_scanned counter, to hold off the "all pages are
 * pinned" detection logic.
 */
/*
 遍历高速缓存中的migratetype,然后根据指定的count
 从高速缓存中交还给buddy system的页,
 当前路径是整个batch个的页都会还回去.
*/
static void free_pcppages_bulk(struct zone *zone, int count,
					struct per_cpu_pages *pcp)
{
	int migratetype = 0;
	int batch_free = 0;
	unsigned long nr_scanned;
	bool isolated_pageblocks;

    /**
         * 虽然管理区可以按照CPU节点分类，但是也可以跨CPU节点进行内存分配，
            因此这里需要用自旋锁保护管理区
         * 使用每CPU缓存的目的，也是为了减少使用这把锁。
      */
	spin_lock(&zone->lock);
	isolated_pageblocks = has_isolate_pageblock(zone);
	nr_scanned = node_page_state(zone->zone_pgdat, NR_PAGES_SCANNED);
	if (nr_scanned)
		__mod_node_page_state(zone->zone_pgdat, NR_PAGES_SCANNED, -nr_scanned);

    /* 连续释放指定数量的页面到伙伴系统 */
	while (count) {
		struct page *page;
		struct list_head *list;

		/*
		 * Remove pages from lists in a round-robin fashion. A
		 * batch_free count is maintained that is incremented when an
		 * empty list is encountered.  This is so more pages are freed
		 * off fuller lists instead of spinning excessively around empty
		 * lists
		 */
		/* 这里是从MIGRATE_UNMOVABLE到MIGRATE_MOVABLE三个
		每CPU缓存链表中依次搜索一个可释放的页面，
		直到找到一个可以回收的缓存链表。 */
		do {
		/* batch_free是最后扫描的链表索引 */
			batch_free++;
			/* 只从MIGRATE_UNMOVABLE..MIGRATE_MOVABLE三个链表中选择页面。从这三个链表中循环选择回收的页面 */
			if (++migratetype == MIGRATE_PCPTYPES)
				migratetype = 0;
			list = &pcp->lists[migratetype];
			/* 如果当前链表为空，则从下一个缓存链表中释放页面 */
		} while (list_empty(list));

		/* This is the only non-empty list. Free them all. */
		/* 只有一个缓存链表中有可回收页面，则从该链表中释放所有页面 */
		if (batch_free == MIGRATE_PCPTYPES)
			batch_free = count;

        /* 本循环从最后一个页面缓存链表中释放多个页面
                  到伙伴系统，越靠后的缓存链表，释放的页面数量越多 */
		do {
			int mt;	/* migratetype of the to-be-freed page */
            /* 取缓存链表的最后一个元素，
                 要么是最早放到缓存中的页面(其硬件缓存的热度也最低)，
                 要么就是"冷"页，不需要考虑硬件缓存 */
			page = list_last_entry(list, struct page, lru);
			/* must delete as __free_one_page list manipulates */
			/* 将页面从链表中摘除 */
			list_del(&page->lru);

			mt = get_pcppage_migratetype(page);
			/* MIGRATE_ISOLATE page should not go to pcplists */
			VM_BUG_ON_PAGE(is_migrate_isolate(mt), page);
			/* Pageblock could have been isolated meanwhile */
			if (unlikely(isolated_pageblocks))
				mt = get_pageblock_migratetype(page);

			if (bulkfree_pcp_prepare(page))
				continue;

            /* 将页面释放回伙伴系统。注意这里没有用migratetype变量，
                     而是用页面属性中的migratetype。原因请参见英文注释 */
			__free_one_page(page, page_to_pfn(page), zone, 0, mt);
            /* 调试代码 */
			trace_mm_page_pcpu_drain(page, 0, mt);
		} while (--count && --batch_free && !list_empty(list));
	}
	spin_unlock(&zone->lock);
}

/**
 * 将多个页面释放到伙伴系统。
 */
static void free_one_page(struct zone *zone,
				struct page *page, unsigned long pfn,
				unsigned int order,
				int migratetype)
{
	unsigned long nr_scanned;
	/* 获得管理区的自旋锁 */
	spin_lock(&zone->lock);
	nr_scanned = node_page_state(zone->zone_pgdat, NR_PAGES_SCANNED);
	if (nr_scanned)
		__mod_node_page_state(zone->zone_pgdat, NR_PAGES_SCANNED, -nr_scanned);

	if (unlikely(has_isolate_pageblock(zone) ||
		is_migrate_isolate(migratetype))) {
		migratetype = get_pfnblock_migratetype(page, pfn);
	}
    /*将页面释放回伙伴系统 */
	__free_one_page(page, pfn, zone, order, migratetype);
	spin_unlock(&zone->lock);
}

static void __meminit __init_single_page(struct page *page, unsigned long pfn,
				unsigned long zone, int nid)
{
	set_page_links(page, zone, nid, pfn);
	init_page_count(page);
	page_mapcount_reset(page);
	page_cpupid_reset_last(page);

	INIT_LIST_HEAD(&page->lru);
#ifdef WANT_PAGE_VIRTUAL
	/* The shift won't overflow because ZONE_NORMAL is below 4G. */
	if (!is_highmem_idx(zone))
		set_page_address(page, __va(pfn << PAGE_SHIFT));
#endif
}

static void __meminit __init_single_pfn(unsigned long pfn, unsigned long zone,
					int nid)
{
	return __init_single_page(pfn_to_page(pfn), pfn, zone, nid);
}

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
static void init_reserved_page(unsigned long pfn)
{
	pg_data_t *pgdat;
	int nid, zid;

	if (!early_page_uninitialised(pfn))
		return;

	nid = early_pfn_to_nid(pfn);
	pgdat = NODE_DATA(nid);

	for (zid = 0; zid < MAX_NR_ZONES; zid++) {
		struct zone *zone = &pgdat->node_zones[zid];

		if (pfn >= zone->zone_start_pfn && pfn < zone_end_pfn(zone))
			break;
	}
	__init_single_pfn(pfn, zid, nid);
}
#else
static inline void init_reserved_page(unsigned long pfn)
{
}
#endif /* CONFIG_DEFERRED_STRUCT_PAGE_INIT */

/*
 * Initialised pages do not have PageReserved set. This function is
 * called for each range allocated by the bootmem allocator and
 * marks the pages PageReserved. The remaining valid pages are later
 * sent to the buddy page allocator.
 */
void __meminit reserve_bootmem_region(phys_addr_t start, phys_addr_t end)
{
	unsigned long start_pfn = PFN_DOWN(start);
	unsigned long end_pfn = PFN_UP(end);

	for (; start_pfn < end_pfn; start_pfn++) {
		if (pfn_valid(start_pfn)) {
			struct page *page = pfn_to_page(start_pfn);

			init_reserved_page(start_pfn);

			/* Avoid false-positive PageTail() */
			INIT_LIST_HEAD(&page->lru);

			SetPageReserved(page);
		}
	}
}

/**
 * 直接释放页面到伙伴系统中
 */
static void __free_pages_ok(struct page *page, unsigned int order)
{
	unsigned long flags;
	int migratetype;
	unsigned long pfn = page_to_pfn(page);

    /* 如果释放页面失败(如重要的管理结构被破坏，或者错误的调用参数)则退出 */
	if (!free_pages_prepare(page, order, true))
		return;

	migratetype = get_pfnblock_migratetype(page, pfn);
	/* 接下来要操作伙伴系统了，
	由于在中断里面也会操作伙伴系统，因此这里需要关中断 */
	local_irq_save(flags);
	/* 统计计数，对释放的页面数量进行计数 */
	__count_vm_events(PGFREE, 1 << order);
	/* 批量释放页到伙伴系统 */
	free_one_page(page_zone(page), page, pfn, order, migratetype);
	/* 恢复中断 */
	local_irq_restore(flags);
}

//将bootmem页面释放给伙伴系统。
static void __init __free_pages_boot_core(struct page *page, unsigned int order)
{
	unsigned int nr_pages = 1 << order;
	struct page *p = page;
	unsigned int loop;

	//数据预取
	prefetchw(p);
	//遍历所有页面
	for (loop = 0; loop < (nr_pages - 1); loop++, p++) {
		prefetchw(p + 1);
		//清除其reserved标志，表示页面可用可交换
		__ClearPageReserved(p);
		set_page_count(p, 0);//将页面引用计数设置为0
	}
	/**
	 * 前面的循环少了一页，这里将最后一页标志置位
	 * 仅仅因为预取的关系，把程序复杂了，感觉不值。
	 */
	__ClearPageReserved(p);
	set_page_count(p, 0);

	//增加zone的页面计数
	page_zone(page)->managed_pages += nr_pages;
	//这里将第一个页面引用计数设置为1!!!!!
	set_page_refcounted(page);
	//将页面添加到zone的链表中。
	//释放后，其引用计数又成0了，这里注意体会一下。
	__free_pages(page, order);
}

#if defined(CONFIG_HAVE_ARCH_EARLY_PFN_TO_NID) || \
	defined(CONFIG_HAVE_MEMBLOCK_NODE_MAP)

static struct mminit_pfnnid_cache early_pfnnid_cache __meminitdata;

int __meminit early_pfn_to_nid(unsigned long pfn)
{
	static DEFINE_SPINLOCK(early_pfn_lock);
	int nid;

	spin_lock(&early_pfn_lock);
	nid = __early_pfn_to_nid(pfn, &early_pfnnid_cache);
	if (nid < 0)
		nid = first_online_node;
	spin_unlock(&early_pfn_lock);

	return nid;
}
#endif

#ifdef CONFIG_NODES_SPAN_OTHER_NODES
static inline bool __meminit meminit_pfn_in_nid(unsigned long pfn, int node,
					struct mminit_pfnnid_cache *state)
{
	int nid;

	nid = __early_pfn_to_nid(pfn, state);
	if (nid >= 0 && nid != node)
		return false;
	return true;
}

/* Only safe to use early in boot when initialisation is single-threaded */
static inline bool __meminit early_pfn_in_nid(unsigned long pfn, int node)
{
	return meminit_pfn_in_nid(pfn, node, &early_pfnnid_cache);
}

#else

static inline bool __meminit early_pfn_in_nid(unsigned long pfn, int node)
{
	return true;
}
static inline bool __meminit meminit_pfn_in_nid(unsigned long pfn, int node,
					struct mminit_pfnnid_cache *state)
{
	return true;
}
#endif


void __init __free_pages_bootmem(struct page *page, unsigned long pfn,
							unsigned int order)
{
	if (early_page_uninitialised(pfn))
		return;
	return __free_pages_boot_core(page, order);
}

/*
 * Check that the whole (or subset of) a pageblock given by the interval of
 * [start_pfn, end_pfn) is valid and within the same zone, before scanning it
 * with the migration of free compaction scanner. The scanners then need to
 * use only pfn_valid_within() check for arches that allow holes within
 * pageblocks.
 *
 * Return struct page pointer of start_pfn, or NULL if checks were not passed.
 *
 * It's possible on some configurations to have a setup like node0 node1 node0
 * i.e. it's possible that all pages within a zones range of pages do not
 * belong to a single zone. We assume that a border between node0 and node1
 * can occur within a single pageblock, but not a node0 node1 node0
 * interleaving within a single pageblock. It is therefore sufficient to check
 * the first and last page of a pageblock and avoid checking each individual
 * page in a pageblock.
 */
struct page *__pageblock_pfn_to_page(unsigned long start_pfn,
				     unsigned long end_pfn, struct zone *zone)
{
	struct page *start_page;
	struct page *end_page;

	/* end_pfn is one past the range we are checking */
	end_pfn--;

	if (!pfn_valid(start_pfn) || !pfn_valid(end_pfn))
		return NULL;

	start_page = pfn_to_page(start_pfn);

	if (page_zone(start_page) != zone)
		return NULL;

	end_page = pfn_to_page(end_pfn);

	/* This gives a shorter code than deriving page_zone(end_page) */
	if (page_zone_id(start_page) != page_zone_id(end_page))
		return NULL;

	return start_page;
}

void set_zone_contiguous(struct zone *zone)
{
	unsigned long block_start_pfn = zone->zone_start_pfn;
	unsigned long block_end_pfn;

	block_end_pfn = ALIGN(block_start_pfn + 1, pageblock_nr_pages);
	for (; block_start_pfn < zone_end_pfn(zone);
			block_start_pfn = block_end_pfn,
			 block_end_pfn += pageblock_nr_pages) {

		block_end_pfn = min(block_end_pfn, zone_end_pfn(zone));

		if (!__pageblock_pfn_to_page(block_start_pfn,
					     block_end_pfn, zone))
			return;
	}

	/* We confirm that there is no hole */
	zone->contiguous = true;
}

void clear_zone_contiguous(struct zone *zone)
{
	zone->contiguous = false;
}

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
static void __init deferred_free_range(struct page *page,
					unsigned long pfn, int nr_pages)
{
	int i;

	if (!page)
		return;

	/* Free a large naturally-aligned chunk if possible */
	if (nr_pages == pageblock_nr_pages &&
	    (pfn & (pageblock_nr_pages - 1)) == 0) {
		set_pageblock_migratetype(page, MIGRATE_MOVABLE);
		__free_pages_boot_core(page, pageblock_order);
		return;
	}

	for (i = 0; i < nr_pages; i++, page++, pfn++) {
		if ((pfn & (pageblock_nr_pages - 1)) == 0)
			set_pageblock_migratetype(page, MIGRATE_MOVABLE);
		__free_pages_boot_core(page, 0);
	}
}

/* Completion tracking for deferred_init_memmap() threads */
static atomic_t pgdat_init_n_undone __initdata;
static __initdata DECLARE_COMPLETION(pgdat_init_all_done_comp);

static inline void __init pgdat_init_report_one_done(void)
{
	if (atomic_dec_and_test(&pgdat_init_n_undone))
		complete(&pgdat_init_all_done_comp);
}

/* Initialise remaining memory on a node */
static int __init deferred_init_memmap(void *data)
{
	pg_data_t *pgdat = data;
	int nid = pgdat->node_id;
	struct mminit_pfnnid_cache nid_init_state = { };
	unsigned long start = jiffies;
	unsigned long nr_pages = 0;
	unsigned long walk_start, walk_end;
	int i, zid;
	struct zone *zone;
	unsigned long first_init_pfn = pgdat->first_deferred_pfn;
	const struct cpumask *cpumask = cpumask_of_node(pgdat->node_id);

	if (first_init_pfn == ULONG_MAX) {
		pgdat_init_report_one_done();
		return 0;
	}

	/* Bind memory initialisation thread to a local node if possible */
	if (!cpumask_empty(cpumask))
		set_cpus_allowed_ptr(current, cpumask);

	/* Sanity check boundaries */
	BUG_ON(pgdat->first_deferred_pfn < pgdat->node_start_pfn);
	BUG_ON(pgdat->first_deferred_pfn > pgdat_end_pfn(pgdat));
	pgdat->first_deferred_pfn = ULONG_MAX;

	/* Only the highest zone is deferred so find it */
	for (zid = 0; zid < MAX_NR_ZONES; zid++) {
		zone = pgdat->node_zones + zid;
		if (first_init_pfn < zone_end_pfn(zone))
			break;
	}

	for_each_mem_pfn_range(i, nid, &walk_start, &walk_end, NULL) {
		unsigned long pfn, end_pfn;
		struct page *page = NULL;
		struct page *free_base_page = NULL;
		unsigned long free_base_pfn = 0;
		int nr_to_free = 0;

		end_pfn = min(walk_end, zone_end_pfn(zone));
		pfn = first_init_pfn;
		if (pfn < walk_start)
			pfn = walk_start;
		if (pfn < zone->zone_start_pfn)
			pfn = zone->zone_start_pfn;

		for (; pfn < end_pfn; pfn++) {
			if (!pfn_valid_within(pfn))
				goto free_range;

			/*
			 * Ensure pfn_valid is checked every
			 * pageblock_nr_pages for memory holes
			 */
			if ((pfn & (pageblock_nr_pages - 1)) == 0) {
				if (!pfn_valid(pfn)) {
					page = NULL;
					goto free_range;
				}
			}

			if (!meminit_pfn_in_nid(pfn, nid, &nid_init_state)) {
				page = NULL;
				goto free_range;
			}

			/* Minimise pfn page lookups and scheduler checks */
			if (page && (pfn & (pageblock_nr_pages - 1)) != 0) {
				page++;
			} else {
				nr_pages += nr_to_free;
				deferred_free_range(free_base_page,
						free_base_pfn, nr_to_free);
				free_base_page = NULL;
				free_base_pfn = nr_to_free = 0;

				page = pfn_to_page(pfn);
				cond_resched();
			}

			if (page->flags) {
				VM_BUG_ON(page_zone(page) != zone);
				goto free_range;
			}

			__init_single_page(page, pfn, zid, nid);
			if (!free_base_page) {
				free_base_page = page;
				free_base_pfn = pfn;
				nr_to_free = 0;
			}
			nr_to_free++;

			/* Where possible, batch up pages for a single free */
			continue;
free_range:
			/* Free the current block of pages to allocator */
			nr_pages += nr_to_free;
			deferred_free_range(free_base_page, free_base_pfn,
								nr_to_free);
			free_base_page = NULL;
			free_base_pfn = nr_to_free = 0;
		}
		/* Free the last block of pages to allocator */
		nr_pages += nr_to_free;
		deferred_free_range(free_base_page, free_base_pfn, nr_to_free);

		first_init_pfn = max(end_pfn, first_init_pfn);
	}

	/* Sanity check that the next zone really is unpopulated */
	WARN_ON(++zid < MAX_NR_ZONES && populated_zone(++zone));

	pr_info("node %d initialised, %lu pages in %ums\n", nid, nr_pages,
					jiffies_to_msecs(jiffies - start));

	pgdat_init_report_one_done();
	return 0;
}
#endif /* CONFIG_DEFERRED_STRUCT_PAGE_INIT */

void __init page_alloc_init_late(void)
{
	struct zone *zone;

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
	int nid;

	/* There will be num_node_state(N_MEMORY) threads */
	atomic_set(&pgdat_init_n_undone, num_node_state(N_MEMORY));
	for_each_node_state(nid, N_MEMORY) {
		kthread_run(deferred_init_memmap, NODE_DATA(nid), "pgdatinit%d", nid);
	}

	/* Block until all are initialised */
	wait_for_completion(&pgdat_init_all_done_comp);

	/* Reinit limits that are based on free pages after the kernel is up */
	files_maxfiles_init();
#endif

	for_each_populated_zone(zone)
		set_zone_contiguous(zone);
}

#ifdef CONFIG_CMA
/* Free whole pageblock and set its migration type to MIGRATE_CMA. */
void __init init_cma_reserved_pageblock(struct page *page)
{
	unsigned i = pageblock_nr_pages;
	struct page *p = page;

	/* 进入buddy的空闲页面其page->_count都需要为0. */
	do {
		__ClearPageReserved(p);
		set_page_count(p, 0);
	} while (++p, --i);

	set_pageblock_migratetype(page, MIGRATE_CMA);

	if (pageblock_order >= MAX_ORDER) {
		i = pageblock_nr_pages;
		p = page;
		do {
			/**
			 * 将一个pageblock块的第一个page的_count设置为1的原因
			 * 是__free_pages的时候会put_page_testzero减1.
			 */
			set_page_refcounted(p);
			__free_pages(p, MAX_ORDER - 1);
			p += MAX_ORDER_NR_PAGES;
		} while (i -= MAX_ORDER_NR_PAGES);
	} else {
		set_page_refcounted(page);
		__free_pages(page, pageblock_order);
	}

	adjust_managed_page_count(page, pageblock_nr_pages);
}
#endif

/*
 * The order of subdivision here is critical for the IO subsystem.
 * Please do not alter this order without good reasons and regression
 * testing. Specifically, as large blocks of memory are subdivided,
 * the order in which smaller blocks are delivered depends on the order
 * they're subdivided in this function. This is the primary factor
 * influencing the order in which pages are delivered to the IO
 * subsystem according to empirical testing, and this is also justified
 * by considering the behavior of a buddy system containing a single
 * large block of memory acted on by a series of small allocations.
 * This behavior is a critical factor in sglist merging's success.
 *
 * -- nyc
 */
 /*
 从下往上找到合适的块以后再从上往下进行拆分
 向下一层的free_area留一半, 然后接着用另一半进行迭代
 * 如果需要分配的内存块长度小于所选择的连续页范围，即如果因为没有更小的
 * 适当内存块可用，而从较高的分配阶分配了一块内存，那么该内存块必须
 * 按照伙伴系统的原理分裂成小的块。这是通过expand()函数完成的。
 */
 /**
 * 将高阶的页面分割成低阶的页面,并放回伙伴系统.
 *        low:        分配出去的低阶
 *        high:        被分割的高阶
 */
static inline void expand(struct zone *zone, struct page *page,
	int low, int high, struct free_area *area,
	int migratetype)
{
	/* size初始值为32 */
	/* 要被分割的高阶页面数量 */
	unsigned long size = 1 << high;

    /* 从高阶向低阶迭代 */
	/* high从5依次变为4，3，size从32变为16,8 */
	/**
     * 我们假设将阶为4的页面组分割,并分配出去阶为2的页
     * 那么我们需要向伙伴系统中分别加入阶为2,3的页面组.
     * 这种情况下,high=4,low=2
     */
	while (high > low) {
		/* 依次递减链表位置和high */
		/* area,high,size分别表示本次要放回伙伴系统的链表指针,阶,当前阶的页面数量 */
		area--;
		high--;
		/* size缩小一倍 */
		size >>= 1;
		/* 检查要加入伙伴系统的第一页是否在管理区的范围内 */
		VM_BUG_ON_PAGE(bad_range(zone, &page[size]), &page[size]);

		/*
		 * Mark as guard pages (or page), that will allow to
		 * merge back to allocator when buddy will be freed.
		 * Corresponding page table entries will not be touched,
		 * pages will stay not present in virtual address space
		 */
		if (set_page_guard(zone, &page[size], high, migratetype))
			continue;

        /* 把page放到area的free_list里面, 这个page是一个数组形式的 比如之前是8 这里变成4,
                那么后半部分会被留下来，前半部分会继续用于迭代 */
        /* 将第一页链入相应的迁移类型链表中 */
		list_add(&page[size].lru, &area->free_list[migratetype]);
		/* 空闲块计数器+1 */
		/* 该阶的空闲页面计数值加1 */
		area->nr_free++;
		/* 相当于page->private = high 表示自己属于order为high的阶的block中 */
		/* 设置第一个页的private为分配阶，并设置伙伴系统标志。表示其属于伙伴系统空闲列表 */
        /* 标记页面的阶 */
		set_page_order(&page[size], high);
	}
}

static void check_new_page_bad(struct page *page)
{
	const char *bad_reason = NULL;
	unsigned long bad_flags = 0;

	if (unlikely(atomic_read(&page->_mapcount) != -1))
		bad_reason = "nonzero mapcount";
	if (unlikely(page->mapping != NULL))
		bad_reason = "non-NULL mapping";
	if (unlikely(page_ref_count(page) != 0))
		bad_reason = "nonzero _count";
	if (unlikely(page->flags & __PG_HWPOISON)) {
		bad_reason = "HWPoisoned (hardware-corrupted)";
		bad_flags = __PG_HWPOISON;
		/* Don't complain about hwpoisoned pages */
		page_mapcount_reset(page); /* remove PageBuddy */
		return;
	}
	if (unlikely(page->flags & PAGE_FLAGS_CHECK_AT_PREP)) {
		bad_reason = "PAGE_FLAGS_CHECK_AT_PREP flag set";
		bad_flags = PAGE_FLAGS_CHECK_AT_PREP;
	}
#ifdef CONFIG_MEMCG
	if (unlikely(page->mem_cgroup))
		bad_reason = "page still charged to cgroup";
#endif
	bad_page(page, bad_reason, bad_flags);
}

/*
 * This page is about to be returned from the page allocator
 */
static inline int check_new_page(struct page *page)
{
	if (likely(page_expected_state(page,
				PAGE_FLAGS_CHECK_AT_PREP|__PG_HWPOISON)))
		return 0;

	check_new_page_bad(page);
	return 1;
}

static inline bool free_pages_prezeroed(bool poisoned)
{
	return IS_ENABLED(CONFIG_PAGE_POISONING_ZERO) &&
		page_poisoning_enabled() && poisoned;
}

#ifdef CONFIG_DEBUG_VM
static bool check_pcp_refill(struct page *page)
{
	return false;
}

static bool check_new_pcp(struct page *page)
{
	return check_new_page(page);
}
#else
static bool check_pcp_refill(struct page *page)
{
	return check_new_page(page);
}
static bool check_new_pcp(struct page *page)
{
	return false;
}
#endif /* CONFIG_DEBUG_VM */

static bool check_new_pages(struct page *page, unsigned int order)
{
	int i;
	for (i = 0; i < (1 << order); i++) {
		struct page *p = page + i;

		if (unlikely(check_new_page(p)))
			return true;
	}

	return false;
}

inline void post_alloc_hook(struct page *page, unsigned int order,
				gfp_t gfp_flags)
{
	set_page_private(page, 0);
	/* 初始化页面引用计数标志 */
	set_page_refcounted(page);

	/* 做一些体系结构相关的工作 */
	arch_alloc_page(page, order);
	kernel_map_pages(page, 1 << order, 1);
	kernel_poison_pages(page, 1 << order, 1);
	kasan_alloc_pages(page, order);
	set_page_owner(page, order, gfp_flags);
}

/*
 * prep_new_page()对页进行几项检查，确保分配之后分配器处于理想状态。
 * 特别地，这意味着在现存的映射中不能使用该页，也没有设置不正确的标志
 * (如PG_locked或PG_buddy)，因为这说明页处于使用中，不应该防止在空闲
 * 列表上。但通常情况下不应该发生错误，否则意味着内核在其他地方出现错误
 */

static void prep_new_page(struct page *page, unsigned int order, gfp_t gfp_flags,
							unsigned int alloc_flags)
{
	int i;
	bool poisoned = true;

	for (i = 0; i < (1 << order); i++) {
		struct page *p = page + i;
		if (poisoned)
			poisoned &= page_is_poisoned(p);
	}

	post_alloc_hook(page, order, gfp_flags);
	/*
	 * 如果设置了__GFP_ZERO，prep_zero_page()
	 * 使用一个特定于体系结构的高效函数将页填充字节0.
	 */
	if (!free_pages_prezeroed(poisoned) && (gfp_flags & __GFP_ZERO))
		for (i = 0; i < (1 << order); i++)
			clear_highpage(page + i);

	/*
	 * 如果设置了__GFP_COMP并请求了多个页，内核必须将这些页组成复合页(compound page)。
	 * 第一个页称作页首(head page)，而所有其余各页称作尾页(tail page)。
	 */
	/* 分配了多页，并且需要组成复杂页 */
	if (order && (gfp_flags & __GFP_COMP))
		prep_compound_page(page, order);

	/*
	 * page is set pfmemalloc when ALLOC_NO_WATERMARKS was necessary to
	 * allocate the page. The expectation is that the caller is taking
	 * steps that will free more memory. The caller should avoid the page
	 * being used for !PFMEMALLOC purposes.
	 */
	if (alloc_flags & ALLOC_NO_WATERMARKS)
		set_page_pfmemalloc(page);
	else
		clear_page_pfmemalloc(page);
}

/*
 * Go through the free lists for the given migratetype and remove
 * the smallest available page from the freelists
 */
 /*
 依次从order开始向上寻找free_area并从list当中取出page块.
 * 按递增顺序遍历内存域的各个特定迁移类型的空闲页列表，直至找到合适的一项。
 *
 * 在管理区中找到一个空闲块。
 * 它需要两个参数：管理区描述符的地址和order。Order表示请求的空闲页块大小的对数值。
 * 如果页框被成功分配，则返回第一个被分配的页框的页描述符。否则返回NULL。
 * 本函数假设调用者已经禁止和本地中断并获得了自旋锁。
 */
 /**
 * 遍历指定迁移类型的伙伴系统链表,
 从链表中移动最小数量的页面返回给调用者.
 这是伙伴系统的快速处理流程.
 *        zone:        在该管理区的伙伴系统中分配页面
 *        order:        要分配的页面数量阶.
 *        migratetype:    在该迁移类型的链表中获取页面
 */
static inline
struct page *__rmqueue_smallest(struct zone *zone, unsigned int order,
						int migratetype)
{
	unsigned int current_order;
	struct free_area *area;
	struct page *page;

	/* Find a page of the appropriate size in the preferred list */
	/* 依次遍历所有阶，直到找到合适的空闲链表 */
	/**
	 * 从所请求的order开始，扫描每个可用块链表进行循环搜索。
	 */
	 /* 从指定的阶到最大阶进行遍历,直到找到一个可以分配的链表 */
	for (current_order = order; current_order < MAX_ORDER; ++current_order) {
	    /* 找到该阶对应的空闲页面链表 */
		area = &(zone->free_area[current_order]);
		/* 当前阶的迁移列表为空 */
		/**
		 * 对应的空闲块链表为空，在更大的空闲块链表中进行循环搜索。
		 */
		page = list_first_entry_or_null(&area->free_list[migratetype],
							struct page, lru);
		if (!page)
			continue; /* 如果失败就尝试更大的块 */
        /* 这个lru取决他的上下文，比如在这里就是free_list */
		/*
		 * 调用list_del()从链表移除一个内存块之后，
		 * 要注意，必须将struct free_area的nr_free成员
		 * 减1.还必须据此更新当前内存域的统计量，
		 * 还可以通过使用__mod_zone_page_state()实现。
		 */
		 /* 将第一个元素从链表中摘除 */
		list_del(&page->lru);
		/*
		 * 从页标志删除PG_buddy位，表示该页不再包含于伙伴系统中，
		 * 并将struct page的private成员设置为0.
		 */
		 /* rmv_page_order将页面的伙伴标志位清0,表示该页不再属于伙伴系统.同时将Private标志清0 */
		rmv_page_order(page);
		/* 减少链表元素计数，并记录内存域的可用内存 */
        /* 当前阶的空闲计数减1 */
		area->nr_free--;
		/* 如果是从高阶分配的，将它分裂，并将剩余的部分放回伙伴系统中 */
		/**
		 * 如果2^order空闲块链表中没有合适的空闲块，那么就是从更大的空闲链表中分配的。
		 * 将剩余的空闲块分散到合适的链表中去。
		 */
		  /**
         * 如果当前阶没有可用的空闲页,而必须将高阶的空闲页面分割,则expand将高阶的页面分割后放回伙伴系统
         * 如:我们将阶为4的页面分割,并向上层返回阶为2的页面,那么,我们需要将其中阶为3,2的页面放回伙伴系统.
         */
		expand(zone, page, order, current_order, area, migratetype);
		/* 设置page的migratetype */
		set_pcppage_migratetype(page, migratetype);
		return page;
	}

	/* 指定迁移类型没有内存，返回NULL */
	/**
	 * 直到循环结束都没有找到合适的空闲块，就返回NULL。
	 */
	return NULL;
}


/*
 * This array describes the order lists are fallen back to when
 * the free lists for the desirable migrate type are depleted
 */

/*
 该数组描述了指定迁移类型的空闲列表耗尽时
 其他空闲列表在备用列表中的次序
 每一行对应一个类型的备用搜索域的顺序,
 在内核想要分配不可移动页MIGRATE_UNMOVABLE时,
 如果对应链表为空, 则遍历fallbacks[MIGRATE_UNMOVABLE],
 首先后退到可回收页链表MIGRATE_RECLAIMABLE,
 接下来到可移动页链表MIGRATE_MOVABLE,
 最后到紧急分配链表MIGRATE_TYPES
*/
static int fallbacks[MIGRATE_TYPES][4] = {
    //  分配不可移动页失败的备用列表
	[MIGRATE_UNMOVABLE]   = { MIGRATE_RECLAIMABLE, MIGRATE_MOVABLE,   MIGRATE_TYPES },
	//  分配可回收页失败时的备用列表
	[MIGRATE_RECLAIMABLE] = { MIGRATE_UNMOVABLE,   MIGRATE_MOVABLE,   MIGRATE_TYPES },
	//  分配可移动页失败时的备用列表
	[MIGRATE_MOVABLE]     = { MIGRATE_RECLAIMABLE, MIGRATE_UNMOVABLE, MIGRATE_TYPES },
#ifdef CONFIG_CMA
	[MIGRATE_CMA]         = { MIGRATE_TYPES }, /* Never used */
#endif
#ifdef CONFIG_MEMORY_ISOLATION
	[MIGRATE_ISOLATE]     = { MIGRATE_TYPES }, /* Never used */
#endif
};

#ifdef CONFIG_CMA
static struct page *__rmqueue_cma_fallback(struct zone *zone,
					unsigned int order)
{
	return __rmqueue_smallest(zone, order, MIGRATE_CMA);
}
#else
static inline struct page *__rmqueue_cma_fallback(struct zone *zone,
					unsigned int order) { return NULL; }
#endif

/*
 * Move the free pages in a range to the free lists of the requested type.
 * Note that start_page and end_pages are not aligned on a pageblock
 * boundary. If alignment is required, use move_freepages_block()
 */
int move_freepages(struct zone *zone,
			  struct page *start_page, struct page *end_page,
			  int migratetype)
{
	struct page *page;
	unsigned int order;
	int pages_moved = 0;

#ifndef CONFIG_HOLES_IN_ZONE
	/*
	 * page_zone is not safe to call in this context when
	 * CONFIG_HOLES_IN_ZONE is set. This bug check is probably redundant
	 * anyway as we check zone boundaries in move_freepages_block().
	 * Remove at a later date when no bug reports exist related to
	 * grouping pages by mobility
	 */
	VM_BUG_ON(page_zone(start_page) != page_zone(end_page));
#endif

	for (page = start_page; page <= end_page;) {
		if (!pfn_valid_within(page_to_pfn(page))) {
			page++;
			continue;
		}

		/* Make sure we are not inadvertently changing nodes */
		VM_BUG_ON_PAGE(page_to_nid(page) != zone_to_nid(zone), page);

		if (!PageBuddy(page)) {
			page++;
			continue;
		}

		order = page_order(page);
		list_move(&page->lru,
			  &zone->free_area[order].free_list[migratetype]);
		page += 1 << order;
		pages_moved += 1 << order;
	}

	return pages_moved;
}

/*
 * move_freepages_block()试图将包含2^pageblock_order个页的整个
 * 内存块(包含当前将分配的内存块在内)转移到新的迁移列表。但只有
 * 空闲页(即设置了PG_buddy标志位的页)才会移动。此外，pageblock_order()
 * 还会考虑到内存域的边界，因此移动页的总数可能小于整个大内存块。
 */
int move_freepages_block(struct zone *zone, struct page *page,
				int migratetype)
{
	unsigned long start_pfn, end_pfn;
	struct page *start_page, *end_page;

	start_pfn = page_to_pfn(page);
	start_pfn = start_pfn & ~(pageblock_nr_pages-1);
	start_page = pfn_to_page(start_pfn);
	end_page = start_page + pageblock_nr_pages - 1;
	end_pfn = start_pfn + pageblock_nr_pages - 1;

	/* Do not cross zone boundaries */
	if (!zone_spans_pfn(zone, start_pfn))
		start_page = page;
	if (!zone_spans_pfn(zone, end_pfn))
		return 0;

	return move_freepages(zone, start_page, end_page, migratetype);
}

static void change_pageblock_range(struct page *pageblock_page,
					int start_order, int migratetype)
{
	int nr_pageblocks = 1 << (start_order - pageblock_order);

	while (nr_pageblocks--) {
		set_pageblock_migratetype(pageblock_page, migratetype);
		pageblock_page += pageblock_nr_pages;
	}
}

/*
 * When we are falling back to another migratetype during allocation, try to
 * steal extra free pages from the same pageblocks to satisfy further
 * allocations, instead of polluting multiple pageblocks.
 *
 * If we are stealing a relatively large buddy page, it is likely there will
 * be more free pages in the pageblock, so try to steal them all. For
 * reclaimable and unmovable allocations, we steal regardless of page size,
 * as fragmentation caused by those allocations polluting movable pageblocks
 * is worse than movable allocations stealing from unmovable and reclaimable
 * pageblocks.
 */
static bool can_steal_fallback(unsigned int order, int start_mt)
{
	/*
	 * Leaving this order check is intended, although there is
	 * relaxed order check in next check. The reason is that
	 * we can actually steal whole pageblock if this condition met,
	 * but, below check doesn't guarantee it and that is just heuristic
	 * so could be changed anytime.
	 */
	if (order >= pageblock_order)
		return true;

	if (order >= pageblock_order / 2 ||
		start_mt == MIGRATE_RECLAIMABLE ||
		start_mt == MIGRATE_UNMOVABLE ||
		page_group_by_mobility_disabled)
		return true;

	return false;
}

/*
 * This function implements actual steal behaviour. If order is large enough,
 * we can steal whole pageblock. If not, we first move freepages in this
 * pageblock and check whether half of pages are moved or not. If half of
 * pages are moved, we can change migratetype of pageblock and permanently
 * use it's pages as requested migratetype in the future.
 */
static void steal_suitable_fallback(struct zone *zone, struct page *page,
							  int start_type)
{
	unsigned int current_order = page_order(page);
	int pages;

	/* Take ownership for orders >= pageblock_order */
/*
	要分割的页面是一个大页面,则将整个页面全部迁移到
	当前迁移类型的链表中,这样可以避免过多的碎片
*/
	if (current_order >= pageblock_order) {
		change_pageblock_range(page, current_order, start_type);
		return;
	}
	/* 将页面从当前迁移链表转换到start_migratetype指定的迁移链表中 */
	pages = move_freepages_block(zone, page, start_type);

	/* Claim the whole block if over half of it is free */
	/* pages是移动的页面数,如果可移动的页面数量较多,则将整个大内存块的迁移类型修改 */
	if (pages >= (1 << (pageblock_order-1)) ||
			page_group_by_mobility_disabled)
		set_pageblock_migratetype(page, start_type);
}

/*
 * Check whether there is a suitable fallback freepage with requested order.
 * If only_stealable is true, this function returns fallback_mt only if
 * we can steal other freepages all together. This would help to reduce
 * fragmentation due to mixed migratetype pages in one pageblock.
 */
int find_suitable_fallback(struct free_area *area, unsigned int order,
			int migratetype, bool only_stealable, bool *can_steal)
{
	int i;
	int fallback_mt;

	if (area->nr_free == 0)
		return -1;

	*can_steal = false;
	for (i = 0;; i++) {
		fallback_mt = fallbacks[migratetype][i];
		if (fallback_mt == MIGRATE_TYPES)
			break;

		if (list_empty(&area->free_list[fallback_mt]))
			continue;

		if (can_steal_fallback(order, migratetype))
			*can_steal = true;

		if (!only_stealable)
			return fallback_mt;

		if (*can_steal)
			return fallback_mt;
	}

	return -1;
}

/*
 * Reserve a pageblock for exclusive use of high-order atomic allocations if
 * there are no empty page blocks that contain a page with a suitable order
 */
static void reserve_highatomic_pageblock(struct page *page, struct zone *zone,
				unsigned int alloc_order)
{
	int mt;
	unsigned long max_managed, flags;

	/*
	 * Limit the number reserved to 1 pageblock or roughly 1% of a zone.
	 * Check is race-prone but harmless.
	 */
	max_managed = (zone->managed_pages / 100) + pageblock_nr_pages;
	if (zone->nr_reserved_highatomic >= max_managed)
		return;

	spin_lock_irqsave(&zone->lock, flags);

	/* Recheck the nr_reserved_highatomic limit under the lock */
	if (zone->nr_reserved_highatomic >= max_managed)
		goto out_unlock;

	/* Yoink! */
	mt = get_pageblock_migratetype(page);
	if (mt != MIGRATE_HIGHATOMIC &&
			!is_migrate_isolate(mt) && !is_migrate_cma(mt)) {
		zone->nr_reserved_highatomic += pageblock_nr_pages;
		set_pageblock_migratetype(page, MIGRATE_HIGHATOMIC);
		move_freepages_block(zone, page, MIGRATE_HIGHATOMIC);
	}

out_unlock:
	spin_unlock_irqrestore(&zone->lock, flags);
}

/*
 * Used when an allocation is about to fail under memory pressure. This
 * potentially hurts the reliability of high-order allocations when under
 * intense memory pressure but failed atomic allocations should be easier
 * to recover from than an OOM.
 *
 * If @force is true, try to unreserve a pageblock even though highatomic
 * pageblock is exhausted.
 */
static bool unreserve_highatomic_pageblock(const struct alloc_context *ac,
						bool force)
{
	struct zonelist *zonelist = ac->zonelist;
	unsigned long flags;
	struct zoneref *z;
	struct zone *zone;
	struct page *page;
	int order;
	bool ret;

	for_each_zone_zonelist_nodemask(zone, z, zonelist, ac->high_zoneidx,
								ac->nodemask) {
		/*
		 * Preserve at least one pageblock unless memory pressure
		 * is really high.
		 */
		if (!force && zone->nr_reserved_highatomic <=
					pageblock_nr_pages)
			continue;

		spin_lock_irqsave(&zone->lock, flags);
		for (order = 0; order < MAX_ORDER; order++) {
			struct free_area *area = &(zone->free_area[order]);

			page = list_first_entry_or_null(
					&area->free_list[MIGRATE_HIGHATOMIC],
					struct page, lru);
			if (!page)
				continue;

			/*
			 * In page freeing path, migratetype change is racy so
			 * we can counter several free pages in a pageblock
			 * in this loop althoug we changed the pageblock type
			 * from highatomic to ac->migratetype. So we should
			 * adjust the count once.
			 */
			if (get_pageblock_migratetype(page) ==
							MIGRATE_HIGHATOMIC) {
				/*
				 * It should never happen but changes to
				 * locking could inadvertently allow a per-cpu
				 * drain to add pages to MIGRATE_HIGHATOMIC
				 * while unreserving so be safe and watch for
				 * underflows.
				 */
				zone->nr_reserved_highatomic -= min(
						pageblock_nr_pages,
						zone->nr_reserved_highatomic);
			}

			/*
			 * Convert to ac->migratetype and avoid the normal
			 * pageblock stealing heuristics. Minimally, the caller
			 * is doing the work and needs the pages. More
			 * importantly, if the block was always converted to
			 * MIGRATE_UNMOVABLE or another type then the number
			 * of pageblocks that cannot be completely freed
			 * may increase.
			 */
			set_pageblock_migratetype(page, ac->migratetype);
			ret = move_freepages_block(zone, page, ac->migratetype);
			if (ret) {
				spin_unlock_irqrestore(&zone->lock, flags);
				return ret;
			}
		}
		spin_unlock_irqrestore(&zone->lock, flags);
	}

	return false;
}

/* Remove an element from the buddy allocator from the fallback list */
static inline struct page *
__rmqueue_fallback(struct zone *zone, unsigned int order, int start_migratetype)
{
	struct free_area *area;
	unsigned int current_order;
	struct page *page;
	int fallback_mt;
	bool can_steal;

/*
和主要分配路径不同的事, 迭代器是从最大的阶数开始迭代的,
这个代表的意思是说我尽量多迁移一点空间给你,
不然你还要迁移的时候我又分配一小块空间给你,
你的碎片间接导致了我的碎片产生, 所以从大到小开始迭代.
*/
	/* Find the largest possible block of pages in the other list */
	/*
	 * 不只是相同的迁移类型，还要考虑备用列表中指定的不同迁移类型。请注意，
	 * 该函数会按照分配阶从大到小遍历!这与通常的策略相反，内核的策略是，如果
	 * 无法避免分配迁移类型不同的内存块，那么就分配一个尽可能大的内存块。如果
	 * 优先选择更小的内存块，则会向其他列表引入碎片，因为不同迁移类型的内存块
	 * 将会混合起来，这显然不是我们想要的
	 */
	 /* 从最高阶搜索,这样可以尽量的将其他迁移列表中的大块分割,避免形成过多的碎片 */
	for (current_order = MAX_ORDER-1;
				current_order >= order && current_order <= MAX_ORDER-1;
				--current_order) {
		/* 该列表没有可用的大块内存 */
		area = &(zone->free_area[current_order]);
		/* 遍历fallback table 找到合适的fallback migratetype 要综合考虑:
		是否有超过一定阈值的空闲页,并且是可以退化的页类型 */
		fallback_mt = find_suitable_fallback(area, current_order,
				start_migratetype, false, &can_steal);
		if (fallback_mt == -1)
			continue;

		/* 取下备用列表的第一个节点，并递减节点计数 */
		page = list_first_entry(&area->free_list[fallback_mt],
						struct page, lru);
		/* 从对应迁类型中"偷取"page
		TODO:具体如何迁移的 我感觉应该是很抽象的迁移就可以了 */
		if (can_steal &&
			get_pageblock_migratetype(page) != MIGRATE_HIGHATOMIC)
			steal_suitable_fallback(zone, page, start_migratetype);

		/* Remove the page from the freelists */
		area->nr_free--;
		list_del(&page->lru);
		rmv_page_order(page);

		/* 将大阶的页面块中未用的部分还回伙伴系统 */
		expand(zone, page, order, current_order, area,
					start_migratetype);
		/*
		 * The pcppage_migratetype may differ from pageblock's
		 * migratetype depending on the decisions in
		 * find_suitable_fallback(). This is OK as long as it does not
		 * differ for MIGRATE_CMA pageblocks. Those can be used as
		 * fallback only via special __rmqueue_cma_fallback() function
		 */
		set_pcppage_migratetype(page, start_migratetype);

		trace_mm_page_alloc_extfrag(page, order, current_order,
			start_migratetype, fallback_mt);

		return page;
	}

	return NULL;
}

/*
 * Do the hard work of removing an element from the buddy allocator.
 * Call me with the zone->lock already held.
 */
/*
 *         zone:        从该管理区的伙伴系统中分配页面.
 *        order:        要分配的页面阶数.即分配的页面数是2^order
 *        migratetype:    优先从本参数指定的迁移类型中分配页面.如果失败,再从备用迁移列表中分配.

 * 根据传递进来的分配阶、用于获取页的内存域、迁移类型，__rmqueue_smallest()
 * 扫描页的列表。直至找到适当的连续内存块。在这样做的时候，可以拆分伙伴。如果
 * 指定的迁移列表不能满足分配请求，则调用__rmqueue_fallback()尝试其他的迁移
 * 列表，作为应急措施
 */
static struct page *__rmqueue(struct zone *zone, unsigned int order,
				int migratetype)
{
	struct page *page;

	/**
	 * 扫描页链表，找到合适的连续内存块。有可能会拆分高阶块。
	 */
	 /* 优先从指定的迁移类型链表中分配页面 */
	page = __rmqueue_smallest(zone, order, migratetype);
	/**
	 * 在指定的迁移列表中没有找到内存，从其他迁移列表中分配。
	 */
	if (unlikely(!page)) {
	    /* MOVABLE的失败优先从cma迁移 */
		if (migratetype == MIGRATE_MOVABLE)
			page = __rmqueue_cma_fallback(zone, order);

        /* 失败的话会从其他备选迁移类型当中迁移page */
		if (!page)
			page = __rmqueue_fallback(zone, order, migratetype);
	}

	trace_mm_page_alloc_zone_locked(page, order, migratetype);
	return page;
}

/*
 * Obtain a specified number of elements from the buddy allocator, all under
 * a single hold of the lock, for efficiency.  Add them to the supplied list.
 * Returns the number of new pages which were placed at *list.
 */
/*
 * 从通常的伙伴系统移除页，然后添加到缓存
 循环了batch次分配了order=0的page逐个添加到高速缓存当中
 如果需要分配的order不为0的话
 还是会直接从buddy-system当中分配不依靠高速缓存.
 高速缓存主要是缓存单页的分配
*/
/**
 * 从伙伴系统中批量取出多个页面。
 * 用于从伙伴系统中批量取出页面放到每CPU页面缓存中
 */
static int rmqueue_bulk(struct zone *zone, unsigned int order,
			unsigned long count, struct list_head *list,
			int migratetype, bool cold)
{
	int i, alloced = 0;
    /* 上层函数已经关了中断，这里需要操作管理区，获取管理区的自旋锁 */
	spin_lock(&zone->lock);
	/*
	 * 遍历per-CPU缓存中的所有页，检查是否有指定迁移类型的页可用。
	 * 如果前一次调用中，用不同迁移类型的页重新填充了缓存，就可能
	 * 找不到。如果无法找到适当的页，则向缓存添加一些符合当前要求的页。
	 */
	 /* 重复指定的次数，从伙伴系统中分配页面 */
	for (i = 0; i < count; ++i) {
	    /* 从伙伴系统中取出页面 */
		struct page *page = __rmqueue(zone, order, migratetype);
		/* 伙伴系统中没有空闲页了，退出 */
		if (unlikely(page == NULL))
			break;

		if (unlikely(check_pcp_refill(page)))
			continue;

		/*
		 * Split buddy pages returned by expand() are received here
		 * in physical page order. The page is added to the callers and
		 * list and the list head then moves forward. From the callers
		 * perspective, the linked list is ordered by page number in
		 * some conditions. This is useful for IO devices that can
		 * merge IO requests if the physical pages are ordered
		 * properly.
		 */
		 /* 根据调用者的要求，将页面放到每CPU缓存链表的头部或者尾部 */
		if (likely(!cold))
			list_add(&page->lru, list);
		else
			list_add_tail(&page->lru, list);
		list = &page->lru;
		alloced++;
		if (is_migrate_cma(get_pcppage_migratetype(page)))
			__mod_zone_page_state(zone, NR_FREE_CMA_PAGES,
					      -(1 << order));
	}

	/*
	 * i pages were removed from the buddy list even if some leak due
	 * to check_pcp_refill failing so adjust NR_FREE_PAGES based
	 * on i. Do not confuse with 'alloced' which is the number of
	 * pages added to the pcp list.
	 */
	 /* 递减管理区的空闲页面计数。 */
	__mod_zone_page_state(zone, NR_FREE_PAGES, -(i << order));
	/* 释放管理区的自旋锁 */
	spin_unlock(&zone->lock);
	return alloced;
}

#ifdef CONFIG_NUMA
/*
 * Called from the vmstat counter updater to drain pagesets of this
 * currently executing processor on remote nodes after they have
 * expired.
 *
 * Note that this function must be called with the thread pinned to
 * a single processor.
 */
void drain_zone_pages(struct zone *zone, struct per_cpu_pages *pcp)
{
	unsigned long flags;
	int to_drain, batch;

	local_irq_save(flags);
	batch = READ_ONCE(pcp->batch);
	to_drain = min(pcp->count, batch);
	if (to_drain > 0) {
		free_pcppages_bulk(zone, to_drain, pcp);
		pcp->count -= to_drain;
	}
	local_irq_restore(flags);
}
#endif

/*
 * Drain pcplists of the indicated processor and zone.
 *
 * The processor must either be the current processor and the
 * thread pinned to the current processor or a processor that
 * is not online.
 */
static void drain_pages_zone(unsigned int cpu, struct zone *zone)
{
	unsigned long flags;
	struct per_cpu_pageset *pset;
	struct per_cpu_pages *pcp;

	local_irq_save(flags);
	pset = per_cpu_ptr(zone->pageset, cpu);

	pcp = &pset->pcp;
	if (pcp->count) {
		free_pcppages_bulk(zone, pcp->count, pcp);
		pcp->count = 0;
	}
	local_irq_restore(flags);
}

/*
 * Drain pcplists of all zones on the indicated processor.
 *
 * The processor must either be the current processor and the
 * thread pinned to the current processor or a processor that
 * is not online.
 */
static void drain_pages(unsigned int cpu)
{
	struct zone *zone;

	for_each_populated_zone(zone) {
		drain_pages_zone(cpu, zone);
	}
}

/*
 * Spill all of this CPU's per-cpu pages back into the buddy allocator.
 *
 * The CPU has to be pinned. When zone parameter is non-NULL, spill just
 * the single zone's pages.
 */
void drain_local_pages(struct zone *zone)
{
	int cpu = smp_processor_id();

	if (zone)
		drain_pages_zone(cpu, zone);
	else
		drain_pages(cpu);
}

/*
 * Spill all the per-cpu pages from all CPUs back into the buddy allocator.
 *
 * When zone parameter is non-NULL, spill just the single zone's pages.
 *
 * Note that this code is protected against sending an IPI to an offline
 * CPU but does not guarantee sending an IPI to newly hotplugged CPUs:
 * on_each_cpu_mask() blocks hotplug and won't talk to offlined CPUs but
 * nothing keeps CPUs from showing up after we populated the cpumask and
 * before the call to on_each_cpu_mask().
 */
void drain_all_pages(struct zone *zone)
{
	int cpu;

	/*
	 * Allocate in the BSS so we wont require allocation in
	 * direct reclaim path for CONFIG_CPUMASK_OFFSTACK=y
	 */
	static cpumask_t cpus_with_pcps;

	/*
	 * We don't care about racing with CPU hotplug event
	 * as offline notification will cause the notified
	 * cpu to drain that CPU pcps and on_each_cpu_mask
	 * disables preemption as part of its processing
	 */
	for_each_online_cpu(cpu) {
		struct per_cpu_pageset *pcp;
		struct zone *z;
		bool has_pcps = false;

		if (zone) {
			pcp = per_cpu_ptr(zone->pageset, cpu);
			if (pcp->pcp.count)
				has_pcps = true;
		} else {
			for_each_populated_zone(z) {
				pcp = per_cpu_ptr(z->pageset, cpu);
				if (pcp->pcp.count) {
					has_pcps = true;
					break;
				}
			}
		}

		if (has_pcps)
			cpumask_set_cpu(cpu, &cpus_with_pcps);
		else
			cpumask_clear_cpu(cpu, &cpus_with_pcps);
	}
	on_each_cpu_mask(&cpus_with_pcps, (smp_call_func_t) drain_local_pages,
								zone, 1);
}

#ifdef CONFIG_HIBERNATION

void mark_free_pages(struct zone *zone)
{
	unsigned long pfn, max_zone_pfn;
	unsigned long flags;
	unsigned int order, t;
	struct page *page;

	if (zone_is_empty(zone))
		return;

	spin_lock_irqsave(&zone->lock, flags);

	max_zone_pfn = zone_end_pfn(zone);
	for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++)
		if (pfn_valid(pfn)) {
			page = pfn_to_page(pfn);

			if (page_zone(page) != zone)
				continue;

			if (!swsusp_page_is_forbidden(page))
				swsusp_unset_page_free(page);
		}

	for_each_migratetype_order(order, t) {
		list_for_each_entry(page,
				&zone->free_area[order].free_list[t], lru) {
			unsigned long i;

			pfn = page_to_pfn(page);
			for (i = 0; i < (1UL << order); i++)
				swsusp_set_page_free(pfn_to_page(pfn + i));
		}
	}
	spin_unlock_irqrestore(&zone->lock, flags);
}
#endif /* CONFIG_PM */

/*
 * Free a 0-order page
 * cold == true ? free a cold page : free a hot page
 */
/**
 * 将一个单页释放到冷热缓存池中
 */
void free_hot_cold_page(struct page *page, bool cold)
{
    /* 获取页的内存域 */
	struct zone *zone = page_zone(page);
	struct per_cpu_pages *pcp;
	unsigned long flags;
	unsigned long pfn = page_to_pfn(page);
	int migratetype;

    /**
              * free_pages_prepare主要是检查页面状态是否允许释放，避免错误的释放页面。
              * 并且处理一些调试相关的事务。
      */
	if (!free_pcp_prepare(page))
		return;

	migratetype = get_pfnblock_migratetype(page, pfn);
	set_pcppage_migratetype(page, migratetype);
/*
	置本地IRQ标志位
*/
	local_irq_save(flags);
	__count_vm_event(PGFREE);

	/*
	 * We only track unmovable, reclaimable and movable on pcp lists.
	 * Free ISOLATE pages back to the allocator because they are being
	 * offlined but treat RESERVE as movable pages so we can get those
	 * areas back if necessary. Otherwise, we may have to free
	 * excessively into the page allocator
	 */
/*
     * 只针对unmovable、reclaimable和movable的pcp列表中的页进行处理
     * 而ISOLATE的页作为movable的页返回或者释放给分配器
      该页面目前不受每CPU的PCP链表管理
*/
	if (migratetype >= MIGRATE_PCPTYPES) {
		if (unlikely(is_migrate_isolate(migratetype))) {
		/* 该页面是从邻近的CPU管理区中调配过来的，仍然放到MIGRATE_ISOLATE中，不转化为可迁移的页面 */
			free_one_page(zone, page, pfn, 0, migratetype);
			goto out;
		}
		/* 如果是从保留的链表中分配的页面，也将其放入可移动页中 */
		migratetype = MIGRATE_MOVABLE;
	}

	/* 获得per-CPU缓存中的页 */
	pcp = &this_cpu_ptr(zone->pageset)->pcp;
	if (!cold)/* 放入列表的首部，这样后续的分配过程可以利用CPU缓存中的热缓存 */
		list_add(&page->lru, &pcp->lists[migratetype]);
	else/* 如果是冷页，则CPU缓存中还没有页面数据，将页面放入页面缓存链表的尾部。 */
		list_add_tail(&page->lru, &pcp->lists[migratetype]);

	 /* 当前CPU页面缓存计数 */
	pcp->count++;
	/*
	 * 如果per-CPU缓存中页的数目超出了pcp->high，则将数量为
	 * pcp->batch的一批内存页还给伙伴系统。该策略称之为
	 * 惰性合并。如果单页直接返回为伙伴系统，那么会发生合并，
	 * 而为了满足后来的分配请求又需要进行拆分。因而惰性
	 * 合并策略阻止了大量可能白费时间的合并操作。
	 */
	if (pcp->count >= pcp->high) {
		unsigned long batch = READ_ONCE(pcp->batch);
        /* 将页面缓存中的页面释放一部分到伙伴系统中 */
		free_pcppages_bulk(zone, batch, pcp);
		/* 递减PCP中的缓存页面数量 */
		pcp->count -= batch;
	}

out:
    /* 恢复中断 */
	local_irq_restore(flags);
}

/*
 * Free a list of 0-order pages
 */
void free_hot_cold_page_list(struct list_head *list, bool cold)
{
	struct page *page, *next;

	list_for_each_entry_safe(page, next, list, lru) {
		trace_mm_page_free_batched(page, cold);
		free_hot_cold_page(page, cold);
	}
}

/*
 * split_page takes a non-compound higher-order page, and splits it into
 * n (1<<order) sub-pages: page[0..n]
 * Each sub-page must be freed individually.
 *
 * Note: this is probably too low level an operation for use in drivers.
 * Please consult with lkml before using this in your driver.
 */
void split_page(struct page *page, unsigned int order)
{
	int i;

	VM_BUG_ON_PAGE(PageCompound(page), page);
	VM_BUG_ON_PAGE(!page_count(page), page);

#ifdef CONFIG_KMEMCHECK
	/*
	 * Split shadow pages too, because free(page[0]) would
	 * otherwise free the whole shadow.
	 */
	if (kmemcheck_page_is_tracked(page))
		split_page(virt_to_page(page[0].shadow), order);
#endif

	for (i = 1; i < (1 << order); i++)
		set_page_refcounted(page + i);
	split_page_owner(page, order);
}
EXPORT_SYMBOL_GPL(split_page);

int __isolate_free_page(struct page *page, unsigned int order)
{
	unsigned long watermark;
	struct zone *zone;
	int mt;

	BUG_ON(!PageBuddy(page));

	zone = page_zone(page);
	mt = get_pageblock_migratetype(page);

	if (!is_migrate_isolate(mt)) {
		/*
		 * Obey watermarks as if the page was being allocated. We can
		 * emulate a high-order watermark check with a raised order-0
		 * watermark, because we already know our high-order page
		 * exists.
		 */
		watermark = min_wmark_pages(zone) + (1UL << order);
		if (!zone_watermark_ok(zone, 0, watermark, 0, ALLOC_CMA))
			return 0;

		__mod_zone_freepage_state(zone, -(1UL << order), mt);
	}

	/* Remove page from free list */
	list_del(&page->lru);
	zone->free_area[order].nr_free--;
	rmv_page_order(page);

	/*
	 * Set the pageblock if the isolated page is at least half of a
	 * pageblock
	 */
	if (order >= pageblock_order - 1) {
		struct page *endpage = page + (1 << order) - 1;
		for (; page < endpage; page += pageblock_nr_pages) {
			int mt = get_pageblock_migratetype(page);
			if (!is_migrate_isolate(mt) && !is_migrate_cma(mt)
				&& mt != MIGRATE_HIGHATOMIC)
				set_pageblock_migratetype(page,
							  MIGRATE_MOVABLE);
		}
	}


	return 1UL << order;
}

/*
 * Update NUMA hit/miss statistics
 *
 * Must be called with interrupts disabled.
 */
static inline void zone_statistics(struct zone *preferred_zone, struct zone *z)
{
#ifdef CONFIG_NUMA
	enum zone_stat_item local_stat = NUMA_LOCAL;

	if (z->node != numa_node_id())
		local_stat = NUMA_OTHER;

	if (z->node == preferred_zone->node)
		__inc_zone_state(z, NUMA_HIT);
	else {
		__inc_zone_state(z, NUMA_MISS);
		__inc_zone_state(preferred_zone, NUMA_FOREIGN);
	}
	__inc_zone_state(z, local_stat);
#endif
}

/*
 * Allocate a page from the given zone. Use pcplists for order-0 allocations.
 */
/*
 构建对buddy分配器裹了一层高速缓存
*/
/**
 * 从伙伴系统的空闲链表中取出空闲页
 */
static inline
struct page *buffered_rmqueue(struct zone *preferred_zone,
			struct zone *zone, unsigned int order,
			gfp_t gfp_flags, unsigned int alloc_flags,
			int migratetype)
{
	unsigned long flags;
	struct page *page;
    /*
	只分配1页(order=0)的情况下,会判断是不是”冷”页
	冷页的区别是会放到高速缓存的末尾,
	相反热页会放到高速缓存的头部
	然后尝试获取高速缓存的page
    */
	bool cold = ((gfp_flags & __GFP_COLD) != 0);

	/*
	 * 如果只分配一页，内核会进行优化，即分配阶为0的情形。该页不是从
	 * 伙伴系统直接取得，而是取自per-CPU的页缓存。
	 */
	/* 只分配一页，也许缓存中可以获得页面 */
	if (likely(order == 0)) {
		struct per_cpu_pages *pcp;
		struct list_head *list;

		/* 判断缓存前，需要禁止中断。因为可能在中断中释放CPU缓存页面 */
        /* 这里需要关中断，因为内存回收过程可能发送核间中断，强制每个核从每CPU缓存中释放页面。而且中断处理函数也会分配单页。 */
		local_irq_save(flags);
		do {
		    /* 取得本CPU的页面缓存对象 */
			pcp = &this_cpu_ptr(zone->pageset)->pcp;
			/* 根据用户指定的迁移类型，从指定的迁移缓存链表中分配 */
			list = &pcp->lists[migratetype];
		/*
		 * 如果缓存为空，内核可借机检查缓存填充水平
		 */
		 /* 缓存为空，需要扩大缓存的大小 */
			if (list_empty(list)) {
/* 从伙伴系统中移除一定数量的页到缓存中，迁移类型记录到private字段中 */
/*
			如果高速缓存为空就调用rmqueue_bulk
			分配一段batch大小的页块到高速缓存中继续尝试分配
*/
    /* 从伙伴系统中摘除一批页面到缓存中，补充的页面个数由每CPU缓存的batch字段指定 */

				pcp->count += rmqueue_bulk(zone, 0,
						pcp->batch, list,
						migratetype, cold);
            /* 如果链表仍然为空，那么说明伙伴系统中页面也没有了，分配失败。 */
				if (unlikely(list_empty(list)))
					goto failed;
			}

		/*
		 * 如果设置了__GFP_COLD，那么必须从per-CPU缓存取得冷页。
		 */
			if (cold)
			/* 如果分配的页面不需要考虑硬件缓存(注意不是每CPU页面缓存)，则取出链表的最后一个节点返回给上层 */
				page = list_last_entry(list, struct page, lru);
			else
			/* 如果要考虑硬件缓存，则取出链表的第一个页面，这个页面是最近刚释放到每CPU缓存的，缓存热度更高 */
			/* 取第一个页，这里可以确定其迁移类型满足要求?不一定 */
				page = list_first_entry(list, struct page, lru);

        /* 将页面从每CPU缓存链表中取出，并将每CPU缓存计数减1 */
			list_del(&page->lru);
			pcp->count--;

		} while (check_new_pcp(page));
	} else {
		/*
		 * 如果需要分配多页，内核调用__rmqueue()会从内存域的伙伴列表
		 * 中选择适当的内存块。如有必要，该函数会自动分解大块内存，
		 * 将未用的部分放回列表中。切记，可能有这样的情况:内存域中有足够
		 * 空闲页满足分配请求，但页是不连续的。
		 * 在这种情况下，__rmqueue()失败并返回NULL指针。
		 */
		 /* 分配的是多个页面，不需要考虑每CPU页面缓存，直接从系统中分配 */
		WARN_ON_ONCE((gfp_flags & __GFP_NOFAIL) && (order > 1));

		/* 关中断，并获得管理区的锁 */
		spin_lock_irqsave(&zone->lock, flags);

		/* 从伙伴系统中分配页面，可能会分裂大的内存块 */
		do {
			page = NULL;
			if (alloc_flags & ALLOC_HARDER) {
				page = __rmqueue_smallest(zone, order, MIGRATE_HIGHATOMIC);
				if (page)
					trace_mm_page_alloc_zone_locked(page, order, migratetype);
			}
			if (!page)
			/* 调用__rmqueue从伙伴系统中分配页面 */
				page = __rmqueue(zone, order, migratetype);
		} while (page && check_new_pages(page, order));
		/* 这里仅仅打开自旋锁，待后面统计计数设置完毕后再开中断 */
		spin_unlock(&zone->lock);
		/* 没有连续页了，失败 */
		if (!page)
			goto failed;
		/* 已经分配了1 << order个页面，这里进行管理区空闲页面统计计数 */
		__mod_zone_freepage_state(zone, -(1 << order),
					  get_pcppage_migratetype(page));
	}

	/* 事件统计计数，调试用 */
	__count_zid_vm_events(PGALLOC, page_zonenum(page), 1 << order);
	zone_statistics(preferred_zone, zone);
	/* 恢复中断 */
	local_irq_restore(flags);

	VM_BUG_ON_PAGE(bad_range(zone, page), page);
	return page;

failed:
	local_irq_restore(flags);
	return NULL;
}

#ifdef CONFIG_FAIL_PAGE_ALLOC

static struct {
	struct fault_attr attr;

	bool ignore_gfp_highmem;
	bool ignore_gfp_reclaim;
	u32 min_order;
} fail_page_alloc = {
	.attr = FAULT_ATTR_INITIALIZER,
	.ignore_gfp_reclaim = true,
	.ignore_gfp_highmem = true,
	.min_order = 1,
};

static int __init setup_fail_page_alloc(char *str)
{
	return setup_fault_attr(&fail_page_alloc.attr, str);
}
__setup("fail_page_alloc=", setup_fail_page_alloc);

static bool should_fail_alloc_page(gfp_t gfp_mask, unsigned int order)
{
	if (order < fail_page_alloc.min_order)
		return false;
	if (gfp_mask & __GFP_NOFAIL)
		return false;
	if (fail_page_alloc.ignore_gfp_highmem && (gfp_mask & __GFP_HIGHMEM))
		return false;
	if (fail_page_alloc.ignore_gfp_reclaim &&
			(gfp_mask & __GFP_DIRECT_RECLAIM))
		return false;

	return should_fail(&fail_page_alloc.attr, 1 << order);
}

#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS

static int __init fail_page_alloc_debugfs(void)
{
	umode_t mode = S_IFREG | S_IRUSR | S_IWUSR;
	struct dentry *dir;

	dir = fault_create_debugfs_attr("fail_page_alloc", NULL,
					&fail_page_alloc.attr);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	if (!debugfs_create_bool("ignore-gfp-wait", mode, dir,
				&fail_page_alloc.ignore_gfp_reclaim))
		goto fail;
	if (!debugfs_create_bool("ignore-gfp-highmem", mode, dir,
				&fail_page_alloc.ignore_gfp_highmem))
		goto fail;
	if (!debugfs_create_u32("min-order", mode, dir,
				&fail_page_alloc.min_order))
		goto fail;

	return 0;
fail:
	debugfs_remove_recursive(dir);

	return -ENOMEM;
}

late_initcall(fail_page_alloc_debugfs);

#endif /* CONFIG_FAULT_INJECTION_DEBUG_FS */

#else /* CONFIG_FAIL_PAGE_ALLOC */

static inline bool should_fail_alloc_page(gfp_t gfp_mask, unsigned int order)
{
	return false;
}

#endif /* CONFIG_FAIL_PAGE_ALLOC */

/*
 * Return true if free base pages are above 'mark'. For high-order checks it
 * will return true of the order-0 watermark is reached and there is at least
 * one free page of a suitable size. Checking now avoids taking the zone lock
 * to check in the allocation paths if no pages are free.
 */
/*
 * 分配内存时设置的标志在zone_watermark_ok()函数中
 * 检查，该函数根据设置的标志判断是否能从给定的内存域分配内存
 * 判断内存域水线是否满足分配要求。也就是空闲内存数量是否超过水线。
 */
bool __zone_watermark_ok(struct zone *z, unsigned int order, unsigned long mark,
			 int classzone_idx, unsigned int alloc_flags,
			 long free_pages)
{
	long min = mark;
	int o;
	const bool alloc_harder = (alloc_flags & ALLOC_HARDER);

	/* free_pages may go negative - that's OK */
	free_pages -= (1 << order) - 1;

	/*
	 * 解释ALLOC_HIGH和ALLOC_HARDER标志。将最小值
	 * 标记降到当前值的一半或四分之一，使得分配过程努力或更加努力
	 */
	if (alloc_flags & ALLOC_HIGH)
		min -= min / 2;

	/*
	 * If the caller does not have rights to ALLOC_HARDER then subtract
	 * the high-atomic reserves. This will over-estimate the size of the
	 * atomic reserve but it avoids a search.
	 */
	if (likely(!alloc_harder))
		free_pages -= z->nr_reserved_highatomic;
	else
		min -= min / 4;

#ifdef CONFIG_CMA
	/* If allocation can't use CMA areas don't use free CMA pages */
	if (!(alloc_flags & ALLOC_CMA))
		free_pages -= zone_page_state(z, NR_FREE_CMA_PAGES);
#endif

	/*
	 * Check watermarks for an order-0 allocation request. If these
	 * are not met, then a high-order request also cannot go ahead
	 * even if a suitable page happened to be free.
	 */
	/*
	 * 检查空闲页的数目是否小于最小值与lowmem_reserve中指定的紧急分配值之和。
	 * 如果不小于，则代码遍历所有小于当前阶的分配阶，从free_pages减去当前分配阶的
	 * 所有空闲页(左移o位时必要的，因为nr_free记载的是当前分配阶的空闲页块
	 * 数目)。同时，每升高一阶，所需空闲页的最小值折半。如果内核遍历所有的
	 * 低端内存域之后，发现内存不足，则不能进行内存分配。
	 */
	if (free_pages <= min + z->lowmem_reserve[classzone_idx])
		return false;

	/* If this is an order-0 request then the watermark is fine */
	if (!order)
		return true;

	/* For a high-order request, check at least one suitable page is free */
	/* 需要去除低阶链表中的内存 */
	for (o = order; o < MAX_ORDER; o++) {
		struct free_area *area = &z->free_area[o];
		int mt;

		if (!area->nr_free)
			continue;

		if (alloc_harder)
			return true;

		for (mt = 0; mt < MIGRATE_PCPTYPES; mt++) {
			if (!list_empty(&area->free_list[mt]))
				return true;
		}

#ifdef CONFIG_CMA
		if ((alloc_flags & ALLOC_CMA) &&
		    !list_empty(&area->free_list[MIGRATE_CMA])) {
			return true;
		}
#endif
	}
	return false;
}

bool zone_watermark_ok(struct zone *z, unsigned int order, unsigned long mark,
		      int classzone_idx, unsigned int alloc_flags)
{
	return __zone_watermark_ok(z, order, mark, classzone_idx, alloc_flags,
					zone_page_state(z, NR_FREE_PAGES));
}

static inline bool zone_watermark_fast(struct zone *z, unsigned int order,
		unsigned long mark, int classzone_idx, unsigned int alloc_flags)
{
	long free_pages = zone_page_state(z, NR_FREE_PAGES);
	long cma_pages = 0;

#ifdef CONFIG_CMA
	/* If allocation can't use CMA areas don't use free CMA pages */
	if (!(alloc_flags & ALLOC_CMA))
		cma_pages = zone_page_state(z, NR_FREE_CMA_PAGES);
#endif

	/*
	 * Fast check for order-0 only. If this fails then the reserves
	 * need to be calculated. There is a corner case where the check
	 * passes but only the high-order atomic reserve are free. If
	 * the caller is !atomic then it'll uselessly search the free
	 * list. That corner case is then slower but it is harmless.
	 */
	if (!order && (free_pages - cma_pages) > mark + z->lowmem_reserve[classzone_idx])
		return true;

	return __zone_watermark_ok(z, order, mark, classzone_idx, alloc_flags,
					free_pages);
}

bool zone_watermark_ok_safe(struct zone *z, unsigned int order,
			unsigned long mark, int classzone_idx)
{
	long free_pages = zone_page_state(z, NR_FREE_PAGES);

	if (z->percpu_drift_mark && free_pages < z->percpu_drift_mark)
		free_pages = zone_page_state_snapshot(z, NR_FREE_PAGES);

	return __zone_watermark_ok(z, order, mark, classzone_idx, 0,
								free_pages);
}

#ifdef CONFIG_NUMA
static bool zone_allows_reclaim(struct zone *local_zone, struct zone *zone)
{
	return node_distance(zone_to_nid(local_zone), zone_to_nid(zone)) <
				RECLAIM_DISTANCE;
}
#else	/* CONFIG_NUMA */
static bool zone_allows_reclaim(struct zone *local_zone, struct zone *zone)
{
	return true;
}
#endif	/* CONFIG_NUMA */

/*
 * get_page_from_freelist goes through the zonelist trying to allocate
 * a page.
 */
/*
 * get_page_from_freelist()通过标志集和分配阶来判断是否能进行分配。
   如果可以，则发起实际的分配操作。
 */
static struct page *
get_page_from_freelist(gfp_t gfp_mask, unsigned int order, int alloc_flags,
						const struct alloc_context *ac)
{
	struct zoneref *z = ac->preferred_zoneref;
	struct zone *zone;
	struct pglist_data *last_pgdat_dirty_limit = NULL;

	/*
	 * Scan zonelist, looking for a zone with enough free.
	 * See also __cpuset_node_allowed() comment in kernel/cpuset.c.
	 */
/*
     在允许的节点中，遍历满足要求的管理区选择合适的zone
	 考虑是否满足分配的wartermark, 是否需要均衡到别的zone当中
*/
/*
      从认定的管理区开始遍历，直到找到一个拥有足够空间的管理区，
      例如，如果high_zoneidx对应的ZONE_HIGHMEM，则遍历顺序为HIGHMEM-->NORMAL-->DMA，
      如果high_zoneidx对应ZONE_NORMAL，则遍历顺序为NORMAL-->DMA
*/
	for_next_zone_zonelist_nodemask(zone, z, ac->zonelist, ac->high_zoneidx,
								ac->nodemask) {
		struct page *page;
		unsigned long mark;

        /*
           检查给定的内存域是否属于该进程允许运行的CPU
        */
		if (cpusets_enabled() &&
			(alloc_flags & ALLOC_CPUSET) &&
			!__cpuset_zone_allowed(zone, gfp_mask))
				continue;
		/*
		 * When allocating a page cache page for writing, we
		 * want to get it from a node that is within its dirty
		 * limit, such that no single node holds more than its
		 * proportional share of globally allowed dirty pages.
		 * The dirty limits take into account the node's
		 * lowmem reserves and high watermark so that kswapd
		 * should be able to balance it without having to
		 * write pages from its LRU list.
		 *
		 * XXX: For now, allow allocations to potentially
		 * exceed the per-node dirty limit in the slowpath
		 * (spread_dirty_pages unset) before going into reclaim,
		 * which is important when on a NUMA setup the allowed
		 * nodes are together not big enough to reach the
		 * global limit.  The proper fix for these situations
		 * will require awareness of nodes in the
		 * dirty-throttling and the flusher threads.
		 */
		if (ac->spread_dirty_pages) {
			if (last_pgdat_dirty_limit == zone->zone_pgdat)
				continue;

			if (!node_dirty_ok(zone->zone_pgdat)) {
				last_pgdat_dirty_limit = zone->zone_pgdat;
				continue;
			}
		}
        /* 根据分配标志，确定使用哪一个水线 */
		mark = zone->watermark[alloc_flags & ALLOC_WMARK_MASK];
		/*
		 * 检查是否有足够的空闲页. 检查水线，空闲内存不足了
		 */
		if (!zone_watermark_fast(zone, order, mark,
				       ac_classzone_idx(ac), alloc_flags)) {
			int ret;

			/* Checked here to keep the fast path fast */
			BUILD_BUG_ON(ALLOC_NO_WATERMARKS < NR_WMARK);
			if (alloc_flags & ALLOC_NO_WATERMARKS)
				goto try_this_zone;

			if (node_reclaim_mode == 0 ||
			    !zone_allows_reclaim(ac->preferred_zoneref->zone, zone))
				continue;

            /* 运行到此，说明该管理区中内存不足，需要对该管理区进行回收 */
			ret = node_reclaim(zone->zone_pgdat, gfp_mask, order);
			switch (ret) {
			/* 当前管理区还没有进行回收 */
			case NODE_RECLAIM_NOSCAN:
				/* did not scan */
				continue;
			/* 进行了回收，但是没有可回收的内存 */
			case NODE_RECLAIM_FULL:
				/* scanned but unreclaimable */
				continue;
			/* 回收部分内存 */
			default:
				/* did we reclaim enough */
				/* 回收的内存较少，仍然不满足分配要求 */
				if (zone_watermark_ok(zone, order, mark,
						ac_classzone_idx(ac), alloc_flags))
					goto try_this_zone;

				continue;
			}
		}
       /**
         * 当前管理区中有足够的可用内存，
         试图在此管理区中分配内存
         */

try_this_zone:
		/*
		 * 如果内存域适用于当前的分配请求，那么
		 * 伙伴系统的分配函数buffered_rmqueue()试图从中分配所需数目的页
		 */
		page = buffered_rmqueue(ac->preferred_zoneref->zone, zone, order,
				gfp_mask, alloc_flags, ac->migratetype);
		if (page) {
			prep_new_page(page, order, gfp_mask, alloc_flags);

			/*
			 * If this is a high-order atomic allocation then check
			 * if the pageblock should be reserved for the future
			 */
			if (unlikely(order && (alloc_flags & ALLOC_HARDER)))
				reserve_highatomic_pageblock(page, zone, order);

			return page;
		}
		/* 运行到这里，说明内存域中没有可分配的内存了，从下一个内存域中分配 */
	}

	return NULL;
}

/*
 * Large machines with many possible nodes should not always dump per-node
 * meminfo in irq context.
 */
static inline bool should_suppress_show_mem(void)
{
	bool ret = false;

#if NODES_SHIFT > 8
	ret = in_interrupt();
#endif
	return ret;
}

static void warn_alloc_show_mem(gfp_t gfp_mask, nodemask_t *nodemask)
{
	unsigned int filter = SHOW_MEM_FILTER_NODES;
	static DEFINE_RATELIMIT_STATE(show_mem_rs, HZ, 1);

	if (should_suppress_show_mem() || !__ratelimit(&show_mem_rs))
		return;

	/*
	 * This documents exceptions given to allocations in certain
	 * contexts that are allowed to allocate outside current's set
	 * of allowed nodes.
	 */
	if (!(gfp_mask & __GFP_NOMEMALLOC))
		if (test_thread_flag(TIF_MEMDIE) ||
		    (current->flags & (PF_MEMALLOC | PF_EXITING)))
			filter &= ~SHOW_MEM_FILTER_NODES;
	if (in_interrupt() || !(gfp_mask & __GFP_DIRECT_RECLAIM))
		filter &= ~SHOW_MEM_FILTER_NODES;

	show_mem(filter, nodemask);
}

void warn_alloc(gfp_t gfp_mask, nodemask_t *nodemask, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	static DEFINE_RATELIMIT_STATE(nopage_rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);

	if ((gfp_mask & __GFP_NOWARN) || !__ratelimit(&nopage_rs) ||
	    debug_guardpage_minorder() > 0)
		return;

	pr_warn("%s: ", current->comm);

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_cont("%pV", &vaf);
	va_end(args);

	pr_cont(", mode:%#x(%pGg), nodemask=", gfp_mask, &gfp_mask);
	if (nodemask)
		pr_cont("%*pbl\n", nodemask_pr_args(nodemask));
	else
		pr_cont("(null)\n");

	cpuset_print_current_mems_allowed();

	dump_stack();
	warn_alloc_show_mem(gfp_mask, nodemask);
}

static inline struct page *
__alloc_pages_cpuset_fallback(gfp_t gfp_mask, unsigned int order,
			      unsigned int alloc_flags,
			      const struct alloc_context *ac)
{
	struct page *page;

	page = get_page_from_freelist(gfp_mask, order,
			alloc_flags|ALLOC_CPUSET, ac);
	/*
	 * fallback to ignore cpuset restriction if our nodes
	 * are depleted
	 */
	if (!page)
		page = get_page_from_freelist(gfp_mask, order,
				alloc_flags, ac);

	return page;
}

static inline struct page *
__alloc_pages_may_oom(gfp_t gfp_mask, unsigned int order,
	const struct alloc_context *ac, unsigned long *did_some_progress)
{
	struct oom_control oc = {
		.zonelist = ac->zonelist,
		.nodemask = ac->nodemask,
		.memcg = NULL,
		.gfp_mask = gfp_mask,
		.order = order,
	};
	struct page *page;

	*did_some_progress = 0;

	/*
	 * Acquire the oom lock.  If that fails, somebody else is
	 * making progress for us.
	 */
	if (!mutex_trylock(&oom_lock)) {
		*did_some_progress = 1;
		schedule_timeout_uninterruptible(1);
		return NULL;
	}

	/*
	 * Go through the zonelist yet one more time, keep very high watermark
	 * here, this is only to catch a parallel oom killing, we must fail if
	 * we're still under heavy pressure.
	 */
	page = get_page_from_freelist(gfp_mask | __GFP_HARDWALL, order,
					ALLOC_WMARK_HIGH|ALLOC_CPUSET, ac);
	if (page)
		goto out;

	/* Coredumps can quickly deplete all memory reserves */
	if (current->flags & PF_DUMPCORE)
		goto out;
	/* The OOM killer will not help higher order allocs */
	if (order > PAGE_ALLOC_COSTLY_ORDER)
		goto out;
	/* The OOM killer does not needlessly kill tasks for lowmem */
	if (ac->high_zoneidx < ZONE_NORMAL)
		goto out;
	if (pm_suspended_storage())
		goto out;
	/*
	 * XXX: GFP_NOFS allocations should rather fail than rely on
	 * other request to make a forward progress.
	 * We are in an unfortunate situation where out_of_memory cannot
	 * do much for this context but let's try it to at least get
	 * access to memory reserved if the current task is killed (see
	 * out_of_memory). Once filesystems are ready to handle allocation
	 * failures more gracefully we should just bail out here.
	 */

	/* The OOM killer may not free memory on a specific node */
	if (gfp_mask & __GFP_THISNODE)
		goto out;

	/* Exhausted what can be done so it's blamo time */
	if (out_of_memory(&oc) || WARN_ON_ONCE(gfp_mask & __GFP_NOFAIL)) {
		*did_some_progress = 1;

		/*
		 * Help non-failing allocations by giving them access to memory
		 * reserves
		 */
		if (gfp_mask & __GFP_NOFAIL)
			page = __alloc_pages_cpuset_fallback(gfp_mask, order,
					ALLOC_NO_WATERMARKS, ac);
	}
out:
	mutex_unlock(&oom_lock);
	return page;
}

/*
 * Maximum number of compaction retries wit a progress before OOM
 * killer is consider as the only way to move forward.
 */
#define MAX_COMPACT_RETRIES 16

#ifdef CONFIG_COMPACTION
/* Try memory compaction for high-order allocations before reclaim */
static struct page *
__alloc_pages_direct_compact(gfp_t gfp_mask, unsigned int order,
		unsigned int alloc_flags, const struct alloc_context *ac,
		enum compact_priority prio, enum compact_result *compact_result)
{
	struct page *page;

	if (!order)
		return NULL;

	current->flags |= PF_MEMALLOC;
	*compact_result = try_to_compact_pages(gfp_mask, order, alloc_flags, ac,
									prio);
	current->flags &= ~PF_MEMALLOC;

	if (*compact_result <= COMPACT_INACTIVE)
		return NULL;

	/*
	 * At least in one zone compaction wasn't deferred or skipped, so let's
	 * count a compaction stall
	 */
	count_vm_event(COMPACTSTALL);

	page = get_page_from_freelist(gfp_mask, order, alloc_flags, ac);

	if (page) {
		struct zone *zone = page_zone(page);

		zone->compact_blockskip_flush = false;
		compaction_defer_reset(zone, order, true);
		count_vm_event(COMPACTSUCCESS);
		return page;
	}

	/*
	 * It's bad if compaction run occurs and fails. The most likely reason
	 * is that pages exist, but not enough to satisfy watermarks.
	 */
	count_vm_event(COMPACTFAIL);

	cond_resched();

	return NULL;
}

static inline bool
should_compact_retry(struct alloc_context *ac, int order, int alloc_flags,
		     enum compact_result compact_result,
		     enum compact_priority *compact_priority,
		     int *compaction_retries)
{
	int max_retries = MAX_COMPACT_RETRIES;
	int min_priority;
	bool ret = false;
	int retries = *compaction_retries;
	enum compact_priority priority = *compact_priority;

	if (!order)
		return false;

	if (compaction_made_progress(compact_result))
		(*compaction_retries)++;

	/*
	 * compaction considers all the zone as desperately out of memory
	 * so it doesn't really make much sense to retry except when the
	 * failure could be caused by insufficient priority
	 */
	if (compaction_failed(compact_result))
		goto check_priority;

	/*
	 * make sure the compaction wasn't deferred or didn't bail out early
	 * due to locks contention before we declare that we should give up.
	 * But do not retry if the given zonelist is not suitable for
	 * compaction.
	 */
	if (compaction_withdrawn(compact_result)) {
		ret = compaction_zonelist_suitable(ac, order, alloc_flags);
		goto out;
	}

	/*
	 * !costly requests are much more important than __GFP_REPEAT
	 * costly ones because they are de facto nofail and invoke OOM
	 * killer to move on while costly can fail and users are ready
	 * to cope with that. 1/4 retries is rather arbitrary but we
	 * would need much more detailed feedback from compaction to
	 * make a better decision.
	 */
	if (order > PAGE_ALLOC_COSTLY_ORDER)
		max_retries /= 4;
	if (*compaction_retries <= max_retries) {
		ret = true;
		goto out;
	}

	/*
	 * Make sure there are attempts at the highest priority if we exhausted
	 * all retries or failed at the lower priorities.
	 */
check_priority:
	min_priority = (order > PAGE_ALLOC_COSTLY_ORDER) ?
			MIN_COMPACT_COSTLY_PRIORITY : MIN_COMPACT_PRIORITY;

	if (*compact_priority > min_priority) {
		(*compact_priority)--;
		*compaction_retries = 0;
		ret = true;
	}
out:
	trace_compact_retry(order, priority, compact_result, retries, max_retries, ret);
	return ret;
}
#else
static inline struct page *
__alloc_pages_direct_compact(gfp_t gfp_mask, unsigned int order,
		unsigned int alloc_flags, const struct alloc_context *ac,
		enum compact_priority prio, enum compact_result *compact_result)
{
	*compact_result = COMPACT_SKIPPED;
	return NULL;
}

static inline bool
should_compact_retry(struct alloc_context *ac, unsigned int order, int alloc_flags,
		     enum compact_result compact_result,
		     enum compact_priority *compact_priority,
		     int *compaction_retries)
{
	struct zone *zone;
	struct zoneref *z;

	if (!order || order > PAGE_ALLOC_COSTLY_ORDER)
		return false;

	/*
	 * There are setups with compaction disabled which would prefer to loop
	 * inside the allocator rather than hit the oom killer prematurely.
	 * Let's give them a good hope and keep retrying while the order-0
	 * watermarks are OK.
	 */
	for_each_zone_zonelist_nodemask(zone, z, ac->zonelist, ac->high_zoneidx,
					ac->nodemask) {
		if (zone_watermark_ok(zone, 0, min_wmark_pages(zone),
					ac_classzone_idx(ac), alloc_flags))
			return true;
	}
	return false;
}
#endif /* CONFIG_COMPACTION */

/* Perform direct synchronous page reclaim */
static int
__perform_reclaim(gfp_t gfp_mask, unsigned int order,
					const struct alloc_context *ac)
{
	struct reclaim_state reclaim_state;
	int progress;

	cond_resched();

	/* We now go into synchronous reclaim */
	cpuset_memory_pressure_bump();
	/*
	 * 设置PF_MEMALLOC标志，用于向其余的内核
	 * 代码表明所有后续的内存分配都需要这样的搜索
	 */
	current->flags |= PF_MEMALLOC;
	lockdep_set_current_reclaim_state(gfp_mask);
	reclaim_state.reclaimed_slab = 0;
	current->reclaim_state = &reclaim_state;

	/*
	 * 查找当前不需要的页，以便换出。该调用被设置/清除PF_MEMALLOC标志
	 * 的代码间隔起来。try_to_free_pages()自身可能也需要分配新的内存。
	 * 由于为获得新内存还需要额外分配一点内存(相当矛盾的情形)，
	 * 该进程当然应该在内存管理方面享有更高优先级，上述标志的设置
	 * 即达到了这一目的。在设置了PF_MEMALLOC标志时，内存分配非常积极。
	 * 此外，设置PF_MEMALLOC标志确保了try_to_free_pages()
	 * 不会递归调用，因为如果此前设置了
	 * try_to_free_pages，那么__alloc_pages()肯定已经返回。
	 * 参见__alloc_pages_slowpath()函数
	 */
	progress = try_to_free_pages(ac->zonelist, order, gfp_mask,
								ac->nodemask);

	current->reclaim_state = NULL;
	lockdep_clear_current_reclaim_state();
	current->flags &= ~PF_MEMALLOC;

	cond_resched();

	return progress;
}

/* The really slow allocator path where we enter direct reclaim */
static inline struct page *
__alloc_pages_direct_reclaim(gfp_t gfp_mask, unsigned int order,
		unsigned int alloc_flags, const struct alloc_context *ac,
		unsigned long *did_some_progress)
{
	struct page *page = NULL;
	bool drained = false;

	*did_some_progress = __perform_reclaim(gfp_mask, order, ac);
	if (unlikely(!(*did_some_progress)))
		return NULL;

retry:
	page = get_page_from_freelist(gfp_mask, order, alloc_flags, ac);

	/*
	 * If an allocation failed after direct reclaim, it could be because
	 * pages are pinned on the per-cpu lists or in high alloc reserves.
	 * Shrink them them and try again
	 */
	if (!page && !drained) {
		unreserve_highatomic_pageblock(ac, false);
		drain_all_pages(NULL);
		drained = true;
		goto retry;
	}

	return page;
}

static void wake_all_kswapds(unsigned int order, const struct alloc_context *ac)
{
	struct zoneref *z;
	struct zone *zone;
	pg_data_t *last_pgdat = NULL;

	for_each_zone_zonelist_nodemask(zone, z, ac->zonelist,
					ac->high_zoneidx, ac->nodemask) {
		if (last_pgdat != zone->zone_pgdat)
			wakeup_kswapd(zone, order, ac->high_zoneidx);
		last_pgdat = zone->zone_pgdat;
	}
}

static inline unsigned int
gfp_to_alloc_flags(gfp_t gfp_mask)
{
	unsigned int alloc_flags = ALLOC_WMARK_MIN | ALLOC_CPUSET;

	/* __GFP_HIGH is assumed to be the same as ALLOC_HIGH to save a branch. */
	BUILD_BUG_ON(__GFP_HIGH != (__force gfp_t) ALLOC_HIGH);

	/*
	 * The caller may dip into page reserves a bit more if the caller
	 * cannot run direct reclaim, or if the caller has realtime scheduling
	 * policy or is asking for __GFP_HIGH memory.  GFP_ATOMIC requests will
	 * set both ALLOC_HARDER (__GFP_ATOMIC) and ALLOC_HIGH (__GFP_HIGH).
	 */
	alloc_flags |= (__force int) (gfp_mask & __GFP_HIGH);

	if (gfp_mask & __GFP_ATOMIC) {
		/*
		 * Not worth trying to allocate harder for __GFP_NOMEMALLOC even
		 * if it can't schedule.
		 */
		if (!(gfp_mask & __GFP_NOMEMALLOC))
			alloc_flags |= ALLOC_HARDER;
		/*
		 * Ignore cpuset mems for GFP_ATOMIC rather than fail, see the
		 * comment for __cpuset_node_allowed().
		 */
		alloc_flags &= ~ALLOC_CPUSET;
	} else if (unlikely(rt_task(current)) && !in_interrupt())
		alloc_flags |= ALLOC_HARDER;

#ifdef CONFIG_CMA
	if (gfpflags_to_migratetype(gfp_mask) == MIGRATE_MOVABLE)
		alloc_flags |= ALLOC_CMA;
#endif
	return alloc_flags;
}

bool gfp_pfmemalloc_allowed(gfp_t gfp_mask)
{
	if (unlikely(gfp_mask & __GFP_NOMEMALLOC))
		return false;

	if (gfp_mask & __GFP_MEMALLOC)
		return true;
	if (in_serving_softirq() && (current->flags & PF_MEMALLOC))
		return true;
	if (!in_interrupt() &&
			((current->flags & PF_MEMALLOC) ||
			 unlikely(test_thread_flag(TIF_MEMDIE))))
		return true;

	return false;
}

/*
 * Maximum number of reclaim retries without any progress before OOM killer
 * is consider as the only way to move forward.
 */
#define MAX_RECLAIM_RETRIES 16

/*
 * Checks whether it makes sense to retry the reclaim to make a forward progress
 * for the given allocation request.
 * The reclaim feedback represented by did_some_progress (any progress during
 * the last reclaim round) and no_progress_loops (number of reclaim rounds without
 * any progress in a row) is considered as well as the reclaimable pages on the
 * applicable zone list (with a backoff mechanism which is a function of
 * no_progress_loops).
 *
 * Returns true if a retry is viable or false to enter the oom path.
 */
static inline bool
should_reclaim_retry(gfp_t gfp_mask, unsigned order,
		     struct alloc_context *ac, int alloc_flags,
		     bool did_some_progress, int *no_progress_loops)
{
	struct zone *zone;
	struct zoneref *z;

	/*
	 * Costly allocations might have made a progress but this doesn't mean
	 * their order will become available due to high fragmentation so
	 * always increment the no progress counter for them
	 */
	if (did_some_progress && order <= PAGE_ALLOC_COSTLY_ORDER)
		*no_progress_loops = 0;
	else
		(*no_progress_loops)++;

	/*
	 * Make sure we converge to OOM if we cannot make any progress
	 * several times in the row.
	 */
	if (*no_progress_loops > MAX_RECLAIM_RETRIES) {
		/* Before OOM, exhaust highatomic_reserve */
		return unreserve_highatomic_pageblock(ac, true);
	}

	/*
	 * Keep reclaiming pages while there is a chance this will lead
	 * somewhere.  If none of the target zones can satisfy our allocation
	 * request even if all reclaimable pages are considered then we are
	 * screwed and have to go OOM.
	 */
	for_each_zone_zonelist_nodemask(zone, z, ac->zonelist, ac->high_zoneidx,
					ac->nodemask) {
		unsigned long available;
		unsigned long reclaimable;
		unsigned long min_wmark = min_wmark_pages(zone);
		bool wmark;

		available = reclaimable = zone_reclaimable_pages(zone);
		available -= DIV_ROUND_UP((*no_progress_loops) * available,
					  MAX_RECLAIM_RETRIES);
		available += zone_page_state_snapshot(zone, NR_FREE_PAGES);

		/*
		 * Would the allocation succeed if we reclaimed the whole
		 * available?
		 */
		wmark = __zone_watermark_ok(zone, order, min_wmark,
				ac_classzone_idx(ac), alloc_flags, available);
		trace_reclaim_retry_zone(z, order, reclaimable,
				available, min_wmark, *no_progress_loops, wmark);
		if (wmark) {
			/*
			 * If we didn't make any progress and have a lot of
			 * dirty + writeback pages then we should wait for
			 * an IO to complete to slow down the reclaim and
			 * prevent from pre mature OOM
			 */
			if (!did_some_progress) {
				unsigned long write_pending;

				write_pending = zone_page_state_snapshot(zone,
							NR_ZONE_WRITE_PENDING);

				if (2 * write_pending > reclaimable) {
					congestion_wait(BLK_RW_ASYNC, HZ/10);
					return true;
				}
			}

			/*
			 * Memory allocation/reclaim might be called from a WQ
			 * context and the current implementation of the WQ
			 * concurrency control doesn't recognize that
			 * a particular WQ is congested if the worker thread is
			 * looping without ever sleeping. Therefore we have to
			 * do a short sleep here rather than calling
			 * cond_resched().
			 */
			if (current->flags & PF_WQ_WORKER)
				schedule_timeout_uninterruptible(1);
			else
				cond_resched();

			return true;
		}
	}

	return false;
}

static inline struct page *
__alloc_pages_slowpath(gfp_t gfp_mask, unsigned int order,
						struct alloc_context *ac)
{
	bool can_direct_reclaim = gfp_mask & __GFP_DIRECT_RECLAIM;
	struct page *page = NULL;
	unsigned int alloc_flags;
	unsigned long did_some_progress;
	enum compact_priority compact_priority;
	enum compact_result compact_result;
	int compaction_retries;
	int no_progress_loops;
	unsigned long alloc_start = jiffies;
	unsigned int stall_timeout = 10 * HZ;
	unsigned int cpuset_mems_cookie;

	/*
	 * In the slowpath, we sanity check order to avoid ever trying to
	 * reclaim >= MAX_ORDER areas which will never succeed. Callers may
	 * be using allocators in order of preference for an area that is
	 * too large.
	 */
	if (order >= MAX_ORDER) {
		WARN_ON_ONCE(!(gfp_mask & __GFP_NOWARN));
		return NULL;
	}

	/*
	 * We also sanity check to catch abuse of atomic reserves being used by
	 * callers that are not in atomic context.
	 */
	if (WARN_ON_ONCE((gfp_mask & (__GFP_ATOMIC|__GFP_DIRECT_RECLAIM)) ==
				(__GFP_ATOMIC|__GFP_DIRECT_RECLAIM)))
		gfp_mask &= ~__GFP_ATOMIC;

retry_cpuset:
	compaction_retries = 0;
	no_progress_loops = 0;
	compact_priority = DEF_COMPACT_PRIORITY;
	cpuset_mems_cookie = read_mems_allowed_begin();

	/*
	 * The fast path uses conservative alloc_flags to succeed only until
	 * kswapd needs to be woken up, and to avoid the cost of setting up
	 * alloc_flags precisely. So we do that now.
	 */
	 /*
	 * 对分配标志进行调整，修改为一些在当前特定情况下更有可能分配成功的标志。同时，
	 * 将水印降低到最小值。对实时进程和指定了__GFP_WAIT标志因而不能睡眠的调用，会设置
	 * ALLOC_HARDER。然后用修改的标志集，再一次调用get_page_from_freelist，试图获得所需的页。
	 */
	alloc_flags = gfp_to_alloc_flags(gfp_mask);

	/*
	 * We need to recalculate the starting point for the zonelist iterator
	 * because we might have used different nodemask in the fast path, or
	 * there was a cpuset modification and we are retrying - otherwise we
	 * could end up iterating over non-eligible zones endlessly.
	 */
	ac->preferred_zoneref = first_zones_zonelist(ac->zonelist,
					ac->high_zoneidx, ac->nodemask);
	if (!ac->preferred_zoneref->zone)
		goto nopage;

/*
    唤醒kswapd进行内存回收
*/
	if (gfp_mask & __GFP_KSWAPD_RECLAIM)
		wake_all_kswapds(order, ac);

	/*
	 * The adjusted alloc_flags might result in immediate success, so try
	 * that first
	 */
	page = get_page_from_freelist(gfp_mask, order, alloc_flags, ac);
	if (page)
		goto got_pg;

	/*
	 * For costly allocations, try direct compaction first, as it's likely
	 * that we have enough base pages and don't need to reclaim. Don't try
	 * that for allocations that are allowed to ignore watermarks, as the
	 * ALLOC_NO_WATERMARKS attempt didn't yet happen.
	 */
	if (can_direct_reclaim && order > PAGE_ALLOC_COSTLY_ORDER &&
		!gfp_pfmemalloc_allowed(gfp_mask)) {
		page = __alloc_pages_direct_compact(gfp_mask, order,
						alloc_flags, ac,
						INIT_COMPACT_PRIORITY,
						&compact_result);
		if (page)
			goto got_pg;

		/*
		 * Checks for costly allocations with __GFP_NORETRY, which
		 * includes THP page fault allocations
		 */
		if (gfp_mask & __GFP_NORETRY) {
			/*
			 * If compaction is deferred for high-order allocations,
			 * it is because sync compaction recently failed. If
			 * this is the case and the caller requested a THP
			 * allocation, we do not want to heavily disrupt the
			 * system, so we fail the allocation instead of entering
			 * direct reclaim.
			 */
			if (compact_result == COMPACT_DEFERRED)
				goto nopage;

			/*
			 * Looks like reclaim/compaction is worth trying, but
			 * sync compaction could be very expensive, so keep
			 * using async compaction.
			 */
			compact_priority = INIT_COMPACT_PRIORITY;
		}
	}

retry:
	/* Ensure kswapd doesn't accidentally go to sleep as long as we loop */
	if (gfp_mask & __GFP_KSWAPD_RECLAIM)
		wake_all_kswapds(order, ac);

	if (gfp_pfmemalloc_allowed(gfp_mask))
		alloc_flags = ALLOC_NO_WATERMARKS;

	/*
	 * Reset the zonelist iterators if memory policies can be ignored.
	 * These allocations are high priority and system rather than user
	 * orientated.
	 */
	 /*
	 * 如果设置了ALLOC_NO_WATERMARKS标志，则再次调用get_page_from_freelist()
	 * 试图获得所需的页这次完全忽略水印，因为设置了ALLOC_NO_WATERMARKS
	 */
	if (!(alloc_flags & ALLOC_CPUSET) || (alloc_flags & ALLOC_NO_WATERMARKS)) {
		ac->zonelist = node_zonelist(numa_node_id(), gfp_mask);
		ac->preferred_zoneref = first_zones_zonelist(ac->zonelist,
					ac->high_zoneidx, ac->nodemask);
	}

	/* Attempt with potentially adjusted zonelist and alloc_flags */
	page = get_page_from_freelist(gfp_mask, order, alloc_flags, ac);
	if (page)
		goto got_pg;

	/* Caller is not willing to reclaim, we can't balance anything */
	if (!can_direct_reclaim)
		goto nopage;

	/* Make sure we know about allocations which stall for too long */
	if (time_after(jiffies, alloc_start + stall_timeout)) {
		warn_alloc(gfp_mask, ac->nodemask,
			"page allocation stalls for %ums, order:%u",
			jiffies_to_msecs(jiffies-alloc_start), order);
		stall_timeout += 10 * HZ;
	}

	/* Avoid recursion of direct reclaim */
	 /*
	 * 因为在分配内存的时候可能在获取可用内存的过程中也需要分配内存，
	 * 所以会设置该标志。如果在分配失败时，发现该标志已经设置，则表示
	 * 在获取内存过程中需要的内存也没有可用内存，所以这时应该返回失败，
	 * 避免递归调用。参见__alloc_pages_direct_reclaim()	  调用者本身就是内存回收进程，不能进入后面的内存回收处理流程，否则死锁
	 */
	if (current->flags & PF_MEMALLOC)
		goto nopage;

	/* Try direct reclaim and then allocating */
	    /**
          * 直接在内存分配上下文中进行内存回收操作。
          */
	page = __alloc_pages_direct_reclaim(gfp_mask, order, alloc_flags, ac,
							&did_some_progress);
    /* 庆幸，回收了一些内存后，满足了上层分配需求 */
	if (page)
		goto got_pg;

	/* Try direct compaction and then allocating */
	/**
          * 尝试压缩内存。这样可以将一些小的外碎片合并成大页面，
          这样也许能够满足调用者的内存分配要求。
          * 内存压缩是通过页面迁移实现的。
          * 第一次调用的时候，是非同步的。第二次调用则是同步方式。
      */
	page = __alloc_pages_direct_compact(gfp_mask, order, alloc_flags, ac,
					compact_priority, &compact_result);
    /* 庆幸，通过压缩内存，分配到了内存 */
	if (page)
		goto got_pg;

	/* Do not loop if specifically requested */
	if (gfp_mask & __GFP_NORETRY)
		goto nopage;

	/*
	 * Do not retry costly high order allocations unless they are
	 * __GFP_REPEAT
	 */
	if (order > PAGE_ALLOC_COSTLY_ORDER && !(gfp_mask & __GFP_REPEAT))
		goto nopage;

	if (should_reclaim_retry(gfp_mask, order, ac, alloc_flags,
				 did_some_progress > 0, &no_progress_loops))
		goto retry;

	/*
	 * It doesn't make any sense to retry for the compaction if the order-0
	 * reclaim is not able to make any progress because the current
	 * implementation of the compaction depends on the sufficient amount
	 * of free memory (see __compaction_suitable)
	 */
	if (did_some_progress > 0 &&
			should_compact_retry(ac, order, alloc_flags,
				compact_result, &compact_priority,
				&compaction_retries))
		goto retry;

	/*
	 * It's possible we raced with cpuset update so the OOM would be
	 * premature (see below the nopage: label for full explanation).
	 */
	if (read_mems_allowed_retry(cpuset_mems_cookie))
		goto retry_cpuset;

	/* Reclaim has failed us, start killing things */
		/*
	 * 如果内核可能执行影响VFS层的调用而又没有设置GFP_NORETRY，那么调用
	 * OOM killer。如果当前要分配大于PAGE_ALLOC_COSTLY_ORDER阶的内存区，
	 * 那么内核会饶恕所选择的进程，不会执行杀死进程的任务，而是承认失败并
	 * 跳转到nopage。
	 */
	page = __alloc_pages_may_oom(gfp_mask, order, ac, &did_some_progress);
	if (page)
		goto got_pg;

	/* Avoid allocations with no watermarks from looping endlessly */
	if (test_thread_flag(TIF_MEMDIE))
		goto nopage;

	/* Retry as long as the OOM killer is making progress */
	if (did_some_progress) {
		no_progress_loops = 0;
		goto retry;
	}

nopage:
	/*
	 * When updating a task's mems_allowed or mempolicy nodemask, it is
	 * possible to race with parallel threads in such a way that our
	 * allocation can fail while the mask is being updated. If we are about
	 * to fail, check if the cpuset changed during allocation and if so,
	 * retry.
	 */
	if (read_mems_allowed_retry(cpuset_mems_cookie))
		goto retry_cpuset;

	/*
	 * Make sure that __GFP_NOFAIL request doesn't leak out and make sure
	 * we always retry
	 */
	if (gfp_mask & __GFP_NOFAIL) {
		/*
		 * All existing users of the __GFP_NOFAIL are blockable, so warn
		 * of any new users that actually require GFP_NOWAIT
		 */
		if (WARN_ON_ONCE(!can_direct_reclaim))
			goto fail;

		/*
		 * PF_MEMALLOC request from this context is rather bizarre
		 * because we cannot reclaim anything and only can loop waiting
		 * for somebody to do a work for us
		 */
		WARN_ON_ONCE(current->flags & PF_MEMALLOC);

		/*
		 * non failing costly orders are a hard requirement which we
		 * are not prepared for much so let's warn about these users
		 * so that we can identify them and convert them to something
		 * else.
		 */
		WARN_ON_ONCE(order > PAGE_ALLOC_COSTLY_ORDER);

		/*
		 * Help non-failing allocations by giving them access to memory
		 * reserves but do not use ALLOC_NO_WATERMARKS because this
		 * could deplete whole memory reserves which would just make
		 * the situation worse
		 */
		page = __alloc_pages_cpuset_fallback(gfp_mask, order, ALLOC_HARDER, ac);
		if (page)
			goto got_pg;

		cond_resched();
		goto retry;
	}
fail:
	/* 内存分配失败了，打印内存分配失败的警告 */
	warn_alloc(gfp_mask, ac->nodemask,
			"page allocation failure: order:%u", order);
got_pg:
	return page;
}

/*
 * This is the 'heart' of the zoned buddy allocator.
 */
 /**
 * 页面分配的核心算法。
 */
struct page *
__alloc_pages_nodemask(gfp_t gfp_mask, unsigned int order,
			struct zonelist *zonelist, nodemask_t *nodemask)
{
	struct page *page;
	unsigned int alloc_flags = ALLOC_WMARK_LOW;
	gfp_t alloc_mask = gfp_mask; /* The gfp_t that was actually used for allocation */

	/**
          high_zoneidx  根据传入的标志，决定从哪个内存区开始分配内存。
          例如，如果没有指定高端内存标志，那么就从normal开始，
          当normal中没有可用内存时，再从DMA32、DMA中分配内存。

          migratetype 根据传入的标志，决定分配页面的迁移方式。
          页面迁移功能可以减少内存外碎片。
      */
	struct alloc_context ac = {
		.high_zoneidx = gfp_zone(gfp_mask),
		.zonelist = zonelist,
		.nodemask = nodemask,
		/*根据gfp_mask得到迁移类分配页的型*/
		.migratetype = gfpflags_to_migratetype(gfp_mask),
	};

/*
    检查cpusets的功能是否打开, 这个是一个cgroup的子模块,
    如果没有设置nodemask就会用cpusets配置的cpuset_current_mems_allowed来限制在哪个node上分配,
    这个也是在NUMA结构当中才会有用的
*/
	if (cpusets_enabled()) {
		alloc_mask |= __GFP_HARDWALL;
		alloc_flags |= ALLOC_CPUSET;
		if (!ac.nodemask)
			ac.nodemask = &cpuset_current_mems_allowed;
	}

    /**
      * 在系统运行的各个阶段，某些标志不能使用。
      * 例如，没有初始化文件系统时，自然不能使用GFP_FS标志。
      * 这里对传入的标志进行整理，以适用系统各个阶段的情况。
      */
	gfp_mask &= gfp_allowed_mask;

	lockdep_trace_alloc(gfp_mask);

    /**
     当前内存压力比较大需要直接回收内存,
     会循环睡眠同步等待页可用
     */
	might_sleep_if(gfp_mask & __GFP_DIRECT_RECLAIM);

    /*
    CONFIG_FAIL_PAGE_ALLOC调试配置选项才有用
     做一些预检查, 一些无法分配的条件会直接报错.
     */
	if (should_fail_alloc_page(gfp_mask, order))
		return NULL;

	/*
	 * Check the zones suitable for the gfp_mask contain at least one
	 * valid zone. It's possible to have an empty zonelist as a result
	 * of __GFP_THISNODE and a memoryless node
	 */
    /**
      * 如果没有任何一个可用管理区，则直接返回NULL。
      * 如果指定了GFP_THISNODE标志并且当前节点没有内存，则可能存在这种情况。
      * 这一般要求上层调用者在发现NULL返回值时，指定其他标志。
      */
	if (unlikely(!zonelist->_zonerefs->zone))
		return NULL;

	if (IS_ENABLED(CONFIG_CMA) && ac.migratetype == MIGRATE_MOVABLE)
		alloc_flags |= ALLOC_CMA;

	/* Dirty zone balancing only done in the fast path */
	ac.spread_dirty_pages = (gfp_mask & __GFP_WRITE);

	/*
	 * The preferred zone is used for statistics but crucially it is
	 * also used as the starting point for the zonelist iterator. It
	 * may get reset for allocations that ignore memory policies.
	 */
/*
     据传入的参数，确定优先在哪个管理区中分配内存

	 从zonelist这个表示分配page的zone的优先顺序链表里获取第一个zoneref,
	 zoneref是一个链表节点用于迭代之后的zone选项
*/
	ac.preferred_zoneref = first_zones_zonelist(ac.zonelist,
					ac.high_zoneidx, ac.nodemask);
	if (!ac.preferred_zoneref->zone) {
		page = NULL;
		/*
		 * This might be due to race with cpuset_current_mems_allowed
		 * update, so make sure we retry with original nodemask in the
		 * slow path.
		 */
		goto no_zone;
	}

	/* First allocation attempt */
	/*
	 * 在最简单的情形中，分配空闲内存区只涉及
	 * 调用一次get_page_from_freelist()，然后返回所需数目的页。

      * 快速分配路径，在这个分配路径中，
      指定了__GFP_HARDWALL和ALLOC_WMARK_LOW、ALLOC_CPUSET标志。
      * 这样，在分配时要考虑内存的CPU亲和性，
      并且尽量在较高的水线上分配，防止某些内存区被过早的击穿。
      */
	page = get_page_from_freelist(alloc_mask, order, alloc_flags, &ac);

    /* 快速路径无法获得内存，这样就需要降低水线标准，或者启动内存回收过程。 */

	if (likely(page))
		goto out;

no_zone:
	/*
	 * Runtime PM, block IO and its error handling path can deadlock
	 * because I/O on the device might not complete.
	 */
	alloc_mask = memalloc_noio_flags(gfp_mask);
	ac.spread_dirty_pages = false;

	/*
	 * Restore the original nodemask if it was potentially replaced with
	 * &cpuset_current_mems_allowed to optimize the fast-path attempt.
	 */
	if (unlikely(ac.nodemask != nodemask))
		ac.nodemask = nodemask;

    /*
    分配完页如果失败会调用__alloc_pages_slowpath
    唤起swapd进程进行页回收从而获取可用的页,
    再尝试调用get_page_from_free_list
    */
	page = __alloc_pages_slowpath(alloc_mask, order, &ac);

out:

	if (memcg_kmem_enabled() && (gfp_mask & __GFP_ACCOUNT) && page &&
	    unlikely(memcg_kmem_charge(page, gfp_mask, order) != 0)) {
		__free_pages(page, order);
		page = NULL;
	}

	if (kmemcheck_enabled && page)
		kmemcheck_pagealloc_alloc(page, order, gfp_mask);

	trace_mm_page_alloc(page, order, alloc_mask, ac.migratetype);

	return page;
}
EXPORT_SYMBOL(__alloc_pages_nodemask);

/*
 * Common helper functions.
 */
/*
 * 返回分配内存块的虚拟地址，而不是page实例
 */
unsigned long __get_free_pages(gfp_t gfp_mask, unsigned int order)
{
	struct page *page;

	/*
	 * __get_free_pages() returns a 32-bit address, which cannot represent
	 * a highmem page
	 */
	VM_BUG_ON((gfp_mask & __GFP_HIGHMEM) != 0);

	page = alloc_pages(gfp_mask, order);
	if (!page)
		return 0;
	/*page_address返回这些页面中起始页面的内核线性地址*/
	return (unsigned long) page_address(page);
}
EXPORT_SYMBOL(__get_free_pages);

/*
 * 分配一页并返回一个page实例，页对应的内存填充0
 */
unsigned long get_zeroed_page(gfp_t gfp_mask)
{
	return __get_free_pages(gfp_mask | __GFP_ZERO, 0);
}
EXPORT_SYMBOL(get_zeroed_page);

/**
 * 释放一组页面，其长度为2^order, 起始地址由页面结构决定
 */
void __free_pages(struct page *page, unsigned int order)
{

/*
    递减引用计数，如果为0表示没有人引用,才真正执行释放操作
*/
	if (put_page_testzero(page)) {
        /*
        如果是释放单页，那么就不必给交换给伙伴系统，
        而是置于per-CPU缓存中，对很可能出现在CPU高速缓存的页，
        则置放到热页的列表中,交回到高速缓存即可,
        如果高速缓存满了再交回给buddy system
        */
		if (order == 0)
			free_hot_cold_page(page, false);
/*
	不为0说明也没有缓存过 直接放回链表中
*/
		else
			__free_pages_ok(page, order);
	}
}

EXPORT_SYMBOL(__free_pages);

/*释放__get_free_pages分配的页面*/
void free_pages(unsigned long addr, unsigned int order)
{
	if (addr != 0) {
		VM_BUG_ON(!virt_addr_valid((void *)addr));
		__free_pages(virt_to_page((void *)addr), order);
	}
}

EXPORT_SYMBOL(free_pages);

/*
 * Page Fragment:
 *  An arbitrary-length arbitrary-offset area of memory which resides
 *  within a 0 or higher order page.  Multiple fragments within that page
 *  are individually refcounted, in the page's reference counter.
 *
 * The page_frag functions below provide a simple allocation framework for
 * page fragments.  This is used by the network stack and network device
 * drivers to provide a backing region of memory for use as either an
 * sk_buff->head, or to be used in the "frags" portion of skb_shared_info.
 */
static struct page *__page_frag_cache_refill(struct page_frag_cache *nc,
					     gfp_t gfp_mask)
{
	struct page *page = NULL;
	gfp_t gfp = gfp_mask;

#if (PAGE_SIZE < PAGE_FRAG_CACHE_MAX_SIZE)
	gfp_mask |= __GFP_COMP | __GFP_NOWARN | __GFP_NORETRY |
		    __GFP_NOMEMALLOC;
	page = alloc_pages_node(NUMA_NO_NODE, gfp_mask,
				PAGE_FRAG_CACHE_MAX_ORDER);
	nc->size = page ? PAGE_FRAG_CACHE_MAX_SIZE : PAGE_SIZE;
#endif
	if (unlikely(!page))
		page = alloc_pages_node(NUMA_NO_NODE, gfp, 0);

	nc->va = page ? page_address(page) : NULL;

	return page;
}

void __page_frag_cache_drain(struct page *page, unsigned int count)
{
	VM_BUG_ON_PAGE(page_ref_count(page) == 0, page);

	if (page_ref_sub_and_test(page, count)) {
		unsigned int order = compound_order(page);

		if (order == 0)
			free_hot_cold_page(page, false);
		else
			__free_pages_ok(page, order);
	}
}
EXPORT_SYMBOL(__page_frag_cache_drain);

void *page_frag_alloc(struct page_frag_cache *nc,
		      unsigned int fragsz, gfp_t gfp_mask)
{
	unsigned int size = PAGE_SIZE;
	struct page *page;
	int offset;

	if (unlikely(!nc->va)) {
refill:
		page = __page_frag_cache_refill(nc, gfp_mask);
		if (!page)
			return NULL;

#if (PAGE_SIZE < PAGE_FRAG_CACHE_MAX_SIZE)
		/* if size can vary use size else just use PAGE_SIZE */
		size = nc->size;
#endif
		/* Even if we own the page, we do not use atomic_set().
		 * This would break get_page_unless_zero() users.
		 */
		page_ref_add(page, size - 1);

		/* reset page count bias and offset to start of new frag */
		nc->pfmemalloc = page_is_pfmemalloc(page);
		nc->pagecnt_bias = size;
		nc->offset = size;
	}

	offset = nc->offset - fragsz;
	if (unlikely(offset < 0)) {
		page = virt_to_page(nc->va);

		if (!page_ref_sub_and_test(page, nc->pagecnt_bias))
			goto refill;

#if (PAGE_SIZE < PAGE_FRAG_CACHE_MAX_SIZE)
		/* if size can vary use size else just use PAGE_SIZE */
		size = nc->size;
#endif
		/* OK, page count is 0, we can safely set it */
		set_page_count(page, size);

		/* reset page count bias and offset to start of new frag */
		nc->pagecnt_bias = size;
		offset = size - fragsz;
	}

	nc->pagecnt_bias--;
	nc->offset = offset;

	return nc->va + offset;
}
EXPORT_SYMBOL(page_frag_alloc);

/*
 * Frees a page fragment allocated out of either a compound or order 0 page.
 */
void page_frag_free(void *addr)
{
	struct page *page = virt_to_head_page(addr);

	if (unlikely(put_page_testzero(page)))
		__free_pages_ok(page, compound_order(page));
}
EXPORT_SYMBOL(page_frag_free);

static void *make_alloc_exact(unsigned long addr, unsigned int order,
		size_t size)
{
	if (addr) {
		unsigned long alloc_end = addr + (PAGE_SIZE << order);
		unsigned long used = addr + PAGE_ALIGN(size);

		split_page(virt_to_page((void *)addr), order);
		while (used < alloc_end) {
			free_page(used);
			used += PAGE_SIZE;
		}
	}
	return (void *)addr;
}

/**
 * alloc_pages_exact - allocate an exact number physically-contiguous pages.
 * @size: the number of bytes to allocate
 * @gfp_mask: GFP flags for the allocation
 *
 * This function is similar to alloc_pages(), except that it allocates the
 * minimum number of pages to satisfy the request.  alloc_pages() can only
 * allocate memory in power-of-two pages.
 *
 * This function is also limited by MAX_ORDER.
 *
 * Memory allocated by this function must be released by free_pages_exact().
 */
void *alloc_pages_exact(size_t size, gfp_t gfp_mask)
{
	unsigned int order = get_order(size);
	unsigned long addr;

	addr = __get_free_pages(gfp_mask, order);
	return make_alloc_exact(addr, order, size);
}
EXPORT_SYMBOL(alloc_pages_exact);

/**
 * alloc_pages_exact_nid - allocate an exact number of physically-contiguous
 *			   pages on a node.
 * @nid: the preferred node ID where memory should be allocated
 * @size: the number of bytes to allocate
 * @gfp_mask: GFP flags for the allocation
 *
 * Like alloc_pages_exact(), but try to allocate on node nid first before falling
 * back.
 */
void * __meminit alloc_pages_exact_nid(int nid, size_t size, gfp_t gfp_mask)
{
	unsigned int order = get_order(size);
	struct page *p = alloc_pages_node(nid, gfp_mask, order);
	if (!p)
		return NULL;
	return make_alloc_exact((unsigned long)page_address(p), order, size);
}

/**
 * free_pages_exact - release memory allocated via alloc_pages_exact()
 * @virt: the value returned by alloc_pages_exact.
 * @size: size of allocation, same value as passed to alloc_pages_exact().
 *
 * Release the memory allocated by a previous call to alloc_pages_exact.
 */
void free_pages_exact(void *virt, size_t size)
{
	unsigned long addr = (unsigned long)virt;
	unsigned long end = addr + PAGE_ALIGN(size);

	while (addr < end) {
		free_page(addr);
		addr += PAGE_SIZE;
	}
}
EXPORT_SYMBOL(free_pages_exact);

/**
 * nr_free_zone_pages - count number of pages beyond high watermark
 * @offset: The zone index of the highest zone
 *
 * nr_free_zone_pages() counts the number of counts pages which are beyond the
 * high watermark within all zones at or below a given zone index.  For each
 * zone, the number of pages is calculated as:
 *     managed_pages - high_pages
 */
static unsigned long nr_free_zone_pages(int offset)
{
	struct zoneref *z;
	struct zone *zone;

	/* Just pick one node, since fallback list is circular */
	unsigned long sum = 0;

	struct zonelist *zonelist = node_zonelist(numa_node_id(), GFP_KERNEL);

	for_each_zone_zonelist(zone, z, zonelist, offset) {
		unsigned long size = zone->managed_pages;
		unsigned long high = high_wmark_pages(zone);
		if (size > high)
			sum += size - high;
	}

	return sum;
}

/**
 * nr_free_buffer_pages - count number of pages beyond high watermark
 *
 * nr_free_buffer_pages() counts the number of pages which are beyond the high
 * watermark within ZONE_DMA and ZONE_NORMAL.
 */
unsigned long nr_free_buffer_pages(void)
{
	return nr_free_zone_pages(gfp_zone(GFP_USER));
}
EXPORT_SYMBOL_GPL(nr_free_buffer_pages);

/**
 * nr_free_pagecache_pages - count number of pages beyond high watermark
 *
 * nr_free_pagecache_pages() counts the number of pages which are beyond the
 * high watermark within all zones.
 */
//在高水线上面的空页数,
unsigned long nr_free_pagecache_pages(void)
{
	return nr_free_zone_pages(gfp_zone(GFP_HIGHUSER_MOVABLE));
}

static inline void show_node(struct zone *zone)
{
	if (IS_ENABLED(CONFIG_NUMA))
		printk("Node %d ", zone_to_nid(zone));
}

long si_mem_available(void)
{
	long available;
	unsigned long pagecache;
	unsigned long wmark_low = 0;
	unsigned long pages[NR_LRU_LISTS];
	struct zone *zone;
	int lru;

	for (lru = LRU_BASE; lru < NR_LRU_LISTS; lru++)
		pages[lru] = global_node_page_state(NR_LRU_BASE + lru);

	for_each_zone(zone)
		wmark_low += zone->watermark[WMARK_LOW];

	/*
	 * Estimate the amount of memory available for userspace allocations,
	 * without causing swapping.
	 */
	available = global_page_state(NR_FREE_PAGES) - totalreserve_pages;

	/*
	 * Not all the page cache can be freed, otherwise the system will
	 * start swapping. Assume at least half of the page cache, or the
	 * low watermark worth of cache, needs to stay.
	 */
	pagecache = pages[LRU_ACTIVE_FILE] + pages[LRU_INACTIVE_FILE];
	pagecache -= min(pagecache / 2, wmark_low);
	available += pagecache;

	/*
	 * Part of the reclaimable slab consists of items that are in use,
	 * and cannot be freed. Cap this estimate at the low watermark.
	 */
	available += global_page_state(NR_SLAB_RECLAIMABLE) -
		     min(global_page_state(NR_SLAB_RECLAIMABLE) / 2, wmark_low);

	if (available < 0)
		available = 0;
	return available;
}
EXPORT_SYMBOL_GPL(si_mem_available);

void si_meminfo(struct sysinfo *val)
{
	val->totalram = totalram_pages;
	val->sharedram = global_node_page_state(NR_SHMEM);
	val->freeram = global_page_state(NR_FREE_PAGES);
	val->bufferram = nr_blockdev_pages();
	val->totalhigh = totalhigh_pages;
	val->freehigh = nr_free_highpages();
	val->mem_unit = PAGE_SIZE;
}

EXPORT_SYMBOL(si_meminfo);

#ifdef CONFIG_NUMA
void si_meminfo_node(struct sysinfo *val, int nid)
{
	int zone_type;		/* needs to be signed */
	unsigned long managed_pages = 0;
	unsigned long managed_highpages = 0;
	unsigned long free_highpages = 0;
	pg_data_t *pgdat = NODE_DATA(nid);

	for (zone_type = 0; zone_type < MAX_NR_ZONES; zone_type++)
		managed_pages += pgdat->node_zones[zone_type].managed_pages;
	val->totalram = managed_pages;
	val->sharedram = node_page_state(pgdat, NR_SHMEM);
	val->freeram = sum_zone_node_page_state(nid, NR_FREE_PAGES);
#ifdef CONFIG_HIGHMEM
	for (zone_type = 0; zone_type < MAX_NR_ZONES; zone_type++) {
		struct zone *zone = &pgdat->node_zones[zone_type];

		if (is_highmem(zone)) {
			managed_highpages += zone->managed_pages;
			free_highpages += zone_page_state(zone, NR_FREE_PAGES);
		}
	}
	val->totalhigh = managed_highpages;
	val->freehigh = free_highpages;
#else
	val->totalhigh = managed_highpages;
	val->freehigh = free_highpages;
#endif
	val->mem_unit = PAGE_SIZE;
}
#endif

/*
 * Determine whether the node should be displayed or not, depending on whether
 * SHOW_MEM_FILTER_NODES was passed to show_free_areas().
 */
static bool show_mem_node_skip(unsigned int flags, int nid, nodemask_t *nodemask)
{
	if (!(flags & SHOW_MEM_FILTER_NODES))
		return false;

	/*
	 * no node mask - aka implicit memory numa policy. Do not bother with
	 * the synchronization - read_mems_allowed_begin - because we do not
	 * have to be precise here.
	 */
	if (!nodemask)
		nodemask = &cpuset_current_mems_allowed;

	return !node_isset(nid, *nodemask);
}

#define K(x) ((x) << (PAGE_SHIFT-10))

static void show_migration_types(unsigned char type)
{
	static const char types[MIGRATE_TYPES] = {
		[MIGRATE_UNMOVABLE]	= 'U',
		[MIGRATE_MOVABLE]	= 'M',
		[MIGRATE_RECLAIMABLE]	= 'E',
		[MIGRATE_HIGHATOMIC]	= 'H',
#ifdef CONFIG_CMA
		[MIGRATE_CMA]		= 'C',
#endif
#ifdef CONFIG_MEMORY_ISOLATION
		[MIGRATE_ISOLATE]	= 'I',
#endif
	};
	char tmp[MIGRATE_TYPES + 1];
	char *p = tmp;
	int i;

	for (i = 0; i < MIGRATE_TYPES; i++) {
		if (type & (1 << i))
			*p++ = types[i];
	}

	*p = '\0';
	printk(KERN_CONT "(%s) ", tmp);
}

/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 *
 * Bits in @filter:
 * SHOW_MEM_FILTER_NODES: suppress nodes that are not allowed by current's
 *   cpuset.
 */
void show_free_areas(unsigned int filter, nodemask_t *nodemask)
{
	unsigned long free_pcp = 0;
	int cpu;
	struct zone *zone;
	pg_data_t *pgdat;

	for_each_populated_zone(zone) {
		if (show_mem_node_skip(filter, zone_to_nid(zone), nodemask))
			continue;

		for_each_online_cpu(cpu)
			free_pcp += per_cpu_ptr(zone->pageset, cpu)->pcp.count;
	}

	printk("active_anon:%lu inactive_anon:%lu isolated_anon:%lu\n"
		" active_file:%lu inactive_file:%lu isolated_file:%lu\n"
		" unevictable:%lu dirty:%lu writeback:%lu unstable:%lu\n"
		" slab_reclaimable:%lu slab_unreclaimable:%lu\n"
		" mapped:%lu shmem:%lu pagetables:%lu bounce:%lu\n"
		" free:%lu free_pcp:%lu free_cma:%lu\n",
		global_node_page_state(NR_ACTIVE_ANON),
		global_node_page_state(NR_INACTIVE_ANON),
		global_node_page_state(NR_ISOLATED_ANON),
		global_node_page_state(NR_ACTIVE_FILE),
		global_node_page_state(NR_INACTIVE_FILE),
		global_node_page_state(NR_ISOLATED_FILE),
		global_node_page_state(NR_UNEVICTABLE),
		global_node_page_state(NR_FILE_DIRTY),
		global_node_page_state(NR_WRITEBACK),
		global_node_page_state(NR_UNSTABLE_NFS),
		global_page_state(NR_SLAB_RECLAIMABLE),
		global_page_state(NR_SLAB_UNRECLAIMABLE),
		global_node_page_state(NR_FILE_MAPPED),
		global_node_page_state(NR_SHMEM),
		global_page_state(NR_PAGETABLE),
		global_page_state(NR_BOUNCE),
		global_page_state(NR_FREE_PAGES),
		free_pcp,
		global_page_state(NR_FREE_CMA_PAGES));

	for_each_online_pgdat(pgdat) {
		if (show_mem_node_skip(filter, pgdat->node_id, nodemask))
			continue;

		printk("Node %d"
			" active_anon:%lukB"
			" inactive_anon:%lukB"
			" active_file:%lukB"
			" inactive_file:%lukB"
			" unevictable:%lukB"
			" isolated(anon):%lukB"
			" isolated(file):%lukB"
			" mapped:%lukB"
			" dirty:%lukB"
			" writeback:%lukB"
			" shmem:%lukB"
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			" shmem_thp: %lukB"
			" shmem_pmdmapped: %lukB"
			" anon_thp: %lukB"
#endif
			" writeback_tmp:%lukB"
			" unstable:%lukB"
			" pages_scanned:%lu"
			" all_unreclaimable? %s"
			"\n",
			pgdat->node_id,
			K(node_page_state(pgdat, NR_ACTIVE_ANON)),
			K(node_page_state(pgdat, NR_INACTIVE_ANON)),
			K(node_page_state(pgdat, NR_ACTIVE_FILE)),
			K(node_page_state(pgdat, NR_INACTIVE_FILE)),
			K(node_page_state(pgdat, NR_UNEVICTABLE)),
			K(node_page_state(pgdat, NR_ISOLATED_ANON)),
			K(node_page_state(pgdat, NR_ISOLATED_FILE)),
			K(node_page_state(pgdat, NR_FILE_MAPPED)),
			K(node_page_state(pgdat, NR_FILE_DIRTY)),
			K(node_page_state(pgdat, NR_WRITEBACK)),
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			K(node_page_state(pgdat, NR_SHMEM_THPS) * HPAGE_PMD_NR),
			K(node_page_state(pgdat, NR_SHMEM_PMDMAPPED)
					* HPAGE_PMD_NR),
			K(node_page_state(pgdat, NR_ANON_THPS) * HPAGE_PMD_NR),
#endif
			K(node_page_state(pgdat, NR_SHMEM)),
			K(node_page_state(pgdat, NR_WRITEBACK_TEMP)),
			K(node_page_state(pgdat, NR_UNSTABLE_NFS)),
			node_page_state(pgdat, NR_PAGES_SCANNED),
			!pgdat_reclaimable(pgdat) ? "yes" : "no");
	}

	for_each_populated_zone(zone) {
		int i;

		if (show_mem_node_skip(filter, zone_to_nid(zone), nodemask))
			continue;

		free_pcp = 0;
		for_each_online_cpu(cpu)
			free_pcp += per_cpu_ptr(zone->pageset, cpu)->pcp.count;

		show_node(zone);
		printk(KERN_CONT
			"%s"
			" free:%lukB"
			" min:%lukB"
			" low:%lukB"
			" high:%lukB"
			" active_anon:%lukB"
			" inactive_anon:%lukB"
			" active_file:%lukB"
			" inactive_file:%lukB"
			" unevictable:%lukB"
			" writepending:%lukB"
			" present:%lukB"
			" managed:%lukB"
			" mlocked:%lukB"
			" slab_reclaimable:%lukB"
			" slab_unreclaimable:%lukB"
			" kernel_stack:%lukB"
			" pagetables:%lukB"
			" bounce:%lukB"
			" free_pcp:%lukB"
			" local_pcp:%ukB"
			" free_cma:%lukB"
			"\n",
			zone->name,
			K(zone_page_state(zone, NR_FREE_PAGES)),
			K(min_wmark_pages(zone)),
			K(low_wmark_pages(zone)),
			K(high_wmark_pages(zone)),
			K(zone_page_state(zone, NR_ZONE_ACTIVE_ANON)),
			K(zone_page_state(zone, NR_ZONE_INACTIVE_ANON)),
			K(zone_page_state(zone, NR_ZONE_ACTIVE_FILE)),
			K(zone_page_state(zone, NR_ZONE_INACTIVE_FILE)),
			K(zone_page_state(zone, NR_ZONE_UNEVICTABLE)),
			K(zone_page_state(zone, NR_ZONE_WRITE_PENDING)),
			K(zone->present_pages),
			K(zone->managed_pages),
			K(zone_page_state(zone, NR_MLOCK)),
			K(zone_page_state(zone, NR_SLAB_RECLAIMABLE)),
			K(zone_page_state(zone, NR_SLAB_UNRECLAIMABLE)),
			zone_page_state(zone, NR_KERNEL_STACK_KB),
			K(zone_page_state(zone, NR_PAGETABLE)),
			K(zone_page_state(zone, NR_BOUNCE)),
			K(free_pcp),
			K(this_cpu_read(zone->pageset->pcp.count)),
			K(zone_page_state(zone, NR_FREE_CMA_PAGES)));
		printk("lowmem_reserve[]:");
		for (i = 0; i < MAX_NR_ZONES; i++)
			printk(KERN_CONT " %ld", zone->lowmem_reserve[i]);
		printk(KERN_CONT "\n");
	}

	for_each_populated_zone(zone) {
		unsigned int order;
		unsigned long nr[MAX_ORDER], flags, total = 0;
		unsigned char types[MAX_ORDER];

		if (show_mem_node_skip(filter, zone_to_nid(zone), nodemask))
			continue;
		show_node(zone);
		printk(KERN_CONT "%s: ", zone->name);

		spin_lock_irqsave(&zone->lock, flags);
		for (order = 0; order < MAX_ORDER; order++) {
			struct free_area *area = &zone->free_area[order];
			int type;

			nr[order] = area->nr_free;
			total += nr[order] << order;

			types[order] = 0;
			for (type = 0; type < MIGRATE_TYPES; type++) {
				if (!list_empty(&area->free_list[type]))
					types[order] |= 1 << type;
			}
		}
		spin_unlock_irqrestore(&zone->lock, flags);
		for (order = 0; order < MAX_ORDER; order++) {
			printk(KERN_CONT "%lu*%lukB ",
			       nr[order], K(1UL) << order);
			if (nr[order])
				show_migration_types(types[order]);
		}
		printk(KERN_CONT "= %lukB\n", K(total));
	}

	hugetlb_show_meminfo();

	printk("%ld total pagecache pages\n", global_node_page_state(NR_FILE_PAGES));

	show_swap_cache_info();
}

static void zoneref_set_zone(struct zone *zone, struct zoneref *zoneref)
{
	zoneref->zone = zone;
	zoneref->zone_idx = zone_idx(zone);
}

/*
 * Builds allocation fallback zone lists.
 *
 * Add all populated zones of a node to the zonelist.
 */
/*
 * 按照分配代价从低廉到昂贵的次序，迭代了分配代价不低于当前内存域的内存域。
 */
static int build_zonelists_node(pg_data_t *pgdat, struct zonelist *zonelist,
				int nr_zones)
{
	struct zone *zone;
	//从最后一个zone开始，当然要优先保护低地址的dma，normal区了。
	enum zone_type zone_type = MAX_NR_ZONES;

	do {
		/* 备用域只能使用比当前域更低级的域。如果NORMAL域只能用NORMAL、DMA作为用备用域 */
		zone_type--;
		//待加到备用列表中的zone
		zone = pgdat->node_zones + zone_type;
		/* 确保该域中确实存在可用页面 */
		if (managed_zone(zone)) {//这个zone上有内存
			//加到备用列表中
			zoneref_set_zone(zone,
				&zonelist->_zonerefs[nr_zones++]);
			check_highest_zone(zone_type);
		}
	} while (zone_type);

	return nr_zones;
}


/*
 *  zonelist_order:
 *  0 = automatic detection of better ordering.
 *  1 = order by ([node] distance, -zonetype)
 *  2 = order by (-zonetype, [node] distance)
 *
 *  If not NUMA, ZONELIST_ORDER_ZONE and ZONELIST_ORDER_NODE will create
 *  the same zonelist. So only NUMA can configure this param.
 */
/*
如果系统当前系统是非NUMA结构的, 则系统中只有一个结点,
配置ZONELIST_ORDER_NODE和ZONELIST_ORDER_ZONE结果相同.
*/
/*
 由系统智能选择Node或Zone方式
*/
#define ZONELIST_ORDER_DEFAULT  0
/*
按节点顺序依次排列，先排列本地节点的所有zone，
再排列其它节点的所有zone
*/
#define ZONELIST_ORDER_NODE     1
/*
按zone类型从高到低依次排列各节点的同相类型zone
*/
#define ZONELIST_ORDER_ZONE     2

/* zonelist order in the kernel.
 * set_zonelist_order() will set this to NODE or ZONE.
 */
static int current_zonelist_order = ZONELIST_ORDER_DEFAULT; // 系统中的当前使用的内存域排列方式
static char zonelist_order_name[3][8] = {"Default", "Node", "Zone"};


#ifdef CONFIG_NUMA
/* The value user specified ....changed by config */
static int user_zonelist_order = ZONELIST_ORDER_DEFAULT;
/* string for sysctl */
#define NUMA_ZONELIST_ORDER_LEN	16
char numa_zonelist_order[16] = "default";

/*
 * interface for configure zonelist ordering.
 * command line option "numa_zonelist_order"
 *	= "[dD]efault	- default, automatic configuration.
 *	= "[nN]ode 	- order by node locality, then by zone within node
 *	= "[zZ]one      - order by zone, then by locality within zone
 */

static int __parse_numa_zonelist_order(char *s)
{
	if (*s == 'd' || *s == 'D') {
		user_zonelist_order = ZONELIST_ORDER_DEFAULT;
	} else if (*s == 'n' || *s == 'N') {
		user_zonelist_order = ZONELIST_ORDER_NODE;
	} else if (*s == 'z' || *s == 'Z') {
		user_zonelist_order = ZONELIST_ORDER_ZONE;
	} else {
		pr_warn("Ignoring invalid numa_zonelist_order value:  %s\n", s);
		return -EINVAL;
	}
	return 0;
}

static __init int setup_numa_zonelist_order(char *s)
{
	int ret;

	if (!s)
		return 0;

	ret = __parse_numa_zonelist_order(s);
	if (ret == 0)
		strlcpy(numa_zonelist_order, s, NUMA_ZONELIST_ORDER_LEN);

	return ret;
}
early_param("numa_zonelist_order", setup_numa_zonelist_order);

/*
 * sysctl handler for numa_zonelist_order
 */
int numa_zonelist_order_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *length,
		loff_t *ppos)
{
	char saved_string[NUMA_ZONELIST_ORDER_LEN];
	int ret;
	static DEFINE_MUTEX(zl_order_mutex);

	mutex_lock(&zl_order_mutex);
	if (write) {
		if (strlen((char *)table->data) >= NUMA_ZONELIST_ORDER_LEN) {
			ret = -EINVAL;
			goto out;
		}
		strcpy(saved_string, (char *)table->data);
	}
	ret = proc_dostring(table, write, buffer, length, ppos);
	if (ret)
		goto out;
	if (write) {
		int oldval = user_zonelist_order;

		ret = __parse_numa_zonelist_order((char *)table->data);
		if (ret) {
			/*
			 * bogus value.  restore saved string
			 */
			strncpy((char *)table->data, saved_string,
				NUMA_ZONELIST_ORDER_LEN);
			user_zonelist_order = oldval;
		} else if (oldval != user_zonelist_order) {
			mutex_lock(&zonelists_mutex);
			build_all_zonelists(NULL, NULL);
			mutex_unlock(&zonelists_mutex);
		}
	}
out:
	mutex_unlock(&zl_order_mutex);
	return ret;
}


#define MAX_NODE_LOAD (nr_online_nodes)
static int node_load[MAX_NUMNODES];

/**
 * find_next_best_node - find the next node that should appear in a given node's fallback list
 * @node: node whose fallback list we're appending
 * @used_node_mask: nodemask_t of already used nodes
 *
 * We use a number of factors to determine which is the next node that should
 * appear on a given node's fallback list.  The node should not have appeared
 * already in @node's fallback list, and it should be the next closest node
 * according to the distance array (which contains arbitrary distance values
 * from each node to each node in the system), and should also prefer nodes
 * with no CPUs, since presumably they'll have very little allocation pressure
 * on them otherwise.
 * It returns -1 if no node is found.
 */
/*
 * 查找没有在node指向的结点的备用列表中并且离node最近的一个结点的ID。
 * 这个函数有可能返回node本身。used_node_mask:存储当前已使用结点的位图
 */
static int find_next_best_node(int node, nodemask_t *used_node_mask)
{
	int n, val;
	int min_val = INT_MAX;
	int best_node = NUMA_NO_NODE;
	const struct cpumask *tmp = cpumask_of_node(0);

	/* Use the local node if we haven't already */
	//当然优先从当前节点开始查找了。
	if (!node_isset(node, *used_node_mask)) {
		node_set(node, *used_node_mask);
		return node;
	}

	/*
	 * 遍历处于N_HIGH_MEMORY状态的所有结点。如果所有可用的结点已经遍历完成，
	 * 此时返回-1. 因为每个结点都会在N_HIGH_MEMORY状态的掩码(node_states数组)
	 * 中设置，所以这里指定N_HIGH_MEMORY状态就可以遍历到系统中的所有结点。
	 * 参见free_area_init_nodes(), 这个函数会在setup_arch()中间接调用到。
	 */
	for_each_node_state(n, N_MEMORY) {

		/* Don't want a node to appear more than once */
		if (node_isset(n, *used_node_mask))//已经在备用列表中了，下一个
			continue;

		/* Use the distance array to find the distance */
		val = node_distance(node, n);//计算节点之间的距离

		/* Penalize nodes under us ("prefer the next node") */
		/*
		 * 在选择best node时，优先选择距离近并且结点ID大于node 的结点。如果
		 * n指向的结点ID小于node指向的结点ID时，则对val加1，减少其被选中的机会。
		 */
		val += (n < node);//如果是前面的节点，加一点距离，优先使用后面的节点

		/* Give preference to headless and unused nodes */
		tmp = cpumask_of_node(n);
		if (!cpumask_empty(tmp))//没有设置CPU亲和性的?
			val += PENALTY_FOR_NODE_WITH_CPUS;

		/* Slight preference for less loaded node */
		val *= (MAX_NODE_LOAD*MAX_NUMNODES);//考虑该节点的负载
		val += node_load[n];

		if (val < min_val) {//找值最小的，即最优的节点
			min_val = val;
			best_node = n;
		}
	}

	if (best_node >= 0)//存在可用节点
		node_set(best_node, *used_node_mask);

	return best_node;
}


/*
 * Build zonelists ordered by node and zones within node.
 * This results in maximum locality--normal zone overflows into local
 * DMA zone, if any--but risks exhausting DMA zone.
 */
//按内存节点顺序排列zone备用列表
static void build_zonelists_in_node_order(pg_data_t *pgdat, int node)
{
	int j;
	struct zonelist *zonelist;

	//备用zone列表指针
	zonelist = &pgdat->node_zonelists[ZONELIST_FALLBACK];
	//找到已经在列表中的zone数量，j是下一个可用索引
	for (j = 0; zonelist->_zonerefs[j].zone != NULL; j++)
		;
	//将备用节点中的zone保存到最后一个可用位置
	j = build_zonelists_node(NODE_DATA(node), zonelist, j);
	//将后面的备用列表置为空。
	zonelist->_zonerefs[j].zone = NULL;
	zonelist->_zonerefs[j].zone_idx = 0;
}

/*
 * Build gfp_thisnode zonelists
 */
static void build_thisnode_zonelists(pg_data_t *pgdat)
{
	int j;
	struct zonelist *zonelist;

	//本节点自己的zone列表
	zonelist = &pgdat->node_zonelists[ZONELIST_NOFALLBACK];
	//构建zone列表，当指定GFP_THISNODE时，使用本节点的zone列表进行分配。
	j = build_zonelists_node(pgdat, zonelist, 0);
	zonelist->_zonerefs[j].zone = NULL;
	zonelist->_zonerefs[j].zone_idx = 0;
}

/*
 * Build zonelists ordered by zone and nodes within zones.
 * This results in conserving DMA zone[s] until all Normal memory is
 * exhausted, but results in overflowing to remote node while memory
 * may still exist in local DMA zone.
 */
static int node_order[MAX_NUMNODES];

/**
 * 以zone类型排列节点的备用列表
 */
static void build_zonelists_in_zone_order(pg_data_t *pgdat, int nr_nodes)
{
	int pos, j, node;
	int zone_type;		/* needs to be signed */
	struct zone *z;
	struct zonelist *zonelist;

	zonelist = &pgdat->node_zonelists[ZONELIST_FALLBACK];
	pos = 0;
	//从高端zone向低端zone遍历。
	for (zone_type = MAX_NR_ZONES - 1; zone_type >= 0; zone_type--) {
		for (j = 0; j < nr_nodes; j++) {//处理所有备用节点
			node = node_order[j];//前面已经按距离、负载等因素将节点排序了。
			z = &NODE_DATA(node)->node_zones[zone_type];
			if (managed_zone(z)) { //这个zone上有内存
				//记录到备用列表中
				zoneref_set_zone(z,
					&zonelist->_zonerefs[pos++]);
				check_highest_zone(zone_type);
			}
		}
	}
	//将最后一个备用索引置空。
	zonelist->_zonerefs[pos].zone = NULL;
	zonelist->_zonerefs[pos].zone_idx = 0;
}

#if defined(CONFIG_64BIT)
/*
 * Devices that require DMA32/DMA are relatively rare and do not justify a
 * penalty to every machine in case the specialised case applies. Default
 * to Node-ordering on 64-bit NUMA machines
 */
static int default_zonelist_order(void)
{
	return ZONELIST_ORDER_NODE;
}
#else
/*
 * On 32-bit, the Normal zone needs to be preserved for allocations accessible
 * by the kernel. If processes running on node 0 deplete the low memory zone
 * then reclaim will occur more frequency increasing stalls and potentially
 * be easier to OOM if a large percentage of the zone is under writeback or
 * dirty. The problem is significantly worse if CONFIG_HIGHPTE is not set.
 * Hence, default to zone ordering on 32-bit.
 */
static int default_zonelist_order(void)
{
	return ZONELIST_ORDER_ZONE;
}
#endif /* CONFIG_64BIT */
/*
如果系统是NUMA结构, 则设置为系统指定的方式即可
当前的排列方式为ZONELIST_ORDER_DEFAULT, 即系统默认方式,
则current_zonelist_order则由内核交给default_zonelist_order采用一定的算法选择一个最优的分配策略,　
目前的系统中如果是32位则配置为ZONE方式, 而如果是６４位系统则设置为NODE方式
当前的排列方式不是默认方式, 则设置为user_zonelist_order指定的内存域排列方式
*/
static void set_zonelist_order(void)
{
	if (user_zonelist_order == ZONELIST_ORDER_DEFAULT)//默认排列方式
		/**
		 * 在64位机器上，使用DMA的时间比较少，因此可以按节点排列而不考虑DMA OOM，这样可以使程序运行得快一点。
		 * 32位机器上，则默认使用zone排列方式。
		 */
		current_zonelist_order = default_zonelist_order();
	else//使用用户指定的排列方式
		current_zonelist_order = user_zonelist_order;
}

/*
 * build_zonelists()需要一个指向pg_data_t实例的指针作为参数，
 * 其中包含了结点内存配置的所有现存信息，而新建的数据结构也会放置其中。
 * 该函数的任务是，在当前处理的结点和系统中其他结点的内存域之间
 * 建立一种等级次序。接下来，根据这种次序分配内存。如果在期望的
 * 节点内存域中，没有空闲内存，那么这种次序就很重要。
 */
static void build_zonelists(pg_data_t *pgdat)
{
	int i, node, load;
	nodemask_t used_mask;
	int local_node, prev_node;
	struct zonelist *zonelist;
	unsigned int order = current_zonelist_order;

	/* initialize zonelists */
	//将节点的两个zonelist初始化为空。
	for (i = 0; i < MAX_ZONELISTS; i++) {
		zonelist = pgdat->node_zonelists + i;
		zonelist->_zonerefs[0].zone = NULL;
		zonelist->_zonerefs[0].zone_idx = 0;
	}

	/* NUMA-aware ordering of nodes */
	local_node = pgdat->node_id;
	load = nr_online_nodes;
	prev_node = local_node;
	nodes_clear(used_mask);

	memset(node_order, 0, sizeof(node_order));
	i = 0;

	//查找备用节点
	while ((node = find_next_best_node(local_node, &used_mask)) >= 0) {
		/*
		 * We don't want to pressure a particular node.
		 * So adding penalty to the first node in same
		 * distance group to make it round-robin.
		 */
		/**
		 * 如果当前选中的节点的距离与前一个节点相同，那么将当前节点负载加一点，这样下一次就不容易再选中该节点
		 * 避免该节点被造成太大的压力。
		 */
		if (node_distance(local_node, node) !=
		    node_distance(local_node, prev_node))
			node_load[node] = load;

		prev_node = node;
		load--;
		//以节点进行排列
		if (order == ZONELIST_ORDER_NODE)
			build_zonelists_in_node_order(pgdat, node);
		else//以zone类型进行排列，在这里记下节点顺序
			node_order[i++] = node;	/* remember order */
	}

	if (order == ZONELIST_ORDER_ZONE) {//按zone类型为序进行排列
		/* calculate node order -- i.e., DMA last! */
		build_zonelists_in_zone_order(pgdat, i);
	}

	//构建本节点自己的zone列表(前面构建的是备用列表)
	build_thisnode_zonelists(pgdat);
}

#ifdef CONFIG_HAVE_MEMORYLESS_NODES
/*
 * Return node id of node used for "local" allocations.
 * I.e., first node id of first zone in arg node's generic zonelist.
 * Used for initializing percpu 'numa_mem', which is used primarily
 * for kernel allocations, so use GFP_KERNEL flags to locate zonelist.
 */
int local_memory_node(int node)
{
	struct zoneref *z;

	z = first_zones_zonelist(node_zonelist(node, GFP_KERNEL),
				   gfp_zone(GFP_KERNEL),
				   NULL);
	return z->zone->node;
}
#endif

static void setup_min_unmapped_ratio(void);
static void setup_min_slab_ratio(void);
#else	/* CONFIG_NUMA */
static void set_zonelist_order(void)
{
	current_zonelist_order = ZONELIST_ORDER_ZONE;
}

/*
初始化每个内存结点的zonelists
完成了节点pgdat上zonelists的初始化工作,
它建立了备用层次结构zonelists.

由于UMA和NUMA架构下结点的层次结构有很大的区别,
因此内核分别提供了两套不同的接口
*/
static void build_zonelists(pg_data_t *pgdat)
{
	int node, local_node;
	enum zone_type j;
	struct zonelist *zonelist;

	local_node = pgdat->node_id;

	/* 得到该内存域的备用域列表指针 */
	zonelist = &pgdat->node_zonelists[ZONELIST_FALLBACK];
	/* 构建内存域的备用域 */
	j = build_zonelists_node(pgdat, zonelist, 0);

	/*
	 * Now we build the zonelist so that it contains the zones
	 * of all the other nodes.
	 * We don't want to pressure a particular node, so when
	 * building the zones for node N, we make sure that the
	 * zones coming right after the local ones are those from
	 * node N+1 (modulo N)
	 */
 	/* 遍历大于当前节点的所有节点，以下两个循环避免过度使用某个节点作为备用节点 */
	for (node = local_node + 1; node < MAX_NUMNODES; node++) {
		if (!node_online(node))
			continue;
		j = build_zonelists_node(NODE_DATA(node), zonelist, j);
	}
	/* 遍历所有小于当前节点的所有节点 */
	for (node = 0; node < local_node; node++) {
		if (!node_online(node))
			continue;
/*
        为pgdat->node_zones[0]建立后备的zone，node_zones[0]后备的zone
            |     存储在node_zonelist[0]内，对于node_zone[0]的后备zone，其后备的zone
            |     链表如下(只考虑UMA体系，而且不考虑ZONE_DMA)：
            |     node_zonelist[0]._zonerefs[0].zone = &node_zones[2];
            |     node_zonelist[0]._zonerefs[0].zone_idx = 2;
            |     node_zonelist[0]._zonerefs[1].zone = &node_zones[1];
            |     node_zonelist[0]._zonerefs[1].zone_idx = 1;
            |     node_zonelist[0]._zonerefs[2].zone = &node_zones[0];
            |     node_zonelist[0]._zonerefs[2].zone_idx = 0;
            |
            |     zonelist->_zonerefs[3].zone = NULL;
            |     zonelist->_zonerefs[3].zone_idx = 0;
*/
		j = build_zonelists_node(NODE_DATA(node), zonelist, j);
	}

	/* NULL表示结束，因为无法确定项数  */
	zonelist->_zonerefs[j].zone = NULL;
	zonelist->_zonerefs[j].zone_idx = 0;
}

#endif	/* CONFIG_NUMA */

/*
 * Boot pageset table. One per cpu which is going to be used for all
 * zones and all nodes. The parameters will be set in such a way
 * that an item put on a list will immediately be handed over to
 * the buddy list. This is safe since pageset manipulation is done
 * with interrupts disabled.
 *
 * The boot_pagesets must be kept even after bootup is complete for
 * unused processors and/or zones. They do play a role for bootstrapping
 * hotplugged processors.
 *
 * zoneinfo_show() and maybe other functions do
 * not check if the processor is online before following the pageset pointer.
 * Other parts of the kernel may not check if the zone is available.
 */
static void setup_pageset(struct per_cpu_pageset *p, unsigned long batch);
static DEFINE_PER_CPU(struct per_cpu_pageset, boot_pageset);
static void setup_zone_pageset(struct zone *zone);

/*
 * Global mutex to protect against size modification of zonelists
 * as well as to serialize pageset setup for the new populated zone.
 */
DEFINE_MUTEX(zonelists_mutex);

/* return values int ....just for stop_machine() */
/*
Memory不支持热插拔, 为每个zone建立后备的zone,
每个zone及自己后备的zone，形成zonelist
*/
static int __build_all_zonelists(void *data)
{
	int nid;
	int cpu;
	pg_data_t *self = data;

#ifdef CONFIG_NUMA
	memset(node_load, 0, sizeof(node_load));
#endif

	if (self && !node_online(self->node_id)) {
        // 为每个zone建立后备zone的列表
		build_zonelists(self);
	}

    // 由于UMA系统只有一个结点，build_zonelists只调用了一次,
    // 就对所有的内存创建了内存域列表
	// 遍历所有节点
	/*
	 * for_each_online_node遍历了系统中所有的活动结点。由于
	 * UMA系统只有一个结点，build_zonelists()只调用了一次，
	 * 就对所有的内存创建了内存域列表。NUMA系统调用该函数
	 * 的次数等同于结点的数目。每次调用对一个不同结点生成内存域数据。
	 */
	for_each_online_node(nid) {
		/* 取节点的内存节点数据 */
		pg_data_t *pgdat = NODE_DATA(nid);

		//构建两个zonelist，一个是备用zone列表，一个是节点自己的zone列表。
		build_zonelists(pgdat);
	}

	/*
	 * Initialize the boot_pagesets that are going to be used
	 * for bootstrapping processors. The real pagesets for
	 * each zone will be allocated later when the per cpu
	 * allocator is available.
	 *
	 * boot_pagesets are used also for bootstrapping offline
	 * cpus if the system is already booted because the pagesets
	 * are needed to initialize allocators on a specific cpu too.
	 * F.e. the percpu allocator needs the page allocator which
	 * needs the percpu allocator in order to allocate its pagesets
	 * (a chicken-egg dilemma).
	 */
	for_each_possible_cpu(cpu) {
		setup_pageset(&per_cpu(boot_pageset, cpu), 0);

#ifdef CONFIG_HAVE_MEMORYLESS_NODES
		/*
		 * We now know the "local memory node" for each node--
		 * i.e., the node of the first zone in the generic zonelist.
		 * Set up numa_mem percpu variable for on-line cpus.  During
		 * boot, only the boot cpu should be on-line;  we'll init the
		 * secondary cpus' numa_mem as they come on-line.  During
		 * node/memory hotplug, we'll fixup all on-line cpus.
		 */
		if (cpu_online(cpu))
			set_cpu_numa_mem(cpu, local_memory_node(cpu_to_node(cpu)));
#endif
	}

	return 0;
}

static noinline void __init
build_all_zonelists_init(void)
{
	/* 设置zonelist中节点和内存域的组织形式
       *  current_zonelist_order变量标识了当前系统的内存组织形式
       *  zonelist_order_name以字符串存储了系统中内存组织形式的名称  */
	__build_all_zonelists(NULL);
	//balabala,输出备份列表信息
	mminit_verify_zonelist();
	//设置当前进程的mems_allowed位图，允许当前进程在所有节点中分配内存。
	cpuset_init_current_mems_allowed();
}

/*
 * Called with zonelists_mutex held always
 * unless system_state == SYSTEM_BOOTING.
 *
 * __ref due to (1) call of __meminit annotated setup_zone_pageset
 * [we're only called with non-NULL zone through __meminit paths] and
 * (2) call of __init annotated helper build_all_zonelists_init
 * [protected by SYSTEM_BOOTING].
 */

/*
初始化内存分配器使用的存储节点中的管理区链表，是为内存管理算法（伙伴管理算法）做准备工作

为系统中的zone建立后备zone的列表.
所有zone的后备列表都在pglist_data->node_zonelists[0]中;
期间也对per-CPU变量boot_pageset做了初始化.

build_all_zonelists建立管理结点及其内存域所需的数据结构。
该函数可以通过宏和抽象机制实现，而不用考虑具体的NUMA或UMA系统。
因为执行的函数实际上有两种形式，所以这样做是可能的：一种用于NUMA系统，而另一种用于UMA系统。
*/
void __ref build_all_zonelists(pg_data_t *pgdat, struct zone *zone)
{
	//设置zone排列方式，是按节点之间的距离还是按zone类型。
	// 设置zonelists中结点的组织顺序
	set_zonelist_order();

	if (system_state == SYSTEM_BOOTING) {//启动阶段
		/* 初始化内存节点及内存域 */
		build_all_zonelists_init(); // 初始化zonelist
	} else {
#ifdef CONFIG_MEMORY_HOTPLUG
		if (zone)
			setup_zone_pageset(zone); // 设置冷热页
#endif
		/* we have to stop all cpus to guarantee there is no user
		   of zonelist */
		//停止所有CPU，并让这些CPU在关中断的状态下调用__build_all_zonelists
		stop_machine(__build_all_zonelists, pgdat, NULL);
		/* cpuset refresh routine should be here */
	}
	//计算所有zone中，在高水线的情况下，可用的内存页面数量	// 获得所有zone中的present_pages总和
	vm_total_pages = nr_free_pagecache_pages();
	/*
	 * Disable grouping by mobility if the number of pages in the
	 * system is too low to allow the mechanism to work. It would be
	 * more accurate, but expensive to check per-zone. This check is
	 * made on memory-hotadd so a system can start with mobility
	 * disabled and enable it later
	 */
	/**
	 * 如果可用内存较少，就不打开页面移动功能。
	 */
	if (vm_total_pages < (pageblock_nr_pages * MIGRATE_TYPES))
		page_group_by_mobility_disabled = 1;
	else
		page_group_by_mobility_disabled = 0;// 对于代码中的判断条件一般不会成立，因为页数会最够多（内存较大）

	pr_info("Built %i zonelists in %s order, mobility grouping %s.  Total pages: %ld\n",
		nr_online_nodes,
		zonelist_order_name[current_zonelist_order],
		page_group_by_mobility_disabled ? "off" : "on",
		vm_total_pages);
#ifdef CONFIG_NUMA
	pr_info("Policy zone: %s\n", zone_names[policy_zone]);
#endif
}

/*
 * Initially all pages are reserved - free ones are freed
 * up by free_all_bootmem() once the early boot process is
 * done. Non-atomic initialization, single-pass.
 */
/**
 * 将zone中的页框PG_reserved置位。表示该页不可用。
 * 同时初始化页框中其他值。

在分配内存时, 如果必须”盗取”不同于预定迁移类型的内存区,
内核在策略上倾向于”盗取”更大的内存区.

由于所有页最初都是可移动的, 那么在内核分配不可移动的内存区时,
则必须”盗取”.

实际上, 在启动期间分配可移动内存区的情况较少,
那么分配器有很高的几率分配长度最大的内存区,
并将其从可移动列表转换到不可移动列表.

由于分配的内存区长度是最大的, 因此不会向可移动内存中引入碎片.

总而言之, 这种做法避免了启动期间内核分配的内存
(经常在系统的整个运行时间都不释放)散布到物理内存各处,
从而使其他类型的内存分配免受碎片的干扰，
这也是页可移动性分组框架的最重要的目标之一
 */
void __meminit memmap_init_zone(unsigned long size, int nid, unsigned long zone,
		unsigned long start_pfn, enum memmap_context context)
{
	struct vmem_altmap *altmap = to_vmem_altmap(__pfn_to_phys(start_pfn));
	unsigned long end_pfn = start_pfn + size;
	pg_data_t *pgdat = NODE_DATA(nid);
	unsigned long pfn;
	unsigned long nr_initialised = 0;
#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
	struct memblock_region *r = NULL, *tmp;
#endif

	if (highest_memmap_pfn < end_pfn - 1)
		highest_memmap_pfn = end_pfn - 1;

	/*
	 * Honor reservation requested by the driver for this ZONE_DEVICE
	 * memory
	 */
	if (altmap && start_pfn == altmap->base_pfn)
		start_pfn += altmap->reserve;

	for (pfn = start_pfn; pfn < end_pfn; pfn++) {
		/*
		 * There can be holes in boot-time mem_map[]s handed to this
		 * function.  They do not exist on hotplugged memory.
		 */
		if (context != MEMMAP_EARLY)
			goto not_early;

		if (!early_pfn_valid(pfn)) {
#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
			/*
			 * Skip to the pfn preceding the next valid one (or
			 * end_pfn), such that we hit a valid pfn (or end_pfn)
			 * on our next iteration of the loop.
			 */
			pfn = memblock_next_valid_pfn(pfn, end_pfn) - 1;
#endif
			continue;
		}
		if (!early_pfn_in_nid(pfn, nid))
			continue;
		if (!update_defer_init(pgdat, pfn, end_pfn, &nr_initialised))
			break;

#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
		/*
		 * Check given memblock attribute by firmware which can affect
		 * kernel memory layout.  If zone==ZONE_MOVABLE but memory is
		 * mirrored, it's an overlapped memmap init. skip it.
		 */
		if (mirrored_kernelcore && zone == ZONE_MOVABLE) {
			if (!r || pfn >= memblock_region_memory_end_pfn(r)) {
				for_each_memblock(memory, tmp)
					if (pfn < memblock_region_memory_end_pfn(tmp))
						break;
				r = tmp;
			}
			if (pfn >= memblock_region_memory_base_pfn(r) &&
			    memblock_is_mirror(r)) {
				/* already initialized as NORMAL */
				pfn = memblock_region_memory_end_pfn(r);
				continue;
			}
		}
#endif

not_early:
		/*
		 * Mark the block movable so that blocks are reserved for
		 * movable at startup. This will force kernel allocations
		 * to reserve their blocks rather than leaking throughout
		 * the address space during boot when many long-lived
		 * kernel allocations are made.
		 *
		 * bitmap is created for zone's valid pfn range. but memmap
		 * can be created for invalid pages (for alignment)
		 * check here not to call set_pageblock_migratetype() against
		 * pfn out of zone.
		 */
		if (!(pfn & (pageblock_nr_pages - 1))) {
			struct page *page = pfn_to_page(pfn);

			__init_single_page(page, pfn, zone, nid);
			set_pageblock_migratetype(page, MIGRATE_MOVABLE);
		} else {
			__init_single_pfn(pfn, zone, nid);
		}
	}
}

/*
 * 初始化空闲列表，空闲页的数目(nr_free)当前仍然规定为0，这显然没有反映真实
 * 的情况。直至停用bootmem分配器、普通的伙伴分配器生效，才会设置正确的数值。
 */
static void __meminit zone_init_free_lists(struct zone *zone)
{
	unsigned int order, t;
	for_each_migratetype_order(order, t) {
		INIT_LIST_HEAD(&zone->free_area[order].free_list[t]);
		zone->free_area[order].nr_free = 0;
	}
}

#ifndef __HAVE_ARCH_MEMMAP_INIT
#define memmap_init(size, nid, zone, start_pfn) \
	memmap_init_zone((size), (nid), (zone), (start_pfn), MEMMAP_EARLY)
#endif

static int zone_batchsize(struct zone *zone)
{
#ifdef CONFIG_MMU
	int batch;

	/*
	 * The per-cpu-pages pools are set to around 1000th of the
	 * size of the zone.  But no more than 1/2 of a meg.
	 *
	 * OK, so we don't know how big the cache is.  So guess.
	 */
	batch = zone->managed_pages / 1024;
	if (batch * PAGE_SIZE > 512 * 1024)
		batch = (512 * 1024) / PAGE_SIZE;
	batch /= 4;		/* We effectively *= 4 below */
	if (batch < 1)
		batch = 1;

	/*
	 * Clamp the batch to a 2^n - 1 value. Having a power
	 * of 2 value was found to be more likely to have
	 * suboptimal cache aliasing properties in some cases.
	 *
	 * For example if 2 tasks are alternately allocating
	 * batches of pages, one task can end up with a lot
	 * of pages of one half of the possible page colors
	 * and the other with pages of the other colors.
	 */
	batch = rounddown_pow_of_two(batch + batch/2) - 1;

	return batch;

#else
	/* The deferral and batching of frees should be suppressed under NOMMU
	 * conditions.
	 *
	 * The problem is that NOMMU needs to be able to allocate large chunks
	 * of contiguous memory as there's no hardware page translation to
	 * assemble apparent contiguous memory from discontiguous pages.
	 *
	 * Queueing large contiguous runs of pages for batching, however,
	 * causes the pages to actually be freed in smaller chunks.  As there
	 * can be a significant delay between the individual batches being
	 * recycled, this leads to the once large chunks of space being
	 * fragmented and becoming unavailable for high-order allocations.
	 */
	return 0;
#endif
}

/*
 * pcp->high and pcp->batch values are related and dependent on one another:
 * ->batch must never be higher then ->high.
 * The following function updates them in a safe manner without read side
 * locking.
 *
 * Any new users of pcp->batch and pcp->high should ensure they can cope with
 * those fields changing asynchronously (acording the the above rule).
 *
 * mutex_is_locked(&pcp_batch_high_lock) required when calling this function
 * outside of boot time (or some other assurance that no concurrent updaters
 * exist).
 */
static void pageset_update(struct per_cpu_pages *pcp, unsigned long high,
		unsigned long batch)
{
       /* start with a fail safe value for batch */
	pcp->batch = 1;
	smp_wmb();

       /* Update high, then batch, in order */
	pcp->high = high;
	smp_wmb();

	pcp->batch = batch;
}

/* a companion to pageset_set_high() */
static void pageset_set_batch(struct per_cpu_pageset *p, unsigned long batch)
{
	pageset_update(&p->pcp, 6 * batch, max(1UL, 1 * batch));
}

static void pageset_init(struct per_cpu_pageset *p)
{
	struct per_cpu_pages *pcp;
	int migratetype;

	memset(p, 0, sizeof(*p));

	pcp = &p->pcp;
	pcp->count = 0;
	for (migratetype = 0; migratetype < MIGRATE_PCPTYPES; migratetype++)
		INIT_LIST_HEAD(&pcp->lists[migratetype]);
}

static void setup_pageset(struct per_cpu_pageset *p, unsigned long batch)
{
	pageset_init(p);
	pageset_set_batch(p, batch);
}

/*
 * pageset_set_high() sets the high water mark for hot per_cpu_pagelist
 * to the value high for the pageset p.
 */
static void pageset_set_high(struct per_cpu_pageset *p,
				unsigned long high)
{
	unsigned long batch = max(1UL, high / 4);
	if ((high / 4) > (PAGE_SHIFT * 8))
		batch = PAGE_SHIFT * 8;

	pageset_update(&p->pcp, high, batch);
}

static void pageset_set_high_and_batch(struct zone *zone,
				       struct per_cpu_pageset *pcp)
{
	if (percpu_pagelist_fraction)
		pageset_set_high(pcp,
			(zone->managed_pages /
				percpu_pagelist_fraction));
	else
		pageset_set_batch(pcp, zone_batchsize(zone));
}

static void __meminit zone_pageset_init(struct zone *zone, int cpu)
{
	struct per_cpu_pageset *pcp = per_cpu_ptr(zone->pageset, cpu);

	pageset_init(pcp);
	pageset_set_high_and_batch(zone, pcp);
}

static void __meminit setup_zone_pageset(struct zone *zone)
{
	int cpu;
	//分配zone的pageset结构
	zone->pageset = alloc_percpu(struct per_cpu_pageset);
	for_each_possible_cpu(cpu)//初始化pageset的值。
		zone_pageset_init(zone, cpu);
}

/*
 * Allocate per cpu pagesets and initialize them.
 * Before this call only boot pagesets were available.
 */
/*
初始化CPU高速缓存行, 为pagesets的第一个数组元素分配内存,
换句话说, 其实就是第一个系统处理器分配

由于在分页情况下，每次存储器访问都要存取多级页表，
这就大大降低了访问速度。所以，为了提高速度，
在CPU中设置一个最近存取页面的高速缓存硬件机制，
当进行存储器访问时，先检查要访问的页面是否在高速缓存中.
*/
void __init setup_per_cpu_pageset(void)
{
	struct pglist_data *pgdat;
	struct zone *zone;

	//遍历所有zone
	for_each_populated_zone(zone)
		//设置每个zone的每CPU缓存。
		setup_zone_pageset(zone);

	for_each_online_pgdat(pgdat)
		pgdat->per_cpu_nodestats =
			alloc_percpu(struct per_cpu_nodestat);
}
// 尝试初始化该内存域的per-CPU缓存
static __meminit void zone_pcp_init(struct zone *zone)
{
	/*
	 * per cpu subsystem is not up at this point. The following code
	 * relies on the ability of the linker to provide the
	 * offset of a (static) per cpu variable into the per cpu area.
	 */
	zone->pageset = &boot_pageset;

	if (populated_zone(zone))
		printk(KERN_DEBUG "  %s zone: %lu pages, LIFO batch:%u\n",
			zone->name, zone->present_pages,
					 zone_batchsize(zone));
}

/*
初始化free_area列表，并将属于该内存域的所有page实例都设置为初始默认值。
正如前文的讨论，调用了memmap_init_zone来初始化内存域的页,
我们还可以回想前文提到的，所有页属性起初都设置MIGRATE_MOVABLE
*/
int __meminit init_currently_empty_zone(struct zone *zone,
					unsigned long zone_start_pfn,
					unsigned long size)
{
	struct pglist_data *pgdat = zone->zone_pgdat;

	//设置内存区中的zone个数，这个感觉有点奇怪
	pgdat->nr_zones = zone_idx(zone) + 1;

	//zone的起始pfn
	zone->zone_start_pfn = zone_start_pfn;

	mminit_dprintk(MMINIT_TRACE, "memmap_init",
			"Initialising map node %d zone %lu pfns %lu -> %lu\n",
			pgdat->node_id,
			(unsigned long)zone_idx(zone),
			zone_start_pfn, (zone_start_pfn + size));

	//初始化free_area链表，并将空闲页面数量设置为0.
	zone_init_free_lists(zone);
	zone->initialized = 1;

	return 0;
}

#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
#ifndef CONFIG_HAVE_ARCH_EARLY_PFN_TO_NID

/*
 * Required by SPARSEMEM. Given a PFN, return what node the PFN is on.
 */
int __meminit __early_pfn_to_nid(unsigned long pfn,
					struct mminit_pfnnid_cache *state)
{
	unsigned long start_pfn, end_pfn;
	int nid;

	if (state->last_start <= pfn && pfn < state->last_end)
		return state->last_nid;

	nid = memblock_search_pfn_nid(pfn, &start_pfn, &end_pfn);
	if (nid != -1) {
		state->last_start = start_pfn;
		state->last_end = end_pfn;
		state->last_nid = nid;
	}

	return nid;
}
#endif /* CONFIG_HAVE_ARCH_EARLY_PFN_TO_NID */

/**
 * free_bootmem_with_active_regions - Call memblock_free_early_nid for each active range
 * @nid: The node to free memory on. If MAX_NUMNODES, all nodes are freed.
 * @max_low_pfn: The highest PFN that will be passed to memblock_free_early_nid
 *
 * If an architecture guarantees that all ranges registered contain no holes
 * and may be freed, this this function may be used instead of calling
 * memblock_free_early_nid() manually.
 */
void __init free_bootmem_with_active_regions(int nid, unsigned long max_low_pfn)
{
	unsigned long start_pfn, end_pfn;
	int i, this_nid;

	for_each_mem_pfn_range(i, nid, &start_pfn, &end_pfn, &this_nid) {
		start_pfn = min(start_pfn, max_low_pfn);
		end_pfn = min(end_pfn, max_low_pfn);

		if (start_pfn < end_pfn)
			memblock_free_early_nid(PFN_PHYS(start_pfn),
					(end_pfn - start_pfn) << PAGE_SHIFT,
					this_nid);
	}
}

/**
 * sparse_memory_present_with_active_regions - Call memory_present for each active range
 * @nid: The node to call memory_present for. If MAX_NUMNODES, all nodes will be used.
 *
 * If an architecture guarantees that all ranges registered contain no holes and may
 * be freed, this function may be used instead of calling memory_present() manually.
 */
void __init sparse_memory_present_with_active_regions(int nid)
{
	unsigned long start_pfn, end_pfn;
	int i, this_nid;

	for_each_mem_pfn_range(i, nid, &start_pfn, &end_pfn, &this_nid)
		memory_present(this_nid, start_pfn, end_pfn);
}

/**
 * get_pfn_range_for_nid - Return the start and end page frames for a node
 * @nid: The nid to return the range for. If MAX_NUMNODES, the min and max PFN are returned.
 * @start_pfn: Passed by reference. On return, it will have the node start_pfn.
 * @end_pfn: Passed by reference. On return, it will have the node end_pfn.
 *
 * It returns the start and end page frame of a node based on information
 * provided by memblock_set_node(). If called for a node
 * with no available memory, a warning is printed and the start and end
 * PFNs will be 0.
 */
void __meminit get_pfn_range_for_nid(unsigned int nid,
			unsigned long *start_pfn, unsigned long *end_pfn)
{
	unsigned long this_start_pfn, this_end_pfn;
	int i;

	*start_pfn = -1UL;
	*end_pfn = 0;

	for_each_mem_pfn_range(i, nid, &this_start_pfn, &this_end_pfn, NULL) {
		*start_pfn = min(*start_pfn, this_start_pfn);
		*end_pfn = max(*end_pfn, this_end_pfn);
	}

	if (*start_pfn == -1UL)
		*start_pfn = 0;
}

/*
 * This finds a zone that can be used for ZONE_MOVABLE pages. The
 * assumption is made that zones within a node are ordered in monotonic
 * increasing memory addresses so that the "highest" populated zone is used
 */
static void __init find_usable_zone_for_movable(void)
{
	int zone_index;
	for (zone_index = MAX_NR_ZONES - 1; zone_index >= 0; zone_index--) {
		if (zone_index == ZONE_MOVABLE)
			continue;

		if (arch_zone_highest_possible_pfn[zone_index] >
				arch_zone_lowest_possible_pfn[zone_index])
			break;
	}

	VM_BUG_ON(zone_index == -1);
	movable_zone = zone_index;
}

/*
 * The zone ranges provided by the architecture do not include ZONE_MOVABLE
 * because it is sized independent of architecture. Unlike the other zones,
 * the starting point for ZONE_MOVABLE is not fixed. It may be different
 * in each node depending on the size of each node and how evenly kernelcore
 * is distributed. This helper function adjusts the zone ranges
 * provided by the architecture for a given node by using the end of the
 * highest usable zone for ZONE_MOVABLE. This preserves the assumption that
 * zones within a node are in order of monotonic increases memory addresses
 */
static void __meminit adjust_zone_range_for_zone_movable(int nid,
					unsigned long zone_type,
					unsigned long node_start_pfn,
					unsigned long node_end_pfn,
					unsigned long *zone_start_pfn,
					unsigned long *zone_end_pfn)
{
	/* Only adjust if ZONE_MOVABLE is on this node */
	if (zone_movable_pfn[nid]) {
		/* Size ZONE_MOVABLE */
		if (zone_type == ZONE_MOVABLE) {
			*zone_start_pfn = zone_movable_pfn[nid];
			*zone_end_pfn = min(node_end_pfn,
				arch_zone_highest_possible_pfn[movable_zone]);

		/* Adjust for ZONE_MOVABLE starting within this range */
		} else if (!mirrored_kernelcore &&
			*zone_start_pfn < zone_movable_pfn[nid] &&
			*zone_end_pfn > zone_movable_pfn[nid]) {
			*zone_end_pfn = zone_movable_pfn[nid];

		/* Check if this whole range is within ZONE_MOVABLE */
		} else if (*zone_start_pfn >= zone_movable_pfn[nid])
			*zone_start_pfn = *zone_end_pfn;
	}
}

/*
 * Return the number of pages a zone spans in a node, including holes
 * present_pages = zone_spanned_pages_in_node() - zone_absent_pages_in_node()
 */
static unsigned long __meminit zone_spanned_pages_in_node(int nid,
					unsigned long zone_type,
					unsigned long node_start_pfn,
					unsigned long node_end_pfn,
					unsigned long *zone_start_pfn,
					unsigned long *zone_end_pfn,
					unsigned long *ignored)
{
	/* When hotadd a new node from cpu_up(), the node should be empty */
	if (!node_start_pfn && !node_end_pfn)
		return 0;

	/* Get the start and end of the zone */
	*zone_start_pfn = arch_zone_lowest_possible_pfn[zone_type];
	*zone_end_pfn = arch_zone_highest_possible_pfn[zone_type];
	adjust_zone_range_for_zone_movable(nid, zone_type,
				node_start_pfn, node_end_pfn,
				zone_start_pfn, zone_end_pfn);

	/* Check that this node has pages within the zone's required range */
	if (*zone_end_pfn < node_start_pfn || *zone_start_pfn > node_end_pfn)
		return 0;

	/* Move the zone boundaries inside the node if necessary */
	*zone_end_pfn = min(*zone_end_pfn, node_end_pfn);
	*zone_start_pfn = max(*zone_start_pfn, node_start_pfn);

	/* Return the spanned pages */
	return *zone_end_pfn - *zone_start_pfn;
}

/*
 * Return the number of holes in a range on a node. If nid is MAX_NUMNODES,
 * then all holes in the requested range will be accounted for.
 */
unsigned long __meminit __absent_pages_in_range(int nid,
				unsigned long range_start_pfn,
				unsigned long range_end_pfn)
{
	unsigned long nr_absent = range_end_pfn - range_start_pfn;
	unsigned long start_pfn, end_pfn;
	int i;

	for_each_mem_pfn_range(i, nid, &start_pfn, &end_pfn, NULL) {
		start_pfn = clamp(start_pfn, range_start_pfn, range_end_pfn);
		end_pfn = clamp(end_pfn, range_start_pfn, range_end_pfn);
		nr_absent -= end_pfn - start_pfn;
	}
	return nr_absent;
}

/**
 * absent_pages_in_range - Return number of page frames in holes within a range
 * @start_pfn: The start PFN to start searching for holes
 * @end_pfn: The end PFN to stop searching for holes
 *
 * It returns the number of pages frames in memory holes within a range.
 */
unsigned long __init absent_pages_in_range(unsigned long start_pfn,
							unsigned long end_pfn)
{
	return __absent_pages_in_range(MAX_NUMNODES, start_pfn, end_pfn);
}

/* Return the number of page frames in holes in a zone on a node */
static unsigned long __meminit zone_absent_pages_in_node(int nid,
					unsigned long zone_type,
					unsigned long node_start_pfn,
					unsigned long node_end_pfn,
					unsigned long *ignored)
{
	unsigned long zone_low = arch_zone_lowest_possible_pfn[zone_type];
	unsigned long zone_high = arch_zone_highest_possible_pfn[zone_type];
	unsigned long zone_start_pfn, zone_end_pfn;
	unsigned long nr_absent;

	/* When hotadd a new node from cpu_up(), the node should be empty */
	if (!node_start_pfn && !node_end_pfn)
		return 0;

	zone_start_pfn = clamp(node_start_pfn, zone_low, zone_high);
	zone_end_pfn = clamp(node_end_pfn, zone_low, zone_high);

	adjust_zone_range_for_zone_movable(nid, zone_type,
			node_start_pfn, node_end_pfn,
			&zone_start_pfn, &zone_end_pfn);
	nr_absent = __absent_pages_in_range(nid, zone_start_pfn, zone_end_pfn);

	/*
	 * ZONE_MOVABLE handling.
	 * Treat pages to be ZONE_MOVABLE in ZONE_NORMAL as absent pages
	 * and vice versa.
	 */
	if (mirrored_kernelcore && zone_movable_pfn[nid]) {
		unsigned long start_pfn, end_pfn;
		struct memblock_region *r;

		for_each_memblock(memory, r) {
			start_pfn = clamp(memblock_region_memory_base_pfn(r),
					  zone_start_pfn, zone_end_pfn);
			end_pfn = clamp(memblock_region_memory_end_pfn(r),
					zone_start_pfn, zone_end_pfn);

			if (zone_type == ZONE_MOVABLE &&
			    memblock_is_mirror(r))
				nr_absent += end_pfn - start_pfn;

			if (zone_type == ZONE_NORMAL &&
			    !memblock_is_mirror(r))
				nr_absent += end_pfn - start_pfn;
		}
	}

	return nr_absent;
}

#else /* CONFIG_HAVE_MEMBLOCK_NODE_MAP */
static inline unsigned long __meminit zone_spanned_pages_in_node(int nid,
					unsigned long zone_type,
					unsigned long node_start_pfn,
					unsigned long node_end_pfn,
					unsigned long *zone_start_pfn,
					unsigned long *zone_end_pfn,
					unsigned long *zones_size)
{
	unsigned int zone;

	*zone_start_pfn = node_start_pfn;
	for (zone = 0; zone < zone_type; zone++)
		*zone_start_pfn += zones_size[zone];

	*zone_end_pfn = *zone_start_pfn + zones_size[zone_type];

	return zones_size[zone_type];
}

static inline unsigned long __meminit zone_absent_pages_in_node(int nid,
						unsigned long zone_type,
						unsigned long node_start_pfn,
						unsigned long node_end_pfn,
						unsigned long *zholes_size)
{
	if (!zholes_size)
		return 0;

	return zholes_size[zone_type];
}

#endif /* CONFIG_HAVE_MEMBLOCK_NODE_MAP */
/*
累计各个内存域的页数，计算结点中页的总数。
对连续内存模型而言，这可以通过zone_sizes_init完成，
但calculate_node_totalpages还考虑了内存空洞
*/
static void __meminit calculate_node_totalpages(struct pglist_data *pgdat,
						unsigned long node_start_pfn,
						unsigned long node_end_pfn,
						unsigned long *zones_size,
						unsigned long *zholes_size)
{
	unsigned long realtotalpages = 0, totalpages = 0;
	enum zone_type i;

	for (i = 0; i < MAX_NR_ZONES; i++) {
		struct zone *zone = pgdat->node_zones + i;
		unsigned long zone_start_pfn, zone_end_pfn;
		unsigned long size, real_size;

		size = zone_spanned_pages_in_node(pgdat->node_id, i,
						  node_start_pfn,
						  node_end_pfn,
						  &zone_start_pfn,
						  &zone_end_pfn,
						  zones_size);
		real_size = size - zone_absent_pages_in_node(pgdat->node_id, i,
						  node_start_pfn, node_end_pfn,
						  zholes_size);
		if (size)
			zone->zone_start_pfn = zone_start_pfn;
		else
			zone->zone_start_pfn = 0;
		zone->spanned_pages = size;
		zone->present_pages = real_size;

		totalpages += size;
		realtotalpages += real_size;
	}

	pgdat->node_spanned_pages = totalpages;
	pgdat->node_present_pages = realtotalpages;
	printk(KERN_DEBUG "On node %d totalpages: %lu\n", pgdat->node_id,
							realtotalpages);
}

#ifndef CONFIG_SPARSEMEM
/*
 * Calculate the size of the zone->blockflags rounded to an unsigned long
 * Start by making sure zonesize is a multiple of pageblock_order by rounding
 * up. Then use 1 NR_PAGEBLOCK_BITS worth of bits per pageblock, finally
 * round what is now in bits to nearest long in bits, then return it in
 * bytes.
 */
static unsigned long __init usemap_size(unsigned long zone_start_pfn, unsigned long zonesize)
{
	unsigned long usemapsize;

	zonesize += zone_start_pfn & (pageblock_nr_pages-1);
	usemapsize = roundup(zonesize, pageblock_nr_pages);
	usemapsize = usemapsize >> pageblock_order;
	usemapsize *= NR_PAGEBLOCK_BITS;
	usemapsize = roundup(usemapsize, 8 * sizeof(unsigned long));

	return usemapsize / 8;
}

static void __init setup_usemap(struct pglist_data *pgdat,
				struct zone *zone,
				unsigned long zone_start_pfn,
				unsigned long zonesize)
{
	unsigned long usemapsize = usemap_size(zone_start_pfn, zonesize);
	zone->pageblock_flags = NULL;
	if (usemapsize)
		zone->pageblock_flags =
			memblock_virt_alloc_node_nopanic(usemapsize,
							 pgdat->node_id);
}
#else
static inline void setup_usemap(struct pglist_data *pgdat, struct zone *zone,
				unsigned long zone_start_pfn, unsigned long zonesize) {}
#endif /* CONFIG_SPARSEMEM */

#ifdef CONFIG_HUGETLB_PAGE_SIZE_VARIABLE

/* Initialise the number of pages represented by NR_PAGEBLOCK_BITS */
void __paginginit set_pageblock_order(void)
{
	unsigned int order;

	/* Check that pageblock_nr_pages has not already been setup */
	if (pageblock_order)
		return;

	if (HPAGE_SHIFT > PAGE_SHIFT)
		order = HUGETLB_PAGE_ORDER;
	else
		order = MAX_ORDER - 1;

	/*
	 * Assume the largest contiguous order of interest is a huge page.
	 * This value may be variable depending on boot parameters on IA64 and
	 * powerpc.
	 */
	pageblock_order = order;
}
#else /* CONFIG_HUGETLB_PAGE_SIZE_VARIABLE */

/*
 * When CONFIG_HUGETLB_PAGE_SIZE_VARIABLE is not set, set_pageblock_order()
 * is unused as pageblock_order is set at compile-time. See
 * include/linux/pageblock-flags.h for the values of pageblock_order based on
 * the kernel config
 */
void __paginginit set_pageblock_order(void)
{
}

#endif /* CONFIG_HUGETLB_PAGE_SIZE_VARIABLE */

static unsigned long __paginginit calc_memmap_size(unsigned long spanned_pages,
						   unsigned long present_pages)
{
	unsigned long pages = spanned_pages;

	/*
	 * Provide a more accurate estimation if there are holes within
	 * the zone and SPARSEMEM is in use. If there are holes within the
	 * zone, each populated memory region may cost us one or two extra
	 * memmap pages due to alignment because memmap pages for each
	 * populated regions may not naturally algined on page boundary.
	 * So the (present_pages >> 4) heuristic is a tradeoff for that.
	 */
	if (spanned_pages > present_pages + (present_pages >> 4) &&
	    IS_ENABLED(CONFIG_SPARSEMEM))
		pages = present_pages;

	return PAGE_ALIGN(pages * sizeof(struct page)) >> PAGE_SHIFT;
}

/*
 * Set up the zone data structures:
 *   - mark all pages reserved
 *   - mark all memory queues empty
 *   - clear the memory bitmaps
 *
 * NOTE: pgdat should get zeroed by caller.
 */
/*
初始化一个节点的所有页面.
*/
static void __paginginit free_area_init_core(struct pglist_data *pgdat)
{
	enum zone_type j;
	int nid = pgdat->node_id;
	int ret;

	pgdat_resize_init(pgdat);
#ifdef CONFIG_NUMA_BALANCING
	spin_lock_init(&pgdat->numabalancing_migrate_lock);
	pgdat->numabalancing_migrate_nr_pages = 0;
	pgdat->numabalancing_migrate_next_window = jiffies;
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	spin_lock_init(&pgdat->split_queue_lock);
	INIT_LIST_HEAD(&pgdat->split_queue);
	pgdat->split_queue_len = 0;
#endif
    /*  初始化pgdat->kswapd_wait等待队列  */
	init_waitqueue_head(&pgdat->kswapd_wait);
    /*  初始化页换出守护进程创建空闲块的大小
           为2^kswapd_max_order  */
	init_waitqueue_head(&pgdat->pfmemalloc_wait);
#ifdef CONFIG_COMPACTION
	init_waitqueue_head(&pgdat->kcompactd_wait);
#endif
	pgdat_page_ext_init(pgdat);
	spin_lock_init(&pgdat->lru_lock);
	lruvec_init(node_lruvec(pgdat));
    /* 遍历每个管理区 */
	for (j = 0; j < MAX_NR_ZONES; j++) {
		struct zone *zone = pgdat->node_zones + j;
		unsigned long size, realsize, freesize, memmap_pages;
		unsigned long zone_start_pfn = zone->zone_start_pfn;

        /*  size为该管理区中的页框数，包括洞 */
		size = zone->spanned_pages;
        /* realsize为管理区中的页框数，不包括洞*/
		realsize = freesize = zone->present_pages;

		/*
		 * Adjust freesize so that it accounts for how much memory
		 * is used by this zone for memmap. This affects the watermark
		 * and per-cpu initialisations
		 */
/*
	     调整realsize的大小，即减去page结构体占用的内存大小
           memmap_pags为包括洞的所有页框的page结构体所占的大小
*/
		memmap_pages = calc_memmap_size(size, realsize);
		if (!is_highmem_idx(j)) {
			if (freesize >= memmap_pages) {
				freesize -= memmap_pages;
				if (memmap_pages)
					printk(KERN_DEBUG
					       "  %s zone: %lu pages used for memmap\n",
					       zone_names[j], memmap_pages);
			} else /*  内存不够存放page结构体  */
				pr_warn("  %s zone: %lu pages exceeds freesize %lu\n",
					zone_names[j], memmap_pages, freesize);
		}

		/* Account for reserved pages */
        /* 调整realsize的大小，即减去DMA保留页的大小 */
		if (j == 0 && freesize > dma_reserve) {
			freesize -= dma_reserve;
			printk(KERN_DEBUG "  %s zone: %lu pages reserved\n",
					zone_names[0], dma_reserve);
		}

		if (!is_highmem_idx(j))
			nr_kernel_pages += freesize;
		/* Charge for highmem memmap if there are enough kernel pages */
		else if (nr_kernel_pages > memmap_pages * 2)
			nr_kernel_pages -= memmap_pages;
		nr_all_pages += freesize;

		/*
		 * Set an approximate value for lowmem here, it will be adjusted
		 * when the bootmem allocator frees pages into the buddy system.
		 * And all highmem pages will be managed by the buddy system.
		 */
		 /* 设置zone->spanned_pages为包括洞的页框数  */
		zone->managed_pages = is_highmem_idx(j) ? realsize : freesize;
#ifdef CONFIG_NUMA
        /* 设置zone中的节点标识符 */
		zone->node = nid;
#endif
        /*  设置zone的名称  */
		zone->name = zone_names[j];
        /* 设置管理区属于的节点对应的pg_data_t结构 */
		zone->zone_pgdat = pgdat;
        /* 初始化各种锁 */
		spin_lock_init(&zone->lock);
		zone_seqlock_init(zone);
		/*
		 * 初始化该内存域的per-CPU缓存
		 */
		zone_pcp_init(zone);

		if (!size)
			continue;

		set_pageblock_order();
        /* 定义了CONFIG_SPARSEMEM该函数为空 */
		setup_usemap(pgdat, zone, zone_start_pfn, size);
        /* 设置pgdat->nr_zones和zone->zone_start_pfn成员
             * 初始化zone->free_area成员
             * 初始化zone->wait_table相关成员
        */
		/*
		 * 初始化free_area列表，并将属于该内存域的所有page实例都设置为初始默认值。
		 */
		ret = init_currently_empty_zone(zone, zone_start_pfn, size);
		BUG_ON(ret);
        /* 初始化该zone对应的page结构 */
		/*
		 * 初始化内存域的页，所有页属性都初始化为MIGRATE_MOVABLE
		 */
		memmap_init(size, nid, j, zone_start_pfn);
	}
}

/*
 * alloc_node_mem_map()负责初始化一个简单但非常重要
 * 的数据结构。系统中的各个物理内存页，都对应着一个
 * struct page实例。该结构的初始化由alloc_node_mem_map()执行
 */
static void __ref alloc_node_mem_map(struct pglist_data *pgdat)
{
	unsigned long __maybe_unused start = 0;
	unsigned long __maybe_unused offset = 0;

	/* Skip empty nodes */
	if (!pgdat->node_spanned_pages)
		return;

#ifdef CONFIG_FLAT_NODE_MEM_MAP
	start = pgdat->node_start_pfn & ~(MAX_ORDER_NR_PAGES - 1);
	offset = pgdat->node_start_pfn - start;
	/* ia64 gets its own node_mem_map, before this, without bootmem */
	if (!pgdat->node_mem_map) {
		unsigned long size, end;
		struct page *map;

		/*
		 * The zone's endpoints aren't required to be MAX_ORDER
		 * aligned but the node_mem_map endpoints must be in order
		 * for the buddy allocator to function correctly.
		 */
		end = pgdat_end_pfn(pgdat);
		end = ALIGN(end, MAX_ORDER_NR_PAGES);
		size =  (end - start) * sizeof(struct page);
		map = alloc_remap(pgdat->node_id, size);
		if (!map)
			map = memblock_virt_alloc_node_nopanic(size,
							       pgdat->node_id);
		pgdat->node_mem_map = map + offset;
	}
#ifndef CONFIG_NEED_MULTIPLE_NODES
	/*
	 * With no DISCONTIG, the global mem_map is just set as node 0's
	 */
	/*
	 * 指向该空间的指针不仅保存在pglist_data实例中，
	 * 还保存在全局变量mem_map中，前提是当前考察的结点
	 * 是系统的第0个结点(如果系统只有一个内存结点，则总是这样)
	 */
	if (pgdat == NODE_DATA(0)) {
		mem_map = NODE_DATA(0)->node_mem_map;
#if defined(CONFIG_HAVE_MEMBLOCK_NODE_MAP) || defined(CONFIG_FLATMEM)
		if (page_to_pfn(mem_map) != pgdat->node_start_pfn)
			mem_map -= offset;
#endif /* CONFIG_HAVE_MEMBLOCK_NODE_MAP */
	}
#endif
#endif /* CONFIG_FLAT_NODE_MEM_MAP */
}

void __paginginit free_area_init_node(int nid, unsigned long *zones_size,
		unsigned long node_start_pfn, unsigned long *zholes_size)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	unsigned long start_pfn = 0;
	unsigned long end_pfn = 0;

	/* pg_data_t should be reset to zero when it's allocated */
	WARN_ON(pgdat->nr_zones || pgdat->kswapd_classzone_idx);

	reset_deferred_meminit(pgdat);
	//设置内存区的节点号和起始pfn
	pgdat->node_id = nid;
	pgdat->node_start_pfn = node_start_pfn;
	pgdat->per_cpu_nodestats = NULL;
#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
	get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);
	pr_info("Initmem setup node %d [mem %#018Lx-%#018Lx]\n", nid,
		(u64)start_pfn << PAGE_SHIFT,
		end_pfn ? ((u64)end_pfn << PAGE_SHIFT) - 1 : 0);
#else
	start_pfn = node_start_pfn;
#endif
    /*  首先累计各个内存域的页数
         *    计算结点中页的总数
         *    对连续内存模型而言
         *    这可以通过zone_sizes_init完成
         *    但calculate_node_totalpages还考虑了内存空洞 */
	calculate_node_totalpages(pgdat, start_pfn, end_pfn,
				  zones_size, zholes_size);

    /*  分配了该节点的页面描述符数组
        *  [pgdat->node_mem_map数组的内存分配  */
	/*
	 * alloc_node_mem_map()负责初始化一个简单但非常重要的数据结构。
	 * 系统中的各个物理内存页，都对应着一个struct page实例。该结构的
	 * 初始化由alloc_node_mem_map()执行
	 */
	alloc_node_mem_map(pgdat);
#ifdef CONFIG_FLAT_NODE_MEM_MAP
	printk(KERN_DEBUG "free_area_init_node: node %d, pgdat %08lx, node_mem_map %08lx\n",
		nid, (unsigned long)pgdat,
		(unsigned long)pgdat->node_mem_map);
#endif

	//初始化zone，设置zone的free_area链表，标记所有页面不可用。
	/*  对该节点的每个区[DMA,NORMAL,HIGH]的的结构进行初始化  */
		/*
	 * 初始化内存域数据结构
	 */
	/* 这里真正对节点内存域的数据结构进行初始化 */
	free_area_init_core(pgdat);
}

#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP

#if MAX_NUMNODES > 1
/*
 * Figure out the number of possible node ids.
 */
void __init setup_nr_node_ids(void)
{
	unsigned int highest;

	highest = find_last_bit(node_possible_map.bits, MAX_NUMNODES);
	nr_node_ids = highest + 1;
}
#endif

/**
 * node_map_pfn_alignment - determine the maximum internode alignment
 *
 * This function should be called after node map is populated and sorted.
 * It calculates the maximum power of two alignment which can distinguish
 * all the nodes.
 *
 * For example, if all nodes are 1GiB and aligned to 1GiB, the return value
 * would indicate 1GiB alignment with (1 << (30 - PAGE_SHIFT)).  If the
 * nodes are shifted by 256MiB, 256MiB.  Note that if only the last node is
 * shifted, 1GiB is enough and this function will indicate so.
 *
 * This is used to test whether pfn -> nid mapping of the chosen memory
 * model has fine enough granularity to avoid incorrect mapping for the
 * populated node map.
 *
 * Returns the determined alignment in pfn's.  0 if there is no alignment
 * requirement (single node).
 */
unsigned long __init node_map_pfn_alignment(void)
{
	unsigned long accl_mask = 0, last_end = 0;
	unsigned long start, end, mask;
	int last_nid = -1;
	int i, nid;

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start, &end, &nid) {
		if (!start || last_nid < 0 || last_nid == nid) {
			last_nid = nid;
			last_end = end;
			continue;
		}

		/*
		 * Start with a mask granular enough to pin-point to the
		 * start pfn and tick off bits one-by-one until it becomes
		 * too coarse to separate the current node from the last.
		 */
		mask = ~((1 << __ffs(start)) - 1);
		while (mask && last_end <= (start & (mask << 1)))
			mask <<= 1;

		/* accumulate all internode masks */
		accl_mask |= mask;
	}

	/* convert mask to number of pages */
	return ~accl_mask + 1;
}

/* Find the lowest pfn for a node */
static unsigned long __init find_min_pfn_for_node(int nid)
{
	unsigned long min_pfn = ULONG_MAX;
	unsigned long start_pfn;
	int i;

	for_each_mem_pfn_range(i, nid, &start_pfn, NULL, NULL)
		min_pfn = min(min_pfn, start_pfn);

	if (min_pfn == ULONG_MAX) {
		pr_warn("Could not find start_pfn for node %d\n", nid);
		return 0;
	}

	return min_pfn;
}

/**
 * find_min_pfn_with_active_regions - Find the minimum PFN registered
 *
 * It returns the minimum PFN based on information provided via
 * memblock_set_node().
 */
/*
 * 用于找到注册的最低内存域中可用的编号最小的页帧。该内存域不一定是ZONE_DMA，
 * 例如，在计算机不需要DMA内存的情况下也可以是ZONE_NORMAL。最低内存域的最大
 * 页帧号可以从max_zone_pfn提供的信息直接获得。
 */
unsigned long __init find_min_pfn_with_active_regions(void)
{
	return find_min_pfn_for_node(MAX_NUMNODES);
}

/*
 * early_calculate_totalpages()
 * Sum pages in active regions for movable zone.
 * Populate N_MEMORY for calculating usable_nodes.
 */
static unsigned long __init early_calculate_totalpages(void)
{
	unsigned long totalpages = 0;
	unsigned long start_pfn, end_pfn;
	int i, nid;

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, &nid) {
		unsigned long pages = end_pfn - start_pfn;

		totalpages += pages;
		if (pages)
			node_set_state(nid, N_MEMORY);
	}
	return totalpages;
}

/*
 * Find the PFN the Movable zone begins in each node. Kernel memory
 * is spread evenly between nodes as long as the nodes have enough
 * memory. When they don't, some nodes will have more kernelcore than
 * others
 */
/*
 * 用于计算进入ZONE_MOVABLE的内存数量。如果kernelcore和movablecore参数都
 * 没有指定，find_zone_movable_pfns_for_nodes()会使ZONE_MOVABLE保持为空，
 * 该机制处于无效状态。谈到从物理内存域提取多少内存用于ZONE_MOVABLE的问题，
 * 必须考虑下面两种情况:
 *   1)用于不可移动分配的内存会平均地分布到所有内存结点上
 *   2)只使用来自最高内存域的内存。在内存较多的32位系统上，
 * 这通常会是ZONE_HIGHMEM，但是对于64位系统，将使用ZONE_NORMAL或ZONE_DMA32。
 * 实际上其作用的效果是:
 *   1)用于为虚拟内存域ZONE_MOVABLE提取内存页的物理内存域，保存在全局变量movable_zone中
 *   2)对每个结点来说，zone_movable_pfn[node_id]表示ZONE_MOVABLE在
 * movable_zone内存域中所取得内存的起始地址。
 * 内核确保这些页将用于满足符合ZONE_MOVABLE职责的内存分配。
 */
static void __init find_zone_movable_pfns_for_nodes(void)
{
	int i, nid;
	unsigned long usable_startpfn;
	unsigned long kernelcore_node, kernelcore_remaining;
	/* save the state before borrow the nodemask */
	nodemask_t saved_node_state = node_states[N_MEMORY];
	unsigned long totalpages = early_calculate_totalpages();
	int usable_nodes = nodes_weight(node_states[N_MEMORY]);
	struct memblock_region *r;

	/* Need to find movable_zone earlier when movable_node is specified. */
	find_usable_zone_for_movable();

	/*
	 * If movable_node is specified, ignore kernelcore and movablecore
	 * options.
	 */
	if (movable_node_is_enabled()) {
		for_each_memblock(memory, r) {
			if (!memblock_is_hotpluggable(r))
				continue;

			nid = r->nid;

			usable_startpfn = PFN_DOWN(r->base);
			zone_movable_pfn[nid] = zone_movable_pfn[nid] ?
				min(usable_startpfn, zone_movable_pfn[nid]) :
				usable_startpfn;
		}

		goto out2;
	}

	/*
	 * If kernelcore=mirror is specified, ignore movablecore option
	 */
	if (mirrored_kernelcore) {
		bool mem_below_4gb_not_mirrored = false;

		for_each_memblock(memory, r) {
			if (memblock_is_mirror(r))
				continue;

			nid = r->nid;

			usable_startpfn = memblock_region_memory_base_pfn(r);

			if (usable_startpfn < 0x100000) {
				mem_below_4gb_not_mirrored = true;
				continue;
			}

			zone_movable_pfn[nid] = zone_movable_pfn[nid] ?
				min(usable_startpfn, zone_movable_pfn[nid]) :
				usable_startpfn;
		}

		if (mem_below_4gb_not_mirrored)
			pr_warn("This configuration results in unmirrored kernel memory.");

		goto out2;
	}

	/*
	 * If movablecore=nn[KMG] was specified, calculate what size of
	 * kernelcore that corresponds so that memory usable for
	 * any allocation type is evenly spread. If both kernelcore
	 * and movablecore are specified, then the value of kernelcore
	 * will be used for required_kernelcore if it's greater than
	 * what movablecore would have allowed.
	 */
	if (required_movablecore) {
		unsigned long corepages;

		/*
		 * Round-up so that ZONE_MOVABLE is at least as large as what
		 * was requested by the user
		 */
		required_movablecore =
			roundup(required_movablecore, MAX_ORDER_NR_PAGES);
		required_movablecore = min(totalpages, required_movablecore);
		corepages = totalpages - required_movablecore;

		required_kernelcore = max(required_kernelcore, corepages);
	}

	/*
	 * If kernelcore was not specified or kernelcore size is larger
	 * than totalpages, there is no ZONE_MOVABLE.
	 */
	if (!required_kernelcore || required_kernelcore >= totalpages)
		goto out;

	/* usable_startpfn is the lowest possible pfn ZONE_MOVABLE can be at */
	usable_startpfn = arch_zone_lowest_possible_pfn[movable_zone];

restart:
	/* Spread kernelcore memory as evenly as possible throughout nodes */
	kernelcore_node = required_kernelcore / usable_nodes;
	for_each_node_state(nid, N_MEMORY) {
		unsigned long start_pfn, end_pfn;

		/*
		 * Recalculate kernelcore_node if the division per node
		 * now exceeds what is necessary to satisfy the requested
		 * amount of memory for the kernel
		 */
		if (required_kernelcore < kernelcore_node)
			kernelcore_node = required_kernelcore / usable_nodes;

		/*
		 * As the map is walked, we track how much memory is usable
		 * by the kernel using kernelcore_remaining. When it is
		 * 0, the rest of the node is usable by ZONE_MOVABLE
		 */
		kernelcore_remaining = kernelcore_node;

		/* Go through each range of PFNs within this node */
		for_each_mem_pfn_range(i, nid, &start_pfn, &end_pfn, NULL) {
			unsigned long size_pages;

			start_pfn = max(start_pfn, zone_movable_pfn[nid]);
			if (start_pfn >= end_pfn)
				continue;

			/* Account for what is only usable for kernelcore */
			if (start_pfn < usable_startpfn) {
				unsigned long kernel_pages;
				kernel_pages = min(end_pfn, usable_startpfn)
								- start_pfn;

				kernelcore_remaining -= min(kernel_pages,
							kernelcore_remaining);
				required_kernelcore -= min(kernel_pages,
							required_kernelcore);

				/* Continue if range is now fully accounted */
				if (end_pfn <= usable_startpfn) {

					/*
					 * Push zone_movable_pfn to the end so
					 * that if we have to rebalance
					 * kernelcore across nodes, we will
					 * not double account here
					 */
					zone_movable_pfn[nid] = end_pfn;
					continue;
				}
				start_pfn = usable_startpfn;
			}

			/*
			 * The usable PFN range for ZONE_MOVABLE is from
			 * start_pfn->end_pfn. Calculate size_pages as the
			 * number of pages used as kernelcore
			 */
			size_pages = end_pfn - start_pfn;
			if (size_pages > kernelcore_remaining)
				size_pages = kernelcore_remaining;
			zone_movable_pfn[nid] = start_pfn + size_pages;

			/*
			 * Some kernelcore has been met, update counts and
			 * break if the kernelcore for this node has been
			 * satisfied
			 */
			required_kernelcore -= min(required_kernelcore,
								size_pages);
			kernelcore_remaining -= size_pages;
			if (!kernelcore_remaining)
				break;
		}
	}

	/*
	 * If there is still required_kernelcore, we do another pass with one
	 * less node in the count. This will push zone_movable_pfn[nid] further
	 * along on the nodes that still have memory until kernelcore is
	 * satisfied
	 */
	usable_nodes--;
	if (usable_nodes && required_kernelcore > usable_nodes)
		goto restart;

out2:
	/* Align start of ZONE_MOVABLE on all nids to MAX_ORDER_NR_PAGES */
	for (nid = 0; nid < MAX_NUMNODES; nid++)
		zone_movable_pfn[nid] =
			roundup(zone_movable_pfn[nid], MAX_ORDER_NR_PAGES);

out:
	/* restore the node_state */
	node_states[N_MEMORY] = saved_node_state;
}

/* Any regular or high memory on that node ? */
static void check_for_memory(pg_data_t *pgdat, int nid)
{
	enum zone_type zone_type;

	if (N_MEMORY == N_NORMAL_MEMORY)
		return;

	for (zone_type = 0; zone_type <= ZONE_MOVABLE - 1; zone_type++) {
		struct zone *zone = &pgdat->node_zones[zone_type];
		if (populated_zone(zone)) {
			node_set_state(nid, N_HIGH_MEMORY);
			if (N_NORMAL_MEMORY != N_HIGH_MEMORY &&
			    zone_type <= ZONE_NORMAL)
				node_set_state(nid, N_NORMAL_MEMORY);
			break;
		}
	}
}

/**
 * free_area_init_nodes - Initialise all pg_data_t and zone data
 * @max_zone_pfn: an array of max PFNs for each zone
 *
 * This will call free_area_init_node() for each active node in the system.
 * Using the page ranges provided by memblock_set_node(), the size of each
 * zone in each node and their holes is calculated. If the maximum PFN
 * between two adjacent zones match, it is assumed that the zone is empty.
 * For example, if arch_max_dma_pfn == arch_max_dma32_pfn, it is assumed
 * that arch_max_dma32_pfn has no pages. It is also assumed that a zone
 * starts where the previous one ended. For example, ZONE_DMA32 starts
 * at arch_max_dma_pfn.
 */
/**
 * 根据boot阶段发现的内存活动区，构建伙伴系统的管理结构
 * max_zone_pfn保存了各个内存域的最大页帧号
 */
void __init free_area_init_nodes(unsigned long *max_zone_pfn)
{
	unsigned long start_pfn, end_pfn;
	int i, nid;

	/* Record where the zone boundaries are */
	memset(arch_zone_lowest_possible_pfn, 0,
				sizeof(arch_zone_lowest_possible_pfn));
	memset(arch_zone_highest_possible_pfn, 0,
				sizeof(arch_zone_highest_possible_pfn));
	/*
	 * 找到注册的最低内存域中可用的编号最小的页帧。
	 */
	start_pfn = find_min_pfn_with_active_regions();

    /*  依次遍历，确定各个内存域的边界    */
	for (i = 0; i < MAX_NR_ZONES; i++) {
        /*  由于ZONE_MOVABLE是一个虚拟内存域
             *  不与真正的硬件内存域关联
             *  该内存域的边界总是设置为0 */
		if (i == ZONE_MOVABLE)
			continue;

		end_pfn = max(max_zone_pfn[i], start_pfn);
		/*
		 * 构建其他内存域的页帧区间，方法很直接:
		 * 第n个内存域的最小页帧，即前一个(第n-1个)
		 * 内存域的最大页帧。当前内存域的最大页帧由max_zone_pfn给出
		 */
		arch_zone_lowest_possible_pfn[i] = start_pfn;
		arch_zone_highest_possible_pfn[i] = end_pfn;

		start_pfn = end_pfn;
	}
	arch_zone_lowest_possible_pfn[ZONE_MOVABLE] = 0;
	arch_zone_highest_possible_pfn[ZONE_MOVABLE] = 0;

	/* Find the PFNs that ZONE_MOVABLE begins at in each node */
	memset(zone_movable_pfn, 0, sizeof(zone_movable_pfn));
    /*  用于计算进入ZONE_MOVABLE的内存数量  */
	/*
	 * 由于ZONE_MOVEABLE是一个虚拟内存域，不与真正的硬件内存域关联，
	 * 该内存域的边界总是设置为0.只有在指定了内核命令行参数kernelcore
	 * 和moveablecore之一时，该内存域才会存在。该内存域一般开始于各个
	 * 结点的某个特定内存域的某一页帧号。 相应的编号在
	 * find_zone_movable_pfns_for_nodes里计算。
	 */
	find_zone_movable_pfns_for_nodes();

	/* Print out the zone ranges */
/*
    将各个内存域的最大、最小页帧号显示出来
*/
	pr_info("Zone ranges:\n");
	for (i = 0; i < MAX_NR_ZONES; i++) {
		if (i == ZONE_MOVABLE)
			continue;
		pr_info("  %-8s ", zone_names[i]);
		if (arch_zone_lowest_possible_pfn[i] ==
				arch_zone_highest_possible_pfn[i])
			pr_cont("empty\n");
		else
			pr_cont("[mem %#018Lx-%#018Lx]\n",
				(u64)arch_zone_lowest_possible_pfn[i]
					<< PAGE_SHIFT,
				((u64)arch_zone_highest_possible_pfn[i]
					<< PAGE_SHIFT) - 1);
	}

	/* Print out the PFNs ZONE_MOVABLE begins at in each node */
	pr_info("Movable zone start for each node\n");
	for (i = 0; i < MAX_NUMNODES; i++) {
        /*  对每个结点来说，zone_movable_pfn[node_id]
             *  表示ZONE_MOVABLE在movable_zone内存域中所取得内存的起始地址
             *  内核确保这些页将用于满足符合ZONE_MOVABLE职责的内存分配 */
		if (zone_movable_pfn[i])
            /*  显示各个内存域的分配情况  */
			pr_info("  Node %d: %#018Lx\n", i,
			       (u64)zone_movable_pfn[i] << PAGE_SHIFT);
	}

	/* Print out the early node map */
	pr_info("Early memory node ranges\n");
	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, &nid)
		pr_info("  node %3d: [mem %#018Lx-%#018Lx]\n", nid,
			(u64)start_pfn << PAGE_SHIFT,
			((u64)end_pfn << PAGE_SHIFT) - 1);

	/* Initialise every node */
	mminit_verify_pageflags_layout();
	setup_nr_node_ids();
    /*  代码遍历所有的活动结点，
       *  并分别对各个结点调用free_area_init_node建立数据结构，
       *  该函数需要结点第一个可用的页帧作为一个参数，
       *  而find_min_pfn_for_node则从early_node_map数组提取该信息   */
	/*
	 * 遍历所有活动结点，并分别对各个结点调用free_area_init_node()建立数据结构。
	 * 该函数需要结点第一个可用的页帧作为一个参数，而find_min_pfn_for_node()
	 * 则从early_node_map数组提取该信息。
	 */
	for_each_online_node(nid) {
		pg_data_t *pgdat = NODE_DATA(nid);
		/*
		 * 对各个结点创建数据结构
		 */
		free_area_init_node(nid, NULL,
				find_min_pfn_for_node(nid), NULL);

        /* Any memory on that node
             * 根据node_present_pages字段判断结点具有内存
             * 则在结点位图中设置N_HIGH_MEMORY标志
             * 该标志只表示结点上存在普通或高端内存
             * 因此check_for_regular_memory
             * 进一步检查低于ZONE_HIGHMEM的内存域中是否有内存
             * 并据此在结点位图中相应地设置N_NORMAL_MEMORY   */
		/*
		 * 如果根据node_present_pages字段判断结点具有内存，则在结点位图中
		 * 设置N_HIGH_MEMORY标志，该标志表示结点上存在普通或高端内存。因此
		 * check_for_regular_memory()进一步检查低于ZONE_HIGHMEM的内存域中
		 * 是否有内存，并据此在结点位图中相应地设置N_NORMAL_MEMORY标志
		 */
		/* 有内存，设置其节点状态标志 */
		if (pgdat->node_present_pages)
			node_set_state(nid, N_MEMORY);
		/* 进一步判断该节点是否有NORMAL内存 */
		check_for_memory(pgdat, nid);
	}
}

static int __init cmdline_parse_core(char *p, unsigned long *core)
{
	unsigned long long coremem;
	if (!p)
		return -EINVAL;

	coremem = memparse(p, &p);
	*core = coremem >> PAGE_SHIFT;

	/* Paranoid check that UL is enough for the coremem value */
	WARN_ON((coremem >> PAGE_SHIFT) > ULONG_MAX);

	return 0;
}

/*
 * kernelcore=size sets the amount of memory for use for allocations that
 * cannot be reclaimed or migrated.
 */
static int __init cmdline_parse_kernelcore(char *p)
{
	/* parse kernelcore=mirror */
	if (parse_option_str(p, "mirror")) {
		mirrored_kernelcore = true;
		return 0;
	}

	return cmdline_parse_core(p, &required_kernelcore);
}

/*
 * movablecore=size sets the amount of memory for use for allocations that
 * can be reclaimed or migrated.
 */
static int __init cmdline_parse_movablecore(char *p)
{
	return cmdline_parse_core(p, &required_movablecore);
}

early_param("kernelcore", cmdline_parse_kernelcore);
early_param("movablecore", cmdline_parse_movablecore);

#endif /* CONFIG_HAVE_MEMBLOCK_NODE_MAP */

void adjust_managed_page_count(struct page *page, long count)
{
	spin_lock(&managed_page_count_lock);
	page_zone(page)->managed_pages += count;
	totalram_pages += count;
#ifdef CONFIG_HIGHMEM
	if (PageHighMem(page))
		totalhigh_pages += count;
#endif
	spin_unlock(&managed_page_count_lock);
}
EXPORT_SYMBOL(adjust_managed_page_count);

unsigned long free_reserved_area(void *start, void *end, int poison, char *s)
{
	void *pos;
	unsigned long pages = 0;

	start = (void *)PAGE_ALIGN((unsigned long)start);
	end = (void *)((unsigned long)end & PAGE_MASK);
	for (pos = start; pos < end; pos += PAGE_SIZE, pages++) {
		if ((unsigned int)poison <= 0xFF)
			memset(pos, poison, PAGE_SIZE);
		free_reserved_page(virt_to_page(pos));
	}

	if (pages && s)
		pr_info("Freeing %s memory: %ldK\n",
			s, pages << (PAGE_SHIFT - 10));

	return pages;
}
EXPORT_SYMBOL(free_reserved_area);

#ifdef	CONFIG_HIGHMEM
void free_highmem_page(struct page *page)
{
	__free_reserved_page(page);
	totalram_pages++;
	page_zone(page)->managed_pages++;
	totalhigh_pages++;
}
#endif


void __init mem_init_print_info(const char *str)
{
	unsigned long physpages, codesize, datasize, rosize, bss_size;
	unsigned long init_code_size, init_data_size;

	physpages = get_num_physpages();
	codesize = _etext - _stext;
	datasize = _edata - _sdata;
	rosize = __end_rodata - __start_rodata;
	bss_size = __bss_stop - __bss_start;
	init_data_size = __init_end - __init_begin;
	init_code_size = _einittext - _sinittext;

/*
    pr_info("_text:0x%p,_stext:%p,_etext:0x%p",_text,_stext,_etext);
    pr_info("_data:0x%p,_sdata:%p,_edata:0x%p",_data,_sdata,_edata);
    pr_info("__start_data_ro_after_init:0x%p,__end_data_ro_after_init:0x%p",__start_data_ro_after_init,__end_data_ro_after_init);
    pr_info("__per_cpu_load:0x%p,__per_cpu_start:%p,__per_cpu_end:0x%p",__per_cpu_load,__per_cpu_start,__per_cpu_end);
    pr_info("__kprobes_text_start:%p,__kprobes_text_end:0x%p",__kprobes_text_start,__kprobes_text_end);
    pr_info("__start_rodata:0x%p,__end_rodata:0x%p",__start_rodata,__end_rodata);
    pr_info("__bss_start:0x%p,__bss_stop:0x%p",__bss_start,__bss_stop);
    pr_info("__init_begin:0x%p,__init_end:0x%p",__init_begin,__init_end);
    pr_info("_sinittext:0x%p,_einittext:0x%p",_sinittext,_einittext);
*/

	/*
	 * Detect special cases and adjust section sizes accordingly:
	 * 1) .init.* may be embedded into .data sections
	 * 2) .init.text.* may be out of [__init_begin, __init_end],
	 *    please refer to arch/tile/kernel/vmlinux.lds.S.
	 * 3) .rodata.* may be embedded into .text or .data sections.
	 */
#define adj_init_size(start, end, size, pos, adj) \
	do { \
		if (start <= pos && pos < end && size > adj) \
			size -= adj; \
	} while (0)

	adj_init_size(__init_begin, __init_end, init_data_size,
		     _sinittext, init_code_size);
	adj_init_size(_stext, _etext, codesize, _sinittext, init_code_size);
	adj_init_size(_sdata, _edata, datasize, __init_begin, init_data_size);
	adj_init_size(_stext, _etext, codesize, __start_rodata, rosize);
	adj_init_size(_sdata, _edata, datasize, __start_rodata, rosize);

#undef	adj_init_size

    /*
    Memory: 52360K/65536K available (7168K kernel code, 445K rwdata, 2028K rodata, 1024K init, 468K bss, 13176K reserved, 0K cma-reserved)
    */
	pr_info("Memory: %luK/%luK available (%luK kernel code, %luK rwdata, %luK rodata, %luK init, %luK bss, %luK reserved, %luK cma-reserved"
#ifdef	CONFIG_HIGHMEM
		", %luK highmem"
#endif
		"%s%s)\n",
		nr_free_pages() << (PAGE_SHIFT - 10),
		physpages << (PAGE_SHIFT - 10),
		codesize >> 10, datasize >> 10, rosize >> 10,
		(init_data_size + init_code_size) >> 10, bss_size >> 10,
		(physpages - totalram_pages - totalcma_pages) << (PAGE_SHIFT - 10),
		totalcma_pages << (PAGE_SHIFT - 10),
#ifdef	CONFIG_HIGHMEM
		totalhigh_pages << (PAGE_SHIFT - 10),
#endif
		str ? ", " : "", str ? str : "");
}

/**
 * set_dma_reserve - set the specified number of pages reserved in the first zone
 * @new_dma_reserve: The number of pages to mark reserved
 *
 * The per-cpu batchsize and zone watermarks are determined by managed_pages.
 * In the DMA zone, a significant percentage may be consumed by kernel image
 * and other unfreeable allocations which can skew the watermarks badly. This
 * function may optionally be used to account for unfreeable pages in the
 * first zone (e.g., ZONE_DMA). The effect will be lower watermarks and
 * smaller per-cpu batchsize.
 */
void __init set_dma_reserve(unsigned long new_dma_reserve)
{
	dma_reserve = new_dma_reserve;
}

void __init free_area_init(unsigned long *zones_size)
{
	free_area_init_node(0, zones_size,
			__pa(PAGE_OFFSET) >> PAGE_SHIFT, NULL);
}

static int page_alloc_cpu_dead(unsigned int cpu)
{

	lru_add_drain_cpu(cpu);
	drain_pages(cpu);

	/*
	 * Spill the event counters of the dead processor
	 * into the current processors event counters.
	 * This artificially elevates the count of the current
	 * processor.
	 */
	vm_events_fold_cpu(cpu);

	/*
	 * Zero the differential counters of the dead processor
	 * so that the vm statistics are consistent.
	 *
	 * This is only okay since the processor is dead and cannot
	 * race with what we are doing.
	 */
	cpu_vm_stats_fold(cpu);
	return 0;
}

void __init page_alloc_init(void)
{
	int ret;

	ret = cpuhp_setup_state_nocalls(CPUHP_PAGE_ALLOC_DEAD,
					"mm/page_alloc:dead", NULL,
					page_alloc_cpu_dead);
	WARN_ON(ret < 0);
}

/*
 * calculate_totalreserve_pages - called when sysctl_lowmem_reserve_ratio
 *	or min_free_kbytes changes.
 */
static void calculate_totalreserve_pages(void)
{
	struct pglist_data *pgdat;
	unsigned long reserve_pages = 0;
	enum zone_type i, j;

	for_each_online_pgdat(pgdat) {

		pgdat->totalreserve_pages = 0;

		for (i = 0; i < MAX_NR_ZONES; i++) {
			struct zone *zone = pgdat->node_zones + i;
			long max = 0;

			/* Find valid and maximum lowmem_reserve in the zone */
			for (j = i; j < MAX_NR_ZONES; j++) {
				if (zone->lowmem_reserve[j] > max)
					max = zone->lowmem_reserve[j];
			}

			/* we treat the high watermark as reserved pages. */
			max += high_wmark_pages(zone);

			if (max > zone->managed_pages)
				max = zone->managed_pages;

			pgdat->totalreserve_pages += max;

			reserve_pages += max;
		}
	}
	totalreserve_pages = reserve_pages;
}

/*
 * setup_per_zone_lowmem_reserve - called whenever
 *	sysctl_lowmem_reserve_ratio changes.  Ensures that each zone
 *	has a correct pages reserved value, so an adequate number of
 *	pages are left in the zone after a successful __alloc_pages().
 */
static void setup_per_zone_lowmem_reserve(void)
{
	struct pglist_data *pgdat;
	enum zone_type j, idx;

	for_each_online_pgdat(pgdat) {
		for (j = 0; j < MAX_NR_ZONES; j++) {
			struct zone *zone = pgdat->node_zones + j;
			unsigned long managed_pages = zone->managed_pages;

			zone->lowmem_reserve[j] = 0;

			idx = j;
			while (idx) {
				struct zone *lower_zone;

				idx--;

				if (sysctl_lowmem_reserve_ratio[idx] < 1)
					sysctl_lowmem_reserve_ratio[idx] = 1;

				lower_zone = pgdat->node_zones + idx;
				lower_zone->lowmem_reserve[j] = managed_pages /
					sysctl_lowmem_reserve_ratio[idx];
				managed_pages += lower_zone->managed_pages;
			}
		}
	}

	/* update totalreserve_pages */
	calculate_totalreserve_pages();
}

static void __setup_per_zone_wmarks(void)
{
	unsigned long pages_min = min_free_kbytes >> (PAGE_SHIFT - 10);
	unsigned long lowmem_pages = 0;
	struct zone *zone;
	unsigned long flags;

	/* Calculate total number of !ZONE_HIGHMEM pages */
	for_each_zone(zone) {
		if (!is_highmem(zone))
			lowmem_pages += zone->managed_pages;
	}

	for_each_zone(zone) {
		u64 tmp;

		spin_lock_irqsave(&zone->lock, flags);
		tmp = (u64)pages_min * zone->managed_pages;
		do_div(tmp, lowmem_pages);
		if (is_highmem(zone)) {
			/*
			 * __GFP_HIGH and PF_MEMALLOC allocations usually don't
			 * need highmem pages, so cap pages_min to a small
			 * value here.
			 *
			 * The WMARK_HIGH-WMARK_LOW and (WMARK_LOW-WMARK_MIN)
			 * deltas control asynch page reclaim, and so should
			 * not be capped for highmem.
			 */
			unsigned long min_pages;

			min_pages = zone->managed_pages / 1024;
			min_pages = clamp(min_pages, SWAP_CLUSTER_MAX, 128UL);
			zone->watermark[WMARK_MIN] = min_pages;
		} else {
			/*
			 * If it's a lowmem zone, reserve a number of pages
			 * proportionate to the zone's size.
			 */
			zone->watermark[WMARK_MIN] = tmp;
		}

		/*
		 * Set the kswapd watermarks distance according to the
		 * scale factor in proportion to available memory, but
		 * ensure a minimum size on small systems.
		 */
		tmp = max_t(u64, tmp >> 2,
			    mult_frac(zone->managed_pages,
				      watermark_scale_factor, 10000));

		zone->watermark[WMARK_LOW]  = min_wmark_pages(zone) + tmp;
		zone->watermark[WMARK_HIGH] = min_wmark_pages(zone) + tmp * 2;

		spin_unlock_irqrestore(&zone->lock, flags);
	}

	/* update totalreserve_pages */
	calculate_totalreserve_pages();
}

/**
 * setup_per_zone_wmarks - called when min_free_kbytes changes
 * or when memory is hot-{added|removed}
 *
 * Ensures that the watermark[min,low,high] values for each zone are set
 * correctly with respect to min_free_kbytes.
 */
void setup_per_zone_wmarks(void)
{
	mutex_lock(&zonelists_mutex);
	__setup_per_zone_wmarks();
	mutex_unlock(&zonelists_mutex);
}

/*
 * Initialise min_free_kbytes.
 *
 * For small machines we want it small (128k min).  For large machines
 * we want it large (64MB max).  But it is not linear, because network
 * bandwidth does not increase linearly with machine size.  We use
 *
 *	min_free_kbytes = 4 * sqrt(lowmem_kbytes), for better accuracy:
 *	min_free_kbytes = sqrt(lowmem_kbytes * 16)
 *
 * which yields
 *
 * 16MB:	512k
 * 32MB:	724k
 * 64MB:	1024k
 * 128MB:	1448k
 * 256MB:	2048k
 * 512MB:	2896k
 * 1024MB:	4096k
 * 2048MB:	5792k
 * 4096MB:	8192k
 * 8192MB:	11584k
 * 16384MB:	16384k
 */
int __meminit init_per_zone_wmark_min(void)
{
	unsigned long lowmem_kbytes;
	int new_min_free_kbytes;

	lowmem_kbytes = nr_free_buffer_pages() * (PAGE_SIZE >> 10);
	new_min_free_kbytes = int_sqrt(lowmem_kbytes * 16);

	if (new_min_free_kbytes > user_min_free_kbytes) {
		min_free_kbytes = new_min_free_kbytes;
		if (min_free_kbytes < 128)
			min_free_kbytes = 128;
		if (min_free_kbytes > 65536)
			min_free_kbytes = 65536;
	} else {
		pr_warn("min_free_kbytes is not updated to %d because user defined value %d is preferred\n",
				new_min_free_kbytes, user_min_free_kbytes);
	}
	setup_per_zone_wmarks();
	refresh_zone_stat_thresholds();
	setup_per_zone_lowmem_reserve();

#ifdef CONFIG_NUMA
	setup_min_unmapped_ratio();
	setup_min_slab_ratio();
#endif

	return 0;
}
core_initcall(init_per_zone_wmark_min)

/*
 * min_free_kbytes_sysctl_handler - just a wrapper around proc_dointvec() so
 *	that we can call two helper functions whenever min_free_kbytes
 *	changes.
 */
int min_free_kbytes_sysctl_handler(struct ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	int rc;

	rc = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (rc)
		return rc;

	if (write) {
		user_min_free_kbytes = min_free_kbytes;
		setup_per_zone_wmarks();
	}
	return 0;
}

int watermark_scale_factor_sysctl_handler(struct ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	int rc;

	rc = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (rc)
		return rc;

	if (write)
		setup_per_zone_wmarks();

	return 0;
}

#ifdef CONFIG_NUMA
static void setup_min_unmapped_ratio(void)
{
	pg_data_t *pgdat;
	struct zone *zone;

	for_each_online_pgdat(pgdat)
		pgdat->min_unmapped_pages = 0;

	for_each_zone(zone)
		zone->zone_pgdat->min_unmapped_pages += (zone->managed_pages *
				sysctl_min_unmapped_ratio) / 100;
}


int sysctl_min_unmapped_ratio_sysctl_handler(struct ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	int rc;

	rc = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (rc)
		return rc;

	setup_min_unmapped_ratio();

	return 0;
}

static void setup_min_slab_ratio(void)
{
	pg_data_t *pgdat;
	struct zone *zone;

	for_each_online_pgdat(pgdat)
		pgdat->min_slab_pages = 0;

	for_each_zone(zone)
		zone->zone_pgdat->min_slab_pages += (zone->managed_pages *
				sysctl_min_slab_ratio) / 100;
}

int sysctl_min_slab_ratio_sysctl_handler(struct ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	int rc;

	rc = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (rc)
		return rc;

	setup_min_slab_ratio();

	return 0;
}
#endif

/*
 * lowmem_reserve_ratio_sysctl_handler - just a wrapper around
 *	proc_dointvec() so that we can call setup_per_zone_lowmem_reserve()
 *	whenever sysctl_lowmem_reserve_ratio changes.
 *
 * The reserve ratio obviously has absolutely no relation with the
 * minimum watermarks. The lowmem reserve ratio can only make sense
 * if in function of the boot time zone sizes.
 */
int lowmem_reserve_ratio_sysctl_handler(struct ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	proc_dointvec_minmax(table, write, buffer, length, ppos);
	setup_per_zone_lowmem_reserve();
	return 0;
}

/*
 * percpu_pagelist_fraction - changes the pcp->high for each zone on each
 * cpu.  It is the fraction of total pages in each zone that a hot per cpu
 * pagelist can have before it gets flushed back to buddy allocator.
 */
int percpu_pagelist_fraction_sysctl_handler(struct ctl_table *table, int write,
	void __user *buffer, size_t *length, loff_t *ppos)
{
	struct zone *zone;
	int old_percpu_pagelist_fraction;
	int ret;

	mutex_lock(&pcp_batch_high_lock);
	old_percpu_pagelist_fraction = percpu_pagelist_fraction;

	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (!write || ret < 0)
		goto out;

	/* Sanity checking to avoid pcp imbalance */
	if (percpu_pagelist_fraction &&
	    percpu_pagelist_fraction < MIN_PERCPU_PAGELIST_FRACTION) {
		percpu_pagelist_fraction = old_percpu_pagelist_fraction;
		ret = -EINVAL;
		goto out;
	}

	/* No change? */
	if (percpu_pagelist_fraction == old_percpu_pagelist_fraction)
		goto out;

	for_each_populated_zone(zone) {
		unsigned int cpu;

		for_each_possible_cpu(cpu)
			pageset_set_high_and_batch(zone,
					per_cpu_ptr(zone->pageset, cpu));
	}
out:
	mutex_unlock(&pcp_batch_high_lock);
	return ret;
}

#ifdef CONFIG_NUMA
int hashdist = HASHDIST_DEFAULT;

static int __init set_hashdist(char *str)
{
	if (!str)
		return 0;
	hashdist = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("hashdist=", set_hashdist);
#endif

#ifndef __HAVE_ARCH_RESERVED_KERNEL_PAGES
/*
 * Returns the number of pages that arch has reserved but
 * is not known to alloc_large_system_hash().
 */
static unsigned long __init arch_reserved_kernel_pages(void)
{
	return 0;
}
#endif

/*
 * allocate a large system hash table from bootmem
 * - it is assumed that the hash table must contain an exact power-of-2
 *   quantity of entries
 * - limit is the number of hash buckets, not the total allocation size
 */
void *__init alloc_large_system_hash(const char *tablename,
				     unsigned long bucketsize,
				     unsigned long numentries,//缓存项的个数
				     int scale,
				     int flags,
				     unsigned int *_hash_shift,
				     unsigned int *_hash_mask,
				     unsigned long low_limit,
				     unsigned long high_limit)
{
	unsigned long long max = high_limit;
	unsigned long log2qty, size;
	void *table = NULL;

	/* allow the kernel cmdline to have a say */
	if (!numentries) {//传递的缓存项数目为0，由内核决定分配多少项
		/* round applicable memory size up to nearest megabyte */
		numentries = nr_kernel_pages;
		numentries -= arch_reserved_kernel_pages();

		/* It isn't necessary when PAGE_SIZE >= 1MB */
		if (PAGE_SHIFT < 20)//每页小于1M，就根据内存有多少M来决定缓存项
			numentries = round_up(numentries, (1<<20)/PAGE_SIZE);

		/* limit to 1 bucket per 2^scale bytes of low memory */
		if (scale > PAGE_SHIFT)
			numentries >>= (scale - PAGE_SHIFT);
		else
			numentries <<= (PAGE_SHIFT - scale);

		/* Make sure we've got at least a 0-order allocation.. */
		if (unlikely(flags & HASH_SMALL)) {//至少分配一页
			/* Makes no sense without HASH_EARLY */
			WARN_ON(!(flags & HASH_EARLY));
			if (!(numentries >> *_hash_shift)) {
				numentries = 1UL << *_hash_shift;
				BUG_ON(!numentries);
			}
		} else if (unlikely((numentries * bucketsize) < PAGE_SIZE))
			numentries = PAGE_SIZE / bucketsize;
	}
	numentries = roundup_pow_of_two(numentries);

	/* limit allocation size to 1/16 total memory by default */
	if (max == 0) {
		max = ((unsigned long long)nr_all_pages << PAGE_SHIFT) >> 4;
		do_div(max, bucketsize);
	}
	max = min(max, 0x80000000ULL);

	if (numentries < low_limit)
		numentries = low_limit;
	if (numentries > max)
		numentries = max;

	log2qty = ilog2(numentries);

	//根据情况从bootmem,vmalloc,伙伴系统中分配内存。感觉这段代码还是晦涩了一点。
	do {
		size = bucketsize << log2qty;
		if (flags & HASH_EARLY)
			table = memblock_virt_alloc_nopanic(size, 0);
		else if (hashdist)
			table = __vmalloc(size, GFP_ATOMIC, PAGE_KERNEL);
		else {
			/*
			 * If bucketsize is not a power-of-two, we may free
			 * some pages at the end of hash table which
			 * alloc_pages_exact() automatically does
			 */
			if (get_order(size) < MAX_ORDER) {
				table = alloc_pages_exact(size, GFP_ATOMIC);
				kmemleak_alloc(table, size, 1, GFP_ATOMIC);
			}
		}
	} while (!table && size > PAGE_SIZE && --log2qty);

	if (!table)
		panic("Failed to allocate %s hash table\n", tablename);

	pr_info("%s hash table entries: %ld (order: %d, %lu bytes)\n",
		tablename, 1UL << log2qty, ilog2(size) - PAGE_SHIFT, size);

	if (_hash_shift)
		*_hash_shift = log2qty;
	if (_hash_mask)
		*_hash_mask = (1 << log2qty) - 1;

	return table;
}

/*
 * This function checks whether pageblock includes unmovable pages or not.
 * If @count is not zero, it is okay to include less @count unmovable pages
 *
 * PageLRU check without isolation or lru_lock could race so that
 * MIGRATE_MOVABLE block might include unmovable pages. It means you can't
 * expect this function should be exact.
 */
bool has_unmovable_pages(struct zone *zone, struct page *page, int count,
			 bool skip_hwpoisoned_pages)
{
	unsigned long pfn, iter, found;
	int mt;

	/*
	 * For avoiding noise data, lru_add_drain_all() should be called
	 * If ZONE_MOVABLE, the zone never contains unmovable pages
	 */
	if (zone_idx(zone) == ZONE_MOVABLE)
		return false;
	mt = get_pageblock_migratetype(page);
	if (mt == MIGRATE_MOVABLE || is_migrate_cma(mt))
		return false;

	pfn = page_to_pfn(page);
	for (found = 0, iter = 0; iter < pageblock_nr_pages; iter++) {
		unsigned long check = pfn + iter;

		if (!pfn_valid_within(check))
			continue;

		page = pfn_to_page(check);

		/*
		 * Hugepages are not in LRU lists, but they're movable.
		 * We need not scan over tail pages bacause we don't
		 * handle each tail page individually in migration.
		 */
		if (PageHuge(page)) {
			iter = round_up(iter + 1, 1<<compound_order(page)) - 1;
			continue;
		}

		/*
		 * We can't use page_count without pin a page
		 * because another CPU can free compound page.
		 * This check already skips compound tails of THP
		 * because their page->_refcount is zero at all time.
		 */
		if (!page_ref_count(page)) {
			if (PageBuddy(page))
				iter += (1 << page_order(page)) - 1;
			continue;
		}

		/*
		 * The HWPoisoned page may be not in buddy system, and
		 * page_count() is not 0.
		 */
		if (skip_hwpoisoned_pages && PageHWPoison(page))
			continue;

		if (!PageLRU(page))
			found++;
		/*
		 * If there are RECLAIMABLE pages, we need to check
		 * it.  But now, memory offline itself doesn't call
		 * shrink_node_slabs() and it still to be fixed.
		 */
		/*
		 * If the page is not RAM, page_count()should be 0.
		 * we don't need more check. This is an _used_ not-movable page.
		 *
		 * The problematic thing here is PG_reserved pages. PG_reserved
		 * is set to both of a memory hole page and a _used_ kernel
		 * page at boot.
		 */
		if (found > count)
			return true;
	}
	return false;
}

bool is_pageblock_removable_nolock(struct page *page)
{
	struct zone *zone;
	unsigned long pfn;

	/*
	 * We have to be careful here because we are iterating over memory
	 * sections which are not zone aware so we might end up outside of
	 * the zone but still within the section.
	 * We have to take care about the node as well. If the node is offline
	 * its NODE_DATA will be NULL - see page_zone.
	 */
	if (!node_online(page_to_nid(page)))
		return false;

	zone = page_zone(page);
	pfn = page_to_pfn(page);
	if (!zone_spans_pfn(zone, pfn))
		return false;

	return !has_unmovable_pages(zone, page, 0, true);
}

#if (defined(CONFIG_MEMORY_ISOLATION) && defined(CONFIG_COMPACTION)) || defined(CONFIG_CMA)

static unsigned long pfn_max_align_down(unsigned long pfn)
{
	return pfn & ~(max_t(unsigned long, MAX_ORDER_NR_PAGES,
			     pageblock_nr_pages) - 1);
}

static unsigned long pfn_max_align_up(unsigned long pfn)
{
	return ALIGN(pfn, max_t(unsigned long, MAX_ORDER_NR_PAGES,
				pageblock_nr_pages));
}

/* [start, end) must belong to a single zone. */
static int __alloc_contig_migrate_range(struct compact_control *cc,
					unsigned long start, unsigned long end)
{
	/* This function is based on compact_zone() from compaction.c. */
	unsigned long nr_reclaimed;
	unsigned long pfn = start;
	unsigned int tries = 0;
	int ret = 0;

	migrate_prep();

	while (pfn < end || !list_empty(&cc->migratepages)) {
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		if (list_empty(&cc->migratepages)) {
			cc->nr_migratepages = 0;
			pfn = isolate_migratepages_range(cc, pfn, end);
			if (!pfn) {
				ret = -EINTR;
				break;
			}
			tries = 0;
		} else if (++tries == 5) {
			ret = ret < 0 ? ret : -EBUSY;
			break;
		}

		nr_reclaimed = reclaim_clean_pages_from_list(cc->zone,
							&cc->migratepages);
		cc->nr_migratepages -= nr_reclaimed;

		ret = migrate_pages(&cc->migratepages, alloc_migrate_target,
				    NULL, 0, cc->mode, MR_CMA);
	}
	if (ret < 0) {
		putback_movable_pages(&cc->migratepages);
		return ret;
	}
	return 0;
}

/**
 * alloc_contig_range() -- tries to allocate given range of pages
 * @start:	start PFN to allocate
 * @end:	one-past-the-last PFN to allocate
 * @migratetype:	migratetype of the underlaying pageblocks (either
 *			#MIGRATE_MOVABLE or #MIGRATE_CMA).  All pageblocks
 *			in range must have the same migratetype and it must
 *			be either of the two.
 *
 * The PFN range does not have to be pageblock or MAX_ORDER_NR_PAGES
 * aligned, however it's the caller's responsibility to guarantee that
 * we are the only thread that changes migrate type of pageblocks the
 * pages fall in.
 *
 * The PFN range must belong to a single zone.
 *
 * Returns zero on success or negative error code.  On success all
 * pages which PFN is in [start, end) are allocated for the caller and
 * need to be freed with free_contig_range().
 */
int alloc_contig_range(unsigned long start, unsigned long end,
		       unsigned migratetype)
{
	unsigned long outer_start, outer_end;
	unsigned int order;
	int ret = 0;

	struct compact_control cc = {
		.nr_migratepages = 0,
		.order = -1,
		.zone = page_zone(pfn_to_page(start)),
		.mode = MIGRATE_SYNC,
		.ignore_skip_hint = true,
		.gfp_mask = GFP_KERNEL,
	};
	INIT_LIST_HEAD(&cc.migratepages);

	/*
	 * What we do here is we mark all pageblocks in range as
	 * MIGRATE_ISOLATE.  Because pageblock and max order pages may
	 * have different sizes, and due to the way page allocator
	 * work, we align the range to biggest of the two pages so
	 * that page allocator won't try to merge buddies from
	 * different pageblocks and change MIGRATE_ISOLATE to some
	 * other migration type.
	 *
	 * Once the pageblocks are marked as MIGRATE_ISOLATE, we
	 * migrate the pages from an unaligned range (ie. pages that
	 * we are interested in).  This will put all the pages in
	 * range back to page allocator as MIGRATE_ISOLATE.
	 *
	 * When this is done, we take the pages in range from page
	 * allocator removing them from the buddy system.  This way
	 * page allocator will never consider using them.
	 *
	 * This lets us mark the pageblocks back as
	 * MIGRATE_CMA/MIGRATE_MOVABLE so that free pages in the
	 * aligned range but not in the unaligned, original range are
	 * put back to page allocator so that buddy can use them.
	 */

	ret = start_isolate_page_range(pfn_max_align_down(start),
				       pfn_max_align_up(end), migratetype,
				       false);
	if (ret)
		return ret;

	/*
	 * In case of -EBUSY, we'd like to know which page causes problem.
	 * So, just fall through. We will check it in test_pages_isolated().
	 */
	ret = __alloc_contig_migrate_range(&cc, start, end);
	if (ret && ret != -EBUSY)
		goto done;

	/*
	 * Pages from [start, end) are within a MAX_ORDER_NR_PAGES
	 * aligned blocks that are marked as MIGRATE_ISOLATE.  What's
	 * more, all pages in [start, end) are free in page allocator.
	 * What we are going to do is to allocate all pages from
	 * [start, end) (that is remove them from page allocator).
	 *
	 * The only problem is that pages at the beginning and at the
	 * end of interesting range may be not aligned with pages that
	 * page allocator holds, ie. they can be part of higher order
	 * pages.  Because of this, we reserve the bigger range and
	 * once this is done free the pages we are not interested in.
	 *
	 * We don't have to hold zone->lock here because the pages are
	 * isolated thus they won't get removed from buddy.
	 */

	lru_add_drain_all();
	drain_all_pages(cc.zone);

	order = 0;
	outer_start = start;
	while (!PageBuddy(pfn_to_page(outer_start))) {
		if (++order >= MAX_ORDER) {
			outer_start = start;
			break;
		}
		outer_start &= ~0UL << order;
	}

	if (outer_start != start) {
		order = page_order(pfn_to_page(outer_start));

		/*
		 * outer_start page could be small order buddy page and
		 * it doesn't include start page. Adjust outer_start
		 * in this case to report failed page properly
		 * on tracepoint in test_pages_isolated()
		 */
		if (outer_start + (1UL << order) <= start)
			outer_start = start;
	}

	/* Make sure the range is really isolated. */
	if (test_pages_isolated(outer_start, end, false)) {
		pr_info("%s: [%lx, %lx) PFNs busy\n",
			__func__, outer_start, end);
		ret = -EBUSY;
		goto done;
	}

	/* Grab isolated pages from freelists. */
	outer_end = isolate_freepages_range(&cc, outer_start, end);
	if (!outer_end) {
		ret = -EBUSY;
		goto done;
	}

	/* Free head and tail (if any) */
	if (start != outer_start)
		free_contig_range(outer_start, start - outer_start);
	if (end != outer_end)
		free_contig_range(end, outer_end - end);

done:
	undo_isolate_page_range(pfn_max_align_down(start),
				pfn_max_align_up(end), migratetype);
	return ret;
}

void free_contig_range(unsigned long pfn, unsigned nr_pages)
{
	unsigned int count = 0;

	for (; nr_pages--; pfn++) {
		struct page *page = pfn_to_page(pfn);

		count += page_count(page) != 1;
		__free_page(page);
	}
	WARN(count != 0, "%d pages are still in use!\n", count);
}
#endif

#ifdef CONFIG_MEMORY_HOTPLUG
/*
 * The zone indicated has a new number of managed_pages; batch sizes and percpu
 * page high values need to be recalulated.
 */
void __meminit zone_pcp_update(struct zone *zone)
{
	unsigned cpu;
	mutex_lock(&pcp_batch_high_lock);
	for_each_possible_cpu(cpu)
		pageset_set_high_and_batch(zone,
				per_cpu_ptr(zone->pageset, cpu));
	mutex_unlock(&pcp_batch_high_lock);
}
#endif

void zone_pcp_reset(struct zone *zone)
{
	unsigned long flags;
	int cpu;
	struct per_cpu_pageset *pset;

	/* avoid races with drain_pages()  */
	local_irq_save(flags);
	if (zone->pageset != &boot_pageset) {
		for_each_online_cpu(cpu) {
			pset = per_cpu_ptr(zone->pageset, cpu);
			drain_zonestat(zone, pset);
		}
		free_percpu(zone->pageset);
		zone->pageset = &boot_pageset;
	}
	local_irq_restore(flags);
}

#ifdef CONFIG_MEMORY_HOTREMOVE
/*
 * All pages in the range must be in a single zone and isolated
 * before calling this.
 */
void
__offline_isolated_pages(unsigned long start_pfn, unsigned long end_pfn)
{
	struct page *page;
	struct zone *zone;
	unsigned int order, i;
	unsigned long pfn;
	unsigned long flags;
	/* find the first valid pfn */
	for (pfn = start_pfn; pfn < end_pfn; pfn++)
		if (pfn_valid(pfn))
			break;
	if (pfn == end_pfn)
		return;
	zone = page_zone(pfn_to_page(pfn));
	spin_lock_irqsave(&zone->lock, flags);
	pfn = start_pfn;
	while (pfn < end_pfn) {
		if (!pfn_valid(pfn)) {
			pfn++;
			continue;
		}
		page = pfn_to_page(pfn);
		/*
		 * The HWPoisoned page may be not in buddy system, and
		 * page_count() is not 0.
		 */
		if (unlikely(!PageBuddy(page) && PageHWPoison(page))) {
			pfn++;
			SetPageReserved(page);
			continue;
		}

		BUG_ON(page_count(page));
		BUG_ON(!PageBuddy(page));
		order = page_order(page);
#ifdef CONFIG_DEBUG_VM
		pr_info("remove from free list %lx %d %lx\n",
			pfn, 1 << order, end_pfn);
#endif
		list_del(&page->lru);
		rmv_page_order(page);
		zone->free_area[order].nr_free--;
		for (i = 0; i < (1 << order); i++)
			SetPageReserved((page+i));
		pfn += (1 << order);
	}
	spin_unlock_irqrestore(&zone->lock, flags);
}
#endif

bool is_free_buddy_page(struct page *page)
{
	struct zone *zone = page_zone(page);
	unsigned long pfn = page_to_pfn(page);
	unsigned long flags;
	unsigned int order;

	spin_lock_irqsave(&zone->lock, flags);
	for (order = 0; order < MAX_ORDER; order++) {
		struct page *page_head = page - (pfn & ((1 << order) - 1));

		if (PageBuddy(page_head) && page_order(page_head) >= order)
			break;
	}
	spin_unlock_irqrestore(&zone->lock, flags);

	return order < MAX_ORDER;
}
