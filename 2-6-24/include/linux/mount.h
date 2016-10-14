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

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>

struct super_block;
struct vfsmount;
struct dentry;
struct mnt_namespace;

/**
 * ��ֹsetuid
 */
#define MNT_NOSUID	0x01
/**
 * �����ļ�ϵͳ��û���������豸������
 */
#define MNT_NODEV	0x02
/**
 * ������ִ��
 */
#define MNT_NOEXEC	0x04
/**
 * ����¼����ʱ����޸�ʱ��
 */
#define MNT_NOATIME	0x08
#define MNT_NODIRATIME	0x10
#define MNT_RELATIME	0x20

/**
 * ����NFS��AFS
 */
#define MNT_SHRINKABLE	0x100

/* ����Ͳ��ɰ�װ�� */
#define MNT_SHARED	0x1000	/* if the vfsmount is a shared mount */
#define MNT_UNBINDABLE	0x2000	/* if the vfsmount is a unbindable mount */
#define MNT_PNODE_MASK	0x3000	/* propagation flag mask */

/**
 * �ļ�ϵͳװ�ص�
 */
struct vfsmount {
	/* ���ڽ�װ�ص���뵽ɢ�б��� */
	struct list_head mnt_hash;
	/* ���ļ�ϵͳ��װ�ص� */
	struct vfsmount *mnt_parent;	/* fs we are mounted on */
	/* װ�ص��ڸ�Ŀ¼�е�dentry�ṹ */
	struct dentry *mnt_mountpoint;	/* dentry of mountpoint */
	/* װ�ص����ڵĸ�Ŀ¼���� */
	struct dentry *mnt_root;	/* root of the mounted tree */
	/* װ�ص�ĳ����� */
	struct super_block *mnt_sb;	/* pointer to superblock */
	/* ����ͷ����ʾ����װ�����ļ�ϵͳ�ڵ��ļ�ϵͳ */
	struct list_head mnt_mounts;	/* list of children, anchored here */
	/* ͨ�����ֶΣ����ļ�ϵͳװ�ص���ӵ����ļ�ϵͳ��mnt_mounts������ */
	struct list_head mnt_child;	/* and going through their mnt_child */
	/* ���ر�־����MNT_NOSUID */
	int mnt_flags;
	/* 4 bytes hole on 64bits arches */
	char *mnt_devname;		/* Name of device e.g. /dev/dsk/hda1 */
	/* ͨ�����ֶ���ӵ������ռ�ļ��ص������� */
	struct list_head mnt_list;
	/* �Զ����ڵ�װ������Ԫ�� */
	struct list_head mnt_expire;	/* link in fs-specific expiry list */
	/* ����װ������Ԫ�� */
	struct list_head mnt_share;	/* circular list of shared mounts */
	/* ����װ�ء���װ��ʹ�õ������ָ�� */
	struct list_head mnt_slave_list;/* list of slave mounts */
	struct list_head mnt_slave;	/* slave list entry */
	struct vfsmount *mnt_master;	/* slave is on master->mnt_slave_list */
	/* �����������ռ� */
	struct mnt_namespace *mnt_ns;	/* containing namespace */
	/*
	 * We put mnt_count & mnt_expiry_mark at the end of struct vfsmount
	 * to let these frequently modified fields in a separate cache line
	 * (so that reads of mnt_flags wont ping-pong on SMP machines)
	 */
	/* ��������ʹ��mntput��mntgetά�� */
	atomic_t mnt_count;
	/* �Ƿ���װ�ع��� */
	int mnt_expiry_mark;		/* true if marked for expiry */
	int mnt_pinned;
};

static inline struct vfsmount *mntget(struct vfsmount *mnt)
{
	if (mnt)
		atomic_inc(&mnt->mnt_count);
	return mnt;
}

extern void mntput_no_expire(struct vfsmount *mnt);
extern void mnt_pin(struct vfsmount *mnt);
extern void mnt_unpin(struct vfsmount *mnt);

static inline void mntput(struct vfsmount *mnt)
{
	if (mnt) {
		mnt->mnt_expiry_mark = 0;
		mntput_no_expire(mnt);
	}
}

extern void free_vfsmnt(struct vfsmount *mnt);
extern struct vfsmount *alloc_vfsmnt(const char *name);
extern struct vfsmount *do_kern_mount(const char *fstype, int flags,
				      const char *name, void *data);

struct file_system_type;
extern struct vfsmount *vfs_kern_mount(struct file_system_type *type,
				      int flags, const char *name,
				      void *data);

struct nameidata;

extern int do_add_mount(struct vfsmount *newmnt, struct nameidata *nd,
			int mnt_flags, struct list_head *fslist);

extern void mark_mounts_for_expiry(struct list_head *mounts);
extern void shrink_submounts(struct vfsmount *mountpoint, struct list_head *mounts);

extern spinlock_t vfsmount_lock;
extern dev_t name_to_dev_t(char *name);

#endif
#endif /* _LINUX_MOUNT_H */
