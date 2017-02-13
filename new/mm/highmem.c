/*
 * High memory handling common code and variables.
 *
 * (C) 1999 Andrea Arcangeli, SuSE GmbH, andrea@suse.de
 *          Gerhard Wichert, Siemens AG, Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with
 * 64-bit physical space. With current x86 CPUs this
 * means up to 64 Gigabytes physical RAM.
 *
 * Rewrote high memory support to move the page cache into
 * high memory. Implemented permanent (schedulable) kmaps
 * based on Linus' idea.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#include <linux/mm.h>
#include <linux/export.h>
#include <linux/swap.h>
#include <linux/bio.h>
#include <linux/pagemap.h>
#include <linux/mempool.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/highmem.h>
#include <linux/kgdb.h>
#include <asm/tlbflush.h>


#if defined(CONFIG_HIGHMEM) || defined(CONFIG_X86_32)
DEFINE_PER_CPU(int, __kmap_atomic_idx);
#endif

/*
 * Virtual_count is not a pure "count".
 *  0 means that it is not mapped, and has not been mapped
 *    since a TLB flush - it is usable.
 *  1 means that there are no users, but it has been mapped
 *    since the last TLB flush - so we can't use it.
 *  n means that there are (n-1) current users of it.
 */
#ifdef CONFIG_HIGHMEM

/*
 * Architecture with aliasing data cache may define the following family of
 * helper functions in its asm/highmem.h to control cache color of virtual
 * addresses where physical memory pages are mapped by kmap.
 */
#ifndef get_pkmap_color

/*
 * Determine color of virtual address where the page should be mapped.
 */
static inline unsigned int get_pkmap_color(struct page *page)
{
	return 0;
}
#define get_pkmap_color get_pkmap_color

/*
 * Get next index for mapping inside PKMAP region for page with given color.
 */
static inline unsigned int get_next_pkmap_nr(unsigned int color)
{
	static unsigned int last_pkmap_nr;

	last_pkmap_nr = (last_pkmap_nr + 1) & LAST_PKMAP_MASK;
	return last_pkmap_nr;
}

/*
 * Determine if page index inside PKMAP region (pkmap_nr) of given color
 * has wrapped around PKMAP region end. When this happens an attempt to
 * flush all unused PKMAP slots is made.
 */
static inline int no_more_pkmaps(unsigned int pkmap_nr, unsigned int color)
{
	return pkmap_nr == 0;
}

/*
 * Get the number of PKMAP entries of the given color. If no free slot is
 * found after checking that many entries, kmap will sleep waiting for
 * someone to call kunmap and free PKMAP slot.
 */
static inline int get_pkmap_entries_count(unsigned int color)
{
	return LAST_PKMAP;
}

/*
 * Get head of a wait queue for PKMAP entries of the given color.
 * Wait queues for different mapping colors should be independent to avoid
 * unnecessary wakeups caused by freeing of slots of other colors.
 */
static inline wait_queue_head_t *get_pkmap_wait_queue_head(unsigned int color)
{
	static DECLARE_WAIT_QUEUE_HEAD(pkmap_map_wait);

	return &pkmap_map_wait;
}
#endif

unsigned long totalhigh_pages __read_mostly;
EXPORT_SYMBOL(totalhigh_pages);


EXPORT_PER_CPU_SYMBOL(__kmap_atomic_idx);

unsigned int nr_free_highpages (void)
{
	struct zone *zone;
	unsigned int pages = 0;

	for_each_populated_zone(zone) {
		if (is_highmem(zone))
			pages += zone_page_state(zone, NR_FREE_PAGES);
	}

	return pages;
}

