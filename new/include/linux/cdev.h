#ifndef _LINUX_CDEV_H
#define _LINUX_CDEV_H

/*linux��ÿ���ַ��豸�������һ��cdev�ṹ�ı���*/
#include <linux/kobject.h>
#include <linux/kdev_t.h>
#include <linux/list.h>

struct file_operations;
struct inode;
struct module;

/*�ַ��豸�ĳ��󣬽���Ϊ��������ַ��豸���������ܽṹ��Ƶ���Ҫ����ʵ��һ�������ַ�Ӳ���豸�����ݽṹ�ĳ�������Ҫ���ӵĶ࣬struct cdev������Ϊһ����Ƕ�ĳ�Ա����������ʵ���豸�����ݽṹ��  �磺
struct my_keypad_dev{
	int a;
	int b;
	int c;
	...
	//��Ƕ��stuct cdev���ݽṹ
	struct cdev cdev;
}*/
struct cdev {
	struct kobject kobj;	//ÿ��cdev����һ��kobject
	struct module *owner;	//�ַ��豸�����������ڵ��ں�ģ�����ָ��
	const struct file_operations *ops; //��������ַ��豸�ļ��ķ���
	struct list_head list;	/* ��cdev��Ӧ���ַ��豸�ļ���inode-i_devices��
				 * ����ͷ
				 * ������ϵͳ�е��ַ��豸�γ�����*/
	dev_t dev;		//�ַ��豸���豸�ţ������豸�źʹ��豸�Ź���
	unsigned int count;	/* ������ͬһ���豸�ŵĴ��豸�ĸ�����
				 * ���ڱ�ʾ�ɵ�ǰ�豸����������Ƶ�ʵ��ͬ���豸������*/
};

/*
	һ�� cdev һ���������ֶ����ʼ����ʽ����̬�ĺͶ�̬�ġ�
	��̬�ڴ涨���ʼ����
	struct cdev my_cdev;
	cdev_init(&my_cdev, &fops);
	my_cdev.owner = THIS_MODULE;
	��̬�ڴ涨���ʼ����
	struct cdev *my_cdev = cdev_alloc();
	my_cdev->ops = &fops;
	my_cdev->owner = THIS_MODULE;
	or
	struct *p = kzalloc(sizeof(struct cdev), GFP_KERNEL)
*/
void cdev_init(struct cdev *, const struct file_operations *);

struct cdev *cdev_alloc(void);

void cdev_put(struct cdev *p);

/*
��ʼ�� cdev ����Ҫ������ӵ�ϵͳ��ȥ��Ϊ�˿��Ե��� cdev_add() ���������� cdev �ṹ��ָ�룬��ʼ�豸��ţ��Լ��豸��ŷ�Χ��������ϵͳ���һ��cdev������ַ��豸��ע��*/
int cdev_add(struct cdev *, dev_t, unsigned);

/*
��һ���ַ��豸����������Ҫ��ʱ�򣨱���ģ��ж�أ����Ϳ����� cdev_del() �������ͷ� cdev ռ�õ��ڴ�*/
void cdev_del(struct cdev *);

void cd_forget(struct inode *);

#endif
