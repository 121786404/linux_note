/*
 * mm/mmap.c
 *
 * Written by obz.
 *
 * Address space accounting code	<alan@lxorguk.ukuu.org.uk>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/mm.h>
#include <linux/vmacache.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/syscalls.h>
#include <linux/capability.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/hugetlb.h>
#include <linux/shmem_fs.h>
#include <linux/profile.h>
#include <linux/export.h>
#include <linux/mount.h>
#include <linux/mempolicy.h>
#include <linux/rmap.h>
#include <linux/mmu_notifier.h>
#include <linux/mmdebug.h>
#include <linux/perf_event.h>
#include <linux/audit.h>
#include <linux/khugepaged.h>
#include <linux/uprobes.h>
#include <linux/rbtree_augmented.h>
#include <linux/notifier.h>
#include <linux/memory.h>
#include <linux/printk.h>
#include <linux/userfaultfd_k.h>
#include <linux/moduleparam.h>
#include <linux/pkeys.h>

#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>

#include "internal.h"

#ifndef arch_mmap_check
#define arch_mmap_check(addr, len, flags)	(0)
#endif

#ifdef CONFIG_HAVE_ARCH_MMAP_RND_BITS
const int mmap_rnd_bits_min = CONFIG_ARCH_MMAP_RND_BITS_MIN;
const int mmap_rnd_bits_max = CONFIG_ARCH_MMAP_RND_BITS_MAX;
int mmap_rnd_bits __read_mostly = CONFIG_ARCH_MMAP_RND_BITS;
#endif
#ifdef CONFIG_HAVE_ARCH_MMAP_RND_COMPAT_BITS
const int mmap_rnd_compat_bits_min = CONFIG_ARCH_MMAP_RND_COMPAT_BITS_MIN;
const int mmap_rnd_compat_bits_max = CONFIG_ARCH_MMAP_RND_COMPAT_BITS_MAX;
int mmap_rnd_compat_bits __read_mostly = CONFIG_ARCH_MMAP_RND_COMPAT_BITS;
#endif

static bool ignore_rlimit_data;
core_param(ignore_rlimit_data, ignore_rlimit_data, bool, 0644);

static void unmap_region(struct mm_struct *mm,
		struct vm_area_struct *vma, struct vm_area_struct *prev,
		unsigned long start, unsigned long end);

/* description of effects of mapping type and prot in current implementation.
 * this is due to the limited x86 page protection hardware.  The expected
 * behavior is in parens:
 *
 * map_type	prot
 *		PROT_NONE	PROT_READ	PROT_WRITE	PROT_EXEC
 * MAP_SHARED	r: (no) no	r: (yes) yes	r: (no) yes	r: (no) yes
 *		w: (no) no	w: (no) no	w: (yes) yes	w: (no) no
 *		x: (no) no	x: (no) yes	x: (no) yes	x: (yes) yes
 *
 * MAP_PRIVATE	r: (no) no	r: (yes) yes	r: (no) yes	r: (no) yes
 *		w: (no) no	w: (no) no	w: (copy) copy	w: (no) no
 *		x: (no) no	x: (no) yes	x: (no) yes	x: (yes) yes
 *
 * On arm64, PROT_EXEC has the following behaviour for both MAP_SHARED and
 * MAP_PRIVATE:
 *								r: (no) no
 *								w: (no) no
 *								x: (yes) yes
 * 线性区的访问权限有16种组合，每种组合所对应的页的保护位存放在protection_map数组中
 */
pgprot_t protection_map[16] = {
	__P000, __P001, __P010, __P011, __P100, __P101, __P110, __P111,
	__S000, __S001, __S010, __S011, __S100, __S101, __S110, __S111
};

pgprot_t vm_get_page_prot(unsigned long vm_flags)
{
	return __pgprot(pgprot_val(protection_map[vm_flags &
				(VM_READ|VM_WRITE|VM_EXEC|VM_SHARED)]) |
			pgprot_val(arch_vm_get_page_prot(vm_flags)));
}
EXPORT_SYMBOL(vm_get_page_prot);

static pgprot_t vm_pgprot_modify(pgprot_t oldprot, unsigned long vm_flags)
{
	return pgprot_modify(oldprot, vm_get_page_prot(vm_flags));
}

/* Update vma->vm_page_prot to reflect vma->vm_flags. */
void vma_set_page_prot(struct vm_area_struct *vma)
{
	unsigned long vm_flags = vma->vm_flags;
	pgprot_t vm_page_prot;

	vm_page_prot = vm_pgprot_modify(vma->vm_page_prot, vm_flags);
	if (vma_wants_writenotify(vma, vm_page_prot)) {
		vm_flags &= ~VM_SHARED;
		vm_page_prot = vm_pgprot_modify(vm_page_prot, vm_flags);
	}
	/* remove_protection_ptes reads vma->vm_page_prot without mmap_sem */
	WRITE_ONCE(vma->vm_page_prot, vm_page_prot);
}

/*
 * Requires inode->i_mapping->i_mmap_rwsem
 */
static void __remove_shared_vm_struct(struct vm_area_struct *vma,
		struct file *file, struct address_space *mapping)
{
	if (vma->vm_flags & VM_DENYWRITE)
		atomic_inc(&file_inode(file)->i_writecount);
	if (vma->vm_flags & VM_SHARED)
		mapping_unmap_writable(mapping);

	flush_dcache_mmap_lock(mapping);
	vma_interval_tree_remove(vma, &mapping->i_mmap);
	flush_dcache_mmap_unlock(mapping);
}

/*
 * Unlink a file-based vm structure from its interval tree, to hide
 * vma from rmap and vmtruncate before freeing its page tables.
 */
void unlink_file_vma(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;

	if (file) {
		struct address_space *mapping = file->f_mapping;
		i_mmap_lock_write(mapping);
		__remove_shared_vm_struct(vma, file, mapping);
		i_mmap_unlock_write(mapping);
	}
}

/*
 * Close a vm structure and free it, returning the next.
 */
static struct vm_area_struct *remove_vma(struct vm_area_struct *vma)
{
	struct vm_area_struct *next = vma->vm_next;

	might_sleep();
	if (vma->vm_ops && vma->vm_ops->close)
		vma->vm_ops->close(vma);
	if (vma->vm_file)
		fput(vma->vm_file);
	mpol_put(vma_policy(vma));
	kmem_cache_free(vm_area_cachep, vma);
	return next;
}

static int do_brk(unsigned long addr, unsigned long len);

SYSCALL_DEFINE1(brk, unsigned long, brk)
{
	unsigned long retval;
	unsigned long newbrk, oldbrk;
	struct mm_struct *mm = current->mm;
	unsigned long min_brk;
	bool populate;

	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;

#ifdef CONFIG_COMPAT_BRK
	/*
	 * CONFIG_COMPAT_BRK can still be overridden by setting
	 * randomize_va_space to 2, which will still cause mm->start_brk
	 * to be arbitrarily shifted
	 */
	if (current->brk_randomized)
		min_brk = mm->start_brk;
	else
		min_brk = mm->end_data;
#else
	min_brk = mm->start_brk;
#endif
	/* 不能将堆缩小到代码段结束处 */
	/**
	 * 首先验证addr参数是否位于进程代码所在的线性区。
	 * 如果是，则立即返回。因为堆不能与进程代码所在的线性区重叠。
	 */
	if (brk < min_brk)
		goto out;

	/*
	 * Check against rlimit here. If this check is done later after the test
	 * of oldbrk with newbrk then it can escape the test and let the data
	 * segment grow beyond its set limit the in case where the limit is
	 * not page aligned -Ram Gupta
	 */
	/* 堆的大小超过了数据段限制 */
	if (check_data_rlimit(rlimit(RLIMIT_DATA), brk, mm->start_brk,
			      mm->end_data, mm->start_data))
		goto out;

	/*
	 * 将请求的地址按页长度对齐。确保brk的新值(原值也同样)是系统页长度
	 * 的倍数。换句话说，一页是用brk能分配的最小内存区域。
	 * 因此在用户空间需要另一个分配器函数，将页拆分为更小的区域。这是C标准库的任务。
	 *
	 * 由于brk系统调用作用于某一个线性区，它分配和释放完整的页。因此，把addr调整
	 * 为PAGE_SIZE的整数倍。然后把调整的结果与内存描述符的brk字段的值进行比较。
	 */
	newbrk = PAGE_ALIGN(brk);
	oldbrk = PAGE_ALIGN(mm->brk);
	/* 没有变化，直接退出 */
	if (oldbrk == newbrk)
		goto set_brk;

	/* Always allow shrinking brk. */
	/*
	 * 在需要收缩堆时将调用do_munmap()。
	 */
	if (brk <= mm->brk) {
		/* 解除映射 */
		if (!do_munmap(mm, newbrk, oldbrk-newbrk))
			goto set_brk;
		goto out;
	}

	/* Check against existing mmap mappings. */
	/*
	 * 如果堆将要扩大，内核首先必须检查新的长度是否超出进程的最大堆长度
	 * 限制。find_vma_intersection()接下来检查扩大的堆是否与进程中现存
	 * 的映射重叠。倘若如此，则什么也不作，立即返回。
	 */
	/* 扩大的堆与其他VMA重叠了，扩展失败。注意中间需要保留一个空白页面 */
	if (find_vma_intersection(mm, oldbrk, newbrk+PAGE_SIZE))
		goto out;

	/* Ok, looks good - let it rip. */
	/*
	 * 扩大堆的实际工作委托给do_brk()。函数总是返回
	 * mm->brk的新值，无论与原值相比是增大、缩小、还是不变。
	 *
	 * 如果一切顺利，则调用do_brk函数。该函数其实是do_mmap的简化版。
	 * 如果它返回oldbrk，则分配成功且sys_brk返回addr的值，否则返回mm->brk值。
	 */
	if (do_brk(oldbrk, newbrk-oldbrk) < 0)
		goto out;

set_brk:
	mm->brk = brk;
	populate = newbrk > oldbrk && (mm->def_flags & VM_LOCKED) != 0;
	up_write(&mm->mmap_sem);
	if (populate)
		mm_populate(oldbrk, newbrk - oldbrk);
	return brk;

out:
	retval = mm->brk;
	up_write(&mm->mmap_sem);
	return retval;
}

static long vma_compute_subtree_gap(struct vm_area_struct *vma)
{
	unsigned long max, subtree_gap;
	max = vma->vm_start;
	if (vma->vm_prev)
		max -= vma->vm_prev->vm_end;
	if (vma->vm_rb.rb_left) {
		subtree_gap = rb_entry(vma->vm_rb.rb_left,
				struct vm_area_struct, vm_rb)->rb_subtree_gap;
		if (subtree_gap > max)
			max = subtree_gap;
	}
	if (vma->vm_rb.rb_right) {
		subtree_gap = rb_entry(vma->vm_rb.rb_right,
				struct vm_area_struct, vm_rb)->rb_subtree_gap;
		if (subtree_gap > max)
			max = subtree_gap;
	}
	return max;
}

#ifdef CONFIG_DEBUG_VM_RB
static int browse_rb(struct mm_struct *mm)
{
	struct rb_root *root = &mm->mm_rb;
	int i = 0, j, bug = 0;
	struct rb_node *nd, *pn = NULL;
	unsigned long prev = 0, pend = 0;

	for (nd = rb_first(root); nd; nd = rb_next(nd)) {
		struct vm_area_struct *vma;
		vma = rb_entry(nd, struct vm_area_struct, vm_rb);
		if (vma->vm_start < prev) {
			pr_emerg("vm_start %lx < prev %lx\n",
				  vma->vm_start, prev);
			bug = 1;
		}
		if (vma->vm_start < pend) {
			pr_emerg("vm_start %lx < pend %lx\n",
				  vma->vm_start, pend);
			bug = 1;
		}
		if (vma->vm_start > vma->vm_end) {
			pr_emerg("vm_start %lx > vm_end %lx\n",
				  vma->vm_start, vma->vm_end);
			bug = 1;
		}
		spin_lock(&mm->page_table_lock);
		if (vma->rb_subtree_gap != vma_compute_subtree_gap(vma)) {
			pr_emerg("free gap %lx, correct %lx\n",
			       vma->rb_subtree_gap,
			       vma_compute_subtree_gap(vma));
			bug = 1;
		}
		spin_unlock(&mm->page_table_lock);
		i++;
		pn = nd;
		prev = vma->vm_start;
		pend = vma->vm_end;
	}
	j = 0;
	for (nd = pn; nd; nd = rb_prev(nd))
		j++;
	if (i != j) {
		pr_emerg("backwards %d, forwards %d\n", j, i);
		bug = 1;
	}
	return bug ? -1 : i;
}

static void validate_mm_rb(struct rb_root *root, struct vm_area_struct *ignore)
{
	struct rb_node *nd;

	for (nd = rb_first(root); nd; nd = rb_next(nd)) {
		struct vm_area_struct *vma;
		vma = rb_entry(nd, struct vm_area_struct, vm_rb);
		VM_BUG_ON_VMA(vma != ignore &&
			vma->rb_subtree_gap != vma_compute_subtree_gap(vma),
			vma);
	}
}

static void validate_mm(struct mm_struct *mm)
{
	int bug = 0;
	int i = 0;
	unsigned long highest_address = 0;
	struct vm_area_struct *vma = mm->mmap;

	while (vma) {
		struct anon_vma *anon_vma = vma->anon_vma;
		struct anon_vma_chain *avc;

		if (anon_vma) {
			anon_vma_lock_read(anon_vma);
			list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
				anon_vma_interval_tree_verify(avc);
			anon_vma_unlock_read(anon_vma);
		}

		highest_address = vma->vm_end;
		vma = vma->vm_next;
		i++;
	}
	if (i != mm->map_count) {
		pr_emerg("map_count %d vm_next %d\n", mm->map_count, i);
		bug = 1;
	}
	if (highest_address != mm->highest_vm_end) {
		pr_emerg("mm->highest_vm_end %lx, found %lx\n",
			  mm->highest_vm_end, highest_address);
		bug = 1;
	}
	i = browse_rb(mm);
	if (i != mm->map_count) {
		if (i != -1)
			pr_emerg("map_count %d rb %d\n", mm->map_count, i);
		bug = 1;
	}
	VM_BUG_ON_MM(bug, mm);
}
#else
#define validate_mm_rb(root, ignore) do { } while (0)
#define validate_mm(mm) do { } while (0)
#endif

RB_DECLARE_CALLBACKS(static, vma_gap_callbacks, struct vm_area_struct, vm_rb,
		     unsigned long, rb_subtree_gap, vma_compute_subtree_gap)

/*
 * Update augmented rbtree rb_subtree_gap values after vma->vm_start or
 * vma->vm_prev->vm_end values changed, without modifying the vma's position
 * in the rbtree.
 */
static void vma_gap_update(struct vm_area_struct *vma)
{
	/*
	 * As it turns out, RB_DECLARE_CALLBACKS() already created a callback
	 * function that does exacltly what we want.
	 */
	vma_gap_callbacks_propagate(&vma->vm_rb, NULL);
}

static inline void vma_rb_insert(struct vm_area_struct *vma,
				 struct rb_root *root)
{
	/* All rb_subtree_gap values must be consistent prior to insertion */
	validate_mm_rb(root, NULL);

	rb_insert_augmented(&vma->vm_rb, root, &vma_gap_callbacks);
}

static void __vma_rb_erase(struct vm_area_struct *vma, struct rb_root *root)
{
	/*
	 * Note rb_erase_augmented is a fairly large inline function,
	 * so make sure we instantiate it only once with our desired
	 * augmented rbtree callbacks.
	 */
	rb_erase_augmented(&vma->vm_rb, root, &vma_gap_callbacks);
}

static __always_inline void vma_rb_erase_ignore(struct vm_area_struct *vma,
						struct rb_root *root,
						struct vm_area_struct *ignore)
{
	/*
	 * All rb_subtree_gap values must be consistent prior to erase,
	 * with the possible exception of the "next" vma being erased if
	 * next->vm_start was reduced.
	 */
	validate_mm_rb(root, ignore);

	__vma_rb_erase(vma, root);
}

static __always_inline void vma_rb_erase(struct vm_area_struct *vma,
					 struct rb_root *root)
{
	/*
	 * All rb_subtree_gap values must be consistent prior to erase,
	 * with the possible exception of the vma being erased.
	 */
	validate_mm_rb(root, vma);

	__vma_rb_erase(vma, root);
}

/*
 * vma has some anon_vma assigned, and is already inserted on that
 * anon_vma's interval trees.
 *
 * Before updating the vma's vm_start / vm_end / vm_pgoff fields, the
 * vma must be removed from the anon_vma's interval trees using
 * anon_vma_interval_tree_pre_update_vma().
 *
 * After the update, the vma will be reinserted using
 * anon_vma_interval_tree_post_update_vma().
 *
 * The entire update must be protected by exclusive mmap_sem and by
 * the root anon_vma's mutex.
 */
static inline void
anon_vma_interval_tree_pre_update_vma(struct vm_area_struct *vma)
{
	struct anon_vma_chain *avc;

	list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
		anon_vma_interval_tree_remove(avc, &avc->anon_vma->rb_root);
}

static inline void
anon_vma_interval_tree_post_update_vma(struct vm_area_struct *vma)
{
	struct anon_vma_chain *avc;

	list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
		anon_vma_interval_tree_insert(avc, &avc->anon_vma->rb_root);
}

/**
 * 确定新里子节点在与给定线性地址对应的红黑树中的位置。并返回
 * 前一个线性区的地址和要插入的叶子节点的父节点的地址
 */