/**
 * �������ÿһ��Ԫ�ض�Ӧ��һ���־�ӳ���kmapҳ,��ʾ��ӳ��ҳ��ʹ�ü�����
 * ������ֵΪ2ʱ����ʾ��һ��ʹ���˸�ҳ��0��ʾû��ʹ�á�1��ʾҳ���Ѿ�ӳ�䣬����TLBû�и��£�����޷�ʹ�á�
 *
 * Pkmap_count�������LAST_PKMAP����������pkmap_page_tableҳ����ÿһ���һ����
 * ����¼�������ں�ӳ��ʹ������Щҳ�������ֵ����Ϊ��
 *	0����Ӧ��ҳ����û��ӳ���κθ߶��ڴ�ҳ�򣬲����ǿ��õġ�
 *	1����Ӧҳ����û��ӳ���κθ߶��ڴ棬��������Ȼ�����á���Ϊ�Դ������һ��ʹ����������Ӧ��TLB��û�б�ˢ�¡�
 *	>1����Ӧ��ҳ����ӳ����һ���߶��ڴ�ҳ�򡣲���������n-1���ں�����ʹ�����ҳ��
 */
static int pkmap_count[LAST_PKMAP];
static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(kmap_lock);

/**
 * ���ڽ��������ں�ӳ���ҳ���ں˿��Գ���ӳ��߶��ڴ浽�ں˵�ַ�ռ��С�
 * ҳ���еı�������LAST_PKMAP�������ȡ�����Ƿ��PAE��
 * ����ֵ������512����1024������ӳ��2MB��4MB�������ں�ӳ�䡣
 */
pte_t * pkmap_page_table;

/*
 * Most architectures have no use for kmap_high_get(), so let's abstract
 * the disabling of IRQ out of the locking in that case to save on a
 * potential useless overhead.
 */
#ifdef ARCH_NEEDS_KMAP_HIGH_GET
#define lock_kmap()             spin_lock_irq(&kmap_lock)
#define unlock_kmap()           spin_unlock_irq(&kmap_lock)
#define lock_kmap_any(flags)    spin_lock_irqsave(&kmap_lock, flags)
#define unlock_kmap_any(flags)  spin_unlock_irqrestore(&kmap_lock, flags)
#else
#define lock_kmap()             spin_lock(&kmap_lock)
#define unlock_kmap()           spin_unlock(&kmap_lock)
#define lock_kmap_any(flags)    \
		do { spin_lock(&kmap_lock); (void)(flags); } while (0)
#define unlock_kmap_any(flags)  \
		do { spin_unlock(&kmap_lock); (void)(flags); } while (0)
#endif

struct page *kmap_to_page(void *vaddr)
{
	unsigned long addr = (unsigned long)vaddr;

	if (addr >= PKMAP_ADDR(0) && addr < PKMAP_ADDR(LAST_PKMAP)) {
		int i = PKMAP_NR(addr);
		return pte_page(pkmap_page_table[i]);
	}

	return virt_to_page(addr);
}
EXPORT_SYMBOL(kmap_to_page);

static void flush_all_zero_pkmaps(void)
{
	int i;
	int need_flush = 0;

	flush_cache_kmaps();

	for (i = 0; i < LAST_PKMAP; i++) {
		struct page *page;

		/*
		 * zero means we don't have anything to do,
		 * >1 means that it is still in use. Only
		 * a count of 1 means that it is free but
		 * needs to be unmapped
		 */
		if (pkmap_count[i] != 1)
			continue;
		pkmap_count[i] = 0;

		/* sanity check */
		BUG_ON(pte_none(pkmap_page_table[i]));

		/*
		 * Don't need an atomic fetch-and-clear op here;
		 * no-one has the page mapped, and cannot get at
		 * its virtual address (and hence PTE) without first
		 * getting the kmap_lock (which is held here).
		 * So no dangers, even with speculative execution.
		 */
		page = pte_page(pkmap_page_table[i]);
		pte_clear(&init_mm, PKMAP_ADDR(i), &pkmap_page_table[i]);

		set_page_address(page, NULL);
		need_flush = 1;
	}
	if (need_flush)
		flush_tlb_kernel_range(PKMAP_ADDR(0), PKMAP_ADDR(LAST_PKMAP));
}

/**
 * kmap_flush_unused - flush all unused kmap mappings in order to remove stray mappings
 */
void kmap_flush_unused(void)
{
	lock_kmap();
	flush_all_zero_pkmaps();
	unlock_kmap();
}

