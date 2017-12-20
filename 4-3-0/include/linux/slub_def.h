#ifndef _LINUX_SLUB_DEF_H
#define _LINUX_SLUB_DEF_H

/*
 * SLUB : A Slab allocator without object queues.
 *
 * (C) 2007 SGI, Christoph Lameter
 */
#include <linux/kobject.h>

enum stat_item {
	ALLOC_FASTPATH,		/* Allocation from cpu slab */
	ALLOC_SLOWPATH,		/* Allocation by getting a new cpu slab */
	FREE_FASTPATH,		/* Free to cpu slab */
	FREE_SLOWPATH,		/* Freeing not to cpu slab */
	FREE_FROZEN,		/* Freeing to frozen slab */
	FREE_ADD_PARTIAL,	/* Freeing moves slab to partial list */
	FREE_REMOVE_PARTIAL,	/* Freeing removes last object */
	ALLOC_FROM_PARTIAL,	/* Cpu slab acquired from node partial list */
	ALLOC_SLAB,		/* Cpu slab acquired from page allocator */
	ALLOC_REFILL,		/* Refill cpu slab from slab freelist */
	ALLOC_NODE_MISMATCH,	/* Switching cpu slab */
	FREE_SLAB,		/* Slab freed to the page allocator */
	CPUSLAB_FLUSH,		/* Abandoning of the cpu slab */
	DEACTIVATE_FULL,	/* Cpu slab was full when deactivated */
	DEACTIVATE_EMPTY,	/* Cpu slab was empty when deactivated */
	DEACTIVATE_TO_HEAD,	/* Cpu slab was moved to the head of partials */
	DEACTIVATE_TO_TAIL,	/* Cpu slab was moved to the tail of partials */
	DEACTIVATE_REMOTE_FREES,/* Slab contained remotely freed objects */
	DEACTIVATE_BYPASS,	/* Implicit deactivation */
	ORDER_FALLBACK,		/* Number of times fallback was necessary */
	CMPXCHG_DOUBLE_CPU_FAIL,/* Failure of this_cpu_cmpxchg_double */
	CMPXCHG_DOUBLE_FAIL,	/* Number of times that cmpxchg double did not match */
	CPU_PARTIAL_ALLOC,	/* Used cpu partial on alloc */
	CPU_PARTIAL_FREE,	/* Refill cpu partial on free */
	CPU_PARTIAL_NODE,	/* Refill cpu partial from node partial */
	CPU_PARTIAL_DRAIN,	/* Drain cpu partial to node partial */
	NR_SLUB_STAT_ITEMS };

struct kmem_cache_cpu {
	/* 下一个可用空闲对象 */
	void **freelist;	/* Pointer to next available object */
	/* 全局事务ID，与锁相关 */
	unsigned long tid;	/* Globally unique transaction id */
	/**
	 * 从哪一个slab中分配
	 * 与page共用结构
	 * 从相应的页面中分配slab
	 */
	struct page *page;	/* The slab from which we are allocating */
	/* 部分空链表 */
	struct page *partial;	/* Partially allocated frozen slabs */
#ifdef CONFIG_SLUB_STATS
	unsigned stat[NR_SLUB_STAT_ITEMS];
#endif
};

/*
 * Word size structure that can be atomically updated or read and that
 * contains both the order and the number of objects that a slab of the
 * given order would contain.
 */
struct kmem_cache_order_objects {
	unsigned long x;
};

/*
 * Slab cache management.
 */
/**
 * SLAB描述符
 */
struct kmem_cache {
	/**
	 * 每CPU缓存SLAB
	 */
	struct kmem_cache_cpu __percpu *cpu_slab;
	/* Used for retriving partial slabs etc */
	/* 标记 */
	unsigned long flags;
	/* partial SLAB中，空缓冲区数量不能低于这个数 */
	unsigned long min_partial;
	/* 对象实际大小，含元数据 */
	int size;		/* The size of an object including meta data */
	/* 对象自身的大小 */
	int object_size;	/* The size of an object without meta data */
	/* 存放空闲指针的偏移量 */
	int offset;		/* Free pointer offset. */
	/**
	 * 每CPU可用partial对象数量
	 * 当每CPU空闲链表为空时，获取该值一半的数量到每CPU空闲链表中
	 */
	int cpu_partial;	/* Number of per cpu partial objects to keep around */
	/**
	 * 保存 slab对象所需要的页框数量order值
	 * 以及slab对象数量
	 * 分别是默认值，最大值、最小值
	 */
	struct kmem_cache_order_objects oo;

	/* Allocation and freeing of slabs */
	struct kmem_cache_order_objects max;
	struct kmem_cache_order_objects min;
	/* 分配页框的标志 */
	gfp_t allocflags;	/* gfp flags to use on each alloc */
	/**
	 * 引用计数
	 * 当用户创建类似SLUB种类时，重用已有的SLUB对象
	 * 这样可以减少数量
	 */
	int refcount;		/* Refcount for slab cache destroy */
	/* 构造函数，新slab对象被分配时，进行一些初始化工作 */
	void (*ctor)(void *);
	/* 元数据偏移量 */
	int inuse;		/* Offset to metadata */
	/* 用于对齐 */
	int align;		/* Alignment */
	/* 对象末尾的保留字节数 */
	int reserved;		/* Reserved bytes at the end of slabs */
	/* 缓存名称 */
	const char *name;	/* Name (only for display!) */
	/* 链接入全局slab_caches链表 */
	struct list_head list;	/* List of slab caches */
#ifdef CONFIG_SYSFS
	/* 指向/sys/slub专用目录 */
	struct kobject kobj;	/* For sysfs */
#endif
#ifdef CONFIG_MEMCG_KMEM
	/* 用于memory cgroup */
	struct memcg_cache_params memcg_params;
	int max_attr_size; /* for propagation, maximum size of a stored attr */
#ifdef CONFIG_SYSFS
	struct kset *memcg_kset;
#endif
#endif

#ifdef CONFIG_NUMA
	/*
	 * Defragmentation by allocating from a remote node.
	 */
	/* 反碎片值，值越小，越倾向于在本节点分配 */
	int remote_node_defrag_ratio;
#endif
	/**
	 * 本高速缓存的SLAB链表，每NUMA节点一个
	 */
	struct kmem_cache_node *node[MAX_NUMNODES];
};

#ifdef CONFIG_SYSFS
#define SLAB_SUPPORTS_SYSFS
void sysfs_slab_remove(struct kmem_cache *);
#else
static inline void sysfs_slab_remove(struct kmem_cache *s)
{
}
#endif


/**
 * virt_to_obj - returns address of the beginning of object.
 * @s: object's kmem_cache
 * @slab_page: address of slab page
 * @x: address within object memory range
 *
 * Returns address of the beginning of object
 */
static inline void *virt_to_obj(struct kmem_cache *s,
				const void *slab_page,
				const void *x)
{
	return (void *)x - ((x - slab_page) % s->size);
}

void object_err(struct kmem_cache *s, struct page *page,
		u8 *object, char *reason);

#endif /* _LINUX_SLUB_DEF_H */
