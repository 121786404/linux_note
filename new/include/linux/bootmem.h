/*
 * Discontiguous memory support, Kanoj Sarcar, SGI, Nov 1999
 */
#ifndef _LINUX_BOOTMEM_H
#define _LINUX_BOOTMEM_H

#include <linux/mmzone.h>
#include <linux/mm_types.h>
#include <asm/dma.h>
#include <asm/processor.h>


/*
在启动过程期间, 尽管内存管理尚未初始化, 
但是内核仍然需要分配内存以创建各种数据结构, 
早期的内核中负责初始化阶段的内存分配器称为引导内存分配器(boot memory allocator—bootmem分配器), 

在耳熟能详的伙伴系统建立前内存都是利用分配器来分配的，
伙伴系统框架建立起来后，bootmem会过度到伙伴系统. 

引导内存分配器(boot memory allocator—bootmem分配器)
基于最先适配(first-first)分配器的原理,使用一个位图来管理页, 
以位图代替原来的空闲链表结构来表示存储空间, 
位图的比特位的数目与系统中物理内存页面数目相同. 
若位图中某一位是1, 则标识该页面已经被分配(已用页), 
否则表示未被占有(未用页).

在需要分配内存时, 分配器逐位的扫描位图, 
直至找到一个能提供足够连续页的位置, 
即所谓的最先最佳(first-best)或最先适配位置.

该分配机制通过记录上一次分配的页面帧号(PFN)结束时的偏移量来实现分配大小小于一页的空间, 
连续的小的空闲空间将被合并存储在一页上.

即使是初始化用的最先适配分配器也必须使用一些数据结构存, 
内核为系统中每一个结点都提供了一个struct bootmem_data结构的实例, 
用于bootmem的内存管理. 它含有引导内存分配器给结点分配内存时所需的信息. 

当然, 这时候内存管理还没有初始化, 因而该结构所需的内存是无法动态分配的, 
必须在编译时分配给内核.

在UMA系统上该分配的实现与CPU无关, 而NUMA系统内存结点与CPU相关联, 
因此采用了特定体系结构的解决方法.
*/

/*
 *  simple boot-time physical memory area allocator.
 */

extern unsigned long max_low_pfn;
extern unsigned long min_low_pfn;

/*
 * highest page
 */
extern unsigned long max_pfn;
/*
 * highest possible page
 */
extern unsigned long long max_possible_pfn;

#ifndef CONFIG_NO_BOOTMEM
/*
 * node_bootmem_map is a map pointer - the bits represent all physical 
 * memory pages (including holes) on the node.
 */
typedef struct bootmem_data {
	//内存块的最小页帧号
	unsigned long node_min_pfn;
	//内存块的低端页帧号，高于此页帧的都被分配了。
	unsigned long node_low_pfn;
	//内存块的页面映射位图
	void *node_bootmem_map;
	unsigned long last_end_off;
	unsigned long hint_idx;
	struct list_head list;
} bootmem_data_t;

extern bootmem_data_t bootmem_node_data[];
#endif

extern unsigned long bootmem_bootmap_pages(unsigned long);

extern unsigned long init_bootmem_node(pg_data_t *pgdat,
				       unsigned long freepfn,
				       unsigned long startpfn,
				       unsigned long endpfn);
extern unsigned long init_bootmem(unsigned long addr, unsigned long memend);

extern unsigned long free_all_bootmem(void);
extern void reset_node_managed_pages(pg_data_t *pgdat);
extern void reset_all_zones_managed_pages(void);

extern void free_bootmem_node(pg_data_t *pgdat,
			      unsigned long addr,
			      unsigned long size);
extern void free_bootmem(unsigned long physaddr, unsigned long size);
extern void free_bootmem_late(unsigned long physaddr, unsigned long size);

/*
 * Flags for reserve_bootmem (also if CONFIG_HAVE_ARCH_BOOTMEM_NODE,
 * the architecture-specific code should honor this).
 *
 * If flags is BOOTMEM_DEFAULT, then the return value is always 0 (success).
 * If flags contains BOOTMEM_EXCLUSIVE, then -EBUSY is returned if the memory
 * already was reserved.
 */
