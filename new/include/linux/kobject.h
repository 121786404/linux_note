/*
 * kobject.h - generic kernel object infrastructure.
 *
 * Copyright (c) 2002-2003 Patrick Mochel
 * Copyright (c) 2002-2003 Open Source Development Labs
 * Copyright (c) 2006-2008 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (c) 2006-2008 Novell Inc.
 *
 * This file is released under the GPLv2.
 *
 * Please read Documentation/kobject.txt before using the kobject
 * interface, ESPECIALLY the parts about reference counts and object
 * destructors.
 */

#ifndef _KOBJECT_H_
#define _KOBJECT_H_

#include <linux/types.h>
#include <linux/list.h>
#include <linux/sysfs.h>
#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/kref.h>
#include <linux/kobject_ns.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>

#define UEVENT_HELPER_PATH_LEN		256
#define UEVENT_NUM_ENVP			32	/* number of env pointers */
#define UEVENT_BUFFER_SIZE		2048	/* buffer for the variables */

#ifdef CONFIG_UEVENT_HELPER
/* path to the userspace helper executed on an event */
extern char uevent_helper[];
#endif

/* counter to tag the uevent, read only except for the kobject core */
extern u64 uevent_seqnum;

/*
 * The actions here must match the index to the string array
 * in lib/kobject_uevent.c
 *
 * Do not add new actions here without checking with the driver-core
 * maintainers. Action strings are not meant to express subsystem
 * or device specific properties. In most cases you want to send a
 * kobject_uevent_env(kobj, KOBJ_CHANGE, env) with additional event
 * specific variables added to the event environment.
 */
/*ö���ͱ���,������kset�����һЩ״̬�仯*/
enum kobject_action {
	KOBJ_ADD,	/*��������ϵͳ���һ��kset����*/
	KOBJ_REMOVE,	/*��ϵͳɾ��һ��kset����*/
	KOBJ_CHANGE,
	KOBJ_MOVE,
	KOBJ_ONLINE,
	KOBJ_OFFLINE,
	KOBJ_MAX
};

/*
ĿǰΪֹ��Kobject��Ҫ�ṩ���¹��ܣ�
1. ͨ��parentָ�룬���Խ�����Kobject�Բ�νṹ����ʽ���������
2. ʹ��һ�����ü�����reference count��������¼Kobject�����õĴ�����
    �������ô�����Ϊ0ʱ�����ͷ�
3. ��sysfs�����ļ�ϵͳ��ϣ���ÿһ��Kobject�������ԣ�
    ���ļ�����ʽ�����ŵ��û��ռ�

object���������»�Ƕ���������ݽṹ��ʹ�ã���ʹ���������£�
1. ����һ��struct kset���͵�ָ�룬���ڳ�ʼ��ʱΪ������ռ䣬
    ��ӵ��ں���
2. ����ʵ������������Լ���������ݽṹԭ�ͣ�
    �����ݽṹ�а�����Kobject
3. ����һ���ʺ��Լ���ktype����ʵ�����лص�����
4. ����Ҫʹ�õ�����Kobject�����ݽṹʱ����̬��������ݽṹ��
    ������Kobject�ռ䣬��ӵ��ں���
5. ÿһ���������ݽṹʱ������kobject_get�ӿ��������ü�����
    ���ý���ʱ������kobject_put�ӿڣ��������ü���
6. �����ü�������Ϊ0ʱ��Kobjectģ�����ktype���ṩ��release�ӿڣ�
    �ͷ��ϲ����ݽṹ�Լ�Kobject���ڴ�ռ�
*/
struct kobject {
/*
��Kobject�����ƣ�ͬʱҲ��sysfs�е�Ŀ¼���ơ�
����Kobject��ӵ�Kernelʱ����Ҫ��������ע�ᵽsysfs�У�
֮��Ͳ�����ֱ���޸ĸ��ֶΡ�
�����Ҫ�޸�Kobject�����֣���Ҫ����kobject_rename�ӿڣ�
�ýӿڻ���������sysfs���������
*/
	const char		*name;
	/*
    ���ڽ�Kobject���뵽Kset�е�list_head
	*/
	struct list_head	entry;
	/*
    ָ��parent kobject���Դ��γɲ�νṹ����sysfs�ͱ���ΪĿ¼�ṹ��
	*/
	struct kobject		*parent;
	/*
    ��kobject���ڵ�Kset������ΪNULL��
    ������ڣ���û��ָ��parent������Kset��Ϊparent
    ��������Kset��һ�������Kobject��
	*/
	struct kset		*kset;
	/*
    ��Kobject���ڵ�kobj_type��ÿ��Kobject������һ��ktype������Kernel����ʾ����
	*/
	struct kobj_type	*ktype;
	/*
    ��Kobject��sysfs�еı�ʾ
	*/
	struct kernfs_node	*sd; /* sysfs directory entry */
	/*
      һ��������ԭ�Ӳ��������ü���
	*/
	struct kref		kref;
#ifdef CONFIG_DEBUG_KOBJECT_RELEASE
	struct delayed_work	release;
#endif
/*
ָʾ��Kobject�Ƿ��Ѿ���ʼ��������Kobject��Init��Put��Add�Ȳ���ʱ�����쳣У�顣
*/
	unsigned int state_initialized:1;
/*
ָʾ��Kobject�Ƿ�����sysfs�г��֣��Ա����Զ�ע��ʱ��sysfs���Ƴ�
*/
	unsigned int state_in_sysfs:1;
/*
    Uevent�ṩ�ˡ��û��ռ�֪ͨ���Ĺ���ʵ�֣�ͨ���ù��ܣ�
    ���ں�����Kobject�����ӡ�ɾ�����޸ĵȶ���ʱ����֪ͨ�û��ռ�
    
    ��¼�Ƿ��Ѿ����û��ռ䷢��ADD uevent������У���û�з���remove uevent��
    �����Զ�ע��ʱ������REMOVE uevent���Ա����û��ռ���ȷ����
*/
	unsigned int state_add_uevent_sent:1;
	unsigned int state_remove_uevent_sent:1;

