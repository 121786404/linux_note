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

/*
Slub分配器的分配机制是：
处理内存申请请求时，获取当前cpu上的slab_cpu变量，
判断当前使用的page上是否由足够可用的空闲对象，
若有则直接返回空闲对象的指针。
否则会从partial list上或者node节点中获取可用的内存
*/
struct kmem_cache_cpu {
    // 下一空闲对象
	void **freelist;	/* Pointer to next available object */
	// transaction ID，保证操作的一致性
	unsigned long tid;	/* Globally unique transaction id */
	// 当前cpu分配对象所用的page
	struct page *page;	/* The slab from which we are allocating */
	// 当前cpu上被冻结的部分空page链表
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
struct kmem_cache {
    /*
       每个cpu独享一个副本，因此分配时并不需要加锁处理
       每个cpu上都有独立的可配分资源，
       线程在申请memory的时候会直接从当前cpu的cpu_slab中分配，
       这样就免去了加锁处理，提高了内存分配的速度。
       当cpu_slab中的可分配内存低于阈值时，
       slub将从node节点中获取更多的Page
    */
    struct kmem_cache_cpu __percpu *cpu_slab;
	/* Used for retriving partial slabs etc */
	/* 高速缓存永久属性的标识，如果SLAB描述符放在外部(不放在SLAB中)，
	则CFLAGS_OFF_SLAB置1 */
	unsigned long flags;
	/*  
	    node节点中部分空缓存区数量最小值；
          如果缓冲区数量大于此值时，
          多余的缓冲区将会被free到伙伴系统中
	*/
	unsigned long min_partial;
	/* 缓冲区中单个对象的所占内存空间大小，包括meta data。
	    size = object size + meta data size */
	int size;		/* The size of an object including meta data */
    /* 对象的实际大小 */
	int object_size;	/* The size of an object without meta data */
    /* free pointer的offset。Page被分为一个个内存块，
          各个内存块通过指针串接成链表，
          offset即指针在单个内存块中的偏移量 */
	int offset;		/* Free pointer offset. */
    /* cpu_slab中部分空对象的最大个数。
          当cpu_slab中部分空对象大于此值时，
          多出的对象将被放到node节点或者free到伙伴系统中 */
	int cpu_partial;	/* Number of per cpu partial objects to keep around */
    /* 保存slab缓冲区需要的页框数量的order值和objects数量的值，
        通过这个值可以计算出需要多少页框，这个是默认值，
        初始化时会根据经验计算这个值 */
	struct kmem_cache_order_objects oo;

	/* Allocation and freeing of slabs */
	/* 保存slab缓冲区需要的页框数量的order值和objects数量的值，
	这个是最大值 */
	struct kmem_cache_order_objects max;
	/* 保存slab缓冲区需要的页框数量的order值和objects数量的值，
	这个是最小值，当默认值oo分配失败时，
	会尝试用最小值去分配连续页框 */
	struct kmem_cache_order_objects min;
	/* 每一次分配时所使用的标志 */
	gfp_t allocflags;	/* gfp flags to use on each alloc */
	/* 重用计数器，当用户请求创建新的SLUB种类时，
	SLUB 分配器重用已创建的相似大小的SLUB，从而减少SLUB种类的个数。 */
	int refcount;		/* Refcount for slab cache destroy */
	/* 创建slab时的构造函数 */
	void (*ctor)(void *);
	/* 元数据的偏移量 */
	int inuse;		/* Offset to metadata */
	/* 对齐 */
	int align;		/* Alignment */
	int reserved;		/* Reserved bytes at the end of slabs */
	/* 高速缓存名字 */
	const char *name;	/* Name (only for display!) */
	/* 所有的 kmem_cache 结构都会链入这个链表，链表头是 slab_caches */
	struct list_head list;	/* List of slab caches */
	int red_left_pad;	/* Left redzone padding size */
#ifdef CONFIG_SYSFS
	struct kobject kobj;	/* For sysfs */
#endif
#ifdef CONFIG_MEMCG
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
	int remote_node_defrag_ratio;
#endif

#ifdef CONFIG_SLAB_FREELIST_RANDOM
	unsigned int *random_seq;
#endif

#ifdef CONFIG_KASAN
	struct kasan_cache kasan_info;
#endif
    /* 创建缓冲区的节点的 slab 信息 */
	struct kmem_cache_node *node[MAX_NUMNODES];
};

#ifdef CONFIG_SYSFS
#define SLAB_SUPPORTS_SYSFS
void sysfs_slab_release(struct kmem_cache *);
#else
static inline void sysfs_slab_release(struct kmem_cache *s)
{
}
#endif

void object_err(struct kmem_cache *s, struct page *page,
		u8 *object, char *reason);

void *fixup_red_left(struct kmem_cache *s, void *p);

static inline void *nearest_obj(struct kmem_cache *cache, struct page *page,
				void *x) {
	void *object = x - (x - page_address(page)) % cache->size;
	void *last_object = page_address(page) +
		(page->objects - 1) * cache->size;
	void *result = (unlikely(object > last_object)) ? last_object : object;

	result = fixup_red_left(cache, result);
	return result;
}

#endif /* _LINUX_SLUB_DEF_H */