static int find_vma_links(struct mm_struct *mm, unsigned long addr,
		unsigned long end, struct vm_area_struct **pprev,
		struct rb_node ***rb_link, struct rb_node **rb_parent)
{
	struct rb_node **__rb_link, *__rb_parent, *rb_prev;

	__rb_link = &mm->mm_rb.rb_node;
	rb_prev = __rb_parent = NULL;

	while (*__rb_link) {
		struct vm_area_struct *vma_tmp;

		__rb_parent = *__rb_link;
		vma_tmp = rb_entry(__rb_parent, struct vm_area_struct, vm_rb);

		if (vma_tmp->vm_end > addr) {
			/* Fail if an existing vma overlaps the area */
			if (vma_tmp->vm_start < end)
				return -ENOMEM;
			__rb_link = &__rb_parent->rb_left;
		} else {
			rb_prev = __rb_parent;
			__rb_link = &__rb_parent->rb_right;
		}
	}

	*pprev = NULL;
	if (rb_prev)
		*pprev = rb_entry(rb_prev, struct vm_area_struct, vm_rb);
	*rb_link = __rb_link;
	*rb_parent = __rb_parent;
	return 0;
}

static unsigned long count_vma_pages_range(struct mm_struct *mm,
		unsigned long addr, unsigned long end)
{
	unsigned long nr_pages = 0;
	struct vm_area_struct *vma;

	/* Find first overlaping mapping */
	vma = find_vma_intersection(mm, addr, end);
	if (!vma)
		return 0;

	nr_pages = (min(end, vma->vm_end) -
		max(addr, vma->vm_start)) >> PAGE_SHIFT;

	/* Iterate over the rest of the overlaps */
	for (vma = vma->vm_next; vma; vma = vma->vm_next) {
		unsigned long overlap_len;

		if (vma->vm_start > end)
			break;

		overlap_len = min(end, vma->vm_end) - vma->vm_start;
		nr_pages += overlap_len >> PAGE_SHIFT;
	}

	return nr_pages;
}

void __vma_link_rb(struct mm_struct *mm, struct vm_area_struct *vma,
		struct rb_node **rb_link, struct rb_node *rb_parent)
{
	/* Update tracking information for the gap following the new vma. */
	if (vma->vm_next)
		vma_gap_update(vma->vm_next);
	else
		mm->highest_vm_end = vma->vm_end;

	/*
	 * vma->vm_prev wasn't known when we followed the rbtree to find the
	 * correct insertion point for that vma. As a result, we could not
	 * update the vma vm_rb parents rb_subtree_gap values on the way down.
	 * So, we first insert the vma with a zero rb_subtree_gap value
	 * (to be consistent with what we did on the way down), and then
	 * immediately update the gap to the correct value. Finally we
	 * rebalance the rbtree after all augmented values have been set.
	 */
	rb_link_node(&vma->vm_rb, rb_parent, rb_link);
	vma->rb_subtree_gap = 0;
	vma_gap_update(vma);
	vma_rb_insert(vma, &mm->mm_rb);
}

static void __vma_link_file(struct vm_area_struct *vma)
{
	struct file *file;

	file = vma->vm_file;
	if (file) {
		struct address_space *mapping = file->f_mapping;

		if (vma->vm_flags & VM_DENYWRITE)
			atomic_dec(&file_inode(file)->i_writecount);
		if (vma->vm_flags & VM_SHARED)
			atomic_inc(&mapping->i_mmap_writable);

		flush_dcache_mmap_lock(mapping);
		vma_interval_tree_insert(vma, &mapping->i_mmap);
		flush_dcache_mmap_unlock(mapping);
	}
}

static void
__vma_link(struct mm_struct *mm, struct vm_area_struct *vma,
	struct vm_area_struct *prev, struct rb_node **rb_link,
	struct rb_node *rb_parent)
{
	/*
	 * __vma_link_list()将新区域放置到进程管理区域
	 * 的线性链表上。完成该工作，只需提供
	 * 使用find_vma_prepare()找到的前一个和后一个区域。
	 */
	__vma_link_list(mm, vma, prev, rb_parent);
	/*
	 * __vma_link_rb()将新区域连接到红黑树的数据结构中
	 */
	__vma_link_rb(mm, vma, rb_link, rb_parent);
}

static void vma_link(struct mm_struct *mm, struct vm_area_struct *vma,
			struct vm_area_struct *prev, struct rb_node **rb_link,
			struct rb_node *rb_parent)
{
	struct address_space *mapping = NULL;

	if (vma->vm_file) { //如果存在文件映射则获取文件对应的地址空间
		mapping = vma->vm_file->f_mapping;
		i_mmap_lock_write(mapping);
	}

    /* 将vma插入到相应的数据结构中--双向链表，红黑树和匿名映射链表*/
	__vma_link(mm, vma, prev, rb_link, rb_parent);
	/*
	 * __vma_link_file()将相关的address_space和映射(
	 * 如果是文件映射)管理起来，并使用
	 * vma_prio_tree_insert()将该区域添加到优先树
	 * 中，对多个相同区域的处理如上所述。
	 */
	__vma_link_file(vma);

	if (mapping)
		i_mmap_unlock_write(mapping);

	mm->map_count++;
	validate_mm(mm);
}

/*
 * Helper for vma_adjust() in the split_vma insert case: insert a vma into the
 * mm's list and rbtree.  It has already been inserted into the interval tree.
 */
static void __insert_vm_struct(struct mm_struct *mm, struct vm_area_struct *vma)
{
	struct vm_area_struct *prev;
	struct rb_node **rb_link, *rb_parent;

	if (find_vma_links(mm, vma->vm_start, vma->vm_end,
			   &prev, &rb_link, &rb_parent))
		BUG();
	__vma_link(mm, vma, prev, rb_link, rb_parent);
	mm->map_count++;
}

/**
 * 从内存描述符链表和红黑树中删除vma。
 *   mm-内存描述符的地址。
 *   vma,prev-两个线性区对象。都应当属于mm。prev应当在线性区的排序中位于vma之前。
 */
static __always_inline void __vma_unlink_common(struct mm_struct *mm,
						struct vm_area_struct *vma,
						struct vm_area_struct *prev,
						bool has_prev,
						struct vm_area_struct *ignore)
{
	struct vm_area_struct *next;

	vma_rb_erase_ignore(vma, &mm->mm_rb, ignore);
	next = vma->vm_next;
	if (has_prev)
		prev->vm_next = next;
	else {
		prev = vma->vm_prev;
		if (prev)
			prev->vm_next = next;
		else
			mm->mmap = next;
	}
	if (next)
		next->vm_prev = prev;

	/* Kill the cache */
	vmacache_invalidate(mm);
}

static inline void __vma_unlink_prev(struct mm_struct *mm,
				     struct vm_area_struct *vma,
				     struct vm_area_struct *prev)
{
	__vma_unlink_common(mm, vma, prev, true, vma);
}

/*
 * We cannot adjust vm_start, vm_end, vm_pgoff fields of a vma that
 * is already present in an i_mmap tree without adjusting the tree.
 * The following helper function should be used when such adjustments
 * are necessary.  The "insert" vma (if any) is to be inserted
 * before we drop the necessary locks.
 */
 /* 执行具体的合并调整操作 */
int __vma_adjust(struct vm_area_struct *vma, unsigned long start,
	unsigned long end, pgoff_t pgoff, struct vm_area_struct *insert,
	struct vm_area_struct *expand)
{
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *next = vma->vm_next, *orig_vma = vma;
	struct address_space *mapping = NULL;
	struct rb_root *root = NULL;
	struct anon_vma *anon_vma = NULL;
	struct file *file = vma->vm_file;
	bool start_changed = false, end_changed = false;
	long adjust_next = 0;
	int remove_next = 0;

	if (next && !insert) {
		struct vm_area_struct *exporter = NULL, *importer = NULL;

        /*指定的范围已经跨越了整个后驱vma，并且有可能超过后驱vma*/
		if (end >= next->vm_end) {
			/*
			 * vma expands, overlapping all the next, and
			 * perhaps the one after too (mprotect case 6).
			 * The only other cases that gets here are
			 * case 1, case 7 and case 8.
			 */
			if (next == expand) {
				/*
				 * The only case where we don't expand "vma"
				 * and we expand "next" instead is case 8.
				 */
				VM_WARN_ON(end != next->vm_end);
				/*
				 * remove_next == 3 means we're
				 * removing "vma" and that to do so we
				 * swapped "vma" and "next".
				 */
				remove_next = 3;
				VM_WARN_ON(file != next->vm_file);
				swap(vma, next);
			} else {
				VM_WARN_ON(expand != vma);
				/*
				 * case 1, 6, 7, remove_next == 2 is case 6,
				 * remove_next == 1 is case 1 or 7.
				 */
				remove_next = 1 + (end > next->vm_end);
				VM_WARN_ON(remove_next == 2 &&
					   end != next->vm_next->vm_end);
				VM_WARN_ON(remove_next == 1 &&
					   end != next->vm_end);
				/* trim end to next, for case 6 first pass */
				end = next->vm_end;
			}

			exporter = next;
			importer = vma;

			/*
			 * If next doesn't have anon_vma, import from vma after
			 * next, if the vma overlaps with it.
			 */
			if (remove_next == 2 && !next->anon_vma)
				exporter = next->vm_next;

		} else if (end > next->vm_start) {/*指定的区域和后驱vma部分重合*/
			/*
			 * vma expands, overlapping part of the next:
			 * mprotect case 5 shifting the boundary up.
			 */
			adjust_next = (end - next->vm_start) >> PAGE_SHIFT;
			exporter = next;
			importer = vma;
			VM_WARN_ON(expand != importer);
		} else if (end < vma->vm_end) { /*指定的区域没到达后驱vma的结束处*/
			/*
			 * vma shrinks, and !insert tells it's not
			 * split_vma inserting another: so it must be
			 * mprotect case 4 shifting the boundary down.
			 */
			adjust_next = -((vma->vm_end - end) >> PAGE_SHIFT);
			exporter = vma;
			importer = next;
			VM_WARN_ON(expand != importer);
		}

		/*
		 * Easily overlooked: when mprotect shifts the boundary,
		 * make sure the expanding vma has anon_vma set if the
		 * shrinking vma had, to cover any anon pages imported.
		 */
		if (exporter && exporter->anon_vma && !importer->anon_vma) {
			int error;

			importer->anon_vma = exporter->anon_vma;
			error = anon_vma_clone(importer, exporter);
			if (error)
				return error;
		}
	}
again:
	vma_adjust_trans_huge(orig_vma, start, end, adjust_next);

	if (file) {//如果有映射文件
		mapping = file->f_mapping; //获取文件对应的address_space
		root = &mapping->i_mmap;
		uprobe_munmap(vma, vma->vm_start, vma->vm_end);

		if (adjust_next)
			uprobe_munmap(next, next->vm_start, next->vm_end);

		i_mmap_lock_write(mapping);
		/*如果指定了待插入的vma，则根据vma是否以非线性的方式映射文件来选择是将 
               vma插入file对应的address_space的优先树(对应线性映射)还是双向链表(非线性映射)*/
		if (insert) {
			/*
			 * Put into interval tree now, so instantiated pages
			 * are visible to arm/parisc __flush_dcache_page
			 * throughout; but we cannot insert into address
			 * space until vma start or end is updated.
			 */
			__vma_link_file(insert);
		}
	}

	anon_vma = vma->anon_vma;
	if (!anon_vma && adjust_next)
		anon_vma = next->anon_vma;
	if (anon_vma) {
		VM_WARN_ON(adjust_next && next->anon_vma &&
			   anon_vma != next->anon_vma);
		anon_vma_lock_write(anon_vma);
		anon_vma_interval_tree_pre_update_vma(vma);
		if (adjust_next)
			anon_vma_interval_tree_pre_update_vma(next);
	}

	if (root) {
		flush_dcache_mmap_lock(mapping);
		vma_interval_tree_remove(vma, root);
		if (adjust_next)
			vma_interval_tree_remove(next, root);
	}

	if (start != vma->vm_start) {
		vma->vm_start = start;
		start_changed = true;
	}
	if (end != vma->vm_end) {
		vma->vm_end = end;
		end_changed = true;
	}
	vma->vm_pgoff = pgoff;
	if (adjust_next) { //调整后驱vma的相关量  
		next->vm_start += adjust_next << PAGE_SHIFT;
		next->vm_pgoff += adjust_next;
	}

	if (root) {
		if (adjust_next) //如果后驱vma被调整了，则重新插入到优先树中
			vma_interval_tree_insert(next, root);
		vma_interval_tree_insert(vma, root);
		flush_dcache_mmap_unlock(mapping);
	}

	if (remove_next) { //给定区域与后驱vma有重合  
		/*
		 * vma_merge has merged next into vma, and needs
		 * us to remove next before dropping the locks.
		 */
		if (remove_next != 3)
			__vma_unlink_prev(mm, next, vma);
		else
			/*
			 * vma is not before next if they've been
			 * swapped.
			 *
			 * pre-swap() next->vm_start was reduced so
			 * tell validate_mm_rb to ignore pre-swap()
			 * "next" (which is stored in post-swap()
			 * "vma").
			 */
			__vma_unlink_common(mm, next, NULL, false, vma);
		if (file) //将后驱vma从文件对应的address space中删除
			__remove_shared_vm_struct(next, file, mapping);
	} else if (insert) {
		/*
		 * split_vma has split insert from vma, and needs
		 * us to insert it before dropping the locks
		 * (it may either follow vma or precede it).
		 */
		__insert_vm_struct(mm, insert); //将待插入的vma插入mm的红黑树，双向链表以及匿名映射链表
	} else {
		if (start_changed)
			vma_gap_update(vma);
		if (end_changed) {
			if (!next)
				mm->highest_vm_end = end;
			else if (!adjust_next)
				vma_gap_update(next);
		}
	}

	if (anon_vma) {
		anon_vma_interval_tree_post_update_vma(vma);
		if (adjust_next)
			anon_vma_interval_tree_post_update_vma(next);
		anon_vma_unlock_write(anon_vma);
	}
	if (mapping)
		i_mmap_unlock_write(mapping);

	if (root) {
		uprobe_mmap(vma);

		if (adjust_next)
			uprobe_mmap(next);
	}

	if (remove_next) {
		if (file) {
			uprobe_munmap(next, next->vm_start, next->vm_end);
			fput(file);
		}
		if (next->anon_vma)
			anon_vma_merge(vma, next);
		mm->map_count--;
		mpol_put(vma_policy(next));
		kmem_cache_free(vm_area_cachep, next);
		/*
		 * In mprotect's case 6 (see comments on vma_merge),
		 * we must remove another next too. It would clutter
		 * up the code too much to do both in one go.
		 */
		if (remove_next != 3) {
			/*
			 * If "next" was removed and vma->vm_end was
			 * expanded (up) over it, in turn
			 * "next->vm_prev->vm_end" changed and the
			 * "vma->vm_next" gap must be updated.
			 */
			next = vma->vm_next;
		} else {
			/*
			 * For the scope of the comment "next" and
			 * "vma" considered pre-swap(): if "vma" was
			 * removed, next->vm_start was expanded (down)
			 * over it and the "next" gap must be updated.
			 * Because of the swap() the post-swap() "vma"
			 * actually points to pre-swap() "next"
			 * (post-swap() "next" as opposed is now a
			 * dangling pointer).
			 */
			next = vma;
		}
		if (remove_next == 2) {
			remove_next = 1;
			end = next->vm_end;
			goto again;
		}
		else if (next)
			vma_gap_update(next);
		else {
			/*
			 * If remove_next == 2 we obviously can't
			 * reach this path.
			 *
			 * If remove_next == 3 we can't reach this
			 * path because pre-swap() next is always not
			 * NULL. pre-swap() "next" is not being
			 * removed and its next->vm_end is not altered
			 * (and furthermore "end" already matches
			 * next->vm_end in remove_next == 3).
			 *
			 * We reach this only in the remove_next == 1
			 * case if the "next" vma that was removed was
			 * the highest vma of the mm. However in such
			 * case next->vm_end == "end" and the extended
			 * "vma" has vma->vm_end == next->vm_end so
			 * mm->highest_vm_end doesn't need any update
			 * in remove_next == 1 case.
			 */
			VM_WARN_ON(mm->highest_vm_end != end);
		}
	}
	if (insert && file)
		uprobe_mmap(insert);

	validate_mm(mm);

	return 0;
}

/*
 * If the vma has a ->close operation then the driver probably needs to release
 * per-vma resources, so we don't attempt to merge those.
 */
static inline int is_mergeable_vma(struct vm_area_struct *vma,
				struct file *file, unsigned long vm_flags,
				struct vm_userfaultfd_ctx vm_userfaultfd_ctx)
{
	/*
	 * VM_SOFTDIRTY should not prevent from VMA merging, if we
	 * match the flags but dirty bit -- the caller should mark
	 * merged VMA as dirty. If dirty bit won't be excluded from
	 * comparison, we increase pressue on the memory system forcing
	 * the kernel to generate new VMAs when old one could be
	 * extended instead.
	 */
	if ((vma->vm_flags ^ vm_flags) & ~VM_SOFTDIRTY)
		return 0;
	if (vma->vm_file != file)
		return 0;
	if (vma->vm_ops && vma->vm_ops->close)
		return 0;
	if (!is_mergeable_vm_userfaultfd_ctx(vma, vm_userfaultfd_ctx))
		return 0;
	return 1;
}

