/*
 * kobject.c - library routines for handling generic kernel objects
 *
 * Copyright (c) 2002-2003 Patrick Mochel <mochel@osdl.org>
 *
 * This file is released under the GPLv2.
 *
 *
 * Please see the file Documentation/kobject.txt for critical information
 * about using the kobject interface.
 */

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/stat.h>

/**
 *	populate_dir - populate directory with attributes.
 *	@kobj:	object we're working on.
 *
 *	Most subsystems have a set of default attributes that 
 *	are associated with an object that registers with them.
 *	This is a helper called during object registration that 
 *	loops through the default attributes of the subsystem 
 *	and creates attributes files for them in sysfs.
 *
 */

static int populate_dir(struct kobject * kobj)
{
	struct kobj_type * t = get_ktype(kobj);
	struct attribute * attr;
	int error = 0;
	int i;
	
	if (t && t->default_attrs) {
		for (i = 0; (attr = t->default_attrs[i]) != NULL; i++) {
			if ((error = sysfs_create_file(kobj,attr)))
				break;
		}
	}
	return error;
}

/* 根据内核对象来创建一个sysfs目录 */
static int create_dir(struct kobject * kobj)
{
	int error = 0;
	if (kobject_name(kobj)) {
		error = sysfs_create_dir(kobj);
		if (!error) {
			if ((error = populate_dir(kobj)))
				sysfs_remove_dir(kobj);
		}
	}
	return error;
}

static inline struct kobject * to_kobj(struct list_head * entry)
{
	return container_of(entry,struct kobject,entry);
}

static int get_kobj_path_length(struct kobject *kobj)
{
	int length = 1;
	struct kobject * parent = kobj;

	/* walk up the ancestors until we hit the one pointing to the 
	 * root.
	 * Add 1 to strlen for leading '/' of each level.
	 */
	do {
		length += strlen(kobject_name(parent)) + 1;
		parent = parent->parent;
	} while (parent);
	return length;
}

static void fill_kobj_path(struct kobject *kobj, char *path, int length)
{
	struct kobject * parent;

	--length;
	for (parent = kobj; parent; parent = parent->parent) {
		int cur = strlen(kobject_name(parent));
		/* back up enough to print this name with '/' */
		length -= cur;
		strncpy (path + length, kobject_name(parent), cur);
		*(path + --length) = '/';
	}

	pr_debug("%s: path = '%s'\n",__FUNCTION__,path);
}

/**
 * kobject_get_path - generate and return the path associated with a given kobj
 * and kset pair.  The result must be freed by the caller with kfree().
 *
 * @kobj:	kobject in question, with which to build the path
 * @gfp_mask:	the allocation type used to allocate the path
 */
char *kobject_get_path(struct kobject *kobj, int gfp_mask)
{
	char *path;
	int len;

	len = get_kobj_path_length(kobj);
	path = kmalloc(len, gfp_mask);
	if (!path)
		return NULL;
	memset(path, 0x00, len);
	fill_kobj_path(kobj, path, len);

	return path;
}

/**
 *	kobject_init - initialize object.
 *	@kobj:	object in question.
 */
/* 内核对象初始化 */
void kobject_init(struct kobject * kobj)
{
        /* 设置引用计数为1 */
	kref_init(&kobj->kref);
        /* 不接入任何双向链表 */
	INIT_LIST_HEAD(&kobj->entry);
        /* 增加对应kset中内嵌对象的引用计数，
          * 如果kset为NULL则不做任何处理
          */
	kobj->kset = kset_get(kobj->kset);
}


/**
 *	unlink - remove kobject from kset list.
 *	@kobj:	kobject.
 *
 *	Remove the kobject from the kset list and decrement
 *	its parent's refcount.
 *	This is separated out, so we can use it in both 
 *	kobject_del() and kobject_add() on error.
 */

static void unlink(struct kobject * kobj)
{
	if (kobj->kset) {
		down_write(&kobj->kset->subsys->rwsem);
		list_del_init(&kobj->entry);
		up_write(&kobj->kset->subsys->rwsem);
	}
	kobject_put(kobj);
}

/**
 *	kobject_add - add an object to the hierarchy.
 *	@kobj:	object.
 */

/* 添加内核对象，同时也在sysfs文件系统中给内核对象创建了对应的目录  */
int kobject_add(struct kobject * kobj)
{
	int error = 0;
	struct kobject * parent;

	if (!(kobj = kobject_get(kobj)))
		return -ENOENT;
	if (!kobj->k_name)
		kobj->k_name = kobj->name;
	/* 增加父kobject的引用计数 */
	parent = kobject_get(kobj->parent);

	pr_debug("kobject %s: registering. parent: %s, set: %s\n",
		 kobject_name(kobj), parent ? kobject_name(parent) : "<NULL>", 
		 kobj->kset ? kobj->kset->kobj.name : "<NULL>" );

	if (kobj->kset) {
		down_write(&kobj->kset->subsys->rwsem);

		/* 如果父为空，则获取对应kset的kobj */
		if (!parent)
			parent = kobject_get(&kobj->kset->kobj);

		list_add_tail(&kobj->entry,&kobj->kset->list);
		up_write(&kobj->kset->subsys->rwsem);
	}
        /* 重新设置kobject的parent属性 */
	kobj->parent = parent;

        /* 在sysfs中创建目录 */
	error = create_dir(kobj);
	if (error) {
		/* unlink does the kobject_put() for us */
		unlink(kobj);
		if (parent)
			kobject_put(parent);
	} else {
		kobject_hotplug(kobj, KOBJ_ADD);
	}

	return error;
}


