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
 ��νmemory model����ʵ���Ǵ�cpu�ĽǶȿ����������ڴ�ķֲ����
 
FPN = page frame number
*/
#if defined(CONFIG_FLATMEM)
/*
�����ϵͳ������һ��processor�ĽǶ�������
�������������ڴ��ʱ�������ַ�ռ���һ�������ģ�
û�пն��ĵ�ַ�ռ䣬��ô���ּ����ϵͳ���ڴ�ģ�;���Flat memory��
�����ڴ�ģ���£������ڴ�Ĺ���Ƚϼ򵥣�
ÿһ������ҳ֡������һ��page���ݽṹ������
���ϵͳ�д���һ��struct page�����飨mem_map����
ÿһ��������Ŀָ��һ��ʵ�ʵ�����ҳ֡��page frame����

��flat memory������£�PFN��mem_map����index�Ĺ�ϵ�����Ե�
FN��struct page���飨mem_map��index�����Թ�ϵ��
��һ���̶���ƫ�ƾ���ARCH_PFN_OFFSET��
����ڴ��Ӧ�������ַ����0����ôPFN��������index��

���⣬����flat memory model���ڵ㣨struct pglist_data��ֻ��һ��
��Ϊ�˺�Discontiguous Memory Model����ͬ���Ļ��ƣ���
*/
#define __pfn_to_page(pfn)	(mem_map + ((pfn) - ARCH_PFN_OFFSET))
#define __page_to_pfn(page)	((unsigned long)((page) - mem_map) + \
				 ARCH_PFN_OFFSET)