static inline int is_mergeable_anon_vma(struct anon_vma *anon_vma1,
					struct anon_vma *anon_vma2,
					struct vm_area_struct *vma)
{
	/*
	 * The list_is_singular() test is to avoid merging VMA cloned from
	 * parents. This can improve scalability caused by anon_vma lock.
	 */
	if ((!anon_vma1 || !anon_vma2) && (!vma ||
		list_is_singular(&vma->anon_vma_chain)))
		return 1;
	return anon_vma1 == anon_vma2;
}

/*
 * Return true if we can merge this (vm_flags,anon_vma,file,vm_pgoff)
 * in front of (at a lower virtual address and file offset than) the vma.
 *
 * We cannot merge two vmas if they have differently assigned (non-NULL)
 * anon_vmas, nor if same anon_vma is assigned but offsets incompatible.
 *
 * We don't check here for the merged mmap wrapping around the end of pagecache
 * indices (16TB on ia32) because do_mmap_pgoff() does not permit mmap's which
 * wrap, nor mmaps which cover the final page at index -1UL.
 */
static int
can_vma_merge_before(struct vm_area_struct *vma, unsigned long vm_flags,
		     struct anon_vma *anon_vma, struct file *file,
		     pgoff_t vm_pgoff,
		     struct vm_userfaultfd_ctx vm_userfaultfd_ctx)
{
	if (is_mergeable_vma(vma, file, vm_flags, vm_userfaultfd_ctx) &&
	    is_mergeable_anon_vma(anon_vma, vma->anon_vma, vma)) {
		if (vma->vm_pgoff == vm_pgoff)
			return 1;
	}
	return 0;
}

/*
 * Return true if we can merge this (vm_flags,anon_vma,file,vm_pgoff)
 * beyond (at a higher virtual address and file offset than) the vma.
 *
 * We cannot merge two vmas if they have differently assigned (non-NULL)
 * anon_vmas, nor if same anon_vma is assigned but offsets incompatible.
 */
static int
can_vma_merge_after(struct vm_area_struct *vma, unsigned long vm_flags,
		    struct anon_vma *anon_vma, struct file *file,
		    pgoff_t vm_pgoff,
		    struct vm_userfaultfd_ctx vm_userfaultfd_ctx)
{
	if (is_mergeable_vma(vma, file, vm_flags, vm_userfaultfd_ctx) &&
	    is_mergeable_anon_vma(anon_vma, vma->anon_vma, vma)) {
		pgoff_t vm_pglen;
		vm_pglen = vma_pages(vma);
		if (vma->vm_pgoff + vm_pglen == vm_pgoff)
			return 1;
	}
	return 0;
}

/*
 * Given a mapping request (addr,end,vm_flags,file,pgoff), figure out
 * whether that can be merged with its predecessor or its successor.
 * Or both (it neatly fills a hole).
 *
 * In most cases - when called for mmap, brk or mremap - [addr,end) is
 * certain not to be mapped by the time vma_merge is called; but when
 * called for mprotect, it is certain to be already mapped (either at
 * an offset within prev, or at the start of next), and the flags of
 * this area are about to be changed to vm_flags - and the no-change
 * case has already been eliminated.
 *
 * The following mprotect cases have to be considered, where AAAA is
 * the area passed down from mprotect_fixup, never extending beyond one
 * vma, PPPPPP is the prev vma specified, and NNNNNN the next vma after:
 *
 *     AAAA             AAAA                AAAA          AAAA
 *    PPPPPPNNNNNN    PPPPPPNNNNNN    PPPPPPNNNNNN    PPPPNNNNXXXX
 *    cannot merge    might become    might become    might become
 *                    PPNNNNNNNNNN    PPPPPPPPPPNN    PPPPPPPPPPPP 6 or
 *    mmap, brk or    case 4 below    case 5 below    PPPPPPPPXXXX 7 or
 *    mremap move:                                    PPPPXXXXXXXX 8
 *        AAAA
 *    PPPP    NNNN    PPPPPPPPPPPP    PPPPPPPPNNNN    PPPPNNNNNNNN
 *    might become    case 1 below    case 2 below    case 3 below
 *
 * It is important for case 8 that the the vma NNNN overlapping the
 * region AAAA is never going to extended over XXXX. Instead XXXX must
 * be extended in region AAAA and NNNN must be removed. This way in
 * all cases where vma_merge succeeds, the moment vma_adjust drops the
 * rmap_locks, the properties of the merged vma will be already
 * correct for the whole merged range. Some of those properties like
 * vm_page_prot/vm_flags may be accessed by rmap_walks and they must
 * be correct for the whole merged range immediately after the
 * rmap_locks are released. Otherwise if XXXX would be removed and
 * NNNN would be extended over the XXXX range, remove_migration_ptes
 * or other rmap walkers (if working on addresses beyond the "end"
 * parameter) may establish ptes with the wrong permissions of NNNN
 * instead of the right permissions of XXXX.
 */
 /*
 mm:新区域所属的进程地址空间
 
 prev:在地址上紧接着新区域的前面一个vma
 
 addr:新区域的起始地址
 
 end:新区域的结束地址
 
 vm_flags:新区域的标识集
 
 anon_vma:新区域所属的匿名映射
 
 file:新区域映射的文件
 
 pgoff:新区域映射文件的偏移
 
 policy:和NUMA相关

 vm_userfaultfd_ctx :

* 在新区域被加到进程的地址空间时，内核会检查它是否可以与一个或多个现存区域合并。
 * vma_merge()在可能的情况下，将一个新区域与周边区域合并。mm是相关进程的地址空间实例
 * 而prev是紧接着新区域之前的区域。rb_parent是该区域在红黑树中的父结点。
 * addr、end和vm_flags分别是新区域的开始地址、结束地址、标志。
 * 如果该区域属于一个文件映射，则file是一个指向表示该文件的file实例的指针。
 * pgoff指定了映射在文件数据内的偏移量。policy参数只在NUMA系统上需要
*/
struct vm_area_struct *vma_merge(struct mm_struct *mm, // 
			struct vm_area_struct *prev, unsigned long addr,
			unsigned long end, unsigned long vm_flags,
			struct anon_vma *anon_vma, struct file *file,
			pgoff_t pgoff, struct mempolicy *policy,
			struct vm_userfaultfd_ctx vm_userfaultfd_ctx)
{
	pgoff_t pglen = (end - addr) >> PAGE_SHIFT;
	struct vm_area_struct *area, *next;
	int err;

	/*
	 * We later require that vma->vm_flags == vm_flags,
	 * so this tests vma->vm_flags & VM_SPECIAL, too.
	 */
	/*
	 * 首席那检查确定前一个区域的结束地址是否对应于新区域的起始地址。倘若如此，内核
	 * 接下来必须检查两个区域，确认二者的标志和映射的文件相同，文件映射内部的偏移量
	 * 符合连续区域的要求，两个区域内都不包含匿名映射，而且两个区域彼此兼容。(如果两个
	 * 文件映射在地址空间中连续，但在文件中不连续，亦无法合并)
	 *
	 * 此标志禁止合并，一般由系统对区域进行特殊管理
	 */
	if (vm_flags & VM_SPECIAL)
		return NULL;

	if (prev) //指定了先驱vma，则获取先驱vma的后驱vma
		next = prev->vm_next;
	else   //否则指定mm的vma链表中的第一个元素为后驱vma
		next = mm->mmap;
	area = next;

	/*
	后驱节点存在，并且后驱vma的结束地址和给定区域的结束地址相同， 
      也就是说两者有重叠，那么调整后驱vma
      */
	if (area && area->vm_end == end)		/* cases 6, 7, 8 */
		next = next->vm_next;

	/* verify some invariant that must be enforced by the caller */
	VM_WARN_ON(prev && addr <= prev->vm_start);
	VM_WARN_ON(area && end > area->vm_end);
	VM_WARN_ON(addr >= end);

	/*
	 * Can it merge with the predecessor?
	 */
   /* 
   	 通过can_vma_merge_after()辅助函数完成检查，检查
	 区域与前一个区域是否可以合并。
	 前一个节点的结尾处与当前起始位置符，有合并的可能性 && 检查二者的属性是否允许合并 
		
     先判断给定的区域能否和前驱vma进行合并，需要判断如下的几个方面: 
       1.前驱vma必须存在 
       2.前驱vma的结束地址正好等于给定区域的起始地址 
       3.两者的struct mempolicy中的相关属性要相同，这项检查只对NUMA架构有意义 
       4.其他相关项必须匹配，包括两者的vm_flags，是否映射同一个文件等等 
     */ 
	if (prev && prev->vm_end == addr &&
			mpol_equal(vma_policy(prev), policy) &&
			can_vma_merge_after(prev, vm_flags,
					    anon_vma, file, pgoff,
					    vm_userfaultfd_ctx)) {
		/*
		 * OK, it can.  Can we now merge in the successor as well?
		 */
		/**
		 * 如果可以，内核接下来检查后一个区域是否可以合并。
		 * 与前一例相比，第一个差别是使用can_vma_merge_before()来检查两个区域是否可以
		 * 合并，替代了can_vma_merge_after()。如果前一个
		 * 和后一个区域都可以与当前区域合并，还必须确认前一个和后一个区域的匿名
		 * 映射可以合并，然后才能创建包含这3个区域的一个单一区域。
		 * 在这两种情况下，调用了vma_adjust()执行
		 * 最后的合并。它会适当地修改涉及的所有数据结构，包括优先树和
		 * vm_area_struct()实例，还包括释放不再需要的结构实例。
		 */
		/* 当前区域的结束地址与后一个区域的起始地址相同，可能合并 && 判断二者属性是否允许合并*/
		  /* 
          *确定可以和前驱vma合并后再判断是否能和后驱vma合并，判断方式和前面一样， 
           不过这里多了一项检查，在给定区域能和前驱、后驱vma合并的情况下还要检查 
           前驱、后驱vma的匿名映射可以合并 
          */ 
		if (next && end == next->vm_start &&
				mpol_equal(policy, vma_policy(next)) &&
				can_vma_merge_before(next, vm_flags,
						     anon_vma, file,
						     pgoff+pglen,
						     vm_userfaultfd_ctx) &&
				is_mergeable_anon_vma(prev->anon_vma,
						      next->anon_vma, NULL)) {
							/* cases 1, 6 */
			/* 将三个区域合并 */
			err = __vma_adjust(prev, prev->vm_start,
					 next->vm_end, prev->vm_pgoff, NULL,
					 prev);
		} else					/* cases 2, 5, 7 */
			/* 与前一个区域进行合并 */
			err = __vma_adjust(prev, prev->vm_start,
					 end, prev->vm_pgoff, NULL, prev);
		if (err)
			return NULL;
		khugepaged_enter_vma_merge(prev, vm_flags);
		return prev;
	}

	/*
	 * Can this new request be merged in front of next?
	 */
	 /*如果前面的步骤失败，
	 那么则从后驱vma开始进行和上面类似的步骤*/ 
	 /* 不能与前一个区域合并，则判断是否能与后一个区域进行合并 */
	if (next && end == next->vm_start &&
			mpol_equal(policy, vma_policy(next)) &&
			can_vma_merge_before(next, vm_flags,
					     anon_vma, file, pgoff+pglen,
					     vm_userfaultfd_ctx)) {
		if (prev && addr < prev->vm_end)	/* case 4 */
			err = __vma_adjust(prev, prev->vm_start,
					 addr, prev->vm_pgoff, NULL, next);
		else {					/* cases 3, 8 */
			err = __vma_adjust(area, addr, next->vm_end,
					 next->vm_pgoff - pglen, NULL, next);
			/*
			 * In case 3 area is already equal to next and
			 * this is a noop, but in case 8 "area" has
			 * been removed and next was expanded over it.
			 */
			area = next;
		}
		if (err)
			return NULL;
		khugepaged_enter_vma_merge(area, vm_flags);
		return area;
	}

	return NULL;
}

/*
 * Rough compatbility check to quickly see if it's even worth looking
 * at sharing an anon_vma.
 *
 * They need to have the same vm_file, and the flags can only differ
 * in things that mprotect may change.
 *
 * NOTE! The fact that we share an anon_vma doesn't _have_ to mean that
 * we can merge the two vma's. For example, we refuse to merge a vma if
 * there is a vm_ops->close() function, because that indicates that the
 * driver is doing some kind of reference counting. But that doesn't
 * really matter for the anon_vma sharing case.
 */
static int anon_vma_compatible(struct vm_area_struct *a, struct vm_area_struct *b)
{
	return a->vm_end == b->vm_start &&
		mpol_equal(vma_policy(a), vma_policy(b)) &&
		a->vm_file == b->vm_file &&
		!((a->vm_flags ^ b->vm_flags) & ~(VM_READ|VM_WRITE|VM_EXEC|VM_SOFTDIRTY)) &&
		b->vm_pgoff == a->vm_pgoff + ((b->vm_start - a->vm_start) >> PAGE_SHIFT);
}

/*
 * Do some basic sanity checking to see if we can re-use the anon_vma
 * from 'old'. The 'a'/'b' vma's are in VM order - one of them will be
 * the same as 'old', the other will be the new one that is trying
 * to share the anon_vma.
 *
 * NOTE! This runs with mm_sem held for reading, so it is possible that
 * the anon_vma of 'old' is concurrently in the process of being set up
 * by another page fault trying to merge _that_. But that's ok: if it
 * is being set up, that automatically means that it will be a singleton
 * acceptable for merging, so we can do all of this optimistically. But
 * we do that READ_ONCE() to make sure that we never re-load the pointer.
 *
 * IOW: that the "list_is_singular()" test on the anon_vma_chain only
 * matters for the 'stable anon_vma' case (ie the thing we want to avoid
 * is to return an anon_vma that is "complex" due to having gone through
 * a fork).
 *
 * We also make sure that the two vma's are compatible (adjacent,
 * and with the same memory policies). That's all stable, even with just
 * a read lock on the mm_sem.
 */
static struct anon_vma *reusable_anon_vma(struct vm_area_struct *old, struct vm_area_struct *a, struct vm_area_struct *b)
{
	if (anon_vma_compatible(a, b)) {
		struct anon_vma *anon_vma = READ_ONCE(old->anon_vma);

		if (anon_vma && list_is_singular(&old->anon_vma_chain))
			return anon_vma;
	}
	return NULL;
}

/*
 * find_mergeable_anon_vma is used by anon_vma_prepare, to check
 * neighbouring vmas for a suitable anon_vma, before it goes off
 * to allocate a new anon_vma.  It checks because a repetitive
 * sequence of mprotects and faults may otherwise lead to distinct
 * anon_vmas being allocated, preventing vma merge in subsequent
 * mprotect.
 */
struct anon_vma *find_mergeable_anon_vma(struct vm_area_struct *vma)
{
	struct anon_vma *anon_vma;
	struct vm_area_struct *near;

	near = vma->vm_next;
	if (!near)
		goto try_prev;

	anon_vma = reusable_anon_vma(near, vma, near);
	if (anon_vma)
		return anon_vma;
try_prev:
	near = vma->vm_prev;
	if (!near)
		goto none;

	anon_vma = reusable_anon_vma(near, near, vma);
	if (anon_vma)
		return anon_vma;
none:
	/*
	 * There's no absolute need to look only at touching neighbours:
	 * we could search further afield for "compatible" anon_vmas.
	 * But it would probably just be a waste of time searching,
	 * or lead to too many vmas hanging off the same anon_vma.
	 * We're trying to allow mprotect remerging later on,
	 * not trying to minimize memory used for anon_vmas.
	 */
	return NULL;
}

/*
 * If a hint addr is less than mmap_min_addr change hint to be as
 * low as possible but still greater than mmap_min_addr
 */
static inline unsigned long round_hint_to_min(unsigned long hint)
{
	hint &= PAGE_MASK;
	if (((void *)hint != NULL) &&
	    (hint < mmap_min_addr))
		return PAGE_ALIGN(mmap_min_addr);
	return hint;
}

static inline int mlock_future_check(struct mm_struct *mm,
				     unsigned long flags,
				     unsigned long len)
{
	unsigned long locked, lock_limit;

	/**
	 * 进程加锁页的总数超过了保存在进程描述符的
	 * rlim[RLIMIT_MEMLOCK].rlim_cur字段中的值。也直接返回错误。
	 */
	/*  mlock MCL_FUTURE? */
	if (flags & VM_LOCKED) {
		locked = len >> PAGE_SHIFT;
		locked += mm->locked_vm;
		lock_limit = rlimit(RLIMIT_MEMLOCK);
		lock_limit >>= PAGE_SHIFT;
		if (locked > lock_limit && !capable(CAP_IPC_LOCK))
			return -EAGAIN;
	}
	return 0;
}

/*
 * The caller must hold down_write(&current->mm->mmap_sem).
 */