#define BOOTMEM_DEFAULT		0
#define BOOTMEM_EXCLUSIVE	(1<<0)

extern int reserve_bootmem(unsigned long addr,
			   unsigned long size,
			   int flags);
extern int reserve_bootmem_node(pg_data_t *pgdat,
				unsigned long physaddr,
				unsigned long size,
				int flags);

extern void *__alloc_bootmem(unsigned long size,
			     unsigned long align,
			     unsigned long goal);
extern void *__alloc_bootmem_nopanic(unsigned long size,
				     unsigned long align,
				     unsigned long goal) __malloc;
extern void *__alloc_bootmem_node(pg_data_t *pgdat,
				  unsigned long size,
				  unsigned long align,
				  unsigned long goal) __malloc;
void *__alloc_bootmem_node_high(pg_data_t *pgdat,
				  unsigned long size,
				  unsigned long align,
				  unsigned long goal) __malloc;
extern void *__alloc_bootmem_node_nopanic(pg_data_t *pgdat,
				  unsigned long size,
				  unsigned long align,
				  unsigned long goal) __malloc;
void *___alloc_bootmem_node_nopanic(pg_data_t *pgdat,
				  unsigned long size,
				  unsigned long align,
				  unsigned long goal,
				  unsigned long limit) __malloc;
extern void *__alloc_bootmem_low(unsigned long size,
				 unsigned long align,
				 unsigned long goal) __malloc;
void *__alloc_bootmem_low_nopanic(unsigned long size,
				 unsigned long align,
				 unsigned long goal) __malloc;
extern void *__alloc_bootmem_low_node(pg_data_t *pgdat,
				      unsigned long size,
				      unsigned long align,
				      unsigned long goal) __malloc;

#ifdef CONFIG_NO_BOOTMEM
/* We are using top down, so it is safe to use 0 here */
#define BOOTMEM_LOW_LIMIT 0
#else
#define BOOTMEM_LOW_LIMIT __pa(MAX_DMA_ADDRESS)
#endif

#ifndef ARCH_LOW_ADDRESS_LIMIT
#define ARCH_LOW_ADDRESS_LIMIT  0xffffffffUL
#endif

