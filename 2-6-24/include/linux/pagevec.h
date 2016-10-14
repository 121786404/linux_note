/*
 * include/linux/pagevec.h
 *
 * In many places it is efficient to batch an operation up against multiple
 * pages.  A pagevec is a multipage container which is used for that.
 */

#ifndef _LINUX_PAGEVEC_H
#define _LINUX_PAGEVEC_H

/* 14 pointers + two long's align the pagevec structure to a power of two */
#define PAGEVEC_SIZE	14

struct page;
struct address_space;

/**
 * ҳ����������
 */
struct pagevec {
	/* pages��������Ч��ָ������ */
	unsigned long nr;
	/* ��Щҳ�Ƿ�������ҳ */
	unsigned long cold;
	/* ҳ������ */
	struct page *pages[PAGEVEC_SIZE];
};

void __pagevec_release(struct pagevec *pvec);
void __pagevec_release_nonlru(struct pagevec *pvec);
void __pagevec_free(struct pagevec *pvec);
void __pagevec_lru_add(struct pagevec *pvec);
void __pagevec_lru_add_active(struct pagevec *pvec);
void pagevec_strip(struct pagevec *pvec);
unsigned pagevec_lookup(struct pagevec *pvec, struct address_space *mapping,
		pgoff_t start, unsigned nr_pages);
unsigned pagevec_lookup_tag(struct pagevec *pvec,
		struct address_space *mapping, pgoff_t *index, int tag,
		unsigned nr_pages);

static inline void pagevec_init(struct pagevec *pvec, int cold)
{
	pvec->nr = 0;
	pvec->cold = cold;
}

static inline void pagevec_reinit(struct pagevec *pvec)
{
	pvec->nr = 0;
}

static inline unsigned pagevec_count(struct pagevec *pvec)
{
	return pvec->nr;
}

static inline unsigned pagevec_space(struct pagevec *pvec)
{
	return PAGEVEC_SIZE - pvec->nr;
}

/*
 * Add a page to a pagevec.  Returns the number of slots still available.
 */
/**
 * ��ҳ��ӵ�ҳ������
 */
static inline unsigned pagevec_add(struct pagevec *pvec, struct page *page)
{
	pvec->pages[pvec->nr++] = page;
	return pagevec_space(pvec);
}


/**
 * �����ͷ�ҳ�����е�ҳ 
 */
static inline void pagevec_release(struct pagevec *pvec)
{
	/* ������Чҳ */
	if (pagevec_count(pvec))
		/* �ͷ�ҳ�����ʹ�ü�����Ϊ0���򷵻ص����ϵͳ�����ҳ��LRU�����ϣ�����������Ƴ��� */
		__pagevec_release(pvec);
}

/**
 * ��pagevec_release���ƣ����ǲ�����LRU���ɵ�����ȷ��ҳ��û��λ��LRU�����С�
 */
static inline void pagevec_release_nonlru(struct pagevec *pvec)
{
	if (pagevec_count(pvec))
		__pagevec_release_nonlru(pvec);
}

/**
 * ��ҳ�淵�������ϵͳ���ɵ�����ȷ�������ü���Ϊ0����δ�������κ�LRU�����С�
 */
static inline void pagevec_free(struct pagevec *pvec)
{
	if (pagevec_count(pvec))
		__pagevec_free(pvec);
}

static inline void pagevec_lru_add(struct pagevec *pvec)
{
	if (pagevec_count(pvec))
		__pagevec_lru_add(pvec);
}

#endif /* _LINUX_PAGEVEC_H */