/**
 * Ϊ���������ں�ӳ�佨����ʼӳ��.
 */
static inline unsigned long map_new_virtual(struct page *page)
{
	unsigned long vaddr;
	int count;
	unsigned int last_pkmap_nr;
	unsigned int color = get_pkmap_color(page);

start:
	/* ���ѭ������һ��pkmap_count���� */
	count = get_pkmap_entries_count(color);
	/* Find an empty entry */
	/**
	 * ɨ��pkmap_count�е����м�����ֵ,ֱ���ҵ�һ����ֵ.
	 */
	for (;;) {
		/* ����һ�β��ҵ�λ�ÿ�ʼ���ҿ��������ַ */
		last_pkmap_nr = get_next_pkmap_nr(color);
		/**
		 * ���������һλ��.�ڴ�0��ʼ����ǰ,ˢ�¼���Ϊ1����.
		 * ������ֵΪ1��ʾҳ�������,���Ƕ�Ӧ��TLB��û��ˢ��.
		 */
		if (no_more_pkmaps(last_pkmap_nr, color)) {
			/* �����м���Ϊ1�ĵ�ַ�������pteӳ�䣬ˢ��tlb���ӳ�ˢ��tlb����Ϊˢ��tlb�Ǻ�ʱ�Ĳ��� */
			flush_all_zero_pkmaps();
			count = get_pkmap_entries_count(color);
		}
		/* �ҵ����õ�ַ */
		/**
		 * �ҵ�����Ϊ0��ҳ����,��ʾ��ҳ�����ҿ���.
		 */
		if (!pkmap_count[last_pkmap_nr])
			break;	/* Found a usable entry */
		/**
		 * count���������������.������������������һ��ҳ����.�����,�����ʾû�п�����,�˳�.
		 */
		if (--count)
			continue;

		/*
		 * Sleep for somebody else to unmap their entries
		 */
		/* ���е����˵��û�п��������ַ������ȴ������ط�����kunmap�ͷ������ַ */
		/**
		 * ���е�����,��ʾû���ҵ�����ҳ����.��˯��һ��.
		 * �ȴ������߳��ͷ�ҳ����,Ȼ���ѱ��߳�.
		 */
		{
			DECLARE_WAITQUEUE(wait, current);
			wait_queue_head_t *pkmap_map_wait =
				get_pkmap_wait_queue_head(color);

			/* ���Լ��ҵ�pkmap_map_wait�ȴ����� */
			__set_current_state(TASK_UNINTERRUPTIBLE);
			/**
			 * ����ǰ�̹߳ҵ�pkmap_map_wait�ȴ�������.
			 */
			add_wait_queue(pkmap_map_wait, &wait);
			/* �ͷ�ȫ������˯�ߡ������ɵ����߻�ȡ */
			unlock_kmap();
			schedule();
			/* �����ط�������kunmap�����»�ȡȫ���������� */
			remove_wait_queue(pkmap_map_wait, &wait);
			lock_kmap();

			/* Somebody else might have mapped it while we slept */
			/* ��˯�ߵĹ����У������ط������Ѿ�����ӳ���˸�ҳ��ֱ�ӷ��ʼ��� */
			/**
			 * �ڵ�ǰ�̵߳ȴ��Ĺ�����,�����߳̿����Ѿ���ҳ�������ӳ��.
			 * ���һ��,����Ѿ�ӳ����,���˳�.
			 * ע��,����û�ж�kmap_lock���н�������.����kmap_lock���Ĳ���,��Ҫ���kmap_high������.
			 * �ܵ�ԭ����:���뱾����ʱ��֤����,Ȼ���ڱ���ǰ�����,����������.
			 * �ں������غ�,����Ȼ�ǹص�.��������.��ʹ�ڱ�������ѭ��Ҳ������.
			 * �ں˾�����ô��,�����˾�ϰ����.������Ŀǰ���ܱ����ѧ����Ӧ���ִ���.
			 */
			if (page_address(page))
				return (unsigned long)page_address(page);

			/* Re-start */
			/* ���²��ҿ��������ַ */
			goto start;
		}
	}
	/* �����õ������ַ */
	/**
	 * ���ܺ���·�����е�������,kmap_lock�������ŵ�.
	 * ����last_pkmap_nr��Ӧ����һ�������ҿ��õı���.
	 */
	vaddr = PKMAP_ADDR(last_pkmap_nr);
	/* �޸�pteӳ���� */
	/**
	 * ����ҳ������,���������ַ�������ַ֮���ӳ��.
	 */
	set_pte_at(&init_mm, vaddr,
		   &(pkmap_page_table[last_pkmap_nr]), mk_pte(page, kmap_prot));

	/* ���ｫʹ�ü�����ʼ��Ϊ1�������߻������Ӽ���Ϊ2����ʾ��һ��ʹ�ü��� */
	/**
	 * 1��ʾ��Ӧ�������,����TLB��Ҫˢ��.
	 * ����������������������ӳ��,Ϊʲô���ǿ��õ���,�����ط����Ὣռ��ô
	 * ��ʵ���õ���,��Ϊ����kmap_high��,kmap_high�����Ὣ���ټ�1.
	 */
	pkmap_count[last_pkmap_nr] = 1;
	set_page_address(page, (void *)vaddr);

	return vaddr;
}

