// SPDX-License-Identifier: GPL-2.0
/*
 *  bootmem - A boot-time physical memory allocator and configurator
 *
 *  Copyright (C) 1999 Ingo Molnar
 *                1999 Kanoj Sarcar, SGI
 *                2008 Johannes Weiner
 *
 * Access to this subsystem has to be serialized externally (which is true
 * for the boot process anyway).
 */
#include <linux/init.h>
#include <linux/pfn.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/kmemleak.h>
#include <linux/range.h>
#include <linux/memblock.h>
#include <linux/bootmem.h>

#include <asm/bug.h>
#include <asm/io.h>

#include "internal.h"
/*
为了实现接口兼容, 内核用mm/memblock.c定义的memblock接口,
实现了一套bootmem的接口机制, 而bootmem的接口我们在上一篇引导内存管理bootmem机制中已经讲过了,
这些实现的bootmem函数接口API, 就定义在mm/nobootmem.c文件中,
然后内核把他们进行了封装, 然后提供了与bootmem相同功能和函数的接口,
这些接口都在include/linux/memblock.h.

在NUMA系统上, 基本的API是相同的, 但是函数增加了_node后缀,
与UMA系统的函数相比, 还需要一些额外的参数, 用于指定内存分配的结点.

*/
#ifndef CONFIG_HAVE_MEMBLOCK
#error CONFIG_HAVE_MEMBLOCK not defined
#endif

#ifndef CONFIG_NEED_MULTIPLE_NODES
/* 管理所有的系统内存 */
struct pglist_data __refdata contig_page_data;
EXPORT_SYMBOL(contig_page_data);
#endif

/* 物理内存数量小于896 MiB的系统上内存页的数目*/
unsigned long max_low_pfn;
unsigned long min_low_pfn;
unsigned long max_pfn;
unsigned long long max_possible_pfn;

static void * __init __alloc_memory_core_early(int nid, u64 size, u64 align,
					u64 goal, u64 limit)
{
	void *ptr;
	u64 addr;
	enum memblock_flags flags = choose_memblock_flags();

	if (limit > memblock.current_limit)
		limit = memblock.current_limit;

again:
	addr = memblock_find_in_range_node(size, align, goal, limit, nid,
					   flags);
	if (!addr && (flags & MEMBLOCK_MIRROR)) {
		flags &= ~MEMBLOCK_MIRROR;
		pr_warn("Could not allocate %pap bytes of mirrored memory\n",
			&size);
		goto again;
	}
	if (!addr)
		return NULL;

	if (memblock_reserve(addr, size))
		return NULL;

	ptr = phys_to_virt(addr);
	memset(ptr, 0, size);
	/*
	 * The min_count is set to 0 so that bootmem allocated blocks
	 * are never reported as leaks.
	 */
	kmemleak_alloc(ptr, size, 0, 0);
	return ptr;
}

/**
 * free_bootmem_late - free bootmem pages directly to page allocator
 * @addr: starting address of the range
 * @size: size of the range in bytes
 *
 * This is only useful when the bootmem allocator has already been torn
 * down, but we are still initializing the system.  Pages are given directly
 * to the page allocator, no bootmem metadata is updated because it is gone.
 */
void __init free_bootmem_late(unsigned long addr, unsigned long size)
{
	unsigned long cursor, end;

	kmemleak_free_part_phys(addr, size);

	cursor = PFN_UP(addr);
	end = PFN_DOWN(addr + size);

	for (; cursor < end; cursor++) {
		__free_pages_bootmem(pfn_to_page(cursor), cursor, 0);
		totalram_pages++;
	}
}
/*
下面是向系统中添加一段内存的情况，页帧号范围为[Ox8800e， Oxaecea] ，以start 为起始来计算其order ，
一开始order 的数值还比较凌乱，等到start 和Ox400 对齐，以后基本上order 都取值为10 了，
也就是都挂入order 为10 的free list 链表中。
free_pages_memory: start=Ox8800e , end=Oxaecea
free_pages_memory: start=Ox8800e , order=l  , __ffs()=l  , ffs()=2
free_pages_memory: start=Ox88010 , order=4  , __ffs()=4  , ffs()=5
free_pages_memory: start=Ox88020 , order=5  , __ffs()=5  , ffs()=6
free_pages_memory: start=Ox88040 , order=6  , __ffs()=6  , ffs()=7
free_pages_memory: start=Ox88080 , order=7  , __ffs()=7  , ffs()=8
free_pages_memory: start=Ox88100 , order=8  , __ffs()=8  , ffs()=9
free_pages_memory: start=Ox88200 , order=9  , __ffs()=9  , ffs()=10
free_pages_memory: start=Ox88400 , order=10 , __ffs()=10 , ffs()=l1
free_pages_memory: start=Ox88800 , order=10 , __ffs()=ll , ffs()=12
free_pages_memory: start=Ox88cOO , order=10 , __ffs()=10 , ffs()=ll
free_pages_memory: start=Ox89000 , order=10 , __ffs()=12 , ffs()=13
free_pages_memory: start=Ox89400 , order=10 , __ffs()=10 , ffs()=ll
free_pages_memory: start=Ox89800 , order=lO , __ffs()=ll , ffs()=12
free_pages_memory: start=Ox89cOO , order=10 , __ffs()=10 , ffs()=ll
*/
static void __init __free_pages_memory(unsigned long start, unsigned long end)
{
	int order;
/*
    从起始页帧号start遍历到emd
*/
    //pr_debug("start %ul ,end: %ul",start,end);

	while (start < end) {
/*
	order 取MAX_ORDER -1 和 _ffs(start) 的最小值
	因为伙伴系统的链表都是2 的n 次幕，最大的链表是2 的10 次方，也就是1024 ，即Ox400 。
	所以，通过ffs函数可以很方便地计算出地址的对齐边界。
	例如start 等于Ox63300 ，那么_ffs(Ox63300)等于8 ，那么这里order 选用8
*/
		order = min(MAX_ORDER - 1UL, __ffs(start));

		while (start + (1UL << order) > end)
			order--;

		__free_pages_bootmem(pfn_to_page(start), start, order);
/*
        循环的步长和order有关
*/
		start += (1UL << order);
	}
}