/*完成后续的内存映射,根据用户空间进程调用mmap api
时传入的参数构造一个struct vm_area_struct对象的实例,
然后调用file->f_op->mmap()

创建并初始化一个新的线性地址区间， 
不过，分配成功之后，可以把这个新的先行区间 
与已有的其他线性区进行合并; 

file和offset:如果新的线性区将把一个文件映射到内存 
                   则使用文件描述符指针file和文件偏移量offset 
addr:这个线性地址指定从何处开始查找一个空闲的区间； 
len:线性区间的长度； 
prot:指定这个线性区所包含页的访问权限，比如读写、执行； 
flag:指定线性区间的其他标志 
*/ 
unsigned long do_mmap(struct file *file, unsigned long addr,
			unsigned long len, unsigned long prot,
			unsigned long flags, vm_flags_t vm_flags,
			unsigned long pgoff, unsigned long *populate)
{
	struct mm_struct *mm = current->mm;
	int pkey = 0;

	*populate = 0;

	/* 参数有效性检查 */
	if (!len)
		return -EINVAL;

	/*
	 * Does the application expect PROT_READ to imply PROT_EXEC?
	 *
	 * (the exception is when the underlying filesystem is noexec
	 *  mounted, in which case we dont add PROT_EXEC.)
	 */
	/* 有读权限，同时该体系结构中，读即意味着执行 */
	if ((prot & PROT_READ) && (current->personality & READ_IMPLIES_EXEC))
		/* 文件在mount时，没有禁止执行 */
		if (!(file && path_noexec(&file->f_path)))
			/* 添加可执行标志 */
			prot |= PROT_EXEC;

	if (!(flags & MAP_FIXED))
		addr = round_hint_to_min(addr);

	/* Careful about overflows.. */
	/* 检查长度是否合法 */
	len = PAGE_ALIGN(len);
	if (!len)
		return -ENOMEM;

	/* offset overflow? */
	/**
	 * 是否越界了
	 */
	if ((pgoff + (len >> PAGE_SHIFT)) < pgoff)
		return -EOVERFLOW;

	/* Too many mappings? */
	/**
	 * 进程映射了过多的线性区。
	 */
	if (mm->map_count > sysctl_max_map_count)
		return -ENOMEM;

	/* Obtain the address to map to. we verify (or select) it and ensure
	 * that it represents a valid section of the address space.
	 */
	/*
	 * 在虚拟地址空间中找到一个适当的区域用于映射。应用程序
	 * 可以对映射指定固定地址、建议一个地址或由内核选择地址
	 *
	 * get_unmapped_area获得新线性区的线性地址区间。
	 * 这个函数可能会调用文件对象的get_unapped_area方法。
	 */
	addr = get_unmapped_area(file, addr, len, pgoff, flags);
	/* 查找一个可用的虚拟地址空间 */
	if (offset_in_page(addr))
		return addr;

	if (prot == PROT_EXEC) {
		pkey = execute_only_pkey(mm);
		if (pkey < 0)
			pkey = 0;
	}

	/* Do simple checking here so the lower-level routines won't have
	 * to. we assume access permissions have been handled by the open
	 * of the memory object, so we don't do any here.
	 *
	 * 将属性标志转换为VM_***标志，易于后续处理。def_flags主要包含
	 * VM_LOCKED标志。该标志表示禁止相应的页换出
	 *
	 * calc_vm_prot_bits()和calc_vm_flag_bits()将系统调用中指定的标志
	 * 和访问权限常数合并到一个共同的标志集中，在后续的操作中比较容易
	 * 处理(MAP_和PROT_标志转换为前缀VM_的标志)。内核从当前运行进程的
	 * mm_struct实例获得def_flags之后，又将其包含到标志集中。def_flags
	 * 的值为0或VM_LOCK。前者不会改变结果标志集，而VM_LOCK意味着随后
	 * 映射的页无法换出。为设置def_flags的值，进程必须发出mlockall()
	 * 系统调用，使用上述机制防止所有未来的映射被换出，即使在创建时没
	 * 有显式指定VM_LOCK标志，也是如此。
	 *
	 * 通过把存放在prot和flags参数中的值进行组合来计算新线性区描述符的标志
	 * mm->def_flags是线性区的默认标志。它只能由mlockall系统调用修改。
	 * 这个调用可以设置VM_LOCKED标志。由此锁住以后申请的RAM中的有页。
	 */
	vm_flags |= calc_vm_prot_bits(prot, pkey) | calc_vm_flag_bits(flags) |
			mm->def_flags | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC;

	/**
	 * flag参数指定新线性区地址区间的页必须被锁在RAM中，但不允许进程
	 * 创建上锁的线性区检查用户是否真的有权限锁定页面
	 */
	if (flags & MAP_LOCKED)
		if (!can_do_mlock())
			return -EPERM;

	if (mlock_future_check(mm, vm_flags, len))
		return -EAGAIN;

	if (file) {
		struct inode *inode = file_inode(file);

		switch (flags & MAP_TYPE) {
		case MAP_SHARED:
		/* 共享映射 */
			/**
			 * 如果请求一个共享可写的内存映射，就检查文件是否为写入而打开的。
			 */
			/* 文件只读，不允许写 */
			if ((prot&PROT_WRITE) && !(file->f_mode&FMODE_WRITE))
				return -EACCES;

			/*
			 * Make sure we don't allow writing to an append-only
			 * file..
			 */
			/**
			 * 如果节点仅仅允许追加写，但是文件以写方式打开，则返回错误。
			 */
			if (IS_APPEND(inode) && (file->f_mode & FMODE_WRITE))
				return -EACCES;

			/*
			 * Make sure there are no mandatory locks on the file.
			 */
			/**
			 * 如果请求一个共享内存映射，就检查文件上没有强制锁。
			 */
			if (locks_verify_locked(file))
				return -EAGAIN;

			vm_flags |= VM_SHARED | VM_MAYSHARE;
			/**
			 * 如果文件没有写权限，那么相应的线性区也不会有写权限。
			 */
			if (!(file->f_mode & FMODE_WRITE))
				vm_flags &= ~(VM_MAYWRITE | VM_SHARED);

			/* fall through */
			/**
			 * 此处没有调用break，也就是说，即便是共享映射，也要进行下面的映射。
			 */
		case MAP_PRIVATE:
			/**
			 * 不论是共享映射还是私有映射，都要检查文件的读权限。
			 */
			/* 文件映射后，必然允许读。但是文件没有读权限 */
			if (!(file->f_mode & FMODE_READ))
				return -EACCES;
			/* 文件系统在挂载时不允许执行，但是mmap允许执行 */
			if (path_noexec(&file->f_path)) {
				if (vm_flags & VM_EXEC)
					return -EPERM;
				vm_flags &= ~VM_MAYEXEC;
			}

			/* 文件没有提供映射方法 */
			if (!file->f_op->mmap)
				return -ENODEV;
			if (vm_flags & (VM_GROWSDOWN|VM_GROWSUP))
				return -EINVAL;
			break;

		default:
			return -EINVAL;
		}
	} else {
	/* 匿名映射 */
		switch (flags & MAP_TYPE) {
		case MAP_SHARED:
			if (vm_flags & (VM_GROWSDOWN|VM_GROWSUP))
				return -EINVAL;
			/*
			 * Ignore pgoff.
			 */
			pgoff = 0;
			vm_flags |= VM_SHARED | VM_MAYSHARE;
			break;
		case MAP_PRIVATE:
			/*
			 * Set pgoff according to addr for anon_vma.
			 */
			pgoff = addr >> PAGE_SHIFT;
			break;
		default:
			return -EINVAL;
		}
	}

	/*
	 * Set 'VM_NORESERVE' if we should not account for the
	 * memory use of this mapping.
	 */
	if (flags & MAP_NORESERVE) {
		/* We honor MAP_NORESERVE if allowed to overcommit */
		if (sysctl_overcommit_memory != OVERCOMMIT_NEVER)
			vm_flags |= VM_NORESERVE;

		/* hugetlb applies strict overcommit unless MAP_NORESERVE */
		if (file && is_file_hugepages(file))
			vm_flags |= VM_NORESERVE;
	}

	addr = mmap_region(file, addr, len, vm_flags, pgoff);
	if (!IS_ERR_VALUE(addr) &&
	    ((vm_flags & VM_LOCKED) ||
	     (flags & (MAP_POPULATE | MAP_NONBLOCK)) == MAP_POPULATE))
		*populate = len;
	return addr;
}

SYSCALL_DEFINE6(mmap_pgoff, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, pgoff)
{
	struct file *file = NULL;
	unsigned long retval;

	if (!(flags & MAP_ANONYMOUS)) {
		audit_mmap_fd(fd, flags);
		file = fget(fd);
		if (!file)
			return -EBADF;
		if (is_file_hugepages(file))
			len = ALIGN(len, huge_page_size(hstate_file(file)));
		retval = -EINVAL;
		if (unlikely(flags & MAP_HUGETLB && !is_file_hugepages(file)))
			goto out_fput;
	} else if (flags & MAP_HUGETLB) {
		struct user_struct *user = NULL;
		struct hstate *hs;

		hs = hstate_sizelog((flags >> MAP_HUGE_SHIFT) & SHM_HUGE_MASK);
		if (!hs)
			return -EINVAL;

		len = ALIGN(len, huge_page_size(hs));
		/*
		 * VM_NORESERVE is used because the reservations will be
		 * taken when vm_ops->mmap() is called
		 * A dummy user value is used because we are not locking
		 * memory so no accounting is necessary
		 */
		file = hugetlb_file_setup(HUGETLB_ANON_FILE, len,
				VM_NORESERVE,
				&user, HUGETLB_ANONHUGE_INODE,
				(flags >> MAP_HUGE_SHIFT) & MAP_HUGE_MASK);
		if (IS_ERR(file))
			return PTR_ERR(file);
	}

	flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);

	retval = vm_mmap_pgoff(file, addr, len, prot, flags, pgoff);
out_fput:
	if (file)
		fput(file);
	return retval;
}

#ifdef __ARCH_WANT_SYS_OLD_MMAP
struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

SYSCALL_DEFINE1(old_mmap, struct mmap_arg_struct __user *, arg)
{
	struct mmap_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	if (offset_in_page(a.offset))
		return -EINVAL;

	return sys_mmap_pgoff(a.addr, a.len, a.prot, a.flags, a.fd,
			      a.offset >> PAGE_SHIFT);
}
#endif /* __ARCH_WANT_SYS_OLD_MMAP */

/*
 * Some shared mappigns will want the pages marked read-only
 * to track write events. If so, we'll downgrade vm_page_prot
 * to the private version (using protection_map[] without the
 * VM_SHARED bit).
 */
int vma_wants_writenotify(struct vm_area_struct *vma, pgprot_t vm_page_prot)
{
	vm_flags_t vm_flags = vma->vm_flags;
	const struct vm_operations_struct *vm_ops = vma->vm_ops;

	/* If it was private or non-writable, the write bit is already clear */
	if ((vm_flags & (VM_WRITE|VM_SHARED)) != ((VM_WRITE|VM_SHARED)))
		return 0;

	/* The backer wishes to know when pages are first written to? */
	if (vm_ops && (vm_ops->page_mkwrite || vm_ops->pfn_mkwrite))
		return 1;

	/* The open routine did something to the protections that pgprot_modify
	 * won't preserve? */
	if (pgprot_val(vm_page_prot) !=
	    pgprot_val(vm_pgprot_modify(vm_page_prot, vm_flags)))
		return 0;

	/* Do we need to track softdirty? */
	if (IS_ENABLED(CONFIG_MEM_SOFT_DIRTY) && !(vm_flags & VM_SOFTDIRTY))
		return 1;

	/* Specialty mapping? */
	if (vm_flags & VM_PFNMAP)
		return 0;

	/* Can the mapping track the dirty pages? */
	return vma->vm_file && vma->vm_file->f_mapping &&
		mapping_cap_account_dirty(vma->vm_file->f_mapping);
}

/*
 * We account for memory if it's a private writeable mapping,
 * not hugepages and VM_NORESERVE wasn't set.
 */
static inline int accountable_mapping(struct file *file, vm_flags_t vm_flags)
{
	/*
	 * hugetlb has its own accounting separate from the core VM
	 * VM_HUGETLB may not be set yet so we cannot check for that flag.
	 */
	if (file && is_file_hugepages(file))
		return 0;

	return (vm_flags & (VM_NORESERVE | VM_SHARED | VM_WRITE)) == VM_WRITE;
}

/*实际的映射在此函数中完成,当该函数被调用时,参数addr已经指向了一块空闲的待映射的MMAP区域中的起始地址*/
unsigned long mmap_region(struct file *file, unsigned long addr,
		unsigned long len, vm_flags_t vm_flags, unsigned long pgoff)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	int error;
	struct rb_node **rb_link, *rb_parent;
	unsigned long charged = 0;

	/* Check against address space limit. */
	if (!may_expand_vm(mm, vm_flags, len >> PAGE_SHIFT)) {
		unsigned long nr_pages;

		/*
		 * MAP_FIXED may remove pages of mappings that intersects with
		 * requested mapping. Account for the pages it would unmap.
		 */
		nr_pages = count_vma_pages_range(mm, addr, addr + len);

		if (!may_expand_vm(mm, vm_flags,
					(len >> PAGE_SHIFT) - nr_pages))
			return -ENOMEM;
	}

	/* Clear old maps */
	/**
	 * find_vma_links确定处于新区间前的线性区对象的位置，以及在红黑树中新线性区的位置。
	 */
	while (find_vma_links(mm, addr, addr + len, &prev, &rb_link,
			      &rb_parent)) {
		/* 解除映射，如果失败则退出 */
		if (do_munmap(mm, addr, len))
			return -ENOMEM;
	}

	/*
	 * Private writable mapping: check memory availability
	 */
	if (accountable_mapping(file, vm_flags)) {
		/**
		 * 需要检查空闲页框数目，并且新的线性区是私有可写页
		 */
		charged = len >> PAGE_SHIFT;
		/**
		 * 如果没有足够的空闲页框，就返回ENOMEM
		 */
		/* 检查内存是否支持映射 */
		if (security_vm_enough_memory_mm(mm, charged))
			return -ENOMEM;
		vm_flags |= VM_ACCOUNT;
	}

	/*
	 * Can we just expand an old mapping?
	 */
	/* 成功合并，不需要分配单独的VMA对象，直接退出 */
	vma = vma_merge(mm, prev, addr, addr + len, vm_flags,
			NULL, file, pgoff, NULL, NULL_VM_UFFD_CTX);
	if (vma)
		goto out;

	/*
	 * Determine the object being mapped and call the appropriate
	 * specific mapper. the address has already been validated, but
	 * not unmapped, but the maps are removed from the list.
	 */
	/*
	 * 在内核分配了所需的内存之后，会执行下列步骤:
	 * 1)分配并初始化一个新的vm_area_struct实例，并插入
	 *   到进程的链表/树数据结构中。
	 * 2)用特定于文件的函数file->f_op->mmap创建映射。
	 *   大多数文件系统将generic_file_mmap()用于该目的。
	 *   它所有的工作，就是将映射的vm_ops成员设置为generic_file_vm_ops。
	 */
	/* 分配VMA描述符 */
	vma = kmem_cache_zalloc(vm_area_cachep, GFP_KERNEL);
	/* 分配失败，退出 */
	if (!vma) {
		error = -ENOMEM;
		goto unacct_error;
	}

	vma->vm_mm = mm;
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_flags = vm_flags;
	vma->vm_page_prot = vm_get_page_prot(vm_flags);
	vma->vm_pgoff = pgoff;
	INIT_LIST_HEAD(&vma->anon_vma_chain);

	/* 文件映射 */
	if (file) {
		if (vm_flags & VM_DENYWRITE) {
			error = deny_write_access(file);
			if (error)
				goto free_vma;
		}
		if (vm_flags & VM_SHARED) {
			error = mapping_map_writable(file->f_mapping);
			if (error)
				goto allow_write_and_free_vma;
		}

		/* ->mmap() can change vma->vm_file, but must guarantee that
		 * vma_link() below can deny write-access if VM_DENYWRITE is set
		 * and map writably if VM_SHARED is set. This usually means the
		 * new file must not have been exposed to user-space, yet.
		 */
		/**
		 * 创建文件映射，一般情况下，该回调是generic_file_mmap。
		 * generic_file_mmap设置vm_ops成员为generic_file_vm_ops 
		 * generic_file_vm_ops的fault回调为filemap_fault
		 */
		vma->vm_file = get_file(file);
		/**
		 * 对映射的文件调用mmap方法，对于大多数文件系统，该方法由generic_file_mmap实现。
		 * 它执行以下步骤:
		 *  将当前时间赋给文件索引节点对象的i_atime字段，并将该索引节点标记为脏。
		 *  用generic_file_vm_ops表的地址初始化线性区描述符的vm_ops字段，在这个表中的方法，
		 *  除了nopage和populate方法外，其他所有都为空。
		 *  其中nopage方法由filemap_nopage实现，而populate方法由filemap_populate实现。
		 */
		error = file->f_op->mmap(file, vma);
		if (error)
			goto unmap_and_free_vma;

		/* Can addr have changed??
		 *
		 * Answer: Yes, several device drivers can do it in their
		 *         f_op->mmap method. -DaveM
		 * Bug: If addr is changed, prev, rb_link, rb_parent should
		 *      be updated for vma_link()
		 */
		WARN_ON_ONCE(addr != vma->vm_start);

		addr = vma->vm_start;
		vm_flags = vma->vm_flags;
	} else if (vm_flags & VM_SHARED) {
		/**
		 * 新线性区有VM_SHARED标志，又不是映射磁盘上的文件。则该线性区是一个共享匿名区。
		 * shmem_zero_setup对它进行初始化。共享匿名区主要用于进程间通信。
		 */
		error = shmem_zero_setup(vma);
		if (error)
			goto free_vma;
	}

	/**
	 * vma_link将新线性区插入到线性区链表和红黑树中。
	 */
	vma_link(mm, vma, prev, rb_link, rb_parent);
	/* Once vma denies write, undo our temporary denial count */
	if (file) {
		if (vm_flags & VM_SHARED)
			mapping_unmap_writable(file->f_mapping);
		if (vm_flags & VM_DENYWRITE)
			allow_write_access(file);
	}
	file = vma->vm_file;
