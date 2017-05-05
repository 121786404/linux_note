#ifndef __ASM_MEMORY_MODEL_H
#define __ASM_MEMORY_MODEL_H

#include <linux/pfn.h>

#ifndef __ASSEMBLY__

#if defined(CONFIG_FLATMEM)

#ifndef ARCH_PFN_OFFSET
#define ARCH_PFN_OFFSET		(0UL)
#endif

#elif defined(CONFIG_DISCONTIGMEM)

#ifndef arch_pfn_to_nid
#define arch_pfn_to_nid(pfn)	pfn_to_nid(pfn)
#endif

#ifndef arch_local_page_offset
#define arch_local_page_offset(pfn, nid)	\
	((pfn) - NODE_DATA(nid)->node_start_pfn)
#endif

#endif /* CONFIG_DISCONTIGMEM */

/*
 * supports 3 memory models.
 */
/*
 所谓memory model，其实就是从cpu的角度看，其物理内存的分布情况
 
FPN = page frame number
*/
#if defined(CONFIG_FLATMEM)
/*
如果从系统中任意一个processor的角度来看，
当它访问物理内存的时候，物理地址空间是一个连续的，
没有空洞的地址空间，那么这种计算机系统的内存模型就是Flat memory。
这种内存模型下，物理内存的管理比较简单，
每一个物理页帧都会有一个page数据结构来抽象，
因此系统中存在一个struct page的数组（mem_map），
每一个数组条目指向一个实际的物理页帧（page frame）。

在flat memory的情况下，PFN和mem_map数组index的关系是线性的
FN和struct page数组（mem_map）index是线性关系，
有一个固定的偏移就是ARCH_PFN_OFFSET，
如果内存对应的物理地址等于0，那么PFN就是数组index。

此外，对于flat memory model，节点（struct pglist_data）只有一个
（为了和Discontiguous Memory Model采用同样的机制）。
*/
#define __pfn_to_page(pfn)	(mem_map + ((pfn) - ARCH_PFN_OFFSET))
#define __page_to_pfn(page)	((unsigned long)((page) - mem_map) + \
				 ARCH_PFN_OFFSET)
#elif defined(CONFIG_DISCONTIGMEM)
/*
如果cpu在访问物理内存的时候，其地址空间有一些空洞，是不连续的，
那么这种计算机系统的内存模型就是Discontiguous memory。

一般而言，NUMA架构的计算机系统的memory model都是选择Discontiguous Memory，
不过，这两个概念其实是不同的。NUMA强调的是memory和processor的位置关系，
和内存模型其实是没有关系的，

只不过，由于同一node上的memory和processor有更紧密的耦合关系（访问更快），
因此需要多个node来管理。

Discontiguous memory本质上是flat memory内存模型的扩展，
整个物理内存的address space大部分是成片的大块内存，
中间会有一些空洞，每一个成片的memory address space属于一个node
（如果局限在一个node内部，其内存模型是flat memory）。

每个节点管理的物理内存保存在struct pglist_data 数据结构的node_mem_map成员中
（概念类似flat memory中的mem_map）。

首先通过arch_pfn_to_nid将PFN转换成node id，
通过NODE_DATA宏定义可以找到该node对应的pglist_data数据结构，
该数据结构的node_start_pfn记录了该node的第一个page frame number，
因此，也就可以得到其对应struct page在node_mem_map的偏移。
*/
#define __pfn_to_page(pfn)			\
({	unsigned long __pfn = (pfn);		\
	unsigned long __nid = arch_pfn_to_nid(__pfn);  \
	NODE_DATA(__nid)->node_mem_map + arch_local_page_offset(__pfn, __nid);\
})

#define __page_to_pfn(pg)						\
({	const struct page *__pg = (pg);					\
	struct pglist_data *__pgdat = NODE_DATA(page_to_nid(__pg));	\
	(unsigned long)(__pg - __pgdat->node_mem_map) +			\
	 __pgdat->node_start_pfn;					\
})

#elif defined(CONFIG_SPARSEMEM_VMEMMAP)

/*
PFN就是vmemmap这个struct page数组的index

#define vmemmap            ((struct page *)VMEMMAP_START - \ 
                 SECTION_ALIGN_DOWN(memstart_addr >> PAGE_SHIFT))

毫无疑问，我们需要在虚拟地址空间中分配一段地址
来安放struct page数组（该数组包含了所有物理内存跨度空间page），
也就是VMEMMAP_START的定义。
*/

/* memmap is virtually contiguous.  */
#define __pfn_to_page(pfn)	(vmemmap + (pfn))
#define __page_to_pfn(page)	(unsigned long)((page) - vmemmap)

#elif defined(CONFIG_SPARSEMEM)
/*
 * Note: section's mem_map is encoded to reflect its start_pfn.
 * section[i].section_mem_map == mem_map's address - start_pfn;
 */