static unsigned long __init __free_memory_core(phys_addr_t start,
				 phys_addr_t end)
{
	unsigned long start_pfn = PFN_UP(start);
	unsigned long end_pfn = min_t(unsigned long,
				      PFN_DOWN(end), max_low_pfn);

	if (start_pfn >= end_pfn)
		return 0;

	__free_pages_memory(start_pfn, end_pfn);

	return end_pfn - start_pfn;
}

static unsigned long __init free_low_memory_core_early(void)
{
	unsigned long count = 0;
	phys_addr_t start, end;
	u64 i;

	memblock_clear_hotplug(0, -1);

	for_each_reserved_mem_region(i, &start, &end)
		reserve_bootmem_region(start, end);

	/*
	 * We need to use NUMA_NO_NODE instead of NODE_DATA(0)->node_id
	 *  because in some case like Node0 doesn't have RAM installed
	 *  low ram will be on Node1
	 */
/*
	 遍历所有的memblock 内存块，找出内存块的起始地址和结束地址。
*/
	for_each_free_mem_range(i, NUMA_NO_NODE, MEMBLOCK_NONE, &start, &end,
				NULL)
/*
		把内存块传递到__free_memory_core中
*/
		count += __free_memory_core(start, end);

	return count;
}

static int reset_managed_pages_done __initdata;

void reset_node_managed_pages(pg_data_t *pgdat)
{
	struct zone *z;

	for (z = pgdat->node_zones; z < pgdat->node_zones + MAX_NR_ZONES; z++)
		z->managed_pages = 0;
}

void __init reset_all_zones_managed_pages(void)
{
	struct pglist_data *pgdat;

	if (reset_managed_pages_done)
		return;

	for_each_online_pgdat(pgdat)
		reset_node_managed_pages(pgdat);

	reset_managed_pages_done = 1;
}

/**
 * free_all_bootmem - release free pages to the buddy allocator
 *
 * Return: the number of pages actually released.
 */
/*
 释放低端memory到伙伴系统
 释放bootmem分配器管理的空闲页框和bootmem的位图所占用的页框，         并计入totalram_page
*/
unsigned long __init free_all_bootmem(void)
{
	unsigned long pages;

	reset_all_zones_managed_pages();

	pages = free_low_memory_core_early();
	totalram_pages += pages;

	return pages;
}

/**
 * free_bootmem_node - mark a page range as usable
 * @pgdat: node the range resides on
 * @physaddr: starting physical address of the range
 * @size: size of the range in bytes
 *
 * Partial pages will be considered reserved and left as they are.
 *
 * The range must reside completely on the specified node.
 */
void __init free_bootmem_node(pg_data_t *pgdat, unsigned long physaddr,
			      unsigned long size)
{
	memblock_free(physaddr, size);
}

/**
 * free_bootmem - mark a page range as usable
 * @addr: starting physical address of the range
 * @size: size of the range in bytes
 *
 * Partial pages will be considered reserved and left as they are.
 *
 * The range must be contiguous but may span node boundaries.
 */
void __init free_bootmem(unsigned long addr, unsigned long size)
{
	memblock_free(addr, size);
}

static void * __init ___alloc_bootmem_nopanic(unsigned long size,
					unsigned long align,
					unsigned long goal,
					unsigned long limit)
{
	void *ptr;

	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc(size, GFP_NOWAIT);

restart:

	ptr = __alloc_memory_core_early(NUMA_NO_NODE, size, align, goal, limit);

	if (ptr)
		return ptr;

	if (goal != 0) {
		goal = 0;
		goto restart;
	}

	return NULL;
}

/**
 * __alloc_bootmem_nopanic - allocate boot memory without panicking
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may happen on any node in the system.
 *
 * Return: address of the allocated region or %NULL on failure.
 */