out:
	perf_event_mmap(vma);

	vm_stat_account(mm, vm_flags, len >> PAGE_SHIFT);
	/*
	 * 如果设置了VM_LOCKED，或者通过系统调用的标志参数显式传递过来，或者通过
	 * mlockall机制隐式设置，内核都会调用make_pages_present()依次扫描映射中的各页
	 * 对每一页触发缺页异常以便读入其数据.当然，这意味着失去了延迟读取带来的
	 * 性能提高，但内核可以确保在映射建立后所涉及的页总是在物理内存中
	 * 毕竟VM_LOCKED标志用来防止从内存换出页,因此这些页必须先读进来
	 */
	if (vm_flags & VM_LOCKED) {
		if (!((vm_flags & VM_SPECIAL) || is_vm_hugetlb_page(vma) ||
					vma == get_gate_vma(current->mm)))
			/* 锁定页面计数 */
			mm->locked_vm += (len >> PAGE_SHIFT);
		else
			vma->vm_flags &= VM_LOCKED_CLEAR_MASK;
	}

	if (file)
		uprobe_mmap(vma);

	/*
	 * New (or expanded) vma always get soft dirty status.
	 * Otherwise user-space soft-dirty page tracker won't
	 * be able to distinguish situation when vma area unmapped,
	 * then new mapped in-place (which must be aimed as
	 * a completely new data area).
	 */
	vma->vm_flags |= VM_SOFTDIRTY;

	vma_set_page_prot(vma);

	return addr;

unmap_and_free_vma:
	vma->vm_file = NULL;
	fput(file);

	/* Undo any partial mapping done by a device driver. */
	unmap_region(mm, vma, prev, vma->vm_start, vma->vm_end);
	charged = 0;
	if (vm_flags & VM_SHARED)
		mapping_unmap_writable(file->f_mapping);
allow_write_and_free_vma:
	if (vm_flags & VM_DENYWRITE)
		allow_write_access(file);
free_vma:
	kmem_cache_free(vm_area_cachep, vma);
unacct_error:
	if (charged)
		vm_unacct_memory(charged);
	return error;
}

unsigned long unmapped_area(struct vm_unmapped_area_info *info)
{
	/*
	 * We implement the search by looking for an rbtree node that
	 * immediately follows a suitable gap. That is,
	 * - gap_start = vma->vm_prev->vm_end <= info->high_limit - length;
	 * - gap_end   = vma->vm_start        >= info->low_limit  + length;
	 * - gap_end - gap_start >= length
	 */

	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long length, low_limit, high_limit, gap_start, gap_end;

	/* Adjust search length to account for worst case alignment overhead */
	length = info->length + info->align_mask;
	if (length < info->length)
		return -ENOMEM;

	/* Adjust search limits by the desired length */
	if (info->high_limit < length)
		return -ENOMEM;
	high_limit = info->high_limit - length;

	if (info->low_limit > high_limit)
		return -ENOMEM;
	low_limit = info->low_limit + length;

	/* Check if rbtree root looks promising */
	if (RB_EMPTY_ROOT(&mm->mm_rb))
		goto check_highest;
	vma = rb_entry(mm->mm_rb.rb_node, struct vm_area_struct, vm_rb);
	if (vma->rb_subtree_gap < length)
		goto check_highest;

	while (true) {
		/* Visit left subtree if it looks promising */
		gap_end = vma->vm_start;
		if (gap_end >= low_limit && vma->vm_rb.rb_left) {
			struct vm_area_struct *left =
				rb_entry(vma->vm_rb.rb_left,
					 struct vm_area_struct, vm_rb);
			if (left->rb_subtree_gap >= length) {
				vma = left;
				continue;
			}
		}

		gap_start = vma->vm_prev ? vma->vm_prev->vm_end : 0;
check_current:
		/* Check if current node has a suitable gap */
		if (gap_start > high_limit)
			return -ENOMEM;
		if (gap_end >= low_limit && gap_end - gap_start >= length)
			goto found;

		/* Visit right subtree if it looks promising */
		if (vma->vm_rb.rb_right) {
			struct vm_area_struct *right =
				rb_entry(vma->vm_rb.rb_right,
					 struct vm_area_struct, vm_rb);
			if (right->rb_subtree_gap >= length) {
				vma = right;
				continue;
			}
		}

		/* Go back up the rbtree to find next candidate node */
		while (true) {
			struct rb_node *prev = &vma->vm_rb;
			if (!rb_parent(prev))
				goto check_highest;
			vma = rb_entry(rb_parent(prev),
				       struct vm_area_struct, vm_rb);
			if (prev == vma->vm_rb.rb_left) {
				gap_start = vma->vm_prev->vm_end;
				gap_end = vma->vm_start;
				goto check_current;
			}
		}
	}

check_highest:
	/* Check highest gap, which does not precede any rbtree node */
	gap_start = mm->highest_vm_end;
	gap_end = ULONG_MAX;  /* Only for VM_BUG_ON below */
	if (gap_start > high_limit)
		return -ENOMEM;

found:
	/* We found a suitable gap. Clip it with the original low_limit. */
	if (gap_start < info->low_limit)
		gap_start = info->low_limit;

	/* Adjust gap address to the desired alignment */
	gap_start += (info->align_offset - gap_start) & info->align_mask;

	VM_BUG_ON(gap_start + info->length > info->high_limit);
	VM_BUG_ON(gap_start + info->length > gap_end);
	return gap_start;
}

unsigned long unmapped_area_topdown(struct vm_unmapped_area_info *info)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long length, low_limit, high_limit, gap_start, gap_end;

	/* Adjust search length to account for worst case alignment overhead */
	length = info->length + info->align_mask;
	if (length < info->length)
		return -ENOMEM;

	/*
	 * Adjust search limits by the desired length.
	 * See implementation comment at top of unmapped_area().
	 */
	gap_end = info->high_limit;
	if (gap_end < length)
		return -ENOMEM;
	high_limit = gap_end - length;

	if (info->low_limit > high_limit)
		return -ENOMEM;
	low_limit = info->low_limit + length;

	/* Check highest gap, which does not precede any rbtree node */
	gap_start = mm->highest_vm_end;
	if (gap_start <= high_limit)
		goto found_highest;

	/* Check if rbtree root looks promising */
	if (RB_EMPTY_ROOT(&mm->mm_rb))
		return -ENOMEM;
	vma = rb_entry(mm->mm_rb.rb_node, struct vm_area_struct, vm_rb);
	if (vma->rb_subtree_gap < length)
		return -ENOMEM;

	while (true) {
		/* Visit right subtree if it looks promising */
		gap_start = vma->vm_prev ? vma->vm_prev->vm_end : 0;
		if (gap_start <= high_limit && vma->vm_rb.rb_right) {
			struct vm_area_struct *right =
				rb_entry(vma->vm_rb.rb_right,
					 struct vm_area_struct, vm_rb);
			if (right->rb_subtree_gap >= length) {
				vma = right;
				continue;
			}
		}

check_current:
		/* Check if current node has a suitable gap */
		gap_end = vma->vm_start;
		if (gap_end < low_limit)
			return -ENOMEM;
		if (gap_start <= high_limit && gap_end - gap_start >= length)
			goto found;

		/* Visit left subtree if it looks promising */
		if (vma->vm_rb.rb_left) {
			struct vm_area_struct *left =
				rb_entry(vma->vm_rb.rb_left,
					 struct vm_area_struct, vm_rb);
			if (left->rb_subtree_gap >= length) {
				vma = left;
				continue;
			}
		}

		/* Go back up the rbtree to find next candidate node */
		while (true) {
			struct rb_node *prev = &vma->vm_rb;
			if (!rb_parent(prev))
				return -ENOMEM;
			vma = rb_entry(rb_parent(prev),
				       struct vm_area_struct, vm_rb);
			if (prev == vma->vm_rb.rb_right) {
				gap_start = vma->vm_prev ?
					vma->vm_prev->vm_end : 0;
				goto check_current;
			}
		}
	}

found:
	/* We found a suitable gap. Clip it with the original high_limit. */
	if (gap_end > info->high_limit)
		gap_end = info->high_limit;

found_highest:
	/* Compute highest gap address at the desired alignment */
	gap_end -= info->length;
	gap_end -= (gap_end - info->align_offset) & info->align_mask;

	VM_BUG_ON(gap_end < info->low_limit);
	VM_BUG_ON(gap_end < gap_start);
	return gap_end;
}

/* Get an address range which is currently unmapped.
 * For shmat() with addr=0.
 *
 * Ugly calling convention alert:
 * Return value with the low bits set means error value,
 * ie
 *	if (ret & ~PAGE_MASK)
 *		error = ret;
 *
 * This function "knows" that -ENOMEM has the bits set.
 */
#ifndef HAVE_ARCH_UNMAPPED_AREA
/**
 * 分配从低端地址向高端地址移动的线性区
 */
unsigned long
arch_get_unmapped_area(struct file *filp, unsigned long addr,
		unsigned long len, unsigned long pgoff, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct vm_unmapped_area_info info;

	/**
	 * 当然了，只要是普通进程地址，就不能超过TASK_SIZE(一般就是3G)
	 */
	if (len > TASK_SIZE - mmap_min_addr)
		return -ENOMEM;

	/**
	 * 检查是否设置了MAP_FIXED标志，该标志表示映射将在固定地址创建。倘若如此，
	 * 内核只会确保该地址满足对齐要求(按页),而且所要求的区间完全在可用地址空间内
	 */
	/* 必须使用传入的地址，直接返回 */
	if (flags & MAP_FIXED)
		return addr;

	/**
	 * 如果没有指定区域位置，内核将调用find_vma()在进程的虚拟内存区中查找适当
	 * 的可用区域。如果指定了一个特定的优先选用(与固定地址不同)地址，内核
	 * 会检查该区域是否与现存区域重叠,如果不重叠，则将该地址作为目标返回
	 */
	/* 优先使用传递的地址 */
	if (addr) {
		/**
		 * 如果地址不是0，就从addr处开始分配,当然，需要将addr按4K取整
		 */
		addr = PAGE_ALIGN(addr); //将地址按页对齐
		/* 查找后一个地址区间 */
		vma = find_vma(mm, addr); //获取一个vma，该vma可能包含了addr也可能在addr后面并且离addr最近
        /*这里确定是否有一块适合的空闲区域，先要保证addr+len不会 
               超过进程地址空间的最大允许范围，然后如果前面vma获取成功的话则要保证 
               vma位于addr的后面并且addr+len不会延伸到该vma的区域*/ 
		/**
		 * TASK_SIZE - len >= addr而不是addr + len <= TASK_SIZE
		 */
		/* 没有整形溢出 && 没有与后一个区间重叠 */
		if (TASK_SIZE - len >= addr && addr >= mmap_min_addr &&
		    (!vma || addr + len <= vma->vm_start))
			/* 没有被映射，这块区间可以使用。 */
			return addr;
	}

	info.flags = 0;
	info.length = len;
	info.low_limit = mm->mmap_base;
	info.high_limit = TASK_SIZE;
	info.align_mask = 0;
	return vm_unmapped_area(&info);
}
#endif

/*
 * This mmap-allocator allocates new areas top-down from below the
 * stack's low limit (the base):
 */
#ifndef HAVE_ARCH_UNMAPPED_AREA_TOPDOWN
unsigned long
arch_get_unmapped_area_topdown(struct file *filp, const unsigned long addr0,
			  const unsigned long len, const unsigned long pgoff,
			  const unsigned long flags)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	unsigned long addr = addr0;
	struct vm_unmapped_area_info info;

	/* requested length too big for entire address space */
	if (len > TASK_SIZE - mmap_min_addr)
		return -ENOMEM;

	if (flags & MAP_FIXED)
		return addr;

	/* requesting a specific address */
	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(mm, addr);
		if (TASK_SIZE - len >= addr && addr >= mmap_min_addr &&
				(!vma || addr + len <= vma->vm_start))
			return addr;
	}

	info.flags = VM_UNMAPPED_AREA_TOPDOWN;
	info.length = len;
	info.low_limit = max(PAGE_SIZE, mmap_min_addr);
	info.high_limit = mm->mmap_base;
	info.align_mask = 0;
	addr = vm_unmapped_area(&info);

	/*
	 * A failed mmap() very likely causes application failure,
	 * so fall back to the bottom-up function here. This scenario
	 * can happen with large stack limits and large mmap()
	 * allocations.
	 */
	if (offset_in_page(addr)) {
		VM_BUG_ON(addr != -ENOMEM);
		info.flags = 0;
		info.low_limit = TASK_UNMAPPED_BASE;
		info.high_limit = TASK_SIZE;
		addr = vm_unmapped_area(&info);
	}

	return addr;
}
#endif

/**
 * 在进程的虚拟地址空间中，查找一段可用的区域
 *
 * 搜索进程的地址空间以找到一个可以使用的线性地址区间。
 * len-区间的长度。
 * addr-指定必须从哪个地址开始进行查找。如果查找成功，
 *    函数返回这个新区间的起始地址。否则返回错误码-ENOMEM.
 */
unsigned long
get_unmapped_area(struct file *file, unsigned long addr, unsigned long len,
		unsigned long pgoff, unsigned long flags)
{
	unsigned long (*get_area)(struct file *, unsigned long,
				  unsigned long, unsigned long, unsigned long);

	unsigned long error = arch_mmap_check(addr, len, flags);
	if (error)
		return error;

	/* Careful about overflows.. */
	if (len > TASK_SIZE)
		return -ENOMEM;

	/* 默认用mm_struct中的get_unmapped_area回调，一般是arch_get_unmapped_area */
	get_area = current->mm->get_unmapped_area;
	if (file) {
		if (file->f_op->get_unmapped_area)
			get_area = file->f_op->get_unmapped_area;
	} else if (flags & MAP_SHARED) {
		/*
		 * mmap_region() will call shmem_zero_setup() to create a file,
		 * so use shmem's get_unmapped_area in case it can be huge.
		 * do_mmap_pgoff() will clear pgoff, so match alignment.
		 */
		pgoff = 0;
		get_area = shmem_get_unmapped_area;
	}
	/* 调用体系结构对应的函数来查找可用区域 */
	addr = get_area(file, addr, len, pgoff, flags);
	if (IS_ERR_VALUE(addr))
		return addr;

	/**
	 * 如果addr不等于0,就检查指定的地址是否在用户态空间。
	 * 这是为了避免用户态程序绕过安全检查而影响内核地址空间。
	 */
	if (addr > TASK_SIZE - len)
		return -ENOMEM;
	/**
	 * 检查是否与页边界对齐。
	 */
	if (offset_in_page(addr))
		return -EINVAL;

	error = security_mmap_addr(addr);
	return error ? error : addr;
}

EXPORT_SYMBOL(get_unmapped_area);

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
/*
 * 通过虚拟地址，find_vma可以查找用户地址空间中
 * 结束地址在给定地址之后的第一个区域，
 * 即满足addr<vm_area_struct->vm_end条件的第一个区域。
 * 该函数的参数不仅包括虚拟地址(addr)，还包括
 * 一个指向mm_struct实例的指针，后者指定了扫描哪个进程的地址空间。
 *
 * 查找给定地址的最邻近区。
 * 它查找线性区的vm_end字段大于addr的第一个线性区的位置。并返回这个线性区描述符的地址。
 * 如果没有这样的线性区存在，就返回NULL。
 * 由find_vma函数所选择的线性区并不一定要包含addr，因为addr可能位于任何线性区之外。
 *     mm-进程内存描述符地址
 *     addr-线性地址。
 */
struct vm_area_struct *find_vma(struct mm_struct *mm, unsigned long addr)
{
	struct rb_node *rb_node;
	struct vm_area_struct *vma;

	/* Check the cache first. */
	//首先尝试mmap_cache中缓存的vma
	/*
	 * 内核首先检查上次处理的区域中是否包含所需的地址，即是否
	 * 该区域的结束地址在目标地址之后，而起始
	 * 地址在目标地址之前。倘若如此，内核
	 * 不会执行if语句，而是立即将指向该区域的指针返回。
	 */
	vma = vmacache_find(mm, addr);
	if (likely(vma))
		return vma;

	rb_node = mm->mm_rb.rb_node;

	/* 遍历红黑树 */
	while (rb_node) {
		struct vm_area_struct *tmp;

		/* 从红黑树的节点找到对应的VMA结构 */
		tmp = rb_entry(rb_node, struct vm_area_struct, vm_rb);

		if (tmp->vm_end > addr) {
/*
		如果当前区域结束地址大于目标地址，则从左子结点开始
*/
			vma = tmp;

			if (tmp->vm_start <= addr)
			/**
			 * 当前线性区包含addr,退出循环并返回vma.
			 */
				break;
				
			/* 节点所在区域太多，移动到左边继续查找, 在左子树中继续 */
			rb_node = rb_node->rb_left;
		} else
/*
		如果当前区域的结束地址小于等于目标地址，则从右子结点开始
*/
			rb_node = rb_node->rb_right;
	}

	/**
	 * 因为下一次find_vma()调用搜索同一个区域中临近地址的可能性很高
	 * 记录下mmap_cache。这样，下次就从mmap_cache继续查找。
	 */
	if (vma)
		vmacache_update(addr, vma); //将结果保存在缓存中
	return vma;
}

EXPORT_SYMBOL(find_vma);

/*
 * Same as find_vma, but also return a pointer to the previous VMA in *pprev.
 */
/**
 * 与find_vma类似，不同的是它把函数选中的前一个线性区描述符的指针赋给附加参数ppre。
 */