/*
memory hotplug的出现让原来完美的设计变得不完美了，
因为即便是一个node中的mem_maps[]也有可能是不连续了。

其实，在出现了sparse memory之后，Discontiguous memory内存模型已经不是那么重要了，
按理说sparse memory最终可以替代Discontiguous memory的，这个替代过程正在进行中，

为什么说sparse memory最终可以替代Discontiguous memory呢？
实际上在sparse memory内存模型下，连续的地址空间按照SECTION（例如1G）被分成了一段一段的，
其中每一section都是hotplug的，因此sparse memory下，内存地址空间可以被切分的更细，
支持更离散的Discontiguous memory。

此外，在sparse memory没有出现之前，NUMA和Discontiguous memory总是剪不断，
理还乱的关系：NUMA并没有规定其内存的连续性，
而Discontiguous memory系统也并非一定是NUMA系统，
但是这两种配置都是multi node的。

有了sparse memory之后，我们终于可以把内存的连续性和NUMA的概念剥离开来：
一个NUMA系统可以是flat memory，也可以是sparse memory，
而一个sparse memory系统可以是NUMA，也可以是UMA的。

整个连续的物理地址空间是按照一个section一个section来切断的，
每一个section内部，其memory是连续的（即符合flat memory的特点），
因此，mem_map的page数组依附于section结构（struct mem_section）而不是node结构了（struct pglist_data）。

当然，无论哪一种memory model，都需要处理PFN和page之间的对应关系，
只不过sparse memory多了一个section的概念，让转换变成了PFN<--->Section<--->page。

我们首先看看如何从PFN到page结构的转换：
kernel中静态定义了一个mem_section的指针数组，
一个section中往往包括多个page，因此需要通过右移将PFN转换成section number，
用section number做为index在mem_section指针数组可以找到该PFN对应的section数据结构。

找到section之后，沿着其section_mem_map就可以找到对应的page数据结构。
顺便一提的是，在开始的时候，
sparse memory使用了一维的memory_section数组（不是指针数组），
这样的实现对于特别稀疏（CONFIG_SPARSEMEM_EXTREME）的系统非常浪费内存。
此外，保存指针对hotplug的支持是比较方便的，
指针等于NULL就意味着该section不存在。
对于非SPARSEMEM_EXTREME配置，我们可以使用二维的mem_section数组

从page到PFN稍微有一点麻烦，实际上PFN分成两个部分：
一部分是section index，另外一个部分是page在该section的偏移。

我们需要首先从page得到section index，也就得到对应的memory_section，
知道了memory_section也就知道该page在section_mem_map，
也就知道了page在该section的偏移，最后可以合成PFN。

对于page到section index的转换，sparse memory有2种方案，
我们先看看经典的方案，也就是保存在page->flags中（配置了SECTION_IN_PAGE_FLAGS）。
这种方法的最大的问题是page->flags中的bit数目不一定够用，
因为这个flag中承载了太多的信息，各种page flag，node id，zone id现在又增加一个section id，
在不同的architecture中无法实现一致性的算法，
有没有一种通用的算法呢？这就是CONFIG_SPARSEMEM_VMEMMAP。

对于经典的sparse memory模型，一个section的struct page数组所占用的内存来自线性映射区，
页表在初始化的时候就建立好了，
分配了page frame也就是分配了虚拟地址。

但是，对于SPARSEMEM_VMEMMAP而言，虚拟地址一开始就分配好了，
是vmemmap开始的一段连续的虚拟地址空间，
每一个page都有一个对应的struct page，当然，只有虚拟地址，没有物理地址。
因此，当一个section被发现后，可以立刻找到对应的struct page的虚拟地址，
当然，还需要分配一个物理的page frame，然后建立页表什么的，
因此，对于这种sparse memory，开销会稍微大一些（多了个建立映射的过程 )
*/
#define __page_to_pfn(pg)					\
({	const struct page *__pg = (pg);				\
	int __sec = page_to_section(__pg);			\
	(unsigned long)(__pg - __section_mem_map_addr(__nr_to_section(__sec)));	\
})

#define __pfn_to_page(pfn)				\
({	unsigned long __pfn = (pfn);			\
	struct mem_section *__sec = __pfn_to_section(__pfn);	\
	__section_mem_map_addr(__sec) + __pfn;		\
})
#endif /* CONFIG_FLATMEM/DISCONTIGMEM/SPARSEMEM */

/*
 * Convert a physical address to a Page Frame Number and back
 */
#define	__phys_to_pfn(paddr)	PHYS_PFN(paddr)
#define	__pfn_to_phys(pfn)	PFN_PHYS(pfn)

#define page_to_pfn __page_to_pfn
#define pfn_to_page __pfn_to_page

#endif /* __ASSEMBLY__ */

#endif