void * __init __alloc_bootmem_nopanic(unsigned long size, unsigned long align,
					unsigned long goal)
{
	unsigned long limit = -1UL;

	return ___alloc_bootmem_nopanic(size, align, goal, limit);
}

static void * __init ___alloc_bootmem(unsigned long size, unsigned long align,
					unsigned long goal, unsigned long limit)
{
	void *mem = ___alloc_bootmem_nopanic(size, align, goal, limit);

	if (mem)
		return mem;
	/*
	 * Whoops, we cannot satisfy the allocation request.
	 */
	pr_alert("bootmem alloc of %lu bytes failed!\n", size);
	panic("Out of memory");
	return NULL;
}

/**
 * __alloc_bootmem - allocate boot memory
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may happen on any node in the system.
 *
 * The function panics if the request can not be satisfied.
 *
 * Return: address of the allocated region.
 */
void * __init __alloc_bootmem(unsigned long size, unsigned long align,
			      unsigned long goal)
{
	unsigned long limit = -1UL;

	return ___alloc_bootmem(size, align, goal, limit);
}

void * __init ___alloc_bootmem_node_nopanic(pg_data_t *pgdat,
						   unsigned long size,
						   unsigned long align,
						   unsigned long goal,
						   unsigned long limit)
{
	void *ptr;

again:
	ptr = __alloc_memory_core_early(pgdat->node_id, size, align,
					goal, limit);
	if (ptr)
		return ptr;

	ptr = __alloc_memory_core_early(NUMA_NO_NODE, size, align,
					goal, limit);
	if (ptr)
		return ptr;

	if (goal) {
		goal = 0;
		goto again;
	}

	return NULL;
}

void * __init __alloc_bootmem_node_nopanic(pg_data_t *pgdat, unsigned long size,
				   unsigned long align, unsigned long goal)
{
	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc_node(size, GFP_NOWAIT, pgdat->node_id);

	return ___alloc_bootmem_node_nopanic(pgdat, size, align, goal, 0);
}

static void * __init ___alloc_bootmem_node(pg_data_t *pgdat, unsigned long size,
				    unsigned long align, unsigned long goal,
				    unsigned long limit)
{
	void *ptr;

	ptr = ___alloc_bootmem_node_nopanic(pgdat, size, align, goal, limit);
	if (ptr)
		return ptr;

	pr_alert("bootmem alloc of %lu bytes failed!\n", size);
	panic("Out of memory");
	return NULL;
}

/**
 * __alloc_bootmem_node - allocate boot memory from a specific node
 * @pgdat: node to allocate from
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may fall back to any node in the system if the specified node
 * can not hold the requested memory.
 *
 * The function panics if the request can not be satisfied.
 *
 * Return: address of the allocated region.
 */
void * __init __alloc_bootmem_node(pg_data_t *pgdat, unsigned long size,
				   unsigned long align, unsigned long goal)
{
	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc_node(size, GFP_NOWAIT, pgdat->node_id);

	return ___alloc_bootmem_node(pgdat, size, align, goal, 0);
}

void * __init __alloc_bootmem_node_high(pg_data_t *pgdat, unsigned long size,
				   unsigned long align, unsigned long goal)
{
	return __alloc_bootmem_node(pgdat, size, align, goal);
}


/**
 * __alloc_bootmem_low - allocate low boot memory
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may happen on any node in the system.
 *
 * The function panics if the request can not be satisfied.
 *
 * Return: address of the allocated region.
 */
void * __init __alloc_bootmem_low(unsigned long size, unsigned long align,
				  unsigned long goal)
{
	return ___alloc_bootmem(size, align, goal, ARCH_LOW_ADDRESS_LIMIT);
}

void * __init __alloc_bootmem_low_nopanic(unsigned long size,
					  unsigned long align,
					  unsigned long goal)
{
	return ___alloc_bootmem_nopanic(size, align, goal,
					ARCH_LOW_ADDRESS_LIMIT);
}

/**
 * __alloc_bootmem_low_node - allocate low boot memory from a specific node
 * @pgdat: node to allocate from
 * @size: size of the request in bytes
 * @align: alignment of the region
 * @goal: preferred starting address of the region
 *
 * The goal is dropped if it can not be satisfied and the allocation will
 * fall back to memory below @goal.
 *
 * Allocation may fall back to any node in the system if the specified node
 * can not hold the requested memory.
 *
 * The function panics if the request can not be satisfied.
 *
 * Return: address of the allocated region.
 */
void * __init __alloc_bootmem_low_node(pg_data_t *pgdat, unsigned long size,
				       unsigned long align, unsigned long goal)
{
	if (WARN_ON_ONCE(slab_is_available()))
		return kzalloc_node(size, GFP_NOWAIT, pgdat->node_id);

	return ___alloc_bootmem_node(pgdat, size, align, goal,
				     ARCH_LOW_ADDRESS_LIMIT);
}