struct vm_area_struct *
find_vma_prev(struct mm_struct *mm, unsigned long addr,
			struct vm_area_struct **pprev)
{
	struct vm_area_struct *vma;

	vma = find_vma(mm, addr);
	if (vma) {
		*pprev = vma->vm_prev;
	} else {
		struct rb_node *rb_node = mm->mm_rb.rb_node;
		*pprev = NULL;
		while (rb_node) {
			*pprev = rb_entry(rb_node, struct vm_area_struct, vm_rb);
			rb_node = rb_node->rb_right;
		}
	}
	return vma;
}

/*
 * Verify that the stack growth is acceptable and
 * update accounting. This is shared with both the
 * grow-up and grow-down cases.
 */
static int acct_stack_growth(struct vm_area_struct *vma, unsigned long size, unsigned long grow)
{
	struct mm_struct *mm = vma->vm_mm;
	struct rlimit *rlim = current->signal->rlim;
	unsigned long new_start, actual_size;

	/* address space limit tests */
	if (!may_expand_vm(mm, vma->vm_flags, grow))
		return -ENOMEM;

	/* Stack limit test */
	actual_size = size;
	if (size && (vma->vm_flags & (VM_GROWSUP | VM_GROWSDOWN)))
		actual_size -= PAGE_SIZE;
	if (actual_size > READ_ONCE(rlim[RLIMIT_STACK].rlim_cur))
		return -ENOMEM;

	/* mlock limit tests */
	if (vma->vm_flags & VM_LOCKED) {
		unsigned long locked;
		unsigned long limit;
		locked = mm->locked_vm + grow;
		limit = READ_ONCE(rlim[RLIMIT_MEMLOCK].rlim_cur);
		limit >>= PAGE_SHIFT;
		if (locked > limit && !capable(CAP_IPC_LOCK))
			return -ENOMEM;
	}

	/* Check to ensure the stack will not grow into a hugetlb-only region */
	new_start = (vma->vm_flags & VM_GROWSUP) ? vma->vm_start :
			vma->vm_end - size;
	if (is_hugepage_only_range(vma->vm_mm, new_start, size))
		return -EFAULT;

	/*
	 * Overcommit..  This must be the final test, as it will
	 * update security statistics.
	 */
	if (security_vm_enough_memory_mm(mm, grow))
		return -ENOMEM;

	return 0;
}

#if defined(CONFIG_STACK_GROWSUP) || defined(CONFIG_IA64)
/*
 * PA-RISC uses this for its stack; IA64 for its Register Backing Store.
 * vma is the last one with address > vma->vm_end.  Have to extend vma.
 */
int expand_upwards(struct vm_area_struct *vma, unsigned long address)
{
	struct mm_struct *mm = vma->vm_mm;
	int error = 0;

	if (!(vma->vm_flags & VM_GROWSUP))
		return -EFAULT;

	/* Guard against wrapping around to address 0. */
	if (address < PAGE_ALIGN(address+4))
		address = PAGE_ALIGN(address+4);
	else
		return -ENOMEM;

	/* We must make sure the anon_vma is allocated. */
	if (unlikely(anon_vma_prepare(vma)))
		return -ENOMEM;

	/*
	 * vma->vm_start/vm_end cannot change under us because the caller
	 * is required to hold the mmap_sem in read mode.  We need the
	 * anon_vma lock to serialize against concurrent expand_stacks.
	 */
	anon_vma_lock_write(vma->anon_vma);

	/* Somebody else might have raced and expanded it already */
	if (address > vma->vm_end) {
		unsigned long size, grow;

		size = address - vma->vm_start;
		grow = (address - vma->vm_end) >> PAGE_SHIFT;

		error = -ENOMEM;
		if (vma->vm_pgoff + (size >> PAGE_SHIFT) >= vma->vm_pgoff) {
			error = acct_stack_growth(vma, size, grow);
			if (!error) {
				/*
				 * vma_gap_update() doesn't support concurrent
				 * updates, but we only hold a shared mmap_sem
				 * lock here, so we need to protect against
				 * concurrent vma expansions.
				 * anon_vma_lock_write() doesn't help here, as
				 * we don't guarantee that all growable vmas
				 * in a mm share the same root anon vma.
				 * So, we reuse mm->page_table_lock to guard
				 * against concurrent vma expansions.
				 */
				spin_lock(&mm->page_table_lock);
				if (vma->vm_flags & VM_LOCKED)
					mm->locked_vm += grow;
				vm_stat_account(mm, vma->vm_flags, grow);
				anon_vma_interval_tree_pre_update_vma(vma);
				vma->vm_end = address;
				anon_vma_interval_tree_post_update_vma(vma);
				if (vma->vm_next)
					vma_gap_update(vma->vm_next);
				else
					mm->highest_vm_end = address;
				spin_unlock(&mm->page_table_lock);

				perf_event_mmap(vma);
			}
		}
	}
	anon_vma_unlock_write(vma->anon_vma);
	khugepaged_enter_vma_merge(vma, vma->vm_flags);
	validate_mm(mm);
	return error;
}
#endif /* CONFIG_STACK_GROWSUP || CONFIG_IA64 */

/*
 * vma is the first one with address < vma->vm_start.  Have to extend vma.
 */
int expand_downwards(struct vm_area_struct *vma,
				   unsigned long address)
{
	struct mm_struct *mm = vma->vm_mm;
	int error;

	address &= PAGE_MASK;
	error = security_mmap_addr(address);
	if (error)
		return error;

	/* We must make sure the anon_vma is allocated. */
	if (unlikely(anon_vma_prepare(vma)))
		return -ENOMEM;

	/*
	 * vma->vm_start/vm_end cannot change under us because the caller
	 * is required to hold the mmap_sem in read mode.  We need the
	 * anon_vma lock to serialize against concurrent expand_stacks.
	 */
	anon_vma_lock_write(vma->anon_vma);

	/* Somebody else might have raced and expanded it already */
	if (address < vma->vm_start) {
		unsigned long size, grow;

		size = vma->vm_end - address;
		grow = (vma->vm_start - address) >> PAGE_SHIFT;

		error = -ENOMEM;
		if (grow <= vma->vm_pgoff) {
			error = acct_stack_growth(vma, size, grow);
			if (!error) {
				/*
				 * vma_gap_update() doesn't support concurrent
				 * updates, but we only hold a shared mmap_sem
				 * lock here, so we need to protect against
				 * concurrent vma expansions.
				 * anon_vma_lock_write() doesn't help here, as
				 * we don't guarantee that all growable vmas
				 * in a mm share the same root anon vma.
				 * So, we reuse mm->page_table_lock to guard
				 * against concurrent vma expansions.
				 */
				spin_lock(&mm->page_table_lock);
				if (vma->vm_flags & VM_LOCKED)
					mm->locked_vm += grow;
				vm_stat_account(mm, vma->vm_flags, grow);
				anon_vma_interval_tree_pre_update_vma(vma);
				vma->vm_start = address;
				vma->vm_pgoff -= grow;
				anon_vma_interval_tree_post_update_vma(vma);
				vma_gap_update(vma);
				spin_unlock(&mm->page_table_lock);

				perf_event_mmap(vma);
			}
		}
	}
	anon_vma_unlock_write(vma->anon_vma);
	khugepaged_enter_vma_merge(vma, vma->vm_flags);
	validate_mm(mm);
	return error;
}

/*
 * Note how expand_stack() refuses to expand the stack all the way to
 * abut the next virtual mapping, *unless* that mapping itself is also
 * a stack mapping. We want to leave room for a guard page, after all
 * (the guard page itself is not added here, that is done by the
 * actual page faulting logic)
 *
 * This matches the behavior of the guard page logic (see mm/memory.c:
 * check_stack_guard_page()), which only allows the guard page to be
 * removed under these circumstances.
 */
#ifdef CONFIG_STACK_GROWSUP
int expand_stack(struct vm_area_struct *vma, unsigned long address)
{
	struct vm_area_struct *next;

	address &= PAGE_MASK;
	next = vma->vm_next;
	if (next && next->vm_start == address + PAGE_SIZE) {
		if (!(next->vm_flags & VM_GROWSUP))
			return -ENOMEM;
	}
	return expand_upwards(vma, address);
}

struct vm_area_struct *
find_extend_vma(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma, *prev;

	addr &= PAGE_MASK;
	vma = find_vma_prev(mm, addr, &prev);
	if (vma && (vma->vm_start <= addr))
		return vma;
	if (!prev || expand_stack(prev, addr))
		return NULL;
	if (prev->vm_flags & VM_LOCKED)
		populate_vma_page_range(prev, addr, prev->vm_end, NULL);
	return prev;
}
#else
/**
 * 当进程操作堆栈时，遇到地址访问错误，那么，将调用本过程，扩展用户态程序的堆栈空间。
 */
int expand_stack(struct vm_area_struct *vma, unsigned long address)
{
	struct vm_area_struct *prev;

	address &= PAGE_MASK;
	prev = vma->vm_prev;
	if (prev && prev->vm_end == address) {
		if (!(prev->vm_flags & VM_GROWSDOWN))
			return -ENOMEM;
	}
	return expand_downwards(vma, address);
}

struct vm_area_struct *
find_extend_vma(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma;
	unsigned long start;

	addr &= PAGE_MASK;
	vma = find_vma(mm, addr);
	if (!vma)
		return NULL;
	if (vma->vm_start <= addr)
		return vma;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		return NULL;
	start = vma->vm_start;
	if (expand_stack(vma, addr))
		return NULL;
	if (vma->vm_flags & VM_LOCKED)
		populate_vma_page_range(vma, addr, start, NULL);
	return vma;
}
#endif

EXPORT_SYMBOL_GPL(find_extend_vma);

/*
 * Ok - we have the memory areas we should free on the vma list,
 * so release them, and do the vma updates.
 *
 * Called with the mm semaphore held.
 */
static void remove_vma_list(struct mm_struct *mm, struct vm_area_struct *vma)
{
	unsigned long nr_accounted = 0;

	/* Update high watermark before we lower total_vm */
	update_hiwater_vm(mm);
	do {
		long nrpages = vma_pages(vma);

		if (vma->vm_flags & VM_ACCOUNT)
			nr_accounted += nrpages;
		vm_stat_account(mm, vma->vm_flags, -nrpages);
		vma = remove_vma(vma);
	} while (vma);
	vm_unacct_memory(nr_accounted);
	validate_mm(mm);
}

/*
 * Get rid of page table information in the indicated region.
 *
 * Called with the mm semaphore held.
 */
/**
 * 遍历线性区链表并释放它们的页框。
 * mm-内存描述符指针
 * vma-指向第一个被删除线性区描述符的指针。
 * prev-指向vma前面的线性区的指针。
 * start,end-界定被删除线性地址区间的范围。
 */
static void unmap_region(struct mm_struct *mm,
		struct vm_area_struct *vma, struct vm_area_struct *prev,
		unsigned long start, unsigned long end)
{
	struct vm_area_struct *next = prev ? prev->vm_next : mm->mmap;
	struct mmu_gather tlb;

	lru_add_drain();
	/**
	 * tlb_gather_mmu初始化每CPU变量mmu_gathers。mmu_gather的值依赖于体系结构。
	 * 通常该变量应该存放成功更新进程页表项所需要的所有信息。
	 * 在x86中，该函数只是简单地把内存描述符指针mm的值赋给本地CPU的mmu_gather变量
	 */
	tlb_gather_mmu(&tlb, mm, start, end);
	update_hiwater_rss(mm);
	/**
	 * 调用unmap_vmas扫描线性地址空间的所有页表项。
	 * 如果只有一个CPU，就调用free_swap_and_cache反复释放相应的页。
	 */
	unmap_vmas(&tlb, vma, start, end);
	/**
	 * free_pgtables回收已经清空的进程页表。
	 */
	free_pgtables(&tlb, vma, prev ? prev->vm_end : FIRST_USER_ADDRESS,
				 next ? next->vm_start : USER_PGTABLES_CEILING);
	/**
	 * 刷新TLB
	 */
	tlb_finish_mmu(&tlb, start, end);
}

/*
 * Create a list of vma's touched by the unmap, removing them from the mm's
 * vma list as we go..
 */
/*
 * detach_vmas_to_be_unmapped()会遍历vm_area_struct实例的线性表，直至要解除
 * 映射的地址范围已经全部涵盖在内。该结构的vm_next成员在此处用于将解除映射
 * 的区域彼此连接起来。该函数还将mmap缓存设置为NULL，使之无效。
 *
 * 从进程的线性地址空间中删除位于线性地址区间中的线性区。
 * 要删除的线性区的描述放在一个排好序的链表中，局部变量mpnt指向该链表的头。
 */
static void
detach_vmas_to_be_unmapped(struct mm_struct *mm, struct vm_area_struct *vma,
	struct vm_area_struct *prev, unsigned long end)
{
	struct vm_area_struct **insertion_point;
	struct vm_area_struct *tail_vma = NULL;

	insertion_point = (prev ? &prev->vm_next : &mm->mmap);
	vma->vm_prev = NULL;
	do {
		vma_rb_erase(vma, &mm->mm_rb);
		mm->map_count--;
		tail_vma = vma;
		vma = vma->vm_next;
	} while (vma && vma->vm_start < end);
	*insertion_point = vma;
	if (vma) {
		vma->vm_prev = prev;
		vma_gap_update(vma);
	} else
		mm->highest_vm_end = prev ? prev->vm_end : 0;
	tail_vma->vm_next = NULL;

	/* Kill the cache */
	vmacache_invalidate(mm);
}

/*
 * __split_vma() bypasses sysctl_max_map_count checking.  We use this on the
 * munmap path where it doesn't make sense to fail.
 */
/**
 * 把与线性地址区间交叉的线性区划分成两个较小的区。一个在线性地址区间外部，
 * 另一个在区间的内部。
 *   mm-内存描述符指针。
 *   vma-要被划分的线性区。
 *   addr-区间与线性区之间交叉点的地址。
 *   new_below-表示区间与线性区之间交叉点在区间超始处还是结束处的标志。
 */
static int __split_vma(struct mm_struct *mm, struct vm_area_struct *vma,
	      unsigned long addr, int new_below)
{
	struct vm_area_struct *new;
	int err;

	if (is_vm_hugetlb_page(vma) && (addr &
					~(huge_page_mask(hstate_vma(vma)))))
		return -EINVAL;

	/**
	 * 获得线性区描述符。如果没有可用的空闲空间，就返回-ENOMEM
	 */
	new = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	/* most fields are the same, copy all, and then fixup */
	*new = *vma;

	INIT_LIST_HEAD(&new->anon_vma_chain);

	if (new_below)
		/**
		 * 如果new_below为1,表示线性地址区间的结束地址在vma线性区的内部。
		 * 因此把新线性区放在vma线性区的前面。因此把new->vm_end和vma->vm_start设置成addr.
		 */
		new->vm_end = addr;
	else {
		/**
		 * 线性地址区间的起始地址在vma线性区的内部。因此必须把新线性区
		 * 放在vma线性区之后。因此把new->vm_start和vma->vm_end设置成addr.
		 */
		new->vm_start = addr;
		new->vm_pgoff += ((addr - vma->vm_start) >> PAGE_SHIFT);
	}

	err = vma_dup_policy(vma, new);
	if (err)
		goto out_free_vma;

	err = anon_vma_clone(new, vma);
	if (err)
		goto out_free_mpol;

	if (new->vm_file)
		get_file(new->vm_file);

	/**
	 * 如果定义了新线性区的open方法，就执行它。
	 */
	if (new->vm_ops && new->vm_ops->open)
		new->vm_ops->open(new);

	/**
	 * 把新线性区链接到线性区链表和红黑树中。
	 */
	if (new_below)
		err = vma_adjust(vma, addr, vma->vm_end, vma->vm_pgoff +
			((addr - new->vm_start) >> PAGE_SHIFT), new);
	else
		err = vma_adjust(vma, vma->vm_start, addr, vma->vm_pgoff, new);

	/* Success. */
	if (!err)
		return 0;

	/* Clean everything up if vma_adjust failed. */
	if (new->vm_ops && new->vm_ops->close)
		new->vm_ops->close(new);
	if (new->vm_file)
		fput(new->vm_file);
	unlink_anon_vmas(new);
 out_free_mpol:
	mpol_put(vma_policy(new));
 out_free_vma:
	kmem_cache_free(vm_area_cachep, new);
	return err;
}

/*
 * Split a vma into two pieces at address 'addr', a new vma is allocated
 * either for the first part or the tail.
 */
int split_vma(struct mm_struct *mm, struct vm_area_struct *vma,
	      unsigned long addr, int new_below)
{
	if (mm->map_count >= sysctl_max_map_count)
		return -ENOMEM;

	return __split_vma(mm, vma, addr, new_below);
}

/* Munmap is split into 2 main parts -- this part which finds
 * what needs doing, and the areas themselves, which do the
 * work.  This now handles partial unmappings.
 * Jeremy Fitzhardinge <jeremy@goop.org>
 */
/**
 * 从当前进程的地址空间中删除一个线性地址区间。要删除的区间并不总是
 * 对应一个线性区。它或者是一个线性区的一部分，或者是多个线性区。
 * mm-进程内存描述符。
 * start-要删除的地址区间的起始地址。
 * len-要删除的长度。
 */