/**
 * kmap_high - map a highmem page into memory
 * @page: &struct page to map
 *
 * Returns the page's virtual memory address.
 *
 * We cannot call this from interrupts, as it may block.
 */
/**
 * ���߶��ڴ�ӳ�䵽�����ַ
 */
void *kmap_high(struct page *page)
{
	unsigned long vaddr;

	/*
	 * For highmem pages, we can't trust "virtual" until
	 * after we have the lock.
	 */
	/* ������������ȫ����������ȷ��page_address����ȷ�� */
	lock_kmap();
	/**
	 * page_address�м��ҳ���Ƿ�ӳ������á�
	 */
	vaddr = (unsigned long)page_address(page);
	/**
	 * û�б�ӳ�䣬�͵���map_new_virtual��ҳ��������ַ���뵽pkmap_page_table��һ�����С�
	 * ����page_address_htableɢ�б��м���һ��Ԫ�ء�
	 */
	/* ��ûӳ�䵽�߶˵�ַ */
	if (!vaddr)
		/* ��������ַ��ӳ�䵽ҳ�棬ע��������ͷ���������������˱������������ж��е��� */
		vaddr = map_new_virtual(page);
	/* ���������ַʹ�ü�������map_new_virtual�������˳�ʼֵΪ1����ʱӦ��Ϊ2���߸����ֵ */
	/**
	 * ʹҳ������Ե�ַ����Ӧ�ļ�������1.
	 */
	pkmap_count[PKMAP_NR(vaddr)]++;
	/**
	 * ����ӳ��ʱ,map_new_virtual�лὫ������Ϊ1,��һ���ټ�1.
	 * ���ӳ��ʱ,����ֵ���ټ�1.
	 * ��֮,����ֵ������С��2.
	 */
	BUG_ON(pkmap_count[PKMAP_NR(vaddr)] < 2);
	/* �ͷ�ȫ�����������ص�ַ */
	unlock_kmap();
	return (void*) vaddr;
}

EXPORT_SYMBOL(kmap_high);

#ifdef ARCH_NEEDS_KMAP_HIGH_GET
/**
 * kmap_high_get - pin a highmem page into memory
 * @page: &struct page to pin
 *
 * Returns the page's current virtual memory address, or NULL if no mapping
 * exists.  If and only if a non null address is returned then a
 * matching call to kunmap_high() is necessary.
 *
 * This can be called from any context.
 */
void *kmap_high_get(struct page *page)
{
	unsigned long vaddr, flags;

	lock_kmap_any(flags);
	vaddr = (unsigned long)page_address(page);
	if (vaddr) {
		BUG_ON(pkmap_count[PKMAP_NR(vaddr)] < 1);
		pkmap_count[PKMAP_NR(vaddr)]++;
	}
	unlock_kmap_any(flags);
	return (void*) vaddr;
}
#endif

