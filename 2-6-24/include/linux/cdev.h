#ifndef _LINUX_CDEV_H
#define _LINUX_CDEV_H
#ifdef __KERNEL__

#include <linux/kobject.h>
#include <linux/kdev_t.h>
#include <linux/list.h>

struct file_operations;
struct inode;
struct module;

/* �ַ��豸���� */
struct cdev {
	/* ͨ���˽ṹ���豸��ӵ�ͨ���豸�ļ�ϵͳ(/sys/)�� */
	struct kobject kobj;
	/* ����ģ�� */
	struct module *owner;
	/* �ļ������ص� */
	const struct file_operations *ops;
	/* �����������б�ʾ���豸��inode */
	struct list_head list;
	/* �豸�� */
	dev_t dev;
	unsigned int count;
};

void cdev_init(struct cdev *, const struct file_operations *);

struct cdev *cdev_alloc(void);

void cdev_put(struct cdev *p);

int cdev_add(struct cdev *, dev_t, unsigned);

void cdev_del(struct cdev *);

void cd_forget(struct inode *);

extern struct backing_dev_info directly_mappable_cdev_bdi;

#endif
#endif
