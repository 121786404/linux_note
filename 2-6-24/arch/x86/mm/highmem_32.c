#include <linux/highmem.h>
#include <linux/module.h>

/**
 * �������˸߶��ڴ������£�ʹ�ô˺������߶��ڴ�ӳ�䵽�ں���
 */
void *kmap(struct page *page)
{
	might_sleep();
	if (!PageHighMem(page))/* ҳ�����ڵͶ˵�ַ���ں˿���ֱ�ӷ��� */
		return page_address(page);/* ����ҳ�������ӳ���ַ */
	/* ���߶��ڴ�ӳ�䵽�ں˵�ַ�ռ��� */
	return kmap_high(page);
}

/**
 * �����kmap������ӳ��
 */
void kunmap(struct page *page)
{
	if (in_interrupt())/* ���������ж��е���kmap��Ҳ���������жϵ���kunmap */
		BUG();
	if (!PageHighMem(page))/* ������Ǹ߶��ڴ棬��ֱ���˳� */
		return;
	/* ����߶��ڴ��kmapӳ�� */
	kunmap_high(page);
}

/*
 * kmap_atomic/kunmap_atomic is significantly faster than kmap/kunmap because
 * no global lock is needed and because the kmap code must perform a global TLB
 * invalidation when the kmap pool wraps.
 *
 * However when holding an atomic kmap is is not legal to sleep, so atomic
 * kmaps are appropriate for short, tight code paths only.
 */
void *kmap_atomic_prot(struct page *page, enum km_type type, pgprot_t prot)
{
	enum fixed_addresses idx;
	unsigned long vaddr;

	/* even !CONFIG_PREEMPT needs this, for in_atomic in do_page_fault */
	pagefault_disable();

	if (!PageHighMem(page))/* ������Ǹ߶��ڴ棬ֱ�ӷ��������Ե�ַ���� */
		return page_address(page);

	/* ���������ȫ�������е�λ�ã��뵱ǰCPU�й� */
	idx = type + KM_TYPE_NR*smp_processor_id();
	/* ���������ַ */
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	/* ��Ӧ��pteӦ����û��ӳ��� */
	BUG_ON(!pte_none(*(kmap_pte-idx)));
	/* ��ҳ��ӳ�䵽�����ַ */
	set_pte(kmap_pte-idx, mk_pte(page, prot));
	arch_flush_lazy_mmu_mode();

	return (void *)vaddr;
}

/**
 * ԭ�ӵ�kmapӳ��һ��ҳ��
 * ���������жϴ�������
 */
void *kmap_atomic(struct page *page, enum km_type type)
{
	return kmap_atomic_prot(page, type, kmap_prot);
}

/* �����kmap_atomic������ӳ�� */
void kunmap_atomic(void *kvaddr, enum km_type type)
{
	unsigned long vaddr = (unsigned long) kvaddr & PAGE_MASK;
	/* ������kmap�����е����� */
	enum fixed_addresses idx = type + KM_TYPE_NR*smp_processor_id();

	/*
	 * Force other mappings to Oops if they'll try to access this pte
	 * without first remap it.  Keeping stale mappings around is a bad idea
	 * also, in case the page changes cacheability attributes or becomes
	 * a protected page in a hypervisor.
	 */
	if (vaddr == __fix_to_virt(FIX_KMAP_BEGIN+idx))
		kpte_clear_flush(kmap_pte-idx, vaddr);/* ���pteӳ�� */
	else {
#ifdef CONFIG_DEBUG_HIGHMEM
		BUG_ON(vaddr < PAGE_OFFSET);
		BUG_ON(vaddr >= (unsigned long)high_memory);
#endif
	}

	arch_flush_lazy_mmu_mode();
	pagefault_enable();
}

/* This is the same as kmap_atomic() but can map memory that doesn't
 * have a struct page associated with it.
 */
void *kmap_atomic_pfn(unsigned long pfn, enum km_type type)
{
	enum fixed_addresses idx;
	unsigned long vaddr;

	pagefault_disable();

	idx = type + KM_TYPE_NR*smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	set_pte(kmap_pte-idx, pfn_pte(pfn, kmap_prot));
	arch_flush_lazy_mmu_mode();

	return (void*) vaddr;
}

struct page *kmap_atomic_to_page(void *ptr)
{
	unsigned long idx, vaddr = (unsigned long)ptr;
	pte_t *pte;

	if (vaddr < FIXADDR_START)
		return virt_to_page(ptr);

	idx = virt_to_fix(vaddr);
	pte = kmap_pte - (idx - FIX_KMAP_BEGIN);
	return pte_page(*pte);
}

EXPORT_SYMBOL(kmap);
EXPORT_SYMBOL(kunmap);
EXPORT_SYMBOL(kmap_atomic);
EXPORT_SYMBOL(kunmap_atomic);
EXPORT_SYMBOL(kmap_atomic_to_page);
