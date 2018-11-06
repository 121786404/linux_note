// __aeabi_ldivmod

#include <linux/swap.h>
#include <linux/mm.h>
#include <linux/bitops.h>
#include <linux/jump_label.h>

void __bad_xchg(volatile void *ptr, int size)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
}

void __bad_cmpxchg(volatile void *ptr, int size)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
}

int __frontswap_store(struct page *page)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return 0;
}
int __frontswap_load(struct page *page)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return 0;
}

void __frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
}
void __frontswap_invalidate_area(unsigned type)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
}
int bpf_prog_offload_compile(struct bpf_prog *prog)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return 0;
}
void bpf_prog_offload_destroy(struct bpf_prog *prog)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
}
int change_huge_pmd(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long addr, pgprot_t newprot, int prot_numa)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return 0;
}
int copy_huge_pmd(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		  pmd_t *dst_pmd, pmd_t *src_pmd, unsigned long addr,
		  struct vm_area_struct *vma)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return 0;
}
int copy_huge_pud(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		  pud_t *dst_pud, pud_t *src_pud, unsigned long addr,
		  struct vm_area_struct *vma)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return 0;
}
int dax_delete_mapping_entry(struct address_space *mapping, pgoff_t index)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return 0;
}
int dax_invalidate_mapping_entry_sync(struct address_space *mapping,
				      pgoff_t index)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return 0;
}
vm_fault_t do_huge_pmd_anonymous_page(struct vm_fault *vmf)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return 0;
}
struct page *follow_trans_huge_pmd(struct vm_area_struct *vma,
				   unsigned long addr,
				   pmd_t *pmd,
				   unsigned int flags)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return NULL;
}
void huge_pmd_set_accessed(struct vm_fault *vmf, pmd_t orig_pmd)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
}
pgoff_t linear_hugepage_index(struct vm_area_struct *vma,
				     unsigned long address)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return 0;
}
bool madvise_free_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		pmd_t *pmd, unsigned long addr, unsigned long next)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return false;
}
int memcg_kmem_charge(struct page *page, gfp_t gfp, int order)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return 0;
}
struct kmem_cache *memcg_kmem_get_cache(struct kmem_cache *cachep)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return NULL;
}
void memcg_kmem_put_cache(struct kmem_cache *cachep)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
}
void memcg_kmem_uncharge(struct page *page, int order)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
}
bool move_huge_pmd(struct vm_area_struct *vma, unsigned long old_addr,
		  unsigned long new_addr, unsigned long old_end,
		  pmd_t *old_pmd, pmd_t *new_pmd)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return false;
}
pte_t pte_mkdevmap(pte_t pte)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return 0;
}
int zap_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		 pmd_t *pmd, unsigned long addr)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return 0;
}
int zap_huge_pud(struct mmu_gather *tlb, struct vm_area_struct *vma,
		 pud_t *pud, unsigned long addr)
{
	printk(KERN_CRIT "%s %d\n", __FUNCTION__,__LINE__);
	return 0;
}
