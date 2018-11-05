// SPDX-License-Identifier: GPL-2.0
/*
 * kobject.h - generic kernel object infrastructure.
 *
 * Copyright (c) 2002-2003 Patrick Mochel
 * Copyright (c) 2002-2003 Open Source Development Labs
 * Copyright (c) 2006-2008 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (c) 2006-2008 Novell Inc.
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
#include <linux/uidgid.h>

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
/*枚举型变量,定义了kset对象的一些状态变化*/
enum kobject_action {
	KOBJ_ADD,	/*表明将向系统添加一个kset对象*/
	KOBJ_REMOVE,	/*从系统删除一个kset对象*/
	KOBJ_CHANGE,
	KOBJ_MOVE,
	KOBJ_ONLINE,
	KOBJ_OFFLINE,
	KOBJ_BIND,
	KOBJ_UNBIND,
	KOBJ_MAX
};

/*
目前为止，Kobject主要提供如下功能：
1. 通过parent指针，可以将所有Kobject以层次结构的形式组合起来。
2. 使用一个引用计数（reference count），来记录Kobject被引用的次数，
    并在引用次数变为0时把它释放
3. 和sysfs虚拟文件系统配合，将每一个Kobject及其特性，
    以文件的形式，开放到用户空间

object大多数情况下会嵌在其它数据结构中使用，其使用流程如下：
1. 定义一个struct kset类型的指针，并在初始化时为它分配空间，
    添加到内核中
2. 根据实际情况，定义自己所需的数据结构原型，
    该数据结构中包含有Kobject
3. 定义一个适合自己的ktype，并实现其中回调函数
4. 在需要使用到包含Kobject的数据结构时，动态分配该数据结构，
    并分配Kobject空间，添加到内核中
5. 每一次引用数据结构时，调用kobject_get接口增加引用计数；
    引用结束时，调用kobject_put接口，减少引用计数
6. 当引用计数减少为0时，Kobject模块调用ktype所提供的release接口，
    释放上层数据结构以及Kobject的内存空间
*/
struct kobject {
/*
该Kobject的名称，同时也是sysfs中的目录名称。
由于Kobject添加到Kernel时，需要根据名字注册到sysfs中，
之后就不能再直接修改该字段。
如果需要修改Kobject的名字，需要调用kobject_rename接口，
该接口会主动处理sysfs的相关事宜
*/
	const char		*name;
	/*
    用于将Kobject加入到Kset中的list_head
	*/
	struct list_head	entry;
	/*
    指向parent kobject，以此形成层次结构（在sysfs就表现为目录结构）
	*/
	struct kobject		*parent;
	/*
    该kobject属于的Kset。可以为NULL。
    如果存在，且没有指定parent，则会把Kset作为parent
    （别忘了Kset是一个特殊的Kobject）
	*/
	struct kset		*kset;
	/*
    该Kobject属于的kobj_type。每个Kobject必须有一个ktype，或者Kernel会提示错误
	*/
	struct kobj_type	*ktype;
	/*
    该Kobject在sysfs中的表示
	*/
	struct kernfs_node	*sd; /* sysfs directory entry */
	/*
      一个可用于原子操作的引用计数
	*/
	struct kref		kref;
#ifdef CONFIG_DEBUG_KOBJECT_RELEASE
	struct delayed_work	release;
#endif
/*
指示该Kobject是否已经初始化，以在Kobject的Init，Put，Add等操作时进行异常校验。
*/
	unsigned int state_initialized:1;
/*
指示该Kobject是否已在sysfs中呈现，以便在自动注销时从sysfs中移除
*/
	unsigned int state_in_sysfs:1;
/*
    Uevent提供了“用户空间通知”的功能实现，通过该功能，
    当内核中有Kobject的增加、删除、修改等动作时，会通知用户空间
    
    记录是否已经向用户空间发送ADD uevent，如果有，且没有发送remove uevent，
    则在自动注销时，补发REMOVE uevent，以便让用户空间正确处理
*/
	unsigned int state_add_uevent_sent:1;
	unsigned int state_remove_uevent_sent:1;