	/*�����kobject����������ĳһ��kset,��ô����״̬�仯���Ե��������ڵ�kset������
	 *�û��ռ䷢��event��Ϣ,��Աuevent_suppress������ʾ����kobject״̬�����仯ʱ,
	 *�Ƿ��������ڵ�kset���û��ռ䷢��event��Ϣ,ֵ1��ʾ����kset��������event��Ϣ*/
	unsigned int uevent_suppress:1;	
};

extern __printf(2, 3)
int kobject_set_name(struct kobject *kobj, const char *name, ...);
extern __printf(2, 0)
int kobject_set_name_vargs(struct kobject *kobj, const char *fmt,
			   va_list vargs);

static inline const char *kobject_name(const struct kobject *kobj)
{
	return kobj->name;
}

/*kobject��ʼ������,���� kobject ���ü���Ϊ 1*/
extern void kobject_init(struct kobject *kobj, struct kobj_type *ktype);
extern __printf(3, 4) __must_check
/*һ�ǽ���kobject�����Ĳ�ι�ϵ,������sysfs�ļ�ϵͳ�н���һ��Ŀ¼,��һ��kobject����ͨ��
kobject_add�������ü���ϵͳǰ,kobject��������Ѿ���ʼ��*/
int kobject_add(struct kobject *kobj, struct kobject *parent,
		const char *fmt, ...);
extern __printf(4, 5) __must_check
/*kobjectע�ắ�����ú���ֻ��kobject_init��kobject_add_varg�ļ����*/
int kobject_init_and_add(struct kobject *kobj,
			 struct kobj_type *ktype, struct kobject *parent,
			 const char *fmt, ...);

/*��linux�豸������(hierarchy)��ɾ��kobj����*/
extern void kobject_del(struct kobject *kobj);

extern struct kobject * __must_check kobject_create(void);
extern struct kobject * __must_check kobject_create_and_add(const char *name,
						struct kobject *parent);

extern int __must_check kobject_rename(struct kobject *, const char *new_name);
extern int __must_check kobject_move(struct kobject *, struct kobject *);

/*���ӻ����kobject�����ü���*/
extern struct kobject *kobject_get(struct kobject *kobj);
extern void kobject_put(struct kobject *kobj);

