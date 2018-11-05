/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MM_CMA_H__
#define __MM_CMA_H__

struct cma {
/*
    CMA area的其实page frame number，base_pfn和count一起定义了该CMA area在内存在的位置
*/
	unsigned long   base_pfn;
/*
	该cma area内存有多少个page
	order_per_bit一起决定了bitmap指针指向内存的大小
*/
	unsigned long   count;
/*
	cma模块使用bitmap来管理其内存的分配，0表示free，1表示已经分配
*/
	unsigned long   *bitmap;
/*
	如果order_per_bit等于0，表示按照一个一个page来分配和释放，
	如果order_per_bit等于1，表示按照2个page组成的block来分配和释放，以此类推
*/
	unsigned int order_per_bit; /* Order of pages represented by one bit */
	struct mutex    lock;
#ifdef CONFIG_CMA_DEBUGFS
	struct hlist_head mem_head;
	spinlock_t mem_head_lock;
#endif
	const char *name;
};

extern struct cma cma_areas[MAX_CMA_AREAS];
extern unsigned cma_area_count;

static inline unsigned long cma_bitmap_maxno(struct cma *cma)
{
	return cma->count >> cma->order_per_bit;
}

#endif