/**
 * kunmap_high - unmap a highmem page into memory
 * @page: &struct page to unmap
 *
 * If ARCH_NEEDS_KMAP_HIGH_GET is not defined then this may be called
 * only from user context.
 */
/**
 * ����߶��ڴ�������ں�ӳ��
 */
void kunmap_high(struct page *page)
{
	unsigned long vaddr;
	unsigned long nr;
	unsigned long flags;
	int need_wakeup;
	unsigned int color = get_pkmap_color(page);
	wait_queue_head_t *pkmap_map_wait;

	/* ��ȡȫ��kmap�� */
	lock_kmap_any(flags);
	/* ����ҳ��������ַ */
	/**
	 * �õ�����ҳ��Ӧ�������ַ��
	 */
	vaddr = (unsigned long)page_address(page);
	/**
	 * vaddr��==0���������ڴ�Խ������ع����˰ɡ�BUGһ��
	 * ���ҳ��û�б�ӳ�����˵�������������쳣��� 
	 */
	BUG_ON(!vaddr);
	/**
	 * ���������ַ���ҵ�ҳ������pkmap_count�е���š�
	 * ����õ�ַ��kmap����ռ��е�����
	 */
	nr = PKMAP_NR(vaddr);

	/*
	 * A count must never go down to zero
	 * without a TLB flush!
	 */
	need_wakeup = 0;
	/* �ݼ������ַ���ü��� */
	switch (--pkmap_count[nr]) {
	case 0:
	/* ��Զ������Ϊ0��Ϊ1�ű�ʾû��ӳ�� */
		BUG();
	case 1:
	/* ��ȫ���ӳ���� */
		/*
		 * Avoid an unnecessary wake_up() function call.
		 * The common case is pkmap_count[] == 1, but
		 * no waiters.
		 * The tasks queued in the wait-queue are guarded
		 * by both the lock in the wait-queue-head and by
		 * the kmap_lock.  As the kmap_lock is held here,
		 * no need for the wait-queue-head's lock.  Simply
		 * test if the queue is empty.
		 */
		/* ����еȴ������ַ�Ľ��̣�����Ҫ���� */
		pkmap_map_wait = get_pkmap_wait_queue_head(color);
		/**
		 * ҳ��������ˡ�need_wakeup�ỽ�ѵȴ��������������̡߳�
		 */
		need_wakeup = waitqueue_active(pkmap_map_wait);
	}
	/* �ͷ�ȫ���� */
	unlock_kmap_any(flags);

	/* do wake-up, if needed, race-free outside of the spin lock */
	/**
	 * �еȴ��̣߳����������ͷ����Ժ��ٻ��ѣ����ⳤʱ������
	 */
	if (need_wakeup)
		wake_up(pkmap_map_wait);
}

EXPORT_SYMBOL(kunmap_high);
#endif

#if defined(HASHED_PAGE_VIRTUAL)

#define PA_HASH_ORDER	7

/*
 * Describes one page->virtual association
 */
/**
 * ���������ڴ�ҳ�������ַ֮��Ĺ���
 */
struct page_address_map {
	/* ҳ����� */
	struct page *page;
	/* ӳ��������ַ */
	void *virtual;
	/* ͨ�����ֶ����ӵ���ϣ��page_address_htable��Ͱ�� */
	struct list_head list;
};

static struct page_address_map page_address_maps[LAST_PKMAP];

/*
 * Hash table bucket
 */
/**
 * page_address_htable��ϣ�����ڷ�ֹ�����ַ��ͻ, ��ɢ�к�����page_slot
 * ��ɢ�б��¼�˸߶��ڴ�ҳ���������ں�ӳ��ӳ����������Ե�ַ��
 */
static struct page_address_slot {
	struct list_head lh;			/* List of page_address_maps */
	spinlock_t lock;			/* Protect this bucket's list */
} ____cacheline_aligned_in_smp page_address_htable[1<<PA_HASH_ORDER];