extern const void *kobject_namespace(struct kobject *kobj);
extern char *kobject_get_path(struct kobject *kobj, gfp_t flag);

/*
��Kobject�������ʹ�ó�����
����Ƕ�ڴ��͵����ݽṹ�У���Kset��device_driver�ȣ���
�����Щ���͵����ݽṹ��Ҳ�����Ƕ�̬���䡢��̬�ͷŵġ�

��ô�ͷŵ�ʱ����ʲô�أ�����Ƕ��Kobject�ͷ�ʱ��
����Kobject���ͷ�����Kobjectģ���Զ���ɵģ������ü���Ϊ0ʱ����
��ô��ôһ���ͷŰ����Լ��Ĵ������ݽṹ�أ� 

��ʱKtype�������ó��ˡ�����֪����
Ktype�е�release�ص����������ͷ�Kobject�������ǰ���Kobject�����ݽṹ�����ڴ�ռ䣬
��ôKtype�����ڲ�����������˭ʵ���أ�
�����ϲ����ݽṹ���ڵ�ģ�飡��Ϊֻ������
�����KobjectǶ���ĸ����ݽṹ�У�
��ͨ��Kobjectָ���Լ���������ݽṹ���ͣ�
�ҵ���Ҫ�ͷŵ��ϲ����ݽṹ��ָ�룬Ȼ���ͷ����� 

����������������ˡ����ԣ�ÿһ����ǶKobject�����ݽṹ��
����kset��device��device_driver�ȵȣ���Ҫʵ��һ��Ktype��
���������еĻص�������ͬ��sysfs��صĲ���Ҳһ����
���뾭��ktype����ת����Ϊsysfs��������Kobject�����������ļ����������壬
����ǶKobject���ϲ����ݽṹ�� 
*/
struct kobj_type {
    /* ���ûص����������Խ�������������kobject�����ݽṹ���ڴ�ռ��ͷŵ� */
	void (*release)(struct kobject *kobj);
	/* �������͵�Kobject��sysfs�ļ�ϵͳ�ӿ� */
	const struct sysfs_ops *sysfs_ops;
	/* �����͵�Kobject��atrribute�б���νattribute������sysfs�ļ�ϵͳ�е�һ���ļ�����������Kobject��ӵ��ں�ʱ��һ��ע�ᵽsysfs��*/
	struct attribute **default_attrs;
	/* ���ļ�ϵͳ��sysfs���������ռ��й�*/
	const struct kobj_ns_type_operations *(*child_ns_type)(struct kobject *kobj);
	const void *(*namespace)(struct kobject *kobj);
};

struct kobj_uevent_env {
	char *argv[3];
	char *envp[UEVENT_NUM_ENVP];
	int envp_idx;
	char buf[UEVENT_BUFFER_SIZE];
	int buflen;
};

/*���Ȳ���¼��Ŀ���,������һ�麯��ָ��,��kset�е�ĳЩkobject������״̬�仯��Ҫ
 *֪ͨ�û��ռ�ʱ,�������еĺ��������*/
struct kset_uevent_ops {
	/*һ��kset����״̬�ı仯,�������ȵ��������ڸ�kset�����uevent_ops�������е�filter
	 *����,����kset����ǰ״̬�ĸı��Ƿ�Ҫ֪ͨ���û���,���uevent_ops->filter����0,
	 *������֪ͨ�û���*/
	int (* const filter)(struct kset *kset, struct kobject *kobj);
	const char *(* const name)(struct kset *kset, struct kobject *kobj);
	int (* const uevent)(struct kset *kset, struct kobject *kobj,
		      struct kobj_uevent_env *env);
};

struct kobj_attribute {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count);
};

extern const struct sysfs_ops kobj_sysfs_ops;

struct sock;