	/*如果该kobject对象隶属于某一个kset,那么它的状态变化可以导致其所在的kset对象向
	 *用户空间发送event消息,成员uevent_suppress用来表示当该kobject状态发生变化时,
	 *是否让其所在的kset向用户空间发送event消息,值1表示不让kset发送这种event消息*/
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

/*kobject初始化函数,设置 kobject 引用计数为 1*/
extern void kobject_init(struct kobject *kobj, struct kobj_type *ktype);
extern __printf(3, 4) __must_check
/*一是建立kobject对象间的层次关系,二是在sysfs文件系统中建立一个目录,将一个kobject对象通过
kobject_add函数调用加入系统前,kobject对象必须已经初始化*/
int kobject_add(struct kobject *kobj, struct kobject *parent,
		const char *fmt, ...);
extern __printf(4, 5) __must_check
/*kobject注册函数，该函数只是kobject_init和kobject_add_varg的简单组合*/
int kobject_init_and_add(struct kobject *kobj,
			 struct kobj_type *ktype, struct kobject *parent,
			 const char *fmt, ...);

/*从linux设备曾测中(hierarchy)中删除kobj对象*/
extern void kobject_del(struct kobject *kobj);

extern struct kobject * __must_check kobject_create(void);
extern struct kobject * __must_check kobject_create_and_add(const char *name,
						struct kobject *parent);

extern int __must_check kobject_rename(struct kobject *, const char *new_name);
extern int __must_check kobject_move(struct kobject *, struct kobject *);

/*增加或减少kobject的引用计数*/
extern struct kobject *kobject_get(struct kobject *kobj);
extern struct kobject * __must_check kobject_get_unless_zero(
						struct kobject *kobj);
extern void kobject_put(struct kobject *kobj);

extern const void *kobject_namespace(struct kobject *kobj);
extern void kobject_get_ownership(struct kobject *kobj,
				  kuid_t *uid, kgid_t *gid);
extern char *kobject_get_path(struct kobject *kobj, gfp_t flag);

/**
 * kobject_has_children - Returns whether a kobject has children.
 * @kobj: the object to test
 *
 * This will return whether a kobject has other kobjects as children.
 *
 * It does NOT account for the presence of attribute files, only sub
 * directories. It also assumes there is no concurrent addition or
 * removal of such children, and thus relies on external locking.
 */
static inline bool kobject_has_children(struct kobject *kobj)
{
	WARN_ON_ONCE(kref_read(&kobj->kref) == 0);

	return kobj->sd && kobj->sd->dir.subdirs;
}

/*
而Kobject大多数的使用场景，
是内嵌在大型的数据结构中（如Kset、device_driver等），
因此这些大型的数据结构，也必须是动态分配、动态释放的。

那么释放的时机是什么呢？是内嵌的Kobject释放时。
但是Kobject的释放是由Kobject模块自动完成的（在引用计数为0时），
那么怎么一并释放包含自己的大型数据结构呢？ 

这时Ktype就派上用场了。我们知道，
Ktype中的release回调函数负责释放Kobject（甚至是包含Kobject的数据结构）的内存空间，
那么Ktype及其内部函数，是由谁实现呢？
是由上层数据结构所在的模块！因为只有它，
才清楚Kobject嵌在哪个数据结构中，
并通过Kobject指针以及自身的数据结构类型，
找到需要释放的上层数据结构的指针，然后释放它。 

讲到这里，就清晰多了。所以，每一个内嵌Kobject的数据结构，
例如kset、device、device_driver等等，都要实现一个Ktype，
并定义其中的回调函数。同理，sysfs相关的操作也一样，
必须经过ktype的中转，因为sysfs看到的是Kobject，而真正的文件操作的主体，
是内嵌Kobject的上层数据结构！ 
*/
struct kobj_type {
    /* 过该回调函数，可以将包含该种类型kobject的数据结构的内存空间释放掉 */
	void (*release)(struct kobject *kobj);
	/* 该种类型的Kobject的sysfs文件系统接口 */
	const struct sysfs_ops *sysfs_ops;
	/* 种类型的Kobject的atrribute列表（所谓attribute，就是sysfs文件系统中的一个文件）。将会在Kobject添加到内核时，一并注册到sysfs中*/
	struct attribute **default_attrs;
	/* 和文件系统（sysfs）的命名空间有关*/
	const struct kobj_ns_type_operations *(*child_ns_type)(struct kobject *kobj);
	const void *(*namespace)(struct kobject *kobj);
	void (*get_ownership)(struct kobject *kobj, kuid_t *uid, kgid_t *gid);
};

struct kobj_uevent_env {
	char *argv[3];
	char *envp[UEVENT_NUM_ENVP];
	int envp_idx;
	char buf[UEVENT_BUFFER_SIZE];
	int buflen;
};

/*对热插拔事件的控制,定义了一组函数指针,当kset中的某些kobject对象发生状态变化需要
 *通知用户空间时,调用其中的函数来完成*/
struct kset_uevent_ops {
	/*一个kset对象状态的变化,将会首先调用隶属于该kset对象的uevent_ops操作集中的filter
	 *函数,决定kset对象当前状态的改变是否要通知到用户层,如果uevent_ops->filter返回0,
	 *将不再通知用户层*/
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
/*kset可以任务是一组kobject的集合,是kobject的容器,kset本身也是一个内核对象,所以需要内嵌
 *一个kobject对象*/
/*kset对象与单个的kobject对象不一样的地方在于,将一个kset对象向系统注册时,如果linux内核
 *编译时启用了CONFIG_HOTPLUG,那么需要将这一事件通知用户空间,这个过程有kobject_uevent完成,
 *如果一个kobject对象不属于任一kset,那么这个孤立的kobject对象将无法通过uevent机制向用户
 *空间发送event消息*/
struct kset {
/* 用于保存该kset下所有的kobject的链表 */
	struct list_head list;
	spinlock_t list_lock;	/*对kset上的list链表进行访问操作时用来作为互斥保护使用的自旋锁*/
	/* 该kset自己的kobject（kset是一个特殊的kobject，也会在sysfs中以目录的形式体现） */
	struct kobject kobj;
	/*
    kset的uevent操作函数集。当任何Kobject需要上报uevent时，
    都要调用它所从属的kset的uevent_ops，
    添加环境变量，或者过滤event（kset可以决定哪些event可以上报）。
    因此，如果一个kobject不属于任何kset时，是不允许发送uevent的
	*/
	const struct kset_uevent_ops *uevent_ops;
} __randomize_layout;
/*用来初始化一个kset对象*/
extern void kset_init(struct kset *kset);
/*用来初始化并向系统注册一个kset对象*/
extern int __must_check kset_register(struct kset *kset);
/*用来将k指向的kset对象从系统中注销,完成的是kset_register的反向操作*/
extern void kset_unregister(struct kset *kset);
/*主要作用是动态产生一kset对象然后将其加入到sysfs文件系统中,参数name是创建的kset对象的名,
 *uevent_ops是新kset对象上用来处理用户空间event消息的操作集,parent_kobj是kset对象的上层
 *(父级)的内核对象指针*/
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
int kobject_synth_uevent(struct kobject *kobj, const char *buf, size_t count);

__printf(2, 3)
int add_uevent_var(struct kobj_uevent_env *env, const char *format, ...);

#endif /* _KOBJECT_H_ */
