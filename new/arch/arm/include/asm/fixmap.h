#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H
/*
Fix map中的fix指的是固定的意思，那么固定什么东西呢？
其实就是虚拟地址是固定的，也就是说，有些虚拟地址在编译（compile-time）的时候就固定下来了，
而这些虚拟地址对应的物理地址不是固定的，是在kernel启动过程中被确定的

*/
/*
固定映射这些地址指向物理内存中的随机位置。
相对于内核空间起始处的线性映射，
在该映射内部的虚拟地址和物理地址之间的关联不是预设的，
而可以自由定义，但定义后不能改变。
固定映射区域会一直延伸到虚拟地址空间顶端。


*/
#define FIXADDR_START		0xffc00000UL
#define FIXADDR_END		0xfff00000UL
#define FIXADDR_TOP		(FIXADDR_END - PAGE_SIZE)

#include <asm/kmap_types.h>
#include <asm/pgtable.h>

/*
fixmap的地址区域有被进一步细分，如下

fixmap地址区域又分成了两个部分，
一部分叫做permanent fixed address，是用于具体的某个内核模块的，使用关系是永久性的。
另外一个叫做temporary fixed address，各个内核模块都可以使用，用完之后就释放，模块和虚拟地址之间是动态的关系


permanent fixed address主要涉及的模块包括：
（1）dtb解析模块。
（2）early console模块。
    标准的串口控制台驱动的初始化在整个kernel初始化过程中是很靠后的事情了，
    如果能够在kernel启动阶段的初期就有一个console，能够输出各种debug信息是多买美妙的事情啊，
    early console就能满足你的这个愿望，这个模块是使用early param来初始化该模块的功能的，
    因此可以很早就投入使用，从而协助工程师了解内核的启动过程。
（3）动态打补丁的模块。
    正文段一般都被映射成read only的，该模块可以使用fix mapped address来映射RW的正文段，
    从动态修改程序正文段，从而完成动态打补丁的功能。

temporary fixed address主要用于early ioremap模块。
linux kernel在fix map区域的虚拟地址空间中开了FIX_BTMAPS_SLOTS个的slot（每个slot的size是NR_FIX_BTMAPS），
内核中的模块都能够通过early_ioremap、early_iounmap的接口来申请或者释放对某个slot 虚拟地址的使用。
*/
enum fixed_addresses {
	FIX_EARLYCON_MEM_BASE,
	__end_of_permanent_fixed_addresses,

	FIX_KMAP_BEGIN = __end_of_permanent_fixed_addresses,
	FIX_KMAP_END = FIX_KMAP_BEGIN + (KM_TYPE_NR * NR_CPUS) - 1,

	/* Support writing RO kernel text via kprobes, jump labels, etc. */
	FIX_TEXT_POKE0,
	FIX_TEXT_POKE1,

	__end_of_fixmap_region,

	/*
	 * Share the kmap() region with early_ioremap(): this is guaranteed
	 * not to clash since early_ioremap() is only available before
	 * paging_init(), and kmap() only after.
	 */
#define NR_FIX_BTMAPS		32
#define FIX_BTMAPS_SLOTS	7
#define TOTAL_FIX_BTMAPS	(NR_FIX_BTMAPS * FIX_BTMAPS_SLOTS)

	FIX_BTMAP_END = __end_of_permanent_fixed_addresses,
	FIX_BTMAP_BEGIN = FIX_BTMAP_END + TOTAL_FIX_BTMAPS - 1,
	__end_of_early_ioremap_region
};

static const enum fixed_addresses __end_of_fixed_addresses =
	__end_of_fixmap_region > __end_of_early_ioremap_region ?
	__end_of_fixmap_region : __end_of_early_ioremap_region;

#define FIXMAP_PAGE_COMMON	(L_PTE_YOUNG | L_PTE_PRESENT | L_PTE_XN | L_PTE_DIRTY)

#define FIXMAP_PAGE_NORMAL	(FIXMAP_PAGE_COMMON | L_PTE_MT_WRITEBACK)
#define FIXMAP_PAGE_RO		(FIXMAP_PAGE_NORMAL | L_PTE_RDONLY)

/* Used by set_fixmap_(io|nocache), both meant for mapping a device */
#define FIXMAP_PAGE_IO		(FIXMAP_PAGE_COMMON | L_PTE_MT_DEV_SHARED | L_PTE_SHARED)
#define FIXMAP_PAGE_NOCACHE	FIXMAP_PAGE_IO

#define __early_set_fixmap	__set_fixmap

#ifdef CONFIG_MMU

void __set_fixmap(enum fixed_addresses idx, phys_addr_t phys, pgprot_t prot);
void __init early_fixmap_init(void);

#include <asm-generic/fixmap.h>

#else

static inline void early_fixmap_init(void) { }

#endif
#endif