/**
 *	kobject_register - initialize and add an object.
 *	@kobj:	object in question.
 */

/* 注册内核对象，也就是将kobject添加到kset当中 */
int kobject_register(struct kobject * kobj)
{
	int error = 0;
	if (kobj) {
		kobject_init(kobj);
		error = kobject_add(kobj);
		if (error) {
			printk("kobject_register failed for %s (%d)\n",
			       kobject_name(kobj),error);
			dump_stack();
		}
	} else
		error = -EINVAL;
	return error;
}


/**
 *	kobject_set_name - Set the name of an object
 *	@kobj:	object.
 *	@name:	name. 
 *
 *	If strlen(name) >= KOBJ_NAME_LEN, then use a dynamically allocated
 *	string that @kobj->k_name points to. Otherwise, use the static 
 *	@kobj->name array.
 */

/* 设置内核模块的名称 */
int kobject_set_name(struct kobject * kobj, const char * fmt, ...)
{
	int error = 0;
	int limit = KOBJ_NAME_LEN;
	int need;
	va_list args;
	char * name;

	/* 
	 * First, try the static array 
	 */
	va_start(args,fmt);
	need = vsnprintf(kobj->name,limit,fmt,args);
	va_end(args);
	if (need < limit) 
		name = kobj->name;
	else {
		/* 
		 * Need more space? Allocate it and try again 
		 */
		limit = need + 1;
		name = kmalloc(limit,GFP_KERNEL);
		if (!name) {
			error = -ENOMEM;
			goto Done;
		}
		va_start(args,fmt);
		need = vsnprintf(name,limit,fmt,args);
		va_end(args);

		/* Still? Give up. */
		if (need >= limit) {
			kfree(name);
			error = -EFAULT;
			goto Done;
		}
	}

	/* Free the old name, if necessary. */
	/* 释放原来老的名称 */
	if (kobj->k_name && kobj->k_name != kobj->name)
		kfree(kobj->k_name);

	/* Now, set the new name */
	kobj->k_name = name;
 Done:
	return error;
}

EXPORT_SYMBOL(kobject_set_name);


/**
 *	kobject_rename - change the name of an object
 *	@kobj:	object in question.
 *	@new_name: object's new name
 */

int kobject_rename(struct kobject * kobj, char *new_name)
{
	int error = 0;

	kobj = kobject_get(kobj);
	if (!kobj)
		return -EINVAL;
	error = sysfs_rename_dir(kobj, new_name);
	kobject_put(kobj);

	return error;
}

/**
 *	kobject_del - unlink kobject from hierarchy.
 * 	@kobj:	object.
 */

void kobject_del(struct kobject * kobj)
{
	kobject_hotplug(kobj, KOBJ_REMOVE);
	sysfs_remove_dir(kobj);
	unlink(kobj);
}

/**
 *	kobject_unregister - remove object from hierarchy and decrement refcount.
 *	@kobj:	object going away.
 */

void kobject_unregister(struct kobject * kobj)
{
	pr_debug("kobject %s: unregistering\n",kobject_name(kobj));
	kobject_del(kobj);
	kobject_put(kobj);
}

/**
 *	kobject_get - increment refcount for object.
 *	@kobj:	object.
 */

/* 增加内核对象的引用计数 */
struct kobject * kobject_get(struct kobject * kobj)
{
	if (kobj)
		kref_get(&kobj->kref);
	return kobj;
}

/**
 *	kobject_cleanup - free kobject resources. 
 *	@kobj:	object.
 */

void kobject_cleanup(struct kobject * kobj)
{
	struct kobj_type * t = get_ktype(kobj);
	struct kset * s = kobj->kset;
	struct kobject * parent = kobj->parent;

	pr_debug("kobject %s: cleaning up\n",kobject_name(kobj));
	if (kobj->k_name != kobj->name)
		kfree(kobj->k_name);
	kobj->k_name = NULL;
	if (t && t->release)
		t->release(kobj);
	/* 释放set当中的引用计数 */
	if (s)
		kset_put(s);
	/* 释放父对象的引用计数 */
	if (parent) 
		kobject_put(parent);
}

/* 如果内核对象的引用计数为0，则需要释放该内核对象 */
static void kobject_release(struct kref *kref)
{
	kobject_cleanup(container_of(kref, struct kobject, kref));
}

/**
 *	kobject_put - decrement refcount for object.
 *	@kobj:	object.
 *
 *	Decrement the refcount, and if 0, call kobject_cleanup().
 */