int do_munmap(struct mm_struct *mm, unsigned long start, size_t len)
{
	unsigned long end;
	struct vm_area_struct *vma, *prev, *last;

	/**
	 * 初步检查：线性区地址不能大于TASK_SIZE，start必须是4096的整数倍。
	 */
	if ((offset_in_page(start)) || start > TASK_SIZE || len > TASK_SIZE-start)
		return -EINVAL;

	/**
	 * len也不能为0
	 */
	len = PAGE_ALIGN(len);
	if (len == 0)
		return -EINVAL;

	/* Find the first overlapping VMA */
	/* 找到要解除映射的VMA及其前一个VMA */
	/**
	 * mpnt是要删除的线性地址区间之后第一个线性区的位置。mpnt->end>start
	 */
	vma = find_vma(mm, start);
	/* 地址没有被映射，退出 */
	if (!vma)
		return 0;
	prev = vma->vm_prev;
	/* we have  start < vma->vm_end  */

	/* if it doesn't overlap, we have nothing.. */
	end = start + len;
	/**
	 * 也没有与线性地址区间重叠的线性区，就不必做什么了。
	 */
	if (vma->vm_start >= end)
		return 0;

	/*
	 * If we need to split any vma, do it now to save pain later.
	 *
	 * Note: mremap's move_vma VM_ACCOUNT handling assumes a partially
	 * unmapped vm_area_struct will remain in use: so lower split_vma
	 * places tmp vma above, and higher split_vma places tmp vma below.
	 */
	/*
	 * 线性区的起始地址在mpnt内，就调用split_vma把线性区mpnt划分成两个较小的区：
	 * 一个区在线性地址区间外部，而另一个区间内部
	 *
	 * 如果解除映射区域的起始地址与find_vma() 找到的区域起始地址不同，则只解除部分
	 * 映射，而不是整个映射区域。在内核这样做之前，首先必须将现存的映射划分为几个
	 * 部分。映射的前一部分不需要解除映射，首先通过split_vma()分裂出来。
	 */
	if (start > vma->vm_start) {
		int error;

		/*
		 * Make sure that map_count on return from munmap() will
		 * not exceed its limit; but let map_count go just above
		 * its limit temporarily, to help free resources as expected.
		 */
		if (end < vma->vm_end && mm->map_count >= sysctl_max_map_count)
			return -ENOMEM;

		/* 将vma拆分 */
		error = __split_vma(mm, vma, start, 0);
		if (error)
			return error;
		/**
		 * prev以前存储的是指向线性区mpnt前面一个线性区的指针。
		 * 现在它指向mpnt，即指向线性地址区间外部的那个新线性区。
		 * 这样，prev仍然指向要删除的那个线性区前面的那个线性区。
		 */
		prev = vma;
	}

	/* Does it split the last one? */
	/*
	 * 如果解除映射的部分区域的末端与原区域末端并不重合，
	 * 那么原区域后部仍然有一部分未解除映射，因此需要对
	 * 这部分也重复上述的处理过程。
	 */
	/* 前面可能将start前面的vma拆分了，这次以end进行查找 */
	last = find_vma(mm, end);
	/**
	 * 如果线性地址空间的结束地址在一个线性区内部，就再次调用
	 * split_vma把最后重叠的那个线性区划分成两个较小的区。
	 */
	if (last && end > last->vm_start) {
		/* 拆分vma */
		int error = __split_vma(mm, last, end, 1);
		if (error)
			return error;
	}
	/**
	 * 确定从哪一个vma开始遍历，查找要munmap的vma 
	 *
	 * 更新mpnt，使它指向线性地址区间的第一个线性区。
	 * 如果prev为空，即没有，就从mm->mmap获得第一个线性区的地址。
	 */
	vma = prev ? prev->vm_next : mm->mmap;

	/*
	 * unlock any mlock()ed ranges before detaching vmas
	 */
	if (mm->locked_vm) {
		struct vm_area_struct *tmp = vma;
		while (tmp && tmp->vm_start < end) {
			if (tmp->vm_flags & VM_LOCKED) {
				mm->locked_vm -= vma_pages(tmp);
				munlock_vma_pages_all(tmp);
			}
			tmp = tmp->vm_next;
		}
	}

	/*
	 * Remove the vma's, and unmap the actual pages
	 */
	/*
	 * 调用detach_vmas_to_be_unmapped()，列出所有需要解除映射
	 * 的区域。由于解除映射操作可能设计地址空间中的任何区域，
	 * 很可能影响连续几个区域。内核可能拆分这一系列区域中首尾
	 * 两端的区域，以确保只影响到完整的区域。
	 */
	detach_vmas_to_be_unmapped(mm, vma, prev, end);
	/*
	 * 调用unmap_region()从页表删除与映射相关的所有项。
	 * 完成后，内核还必须确保将相关的项从TLB移除或使之无效。
	 */
	unmap_region(mm, vma, prev, start, end);

	arch_unmap(mm, vma, start, end);

	/* Fix up all other VM information */
	/*
	 * 释放vm_area_struct实例占用的内存，完成从内核中删除映射的工作。
	 */
	remove_vma_list(mm, vma);

	return 0;
}

int vm_munmap(unsigned long start, size_t len)
{
	int ret;
	struct mm_struct *mm = current->mm;

	/* 获取mmap_sem信号量 */
	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;
	/* 调用do_munmap完成实际的工作 */
	ret = do_munmap(mm, start, len);
	up_write(&mm->mmap_sem);
	return ret;
}
EXPORT_SYMBOL(vm_munmap);

/**
 * 解决文件映射
 * addr,len:要解除映射的起始地址及其长度
 */
SYSCALL_DEFINE2(munmap, unsigned long, addr, size_t, len)
{
	int ret;
	struct mm_struct *mm = current->mm;

	profile_munmap(addr);
	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;
	ret = do_munmap(mm, addr, len);
	up_write(&mm->mmap_sem);
	return ret;
}


/*
 * Emulation of deprecated remap_file_pages() syscall.
 */
SYSCALL_DEFINE5(remap_file_pages, unsigned long, start, unsigned long, size,
		unsigned long, prot, unsigned long, pgoff, unsigned long, flags)
{

	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long populate = 0;
	unsigned long ret = -EINVAL;
	struct file *file;

	pr_warn_once("%s (%d) uses deprecated remap_file_pages() syscall. See Documentation/vm/remap_file_pages.txt.\n",
		     current->comm, current->pid);

	if (prot)
		return ret;
	start = start & PAGE_MASK;
	size = size & PAGE_MASK;

	if (start + size <= start)
		return ret;

	/* Does pgoff wrap? */
	if (pgoff + (size >> PAGE_SHIFT) < pgoff)
		return ret;

	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;

	vma = find_vma(mm, start);

	if (!vma || !(vma->vm_flags & VM_SHARED))
		goto out;

	if (start < vma->vm_start)
		goto out;

	if (start + size > vma->vm_end) {
		struct vm_area_struct *next;

		for (next = vma->vm_next; next; next = next->vm_next) {
			/* hole between vmas ? */
			if (next->vm_start != next->vm_prev->vm_end)
				goto out;

			if (next->vm_file != vma->vm_file)
				goto out;

			if (next->vm_flags != vma->vm_flags)
				goto out;

			if (start + size <= next->vm_end)
				break;
		}

		if (!next)
			goto out;
	}

	prot |= vma->vm_flags & VM_READ ? PROT_READ : 0;
	prot |= vma->vm_flags & VM_WRITE ? PROT_WRITE : 0;
	prot |= vma->vm_flags & VM_EXEC ? PROT_EXEC : 0;

	flags &= MAP_NONBLOCK;
	flags |= MAP_SHARED | MAP_FIXED | MAP_POPULATE;
	if (vma->vm_flags & VM_LOCKED) {
		struct vm_area_struct *tmp;
		flags |= MAP_LOCKED;

		/* drop PG_Mlocked flag for over-mapped range */
		for (tmp = vma; tmp->vm_start >= start + size;
				tmp = tmp->vm_next) {
			/*
			 * Split pmd and munlock page on the border
			 * of the range.
			 */
			vma_adjust_trans_huge(tmp, start, start + size, 0);

			munlock_vma_pages_range(tmp,
					max(tmp->vm_start, start),
					min(tmp->vm_end, start + size));
		}
	}

	file = get_file(vma->vm_file);
	ret = do_mmap_pgoff(vma->vm_file, start, size,
			prot, flags, pgoff, &populate);
	fput(file);
out:
	up_write(&mm->mmap_sem);
	if (populate)
		mm_populate(ret, populate);
	if (!IS_ERR_VALUE(ret))
		ret = 0;
	return ret;
}

static inline void verify_mm_writelocked(struct mm_struct *mm)
{
#ifdef CONFIG_DEBUG_VM
	if (unlikely(down_read_trylock(&mm->mmap_sem))) {
		WARN_ON(1);
		up_read(&mm->mmap_sem);
	}
#endif
}

/*
 *  this is really a simplified "do_mmap".  it only handles
 *  anonymous maps.  eventually we may be able to do some
 *  brk-specific accounting here.
 */
/**
 * 它其实是do_mmap的简化版，但是它比do_mmap快，因为它假定线性区不
 * 映射磁盘上的文件。从而避免了检查线性区对象上的几个字段。
 */
static int do_brk_flags(unsigned long addr, unsigned long request, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma, *prev;
	unsigned long len;
	struct rb_node **rb_link, *rb_parent;
	pgoff_t pgoff = addr >> PAGE_SHIFT;
	int error;

	len = PAGE_ALIGN(request);
	if (len < request)
		return -ENOMEM;
	if (!len)
		return 0;

	/* Until we need other flags, refuse anything except VM_EXEC. */
	if ((flags & (~VM_EXEC)) != 0)
		return -EINVAL;
	flags |= VM_DATA_DEFAULT_FLAGS | VM_ACCOUNT | mm->def_flags;

	error = get_unmapped_area(NULL, addr, len, 0, MAP_FIXED);
	if (offset_in_page(error))
		return error;

	error = mlock_future_check(mm, mm->def_flags, len);
	if (error)
		return error;

	/*
	 * mm->mmap_sem is required to protect against another thread
	 * changing the mappings in case we sleep.
	 */
	verify_mm_writelocked(mm);

	/*
	 * Clear old maps.  this also does some error checking for us
	 */
	while (find_vma_links(mm, addr, addr + len, &prev, &rb_link,
			      &rb_parent)) {
		if (do_munmap(mm, addr, len))
			return -ENOMEM;
	}

	/* Check against address space limits *after* clearing old maps... */
	if (!may_expand_vm(mm, flags, len >> PAGE_SHIFT))
		return -ENOMEM;

	if (mm->map_count > sysctl_max_map_count)
		return -ENOMEM;

	if (security_vm_enough_memory_mm(mm, len >> PAGE_SHIFT))
		return -ENOMEM;

	/* Can we just expand an old private anonymous mapping? */
	vma = vma_merge(mm, prev, addr, addr + len, flags,
			NULL, NULL, pgoff, NULL, NULL_VM_UFFD_CTX);
	if (vma)
		goto out;

	/*
	 * create a vma struct for an anonymous mapping
	 */
	vma = kmem_cache_zalloc(vm_area_cachep, GFP_KERNEL);
	if (!vma) {
		vm_unacct_memory(len >> PAGE_SHIFT);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&vma->anon_vma_chain);
	vma->vm_mm = mm;
	vma->vm_start = addr;
	vma->vm_end = addr + len;
	vma->vm_pgoff = pgoff;
	vma->vm_flags = flags;
	vma->vm_page_prot = vm_get_page_prot(flags);
	vma_link(mm, vma, prev, rb_link, rb_parent);
out:
	perf_event_mmap(vma);
	mm->total_vm += len >> PAGE_SHIFT;
	mm->data_vm += len >> PAGE_SHIFT;
	if (flags & VM_LOCKED)
		mm->locked_vm += (len >> PAGE_SHIFT);
	vma->vm_flags |= VM_SOFTDIRTY;
	return 0;
}

static int do_brk(unsigned long addr, unsigned long len)
{
	return do_brk_flags(addr, len, 0);
}

int vm_brk_flags(unsigned long addr, unsigned long len, unsigned long flags)
{
	struct mm_struct *mm = current->mm;
	int ret;
	bool populate;

	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;

	ret = do_brk_flags(addr, len, flags);
	populate = ((mm->def_flags & VM_LOCKED) != 0);
	up_write(&mm->mmap_sem);
	if (populate && !ret)
		mm_populate(addr, len);
	return ret;
}
EXPORT_SYMBOL(vm_brk_flags);

int vm_brk(unsigned long addr, unsigned long len)
{
	return vm_brk_flags(addr, len, 0);
}
EXPORT_SYMBOL(vm_brk);

/* Release all mmaps. */
void exit_mmap(struct mm_struct *mm)
{
	struct mmu_gather tlb;
	struct vm_area_struct *vma;
	unsigned long nr_accounted = 0;

	/* mm's last user has gone, and its about to be pulled down */
	mmu_notifier_release(mm);

	if (mm->locked_vm) {
		vma = mm->mmap;
		while (vma) {
			if (vma->vm_flags & VM_LOCKED)
				munlock_vma_pages_all(vma);
			vma = vma->vm_next;
		}
	}

	arch_exit_mmap(mm);

	vma = mm->mmap;
	if (!vma)	/* Can happen if dup_mmap() received an OOM */
		return;

	lru_add_drain();
	flush_cache_mm(mm);
	tlb_gather_mmu(&tlb, mm, 0, -1);
	/* update_hiwater_rss(mm) here? but nobody should be looking */
	/* Use -1 here to ensure all VMAs in the mm are unmapped */
	unmap_vmas(&tlb, vma, 0, -1);

	free_pgtables(&tlb, vma, FIRST_USER_ADDRESS, USER_PGTABLES_CEILING);
	tlb_finish_mmu(&tlb, 0, -1);

	/*
	 * Walk the list again, actually closing and freeing it,
	 * with preemption enabled, without holding any MM locks.
	 */
	while (vma) {
		if (vma->vm_flags & VM_ACCOUNT)
			nr_accounted += vma_pages(vma);
		vma = remove_vma(vma);
	}
	vm_unacct_memory(nr_accounted);
}

/* Insert vm structure into process list sorted by address
 * and into the inode's i_mmap tree.  If vm_file is non-NULL
 * then i_mmap_rwsem is taken here.
 */
/**
 * 在线性区对象链表和内存描述符的红黑树中插入一个vm_area_struct结构。
 * mm-指定进程内存描述符的地址。
 * vmp-指定要插入的vm_area_struct对象的地址。要求线性区对象的vm_start和vm_end字段必须被初始化。
 */
int insert_vm_struct(struct mm_struct *mm, struct vm_area_struct *vma)
{
	struct vm_area_struct *prev;
	struct rb_node **rb_link, *rb_parent;

	/**
	 * 根据待插入区域的起始地址，查找前一个区域，红黑树的父节点，叶节点
	 */
	if (find_vma_links(mm, vma->vm_start, vma->vm_end,
			   &prev, &rb_link, &rb_parent))
		return -ENOMEM;
	/* 此VMA需要接受审计 && 超过允许的空间范围了 */
	if ((vma->vm_flags & VM_ACCOUNT) &&
	     security_vm_enough_memory_mm(mm, vma_pages(vma)))
		return -ENOMEM;

	/*
	 * The vm_pgoff of a purely anonymous vma should be irrelevant
	 * until its first write fault, when page's anon_vma and index
	 * are set.  But now set the vm_pgoff it will almost certainly
	 * end up with (unless mremap moves it elsewhere before that
	 * first wfault), so /proc/pid/maps tells a consistent story.
	 *
	 * By setting it to reflect the virtual start address of the
	 * vma, merges and splits can happen in a seamless way, just
	 * using the existing file pgoff checks and manipulations.
	 * Similarly in do_mmap_pgoff and in do_brk.
	 */
	/* 匿名映射的vm_pgoff表示起始地址的页号 */
	if (vma_is_anonymous(vma)) {
		BUG_ON(vma->anon_vma);
		vma->vm_pgoff = vma->vm_start >> PAGE_SHIFT;
	}

	/*
	 * find_vma_links()查到的信息足以使用vma_link()将
	 * 新区域合并到该进程现存的数据结构中。
	 *
	 * 调用vma_link执行以下操作:
	 *   在mm->mmap所指向的链表中插入线性区。
	 *   在红黑树中插入线性区。
	 *   如果线性区是匿名的，就把它插入相应的anon_vma数据结构作为头节点的链表中。
	 *   递增mm->map_count计数器。
	 *   如果线性区包含一个内存映射文件，则vma_link执行其他与内存映射文件相关的任务。
	 */
	vma_link(mm, vma, prev, rb_link, rb_parent);
	return 0;
}

/*
 * Copy the vma structure to a new location in the same mm,
 * prior to moving page table entries, to effect an mremap move.
 */