#elif defined(CONFIG_DISCONTIGMEM)
/*
���cpu�ڷ��������ڴ��ʱ�����ַ�ռ���һЩ�ն����ǲ������ģ�
��ô���ּ����ϵͳ���ڴ�ģ�;���Discontiguous memory��

һ����ԣ�NUMA�ܹ��ļ����ϵͳ��memory model����ѡ��Discontiguous Memory��
������������������ʵ�ǲ�ͬ�ġ�NUMAǿ������memory��processor��λ�ù�ϵ��
���ڴ�ģ����ʵ��û�й�ϵ�ģ�

ֻ����������ͬһnode�ϵ�memory��processor�и����ܵ���Ϲ�ϵ�����ʸ��죩��
�����Ҫ���node������

Discontiguous memory��������flat memory�ڴ�ģ�͵���չ��
���������ڴ��address space�󲿷��ǳ�Ƭ�Ĵ���ڴ棬
�м����һЩ�ն���ÿһ����Ƭ��memory address space����һ��node
�����������һ��node�ڲ������ڴ�ģ����flat memory����

ÿ���ڵ����������ڴ汣����struct pglist_data ���ݽṹ��node_mem_map��Ա��
����������flat memory�е�mem_map����

����ͨ��arch_pfn_to_nid��PFNת����node id��
ͨ��NODE_DATA�궨������ҵ���node��Ӧ��pglist_data���ݽṹ��
�����ݽṹ��node_start_pfn��¼�˸�node�ĵ�һ��page frame number��
��ˣ�Ҳ�Ϳ��Եõ����Ӧstruct page��node_mem_map��ƫ�ơ�
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
PFN����vmemmap���struct page�����index

#define vmemmap            ((struct page *)VMEMMAP_START - \ 
                 SECTION_ALIGN_DOWN(memstart_addr >> PAGE_SHIFT))

�������ʣ�������Ҫ�������ַ�ռ��з���һ�ε�ַ
������struct page���飨��������������������ڴ��ȿռ�page����
Ҳ����VMEMMAP_START�Ķ��塣
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
memory hotplug�ĳ�����ԭ����������Ʊ�ò������ˣ�
��Ϊ������һ��node�е�mem_maps[]Ҳ�п����ǲ������ˡ�

��ʵ���ڳ�����sparse memory֮��Discontiguous memory�ڴ�ģ���Ѿ�������ô��Ҫ�ˣ�
����˵sparse memory���տ������Discontiguous memory�ģ��������������ڽ����У�

Ϊʲô˵sparse memory���տ������Discontiguous memory�أ�
ʵ������sparse memory�ڴ�ģ���£������ĵ�ַ�ռ䰴��SECTION������1G�����ֳ���һ��һ�εģ�
����ÿһsection����hotplug�ģ����sparse memory�£��ڴ��ַ�ռ���Ա��зֵĸ�ϸ��
֧�ָ���ɢ��Discontiguous memory��

���⣬��sparse memoryû�г���֮ǰ��NUMA��Discontiguous memory���Ǽ����ϣ�
���ҵĹ�ϵ��NUMA��û�й涨���ڴ�������ԣ�
��Discontiguous memoryϵͳҲ����һ����NUMAϵͳ��
�������������ö���multi node�ġ�

����sparse memory֮���������ڿ��԰��ڴ�������Ժ�NUMA�ĸ�����뿪����
һ��NUMAϵͳ������flat memory��Ҳ������sparse memory��
��һ��sparse memoryϵͳ������NUMA��Ҳ������UMA�ġ�

���������������ַ�ռ��ǰ���һ��sectionһ��section���жϵģ�
ÿһ��section�ڲ�����memory�������ģ�������flat memory���ص㣩��
��ˣ�mem_map��page����������section�ṹ��struct mem_section��������node�ṹ�ˣ�struct pglist_data����

��Ȼ��������һ��memory model������Ҫ����PFN��page֮��Ķ�Ӧ��ϵ��
ֻ����sparse memory����һ��section�ĸ����ת�������PFN<--->Section<--->page��

�������ȿ�����δ�PFN��page�ṹ��ת����
kernel�о�̬������һ��mem_section��ָ�����飬
һ��section�������������page�������Ҫͨ�����ƽ�PFNת����section number��
��section number��Ϊindex��mem_sectionָ����������ҵ���PFN��Ӧ��section���ݽṹ��

�ҵ�section֮��������section_mem_map�Ϳ����ҵ���Ӧ��page���ݽṹ��
˳��һ����ǣ��ڿ�ʼ��ʱ��
sparse memoryʹ����һά��memory_section���飨����ָ�����飩��
������ʵ�ֶ����ر�ϡ�裨CONFIG_SPARSEMEM_EXTREME����ϵͳ�ǳ��˷��ڴ档
���⣬����ָ���hotplug��֧���ǱȽϷ���ģ�
ָ�����NULL����ζ�Ÿ�section�����ڡ�
���ڷ�SPARSEMEM_EXTREME���ã����ǿ���ʹ�ö�ά��mem_section����

��page��PFN��΢��һ���鷳��ʵ����PFN�ֳ��������֣�
һ������section index������һ��������page�ڸ�section��ƫ�ơ�

������Ҫ���ȴ�page�õ�section index��Ҳ�͵õ���Ӧ��memory_section��
֪����memory_sectionҲ��֪����page��section_mem_map��
Ҳ��֪����page�ڸ�section��ƫ�ƣ������Ժϳ�PFN��

����page��section index��ת����sparse memory��2�ַ�����
�����ȿ�������ķ�����Ҳ���Ǳ�����page->flags�У�������SECTION_IN_PAGE_FLAGS����
���ַ���������������page->flags�е�bit��Ŀ��һ�����ã�
��Ϊ���flag�г�����̫�����Ϣ������page flag��node id��zone id����������һ��section id��
�ڲ�ͬ��architecture���޷�ʵ��һ���Ե��㷨��
��û��һ��ͨ�õ��㷨�أ������CONFIG_SPARSEMEM_VMEMMAP��

���ھ����sparse memoryģ�ͣ�һ��section��struct page������ռ�õ��ڴ���������ӳ������
ҳ���ڳ�ʼ����ʱ��ͽ������ˣ�
������page frameҲ���Ƿ����������ַ��

���ǣ�����SPARSEMEM_VMEMMAP���ԣ������ַһ��ʼ�ͷ�����ˣ�
��vmemmap��ʼ��һ�������������ַ�ռ䣬
ÿһ��page����һ����Ӧ��struct page����Ȼ��ֻ�������ַ��û�������ַ��
��ˣ���һ��section�����ֺ󣬿��������ҵ���Ӧ��struct page�������ַ��
��Ȼ������Ҫ����һ�������page frame��Ȼ����ҳ��ʲô�ģ�
��ˣ���������sparse memory����������΢��һЩ�����˸�����ӳ��Ĺ��� )
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
