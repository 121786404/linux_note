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


/* 文件系统挂载点结构 */
struct vfsmount
{
        /* 形成全局的hash链表 */
	struct list_head mnt_hash;
        /* 指向父文件系统，这个文件系统安装其上 */
	struct vfsmount *mnt_parent;	/* fs we are mounted on */
        /* 挂载点文件系统的目录，如把文件系统挂在到某个路径上，则对应的是这个路径的dentry */
	struct dentry *mnt_mountpoint;	/* dentry of mountpoint */
        /* 指向这个文件系统的根目录‘/' ，有时并不是'/'这个目录，特别是MS_BIND的时候 */
	struct dentry *mnt_root;	/* root of the mounted tree */
        /* 指向文件系统的超级块 */
	struct super_block *mnt_sb;	/* pointer to superblock */
        /* 所有挂载在该文件系统上的vfsmount，与子挂载点的mnt_child形成双向链表 */
	struct list_head mnt_mounts;	/* list of children, anchored here */
	struct list_head mnt_child;	/* and going through their mnt_child */
	atomic_t mnt_count;             /* 引用计数 */
	int mnt_flags;
	int mnt_expiry_mark;		/* true if marked for expiry */
        /* 设备名称，其实也就是路径吧，也可能是特殊文件系统的rootfs，proc等 */
	char *mnt_devname;		/* Name of device e.g. /dev/dsk/hda1 */
	struct list_head mnt_list;
	struct list_head mnt_fslink;	/* link in fs-specific expiry list */
        /* 挂载点对应的名称空间 */
	struct namespace *mnt_namespace; /* containing namespace */
};

/* 增加挂载点的引用计数 */
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