/**
 * struct kset - a set of kobjects of a specific type, belonging to a specific subsystem.
 *
 * A kset defines a group of kobjects.  They can be individually
 * different "types" but overall these kobjects all want to be grouped
 * together and operated on in the same manner.  ksets are used to
 * define the attribute callbacks and other common events that happen to
 * a kobject.
 *
 * @list: the list of all kobjects for this kset
 * @list_lock: a lock for iterating over the kobjects
 * @kobj: the embedded kobject for this kset (recursion, isn't it fun...)
 * @uevent_ops: the set of uevent operations for this kset.  These are
 * called whenever a kobject has something happen to it so that the kset
 * can add new environment variables, or filter out the uevents if so
 * desired.
 */
/*kset����������һ��kobject�ļ���,��kobject������,kset����Ҳ��һ���ں˶���,������Ҫ��Ƕ
 *һ��kobject����*/
/*kset�����뵥����kobject����һ���ĵط�����,��һ��kset������ϵͳע��ʱ,���linux�ں�
 *����ʱ������CONFIG_HOTPLUG,��ô��Ҫ����һ�¼�֪ͨ�û��ռ�,���������kobject_uevent���,
 *���һ��kobject����������һkset,��ô���������kobject�����޷�ͨ��uevent�������û�
 *�ռ䷢��event��Ϣ*/
struct kset {
/* ���ڱ����kset�����е�kobject������ */
	struct list_head list;
	spinlock_t list_lock;	/*��kset�ϵ�list������з��ʲ���ʱ������Ϊ���Ᵽ��ʹ�õ�������*/
	/* ��kset�Լ���kobject��kset��һ�������kobject��Ҳ����sysfs����Ŀ¼����ʽ���֣� */
	struct kobject kobj;
	/*
    kset��uevent���������������κ�Kobject��Ҫ�ϱ�ueventʱ��
    ��Ҫ��������������kset��uevent_ops��
    ��ӻ������������߹���event��kset���Ծ�����Щevent�����ϱ�����
    ��ˣ����һ��kobject�������κ�ksetʱ���ǲ�������uevent��
	*/
	const struct kset_uevent_ops *uevent_ops;
};

/*������ʼ��һ��kset����*/
extern void kset_init(struct kset *kset);
/*������ʼ������ϵͳע��һ��kset����*/
extern int __must_check kset_register(struct kset *kset);
/*������kָ���kset�����ϵͳ��ע��,��ɵ���kset_register�ķ������*/
extern void kset_unregister(struct kset *kset);
/*��Ҫ�����Ƕ�̬����һkset����Ȼ������뵽sysfs�ļ�ϵͳ��,����name�Ǵ�����kset�������,
 *uevent_ops����kset���������������û��ռ�event��Ϣ�Ĳ�����,parent_kobj��kset������ϲ�
 *(����)���ں˶���ָ��*/
extern struct kset * __must_check kset_create_and_add(const char *name,
						const struct kset_uevent_ops *u,
						struct kobject *parent_kobj);

static inline struct kset *to_kset(struct kobject *kobj)
{
	return kobj ? container_of(kobj, struct kset, kobj) : NULL;
}

static inline struct kset *kset_get(struct kset *k)
{
	return k ? to_kset(kobject_get(&k->kobj)) : NULL;
}

static inline void kset_put(struct kset *k)
{
	kobject_put(&k->kobj);
}

static inline struct kobj_type *get_ktype(struct kobject *kobj)
{
	return kobj->ktype;
}

extern struct kobject *kset_find_obj(struct kset *, const char *);

/* The global /sys/kernel/ kobject for people to chain off of */
extern struct kobject *kernel_kobj;
/* The global /sys/kernel/mm/ kobject for people to chain off of */
extern struct kobject *mm_kobj;
/* The global /sys/hypervisor/ kobject for people to chain off of */
extern struct kobject *hypervisor_kobj;
/* The global /sys/power/ kobject for people to chain off of */
extern struct kobject *power_kobj;
/* The global /sys/firmware/ kobject for people to chain off of */
extern struct kobject *firmware_kobj;

int kobject_uevent(struct kobject *kobj, enum kobject_action action);
int kobject_uevent_env(struct kobject *kobj, enum kobject_action action,
			char *envp[]);

__printf(2, 3)
int add_uevent_var(struct kobj_uevent_env *env, const char *format, ...);

int kobject_action_type(const char *buf, size_t count,
			enum kobject_action *type);

#endif /* _KOBJECT_H_ */