void kobject_put(struct kobject * kobj)
{
	if (kobj)
		kref_put(&kobj->kref, kobject_release);
}


/**
 *	kset_init - initialize a kset for use
 *	@k:	kset 
 */

/* kset初始化 */
void kset_init(struct kset * k)
{
        /* 先对内嵌对象 进行初始化 */
	kobject_init(&k->kobj);
	INIT_LIST_HEAD(&k->list);
}


/**
 *	kset_add - add a kset object to the hierarchy.
 *	@k:	kset.
 *
 *	Simply, this adds the kset's embedded kobject to the 
 *	hierarchy. 
 *	We also try to make sure that the kset's embedded kobject
 *	has a parent before it is added. We only care if the embedded
 *	kobject is not part of a kset itself, since kobject_add()
 *	assigns a parent in that case. 
 *	If that is the case, and the kset has a controlling subsystem,
 *	then we set the kset's parent to be said subsystem. 
 */

int kset_add(struct kset * k)
{
	if (!k->kobj.parent && !k->kobj.kset && k->subsys)
		k->kobj.parent = &k->subsys->kset.kobj;

	return kobject_add(&k->kobj);
}


/**
 *	kset_register - initialize and add a kset.
 *	@k:	kset.
 */

int kset_register(struct kset * k)
{
	kset_init(k);
	return kset_add(k);
}


/**
 *	kset_unregister - remove a kset.
 *	@k:	kset.
 */

/* 内核级反注册 */
void kset_unregister(struct kset * k)
{
	kobject_unregister(&k->kobj);
}


/**
 *	kset_find_obj - search for object in kset.
 *	@kset:	kset we're looking in.
 *	@name:	object's name.
 *
 *	Lock kset via @kset->subsys, and iterate over @kset->list,
 *	looking for a matching kobject. If matching object is found
 *	take a reference and return the object.
 */

struct kobject * kset_find_obj(struct kset * kset, const char * name)
{
	struct list_head * entry;
	struct kobject * ret = NULL;

	down_read(&kset->subsys->rwsem);
	list_for_each(entry,&kset->list) {
		struct kobject * k = to_kobj(entry);
		if (kobject_name(k) && !strcmp(kobject_name(k),name)) {
			ret = kobject_get(k);
			break;
		}
	}
	up_read(&kset->subsys->rwsem);
	return ret;
}

/* 子系统初始化 */
void subsystem_init(struct subsystem * s)
{
	init_rwsem(&s->rwsem);
	kset_init(&s->kset);
}

/**
 *	subsystem_register - register a subsystem.
 *	@s:	the subsystem we're registering.
 *
 *	Once we register the subsystem, we want to make sure that 
 *	the kset points back to this subsystem for correct usage of 
 *	the rwsem. 
 */

/* 子系统注册 */
int subsystem_register(struct subsystem * s)
{
	int error;

	subsystem_init(s);
	pr_debug("subsystem %s: registering\n",s->kset.kobj.name);

	if (!(error = kset_add(&s->kset))) {
		if (!s->kset.subsys)
			s->kset.subsys = s;
	}
	return error;
}

/* 反注册子系统 */
void subsystem_unregister(struct subsystem * s)
{
	pr_debug("subsystem %s: unregistering\n",s->kset.kobj.name);
	kset_unregister(&s->kset);
}


/**
 *	subsystem_create_file - export sysfs attribute file.
 *	@s:	subsystem.
 *	@a:	subsystem attribute descriptor.
 */

int subsys_create_file(struct subsystem * s, struct subsys_attribute * a)
{
	int error = 0;
	if (subsys_get(s)) {
		error = sysfs_create_file(&s->kset.kobj,&a->attr);
		subsys_put(s);
	}
	return error;
}


/**
 *	subsystem_remove_file - remove sysfs attribute file.
 *	@s:	subsystem.
 *	@a:	attribute desciptor.
 */

void subsys_remove_file(struct subsystem * s, struct subsys_attribute * a)
{
	if (subsys_get(s)) {
		sysfs_remove_file(&s->kset.kobj,&a->attr);
		subsys_put(s);
	}
}

EXPORT_SYMBOL(kobject_get_path);
EXPORT_SYMBOL(kobject_init);
EXPORT_SYMBOL(kobject_register);
EXPORT_SYMBOL(kobject_unregister);
EXPORT_SYMBOL(kobject_get);
EXPORT_SYMBOL(kobject_put);
EXPORT_SYMBOL(kobject_add);
EXPORT_SYMBOL(kobject_del);
EXPORT_SYMBOL(kobject_rename);

EXPORT_SYMBOL(kset_register);
EXPORT_SYMBOL(kset_unregister);
EXPORT_SYMBOL(kset_find_obj);

EXPORT_SYMBOL(subsystem_init);
EXPORT_SYMBOL(subsystem_register);
EXPORT_SYMBOL(subsystem_unregister);
EXPORT_SYMBOL(subsys_create_file);
EXPORT_SYMBOL(subsys_remove_file);