#define alloc_bootmem(x) \
	__alloc_bootmem(x, SMP_CACHE_BYTES, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_align(x, align) \
	__alloc_bootmem(x, align, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_nopanic(x) \
	__alloc_bootmem_nopanic(x, SMP_CACHE_BYTES, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_pages(x) \
	__alloc_bootmem(x, PAGE_SIZE, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_pages_nopanic(x) \
	__alloc_bootmem_nopanic(x, PAGE_SIZE, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_node(pgdat, x) \
	__alloc_bootmem_node(pgdat, x, SMP_CACHE_BYTES, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_node_nopanic(pgdat, x) \
	__alloc_bootmem_node_nopanic(pgdat, x, SMP_CACHE_BYTES, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_pages_node(pgdat, x) \
	__alloc_bootmem_node(pgdat, x, PAGE_SIZE, BOOTMEM_LOW_LIMIT)
#define alloc_bootmem_pages_node_nopanic(pgdat, x) \
	__alloc_bootmem_node_nopanic(pgdat, x, PAGE_SIZE, BOOTMEM_LOW_LIMIT)

#define alloc_bootmem_low(x) \
	__alloc_bootmem_low(x, SMP_CACHE_BYTES, 0)
#define alloc_bootmem_low_pages_nopanic(x) \
	__alloc_bootmem_low_nopanic(x, PAGE_SIZE, 0)
#define alloc_bootmem_low_pages(x) \
	__alloc_bootmem_low(x, PAGE_SIZE, 0)
#define alloc_bootmem_low_pages_node(pgdat, x) \
	__alloc_bootmem_low_node(pgdat, x, PAGE_SIZE, 0)


#if defined(CONFIG_HAVE_MEMBLOCK) && defined(CONFIG_NO_BOOTMEM)

/* FIXME: use MEMBLOCK_ALLOC_* variants here */
#define BOOTMEM_ALLOC_ACCESSIBLE	0
#define BOOTMEM_ALLOC_ANYWHERE		(~(phys_addr_t)0)

/* FIXME: Move to memblock.h at a point where we remove nobootmem.c */
void *memblock_virt_alloc_try_nid_nopanic(phys_addr_t size,
		phys_addr_t align, phys_addr_t min_addr,
		phys_addr_t max_addr, int nid);
void *memblock_virt_alloc_try_nid(phys_addr_t size, phys_addr_t align,
		phys_addr_t min_addr, phys_addr_t max_addr, int nid);
void __memblock_free_early(phys_addr_t base, phys_addr_t size);
void __memblock_free_late(phys_addr_t base, phys_addr_t size);

static inline void * __init memblock_virt_alloc(
					phys_addr_t size,  phys_addr_t align)
{
	return memblock_virt_alloc_try_nid(size, align, BOOTMEM_LOW_LIMIT,
					    BOOTMEM_ALLOC_ACCESSIBLE,
					    NUMA_NO_NODE);
}

static inline void * __init memblock_virt_alloc_nopanic(
					phys_addr_t size, phys_addr_t align)
{
	return memblock_virt_alloc_try_nid_nopanic(size, align,
						    BOOTMEM_LOW_LIMIT,
						    BOOTMEM_ALLOC_ACCESSIBLE,
						    NUMA_NO_NODE);
}

static inline void * __init memblock_virt_alloc_low(
					phys_addr_t size, phys_addr_t align)
{
	return memblock_virt_alloc_try_nid(size, align,
						   BOOTMEM_LOW_LIMIT,
						   ARCH_LOW_ADDRESS_LIMIT,
						   NUMA_NO_NODE);
}
static inline void * __init memblock_virt_alloc_low_nopanic(
					phys_addr_t size, phys_addr_t align)
{
	return memblock_virt_alloc_try_nid_nopanic(size, align,
						   BOOTMEM_LOW_LIMIT,
						   ARCH_LOW_ADDRESS_LIMIT,
						   NUMA_NO_NODE);
}

static inline void * __init memblock_virt_alloc_from_nopanic(
		phys_addr_t size, phys_addr_t align, phys_addr_t min_addr)
{
	return memblock_virt_alloc_try_nid_nopanic(size, align, min_addr,
						    BOOTMEM_ALLOC_ACCESSIBLE,
						    NUMA_NO_NODE);
}

static inline void * __init memblock_virt_alloc_node(
						phys_addr_t size, int nid)
{
	return memblock_virt_alloc_try_nid(size, 0, BOOTMEM_LOW_LIMIT,
					    BOOTMEM_ALLOC_ACCESSIBLE, nid);
}

static inline void * __init memblock_virt_alloc_node_nopanic(
						phys_addr_t size, int nid)
{
	return memblock_virt_alloc_try_nid_nopanic(size, 0, BOOTMEM_LOW_LIMIT,
						    BOOTMEM_ALLOC_ACCESSIBLE,
						    nid);
}

static inline void __init memblock_free_early(
					phys_addr_t base, phys_addr_t size)
{
	__memblock_free_early(base, size);
}

static inline void __init memblock_free_early_nid(
				phys_addr_t base, phys_addr_t size, int nid)
{
	__memblock_free_early(base, size);
}

static inline void __init memblock_free_late(
					phys_addr_t base, phys_addr_t size)
{
	__memblock_free_late(base, size);
}

#else

#define BOOTMEM_ALLOC_ACCESSIBLE	0


/* Fall back to all the existing bootmem APIs */
/**
 * 在bootmem中分配内存
 */
static inline void * __init memblock_virt_alloc(
					phys_addr_t size,  phys_addr_t align)
{
	if (!align)
		align = SMP_CACHE_BYTES;
	return __alloc_bootmem(size, align, BOOTMEM_LOW_LIMIT);
}

static inline void * __init memblock_virt_alloc_nopanic(
					phys_addr_t size, phys_addr_t align)
{
	if (!align)
		align = SMP_CACHE_BYTES;
	return __alloc_bootmem_nopanic(size, align, BOOTMEM_LOW_LIMIT);
}

static inline void * __init memblock_virt_alloc_low(
					phys_addr_t size, phys_addr_t align)
{
	if (!align)
		align = SMP_CACHE_BYTES;
	return __alloc_bootmem_low(size, align, 0);
}

static inline void * __init memblock_virt_alloc_low_nopanic(
					phys_addr_t size, phys_addr_t align)
{
	if (!align)
		align = SMP_CACHE_BYTES;
	return __alloc_bootmem_low_nopanic(size, align, 0);
}

static inline void * __init memblock_virt_alloc_from_nopanic(
		phys_addr_t size, phys_addr_t align, phys_addr_t min_addr)
{
	return __alloc_bootmem_nopanic(size, align, min_addr);
}

static inline void * __init memblock_virt_alloc_node(
						phys_addr_t size, int nid)
{
	return __alloc_bootmem_node(NODE_DATA(nid), size, SMP_CACHE_BYTES,
				     BOOTMEM_LOW_LIMIT);
}

static inline void * __init memblock_virt_alloc_node_nopanic(
						phys_addr_t size, int nid)
{
	return __alloc_bootmem_node_nopanic(NODE_DATA(nid), size,
					     SMP_CACHE_BYTES,
					     BOOTMEM_LOW_LIMIT);
}

static inline void * __init memblock_virt_alloc_try_nid(phys_addr_t size,
	phys_addr_t align, phys_addr_t min_addr, phys_addr_t max_addr, int nid)
{
	return __alloc_bootmem_node_high(NODE_DATA(nid), size, align,
					  min_addr);
}

static inline void * __init memblock_virt_alloc_try_nid_nopanic(
			phys_addr_t size, phys_addr_t align,
			phys_addr_t min_addr, phys_addr_t max_addr, int nid)
{
	return ___alloc_bootmem_node_nopanic(NODE_DATA(nid), size, align,
				min_addr, max_addr);
}

static inline void __init memblock_free_early(
					phys_addr_t base, phys_addr_t size)
{
	free_bootmem(base, size);
}

static inline void __init memblock_free_early_nid(
				phys_addr_t base, phys_addr_t size, int nid)
{
	free_bootmem_node(NODE_DATA(nid), base, size);
}

static inline void __init memblock_free_late(
					phys_addr_t base, phys_addr_t size)
{
	free_bootmem_late(base, size);
}
#endif /* defined(CONFIG_HAVE_MEMBLOCK) && defined(CONFIG_NO_BOOTMEM) */

#ifdef CONFIG_HAVE_ARCH_ALLOC_REMAP
extern void *alloc_remap(int nid, unsigned long size);
#else
static inline void *alloc_remap(int nid, unsigned long size)
{
	return NULL;
}
#endif /* CONFIG_HAVE_ARCH_ALLOC_REMAP */

extern void *alloc_large_system_hash(const char *tablename,
				     unsigned long bucketsize,
				     unsigned long numentries,
				     int scale,
				     int flags,
				     unsigned int *_hash_shift,
				     unsigned int *_hash_mask,
				     unsigned long low_limit,
				     unsigned long high_limit);

#define HASH_EARLY	0x00000001	/* Allocating during early boot? */
#define HASH_SMALL	0x00000002	/* sub-page allocation allowed, min
					 * shift passed via *_hash_shift */

/* Only NUMA needs hash distribution. 64bit NUMA architectures have
 * sufficient vmalloc space.
 */
#ifdef CONFIG_NUMA
#define HASHDIST_DEFAULT IS_ENABLED(CONFIG_64BIT)
extern int hashdist;		/* Distribute hashes across NUMA nodes? */
#else
#define hashdist (0)
#endif


#endif /* _LINUX_BOOTMEM_H */
