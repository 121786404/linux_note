/*
 *  linux/arch/i386/mm/mmap.c
 *
 *  flexible mmap layout support
 *
 * Copyright 2003-2004 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * Started by Ingo Molnar <mingo@elte.hu>
 */

#include <linux/personality.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/sched.h>

/*
 * Top of mmap area (just below the process stack).
 *
 * Leave an at least ~128 MB hole.
 */
#define MIN_GAP (128*1024*1024)
#define MAX_GAP (TASK_SIZE/6*5)

static inline unsigned long mmap_base(struct mm_struct *mm)
{
	unsigned long gap = current->signal->rlim[RLIMIT_STACK].rlim_cur;
	unsigned long random_factor = 0;

	if (current->flags & PF_RANDOMIZE)/* �����˵�ַ�ռ���������� */
		/* �õ�������� */
		random_factor = get_random_int() % (1024*1024);

	/* ȷ����ջ����ռ��128M */
	if (gap < MIN_GAP)
		gap = MIN_GAP;
	else if (gap > MAX_GAP)/* ������ռ��1/6�������ַ�ռ� */
		gap = MAX_GAP;

	return PAGE_ALIGN(TASK_SIZE - gap - random_factor);
}

/*
 * This function, called very early during the creation of a new
 * process VM image, sets up which VM layout function to use:
 */
/**
 * �ڴ�������ʱ��ȷ�����Ϊ���������ַ�ռ���в��֡�
 */
void arch_pick_mmap_layout(struct mm_struct *mm)
{
	/*
	 * Fall back to the standard layout if the personality
	 * bit is set, or if the expected stack growth is unlimited:
	 */
	if (sysctl_legacy_va_layout ||/* ǿ��ʹ�þɵĲ��� */
			(current->personality & ADDR_COMPAT_LAYOUT) ||/* ��Ҫ���ݾɵĶ������ļ� */
			current->signal->rlim[RLIMIT_STACK].rlim_cur == RLIM_INFINITY) {/* ��ջ�ռ䲻�����ƣ��޷�ʹ���µĲ��� */
		/* ���䲼���У�mmap�ռ俪ʼ���û�̬�ռ��1/3�� */
		mm->mmap_base = TASK_UNMAPPED_BASE;
		/* x86�ܹ��£�ʹ��Ĭ�ϵ�get_unmapped_area���� */
		mm->get_unmapped_area = arch_get_unmapped_area;
		mm->unmap_area = arch_unmap_area;
	} else {
		/* �²����£�����ַ������ջ��ַ */
		mm->mmap_base = mmap_base(mm);
		/* mmap����Ӷ���͵�ַ��չ */
		mm->get_unmapped_area = arch_get_unmapped_area_topdown;
		mm->unmap_area = arch_unmap_area_topdown;
	}
}
