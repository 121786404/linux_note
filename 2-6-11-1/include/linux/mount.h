/*
 *
 * Definitions for mount interface. This describes the in the kernel build 
 * linkedlist with mounted filesystems.
 *
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 *
 * Version: $Id: mount.h,v 2.0 1996/11/17 16:48:14 mvw Exp mvw $
 *
 */
#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H
#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

#define MNT_NOSUID	1
#define MNT_NODEV	2
#define MNT_NOEXEC	4


/* �ļ�ϵͳ���ص�ṹ */
struct vfsmount
{
        /* �γ�ȫ�ֵ�hash���� */
	struct list_head mnt_hash;
        /* ָ���ļ�ϵͳ������ļ�ϵͳ��װ���� */
	struct vfsmount *mnt_parent;	/* fs we are mounted on */
        /* ���ص��ļ�ϵͳ��Ŀ¼������ļ�ϵͳ���ڵ�ĳ��·���ϣ����Ӧ�������·����dentry */
	struct dentry *mnt_mountpoint;	/* dentry of mountpoint */
        /* ָ������ļ�ϵͳ�ĸ�Ŀ¼��/' ����ʱ������'/'���Ŀ¼���ر���MS_BIND��ʱ�� */
	struct dentry *mnt_root;	/* root of the mounted tree */
        /* ָ���ļ�ϵͳ�ĳ����� */
	struct super_block *mnt_sb;	/* pointer to superblock */
        /* ���й����ڸ��ļ�ϵͳ�ϵ�vfsmount�����ӹ��ص��mnt_child�γ�˫������ */
	struct list_head mnt_mounts;	/* list of children, anchored here */
	struct list_head mnt_child;	/* and going through their mnt_child */
	atomic_t mnt_count;             /* ���ü��� */
	int mnt_flags;
	int mnt_expiry_mark;		/* true if marked for expiry */
        /* �豸���ƣ���ʵҲ����·���ɣ�Ҳ�����������ļ�ϵͳ��rootfs��proc�� */
	char *mnt_devname;		/* Name of device e.g. /dev/dsk/hda1 */
	struct list_head mnt_list;
	struct list_head mnt_fslink;	/* link in fs-specific expiry list */
        /* ���ص��Ӧ�����ƿռ� */
	struct namespace *mnt_namespace; /* containing namespace */
};

/* ���ӹ��ص�����ü��� */
static inline struct vfsmount *mntget(struct vfsmount *mnt)
{
	if (mnt)
		atomic_inc(&mnt->mnt_count);
	return mnt;
}

extern void __mntput(struct vfsmount *mnt);

static inline void _mntput(struct vfsmount *mnt)
{
	if (mnt) {
		if (atomic_dec_and_test(&mnt->mnt_count))
			__mntput(mnt);
	}
}

static inline void mntput(struct vfsmount *mnt)
{
	if (mnt) {
		mnt->mnt_expiry_mark = 0;
		_mntput(mnt);
	}
}

extern void free_vfsmnt(struct vfsmount *mnt);
extern struct vfsmount *alloc_vfsmnt(const char *name);
extern struct vfsmount *do_kern_mount(const char *fstype, int flags,
				      const char *name, void *data);

struct nameidata;

extern int do_add_mount(struct vfsmount *newmnt, struct nameidata *nd,
			int mnt_flags, struct list_head *fslist);

extern void mark_mounts_for_expiry(struct list_head *mounts);

extern spinlock_t vfsmount_lock;

#endif
#endif /* _LINUX_MOUNT_H */
