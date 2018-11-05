/* SPDX-License-Identifier: GPL-2.0 */
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
    /* 空闲对象队列的指针 */
	void **freelist;	/* Pointer to next available object */
	// transaction ID，保证操作的一致性
	/*
    	用于保证cmpxchg_double计算发生在正确的CPU上，
    	并且可作为一个锁保证不会同时申请这个kmem_cache_cpu的对象
	*/
	unsigned long tid;	/* Globally unique transaction id */
    /*
        指向slab对象来源的内存页面
    */
	struct page *page;	/* The slab from which we are allocating */
	// 当前cpu上被冻结的部分空page链表
#ifdef CONFIG_SLUB_CPU_PARTIAL
    /*
        指向曾分配完所有的对象，但当前已回收至少一个对象的page
    */
	struct page *partial;	/* Partially allocated frozen slabs */
#endif
#ifdef CONFIG_SLUB_STATS
    /*用以记录slab的状态*/
	unsigned stat[NR_SLUB_STAT_ITEMS];
#endif
};

#ifdef CONFIG_SLUB_CPU_PARTIAL
#define slub_percpu_partial(c)		((c)->partial)

#define slub_set_percpu_partial(c, p)		\
({						\
	slub_percpu_partial(c) = (p)->next;	\
})

#define slub_percpu_partial_read_once(c)     READ_ONCE(slub_percpu_partial(c))
#else
#define slub_percpu_partial(c)			NULL

#define slub_set_percpu_partial(c, p)

#define slub_percpu_partial_read_once(c)	NULL
#endif // CONFIG_SLUB_CPU_PARTIAL

/*
 * Word size structure that can be atomically updated or read and that
 * contains both the order and the number of objects that a slab of the
 * given order would contain.
 */
struct kmem_cache_order_objects {
	unsigned int x;
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
	/*
    	高速缓存永久属性的标识，如果SLAB描述符放在外部(不放在SLAB中)，
    	则CFLAGS_OFF_SLAB置1
	*/
	slab_flags_t flags;
	/*
        node结点中部分空slab缓冲区数量不能小于这个值,如果小于这个值,
        空闲slab缓冲区则不能够进行释放,而是将空闲slab加入到node结点的部分空slab链表中
	*/
	unsigned long min_partial;
	/*
	    缓冲区中单个对象的所占内存空间大小，包括meta data。
	    size = object size + meta data size
	*/
	unsigned int size;	/* The size of an object including meta data */
    /*
        对象的实际大小
    */
	unsigned int object_size;/* The size of an object without meta data */
    /*
        free pointer的offset。Page被分为一个个内存块，
        各个内存块通过指针串接成链表，
        offset即free pointer在单个内存块中的偏移量
    */
	unsigned int offset;	/* Free pointer offset. */
    /*
        cpu_slab中部分空对象的最大个数。
        当cpu_slab中部分空对象大于此值时，
        多出的对象将被放到node节点或者free到伙伴系统中

        每个CPU在partial链表中的最多对象个数

        该数据决定了：
        1）当使用到了极限时，每个CPU的partial slab释放到每个管理节点链表的个数；
        2）当使用完每个CPU的对象数时，CPU的partial slab来自每个管理节点的对象数。
    */
#ifdef CONFIG_SLUB_CPU_PARTIAL
	/* Number of per cpu partial objects to keep around */
	unsigned int cpu_partial;
#endif
    /*
        保存slab缓冲区需要的页框数量的order值和objects数量的值，
        通过这个值可以计算出需要多少页框，这个是默认值，
        初始化时会根据经验计算这个值
        存放分配给slab的页框的阶数(高16位)和slab中的对象数量(低16位)
    */
	struct kmem_cache_order_objects oo;

	/* Allocation and freeing of slabs */
	/*
    	保存slab缓冲区需要的页框数量的order值和objects数量的值，这个是最大值
	*/
	struct kmem_cache_order_objects max;
	/*
    	保存slab缓冲区需要的页框数量的order值和objects数量的值，
    	这个是最小值，当默认值oo分配失败时，会尝试用最小值去分配连续页框
	*/
	struct kmem_cache_order_objects min;
	/*
	    每一次分配时所使用的标志
	*/
	gfp_t allocflags;	/* gfp flags to use on each alloc */
	/*
    	重用计数器，当用户请求创建新的SLUB种类时，
    	SLUB 分配器重用已创建的相似大小的SLUB，从而减少SLUB种类的个数。
	*/
	int refcount;		/* Refcount for slab cache destroy */
	/*
	    创建slab时的构造函数
	*/
	void (*ctor)(void *);
	/*
	    元数据的偏移量
	*/
	unsigned int inuse;		/* Offset to metadata */
	/*
	    对齐
	*/
	unsigned int align;		/* Alignment */
	unsigned int red_left_pad;	/* Left redzone padding size */
	/*
	    高速缓存名字
	*/
	const char *name;	/* Name (only for display!) */
	/*
	    所有的 kmem_cache 结构都会链入这个链表，链表头是 slab_caches
	*/
	struct list_head list;	/* List of slab caches */
#ifdef CONFIG_SYSFS
	struct kobject kobj;	/* For sysfs */
	struct work_struct kobj_remove_work;
#endif
#ifdef CONFIG_MEMCG
	struct memcg_cache_params memcg_params;
	/* for propagation, maximum size of a stored attr */
	unsigned int max_attr_size;
#ifdef CONFIG_SYSFS
	struct kset *memcg_kset;
#endif
#endif

#ifdef CONFIG_SLAB_FREELIST_HARDENED
	unsigned long random;
#endif

#ifdef CONFIG_NUMA
	/*
	 * Defragmentation by allocating from a remote node.
	 */
	/*
	    该值越小，越倾向于从本节点分配对象
	*/
	unsigned int remote_node_defrag_ratio;
#endif

#ifdef CONFIG_SLAB_FREELIST_RANDOM
	unsigned int *random_seq;
#endif

#ifdef CONFIG_KASAN
	struct kasan_cache kasan_info;
#endif

	unsigned int useroffset;	/* Usercopy region offset */
	unsigned int usersize;		/* Usercopy region size */

	struct kmem_cache_node *node[MAX_NUMNODES];
};

#ifdef CONFIG_SLUB_CPU_PARTIAL
#define slub_cpu_partial(s)		((s)->cpu_partial)
#define slub_set_cpu_partial(s, n)		\
({						\
	slub_cpu_partial(s) = (n);		\
})
#else
#define slub_cpu_partial(s)		(0)
#define slub_set_cpu_partial(s, n)
#endif // CONFIG_SLUB_CPU_PARTIAL

#ifdef CONFIG_SYSFS
#define SLAB_SUPPORTS_SYSFS
void sysfs_slab_unlink(struct kmem_cache *);
void sysfs_slab_release(struct kmem_cache *);
#else
static inline void sysfs_slab_unlink(struct kmem_cache *s)
{
}
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