struct vm_area_struct *copy_vma(struct vm_area_struct **vmap,
	unsigned long addr, unsigned long len, pgoff_t pgoff,
	bool *need_rmap_locks)
{
	struct vm_area_struct *vma = *vmap;
	unsigned long vma_start = vma->vm_start;
	struct mm_struct *mm = vma->vm_mm;
	struct vm_area_struct *new_vma, *prev;
	struct rb_node **rb_link, *rb_parent;
	bool faulted_in_anon_vma = true;

	/*
	 * If anonymous vma has not yet been faulted, update new pgoff
	 * to match new location, to increase its chance of merging.
	 */
	if (unlikely(vma_is_anonymous(vma) && !vma->anon_vma)) {
		pgoff = addr >> PAGE_SHIFT;
		faulted_in_anon_vma = false;
	}

	if (find_vma_links(mm, addr, addr + len, &prev, &rb_link, &rb_parent))
		return NULL;	/* should never get here */
	new_vma = vma_merge(mm, prev, addr, addr + len, vma->vm_flags,
			    vma->anon_vma, vma->vm_file, pgoff, vma_policy(vma),
			    vma->vm_userfaultfd_ctx);
	if (new_vma) {
		/*
		 * Source vma may have been merged into new_vma
		 */
		if (unlikely(vma_start >= new_vma->vm_start &&
			     vma_start < new_vma->vm_end)) {
			/*
			 * The only way we can get a vma_merge with
			 * self during an mremap is if the vma hasn't
			 * been faulted in yet and we were allowed to
			 * reset the dst vma->vm_pgoff to the
			 * destination address of the mremap to allow
			 * the merge to happen. mremap must change the
			 * vm_pgoff linearity between src and dst vmas
			 * (in turn preventing a vma_merge) to be
			 * safe. It is only safe to keep the vm_pgoff
			 * linear if there are no pages mapped yet.
			 */
			VM_BUG_ON_VMA(faulted_in_anon_vma, new_vma);
			*vmap = vma = new_vma;
		}
		*need_rmap_locks = (new_vma->vm_pgoff <= vma->vm_pgoff);
	} else {
		new_vma = kmem_cache_alloc(vm_area_cachep, GFP_KERNEL);
		if (!new_vma)
			goto out;
		*new_vma = *vma;
		new_vma->vm_start = addr;
		new_vma->vm_end = addr + len;
		new_vma->vm_pgoff = pgoff;
		if (vma_dup_policy(vma, new_vma))
			goto out_free_vma;
		INIT_LIST_HEAD(&new_vma->anon_vma_chain);
		if (anon_vma_clone(new_vma, vma))
			goto out_free_mempol;
		if (new_vma->vm_file)
			get_file(new_vma->vm_file);
		if (new_vma->vm_ops && new_vma->vm_ops->open)
			new_vma->vm_ops->open(new_vma);
		vma_link(mm, new_vma, prev, rb_link, rb_parent);
		*need_rmap_locks = false;
	}
	return new_vma;

out_free_mempol:
	mpol_put(vma_policy(new_vma));
out_free_vma:
	kmem_cache_free(vm_area_cachep, new_vma);
out:
	return NULL;
}

/*
 * Return true if the calling process may expand its vm space by the passed
 * number of pages
 */
bool may_expand_vm(struct mm_struct *mm, vm_flags_t flags, unsigned long npages)
{
	if (mm->total_vm + npages > rlimit(RLIMIT_AS) >> PAGE_SHIFT)
		return false;

	if (is_data_mapping(flags) &&
	    mm->data_vm + npages > rlimit(RLIMIT_DATA) >> PAGE_SHIFT) {
		/* Workaround for Valgrind */
		if (rlimit(RLIMIT_DATA) == 0 &&
		    mm->data_vm + npages <= rlimit_max(RLIMIT_DATA) >> PAGE_SHIFT)
			return true;
		if (!ignore_rlimit_data) {
			pr_warn_once("%s (%d): VmData %lu exceed data ulimit %lu. Update limits or use boot option ignore_rlimit_data.\n",
				     current->comm, current->pid,
				     (mm->data_vm + npages) << PAGE_SHIFT,
				     rlimit(RLIMIT_DATA));
			return false;
		}
	}

	return true;
}

void vm_stat_account(struct mm_struct *mm, vm_flags_t flags, long npages)
{
	mm->total_vm += npages;

	if (is_exec_mapping(flags))
		mm->exec_vm += npages;
	else if (is_stack_mapping(flags))
		mm->stack_vm += npages;
	else if (is_data_mapping(flags))
		mm->data_vm += npages;
}

static int special_mapping_fault(struct vm_area_struct *vma,
				 struct vm_fault *vmf);

/*
 * Having a close hook prevents vma merging regardless of flags.
 */
static void special_mapping_close(struct vm_area_struct *vma)
{
}

static const char *special_mapping_name(struct vm_area_struct *vma)
{
	return ((struct vm_special_mapping *)vma->vm_private_data)->name;
}

static int special_mapping_mremap(struct vm_area_struct *new_vma)
{
	struct vm_special_mapping *sm = new_vma->vm_private_data;

	if (sm->mremap)
		return sm->mremap(sm, new_vma);
	return 0;
}

static const struct vm_operations_struct special_mapping_vmops = {
	.close = special_mapping_close,
	.fault = special_mapping_fault,
	.mremap = special_mapping_mremap,
	.name = special_mapping_name,
};

static const struct vm_operations_struct legacy_special_mapping_vmops = {
	.close = special_mapping_close,
	.fault = special_mapping_fault,
};

static int special_mapping_fault(struct vm_area_struct *vma,
				struct vm_fault *vmf)
{
	pgoff_t pgoff;
	struct page **pages;

	if (vma->vm_ops == &legacy_special_mapping_vmops) {
		pages = vma->vm_private_data;
	} else {
		struct vm_special_mapping *sm = vma->vm_private_data;

		if (sm->fault)
			return sm->fault(sm, vma, vmf);

		pages = sm->pages;
	}

	for (pgoff = vmf->pgoff; pgoff && *pages; ++pages)
		pgoff--;

	if (*pages) {
		struct page *page = *pages;
		get_page(page);
		vmf->page = page;
		return 0;
	}

	return VM_FAULT_SIGBUS;
}

static struct vm_area_struct *__install_special_mapping(
	struct mm_struct *mm,
	unsigned long addr, unsigned long len,
	unsigned long vm_flags, void *priv,
	const struct vm_operations_struct *ops)
{
	int ret;
	struct vm_area_struct *vma;

	vma = kmem_cache_zalloc(vm_area_cachep, GFP_KERNEL);
	if (unlikely(vma == NULL))
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&vma->anon_vma_chain);
	vma->vm_mm = mm;
	vma->vm_start = addr;
	vma->vm_end = addr + len;

	vma->vm_flags = vm_flags | mm->def_flags | VM_DONTEXPAND | VM_SOFTDIRTY;
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);

	vma->vm_ops = ops;
	vma->vm_private_data = priv;

	ret = insert_vm_struct(mm, vma);
	if (ret)
		goto out;

	vm_stat_account(mm, vma->vm_flags, len >> PAGE_SHIFT);

	perf_event_mmap(vma);

	return vma;

out:
	kmem_cache_free(vm_area_cachep, vma);
	return ERR_PTR(ret);
}

bool vma_is_special_mapping(const struct vm_area_struct *vma,
	const struct vm_special_mapping *sm)
{
	return vma->vm_private_data == sm &&
		(vma->vm_ops == &special_mapping_vmops ||
		 vma->vm_ops == &legacy_special_mapping_vmops);
}

/*
 * Called with mm->mmap_sem held for writing.
 * Insert a new vma covering the given region, with the given flags.
 * Its pages are supplied by the given array of struct page *.
 * The array can be shorter than len >> PAGE_SHIFT if it's null-terminated.
 * The region past the last page supplied will always produce SIGBUS.
 * The array pointer and the pages it points to are assumed to stay alive
 * for as long as this mapping might exist.
 */
struct vm_area_struct *_install_special_mapping(
	struct mm_struct *mm,
	unsigned long addr, unsigned long len,
	unsigned long vm_flags, const struct vm_special_mapping *spec)
{
	return __install_special_mapping(mm, addr, len, vm_flags, (void *)spec,
					&special_mapping_vmops);
}

int install_special_mapping(struct mm_struct *mm,
			    unsigned long addr, unsigned long len,
			    unsigned long vm_flags, struct page **pages)
{
	struct vm_area_struct *vma = __install_special_mapping(
		mm, addr, len, vm_flags, (void *)pages,
		&legacy_special_mapping_vmops);

	return PTR_ERR_OR_ZERO(vma);
}

static DEFINE_MUTEX(mm_all_locks_mutex);

static void vm_lock_anon_vma(struct mm_struct *mm, struct anon_vma *anon_vma)
{
	if (!test_bit(0, (unsigned long *) &anon_vma->root->rb_root.rb_node)) {
		/*
		 * The LSB of head.next can't change from under us
		 * because we hold the mm_all_locks_mutex.
		 */
		down_write_nest_lock(&anon_vma->root->rwsem, &mm->mmap_sem);
		/*
		 * We can safely modify head.next after taking the
		 * anon_vma->root->rwsem. If some other vma in this mm shares
		 * the same anon_vma we won't take it again.
		 *
		 * No need of atomic instructions here, head.next
		 * can't change from under us thanks to the
		 * anon_vma->root->rwsem.
		 */
		if (__test_and_set_bit(0, (unsigned long *)
				       &anon_vma->root->rb_root.rb_node))
			BUG();
	}
}

static void vm_lock_mapping(struct mm_struct *mm, struct address_space *mapping)
{
	if (!test_bit(AS_MM_ALL_LOCKS, &mapping->flags)) {
		/*
		 * AS_MM_ALL_LOCKS can't change from under us because
		 * we hold the mm_all_locks_mutex.
		 *
		 * Operations on ->flags have to be atomic because
		 * even if AS_MM_ALL_LOCKS is stable thanks to the
		 * mm_all_locks_mutex, there may be other cpus
		 * changing other bitflags in parallel to us.
		 */
		if (test_and_set_bit(AS_MM_ALL_LOCKS, &mapping->flags))
			BUG();
		down_write_nest_lock(&mapping->i_mmap_rwsem, &mm->mmap_sem);
	}
}

/*
 * This operation locks against the VM for all pte/vma/mm related
 * operations that could ever happen on a certain mm. This includes
 * vmtruncate, try_to_unmap, and all page faults.
 *
 * The caller must take the mmap_sem in write mode before calling
 * mm_take_all_locks(). The caller isn't allowed to release the
 * mmap_sem until mm_drop_all_locks() returns.
 *
 * mmap_sem in write mode is required in order to block all operations
 * that could modify pagetables and free pages without need of
 * altering the vma layout. It's also needed in write mode to avoid new
 * anon_vmas to be associated with existing vmas.
 *
 * A single task can't take more than one mm_take_all_locks() in a row
 * or it would deadlock.
 *
 * The LSB in anon_vma->rb_root.rb_node and the AS_MM_ALL_LOCKS bitflag in
 * mapping->flags avoid to take the same lock twice, if more than one
 * vma in this mm is backed by the same anon_vma or address_space.
 *
 * We take locks in following order, accordingly to comment at beginning
 * of mm/rmap.c:
 *   - all hugetlbfs_i_mmap_rwsem_key locks (aka mapping->i_mmap_rwsem for
 *     hugetlb mapping);
 *   - all i_mmap_rwsem locks;
 *   - all anon_vma->rwseml
 *
 * We can take all locks within these types randomly because the VM code
 * doesn't nest them and we protected from parallel mm_take_all_locks() by
 * mm_all_locks_mutex.
 *
 * mm_take_all_locks() and mm_drop_all_locks are expensive operations
 * that may have to take thousand of locks.
 *
 * mm_take_all_locks() can fail if it's interrupted by signals.
 */
int mm_take_all_locks(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	struct anon_vma_chain *avc;

	BUG_ON(down_read_trylock(&mm->mmap_sem));

	mutex_lock(&mm_all_locks_mutex);

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (signal_pending(current))
			goto out_unlock;
		if (vma->vm_file && vma->vm_file->f_mapping &&
				is_vm_hugetlb_page(vma))
			vm_lock_mapping(mm, vma->vm_file->f_mapping);
	}

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (signal_pending(current))
			goto out_unlock;
		if (vma->vm_file && vma->vm_file->f_mapping &&
				!is_vm_hugetlb_page(vma))
			vm_lock_mapping(mm, vma->vm_file->f_mapping);
	}

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (signal_pending(current))
			goto out_unlock;
		if (vma->anon_vma)
			list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
				vm_lock_anon_vma(mm, avc->anon_vma);
	}

	return 0;

out_unlock:
	mm_drop_all_locks(mm);
	return -EINTR;
}

static void vm_unlock_anon_vma(struct anon_vma *anon_vma)
{
	if (test_bit(0, (unsigned long *) &anon_vma->root->rb_root.rb_node)) {
		/*
		 * The LSB of head.next can't change to 0 from under
		 * us because we hold the mm_all_locks_mutex.
		 *
		 * We must however clear the bitflag before unlocking
		 * the vma so the users using the anon_vma->rb_root will
		 * never see our bitflag.
		 *
		 * No need of atomic instructions here, head.next
		 * can't change from under us until we release the
		 * anon_vma->root->rwsem.
		 */
		if (!__test_and_clear_bit(0, (unsigned long *)
					  &anon_vma->root->rb_root.rb_node))
			BUG();
		anon_vma_unlock_write(anon_vma);
	}
}

static void vm_unlock_mapping(struct address_space *mapping)
{
	if (test_bit(AS_MM_ALL_LOCKS, &mapping->flags)) {
		/*
		 * AS_MM_ALL_LOCKS can't change to 0 from under us
		 * because we hold the mm_all_locks_mutex.
		 */
		i_mmap_unlock_write(mapping);
		if (!test_and_clear_bit(AS_MM_ALL_LOCKS,
					&mapping->flags))
			BUG();
	}
}

/*
 * The mmap_sem cannot be released by the caller until
 * mm_drop_all_locks() returns.
 */
void mm_drop_all_locks(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	struct anon_vma_chain *avc;

	BUG_ON(down_read_trylock(&mm->mmap_sem));
	BUG_ON(!mutex_is_locked(&mm_all_locks_mutex));

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (vma->anon_vma)
			list_for_each_entry(avc, &vma->anon_vma_chain, same_vma)
				vm_unlock_anon_vma(avc->anon_vma);
		if (vma->vm_file && vma->vm_file->f_mapping)
			vm_unlock_mapping(vma->vm_file->f_mapping);
	}

	mutex_unlock(&mm_all_locks_mutex);
}

/*
 * initialise the VMA slab
 */
void __init mmap_init(void)
{
	int ret;

	ret = percpu_counter_init(&vm_committed_as, 0, GFP_KERNEL);
	VM_BUG_ON(ret);
}

/*
 * Initialise sysctl_user_reserve_kbytes.
 *
 * This is intended to prevent a user from starting a single memory hogging
 * process, such that they cannot recover (kill the hog) in OVERCOMMIT_NEVER
 * mode.
 *
 * The default value is min(3% of free memory, 128MB)
 * 128MB is enough to recover with sshd/login, bash, and top/kill.
 */
static int init_user_reserve(void)
{
	unsigned long free_kbytes;

	free_kbytes = global_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10);

	sysctl_user_reserve_kbytes = min(free_kbytes / 32, 1UL << 17);
	return 0;
}
subsys_initcall(init_user_reserve);

/*
 * Initialise sysctl_admin_reserve_kbytes.
 *
 * The purpose of sysctl_admin_reserve_kbytes is to allow the sys admin
 * to log in and kill a memory hogging process.
 *
 * Systems with more than 256MB will reserve 8MB, enough to recover
 * with sshd, bash, and top in OVERCOMMIT_GUESS. Smaller systems will
 * only reserve 3% of free pages by default.
 */
static int init_admin_reserve(void)
{
	unsigned long free_kbytes;

	free_kbytes = global_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10);

	sysctl_admin_reserve_kbytes = min(free_kbytes / 32, 1UL << 13);
	return 0;
}
subsys_initcall(init_admin_reserve);

/*
 * Reinititalise user and admin reserves if memory is added or removed.
 *
 * The default user reserve max is 128MB, and the default max for the
 * admin reserve is 8MB. These are usually, but not always, enough to
 * enable recovery from a memory hogging process using login/sshd, a shell,
 * and tools like top. It may make sense to increase or even disable the
 * reserve depending on the existence of swap or variations in the recovery
 * tools. So, the admin may have changed them.
 *
 * If memory is added and the reserves have been eliminated or increased above
 * the default max, then we'll trust the admin.
 *
 * If memory is removed and there isn't enough free memory, then we
 * need to reset the reserves.
 *
 * Otherwise keep the reserve set by the admin.
 */
static int reserve_mem_notifier(struct notifier_block *nb,
			     unsigned long action, void *data)
{
	unsigned long tmp, free_kbytes;

	switch (action) {
	case MEM_ONLINE:
		/* Default max is 128MB. Leave alone if modified by operator. */
		tmp = sysctl_user_reserve_kbytes;
		if (0 < tmp && tmp < (1UL << 17))
			init_user_reserve();

		/* Default max is 8MB.  Leave alone if modified by operator. */
		tmp = sysctl_admin_reserve_kbytes;
		if (0 < tmp && tmp < (1UL << 13))
			init_admin_reserve();

		break;
	case MEM_OFFLINE:
		free_kbytes = global_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10);

		if (sysctl_user_reserve_kbytes > free_kbytes) {
			init_user_reserve();
			pr_info("vm.user_reserve_kbytes reset to %lu\n",
				sysctl_user_reserve_kbytes);
		}

		if (sysctl_admin_reserve_kbytes > free_kbytes) {
			init_admin_reserve();
			pr_info("vm.admin_reserve_kbytes reset to %lu\n",
				sysctl_admin_reserve_kbytes);
		}
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block reserve_mem_nb = {
	.notifier_call = reserve_mem_notifier,
};

static int __meminit init_reserve_notifier(void)
{
	if (register_hotmemory_notifier(&reserve_mem_nb))
		pr_err("Failed registering memory add/remove notifier for admin reserve\n");

	return 0;
}
subsys_initcall(init_reserve_notifier);