static struct page_address_slot *page_slot(const struct page *page)
{
	return &page_address_htable[hash_ptr(page, PA_HASH_ORDER)];
}

/**
 * page_address - get the mapped virtual address of a page
 * @page: &struct page to get the virtual address of
 *
 * Returns the page's virtual address.
 */
/**
 * ȷ��ĳ������ҳ��������ַ��
 * ������page_address_htable��ϣ���в���
 */
void *page_address(const struct page *page)
{
	unsigned long flags;
	void *ret;
	struct page_address_slot *pas;

	/**
	 * ���ҳ���ڸ߶��ڴ���(PG_highmem��־Ϊ0)�������Ե�ַ���Ǵ��ڵġ�
	 * ����ͨ������ҳ���±꣬Ȼ����ת���������ַ�������������ַ�õ����Ե�ַ��
	 * ������Ǹ߶��ڴ棬��ֱ�ӷ��������Ե�ַ
	 */
	if (!PageHighMem(page))
		/**
		 * ����ȼ���__va((unsigned long)(page - mem_map) << 12)
		 */
		return lowmem_page_address(page);

	/* ������page_address_htable�е�λ�� */
	/**
	 * ����ҳ���ڸ߶��ڴ���(PG_highmem��־Ϊ1)����page_address_htableɢ�б��в��ҡ�
	 */
	pas = page_slot(page);
	ret = NULL;
	/* ��ȡͰ���� */
	spin_lock_irqsave(&pas->lock, flags);
	/* Ͱ��Ϊ�� */
	if (!list_empty(&pas->lh)) {
		struct page_address_map *pam;

		/* ��Ͱ�б��� */
		list_for_each_entry(pam, &pas->lh, list) {
			/**
			 * ��page_address_htable���ҵ������ض�Ӧ�������ַ��
			 * �ҵ���ҳ�����������ַ
			 */
			if (pam->page == page) {
				ret = pam->virtual;
				goto done;
			}
		}
	}
	/**
	 * û����page_address_htable���ҵ�������Ĭ��ֵNULL��
	 */
done:
	/* �ͷ��������ؽ�� */
	spin_unlock_irqrestore(&pas->lock, flags);
	return ret;
}

EXPORT_SYMBOL(page_address);

/**
 * set_page_address - set a page's virtual address
 * @page: &struct page to set
 * @virtual: virtual address to use
 */
void set_page_address(struct page *page, void *virtual)
{
	unsigned long flags;
	struct page_address_slot *pas;
	struct page_address_map *pam;

	BUG_ON(!PageHighMem(page));

	pas = page_slot(page);
	if (virtual) {		/* Add */
		pam = &page_address_maps[PKMAP_NR((unsigned long)virtual)];
		pam->page = page;
		pam->virtual = virtual;

		spin_lock_irqsave(&pas->lock, flags);
		list_add_tail(&pam->list, &pas->lh);
		spin_unlock_irqrestore(&pas->lock, flags);
	} else {		/* Remove */
		spin_lock_irqsave(&pas->lock, flags);
		list_for_each_entry(pam, &pas->lh, list) {
			if (pam->page == page) {
				list_del(&pam->list);
				spin_unlock_irqrestore(&pas->lock, flags);
				goto done;
			}
		}
		spin_unlock_irqrestore(&pas->lock, flags);
	}
done:
	return;
}

/*
��ʼ��page_address_pool����
��page_address_maps����Ԫ�ذ������������
page_address_pool����; 
��ʼ��page_address_htable����.
*/
void __init page_address_init(void)
{
	int i;

	//����vmalloc�����ַ�ռ�Ĺ�ϣͰ
	for (i = 0; i < ARRAY_SIZE(page_address_htable); i++) {
		//��ʼ��Ͱ����ͷ
		INIT_LIST_HEAD(&page_address_htable[i].lh);
		//��ʼ������Ͱ����������
		spin_lock_init(&page_address_htable[i].lock);
	}
}

#endif	/* defined(CONFIG_HIGHMEM) && !defined(WANT_PAGE_VIRTUAL) */
