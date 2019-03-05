#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H
/*
 * Dynamic loading of modules into the kernel.
 *
 * Rewritten by Richard Henderson <rth@tamu.edu> Dec 1996
 * Rewritten again by Rusty Russell, 2002
 */
#include <linux/list.h>
#include <linux/stat.h>
#include <linux/compiler.h>
#include <linux/cache.h>
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/elf.h>
#include <linux/stringify.h>
#include <linux/kobject.h>
#include <linux/moduleparam.h>
#include <linux/jump_label.h>
#include <linux/export.h>
#include <linux/rbtree_latch.h>
#include <linux/error-injection.h>
#include <linux/tracepoint-defs.h>

#include <linux/percpu.h>
#include <asm/module.h>


/*

将moudle.c编译为 moudle.o
modpost(scripts\mod\modpost.c)根据moudle.c生成moudle.mod.c
将moudle.mod.c编译为moudle.mod.o
将moudle.o和moudle.mod.o链接为moudle.ko



其定义了一个类型为 module 的全局变量 __this_module，成员 init 为 init_module（即 hello_init），
且该变量链接到 .gnu.linkonce.this_module段中。

modutils 会调用 sys_init_module api，调用到init_module函数



MODULE_INFO(vermagic, VERMAGIC_STRING);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xc6c01fa, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "0352AD4358D8B5ECA60036D");

*/



/* In stripped ARM and x86-64 modules, ~ is surprisingly rare. */
#define MODULE_SIG_STRING "~Module signature appended~\n"

/* Not Yet Implemented */
#define MODULE_SUPPORTED_DEVICE(name)

#define MODULE_NAME_LEN MAX_PARAM_PREFIX_LEN

struct modversion_info {
	unsigned long crc;
	char name[MODULE_NAME_LEN];
};

struct module;
struct exception_table_entry;

struct module_kobject {
	struct kobject kobj;
	struct module *mod;
	struct kobject *drivers_dir;
	struct module_param_attrs *mp;
	struct completion *kobj_completion;
} __randomize_layout;

struct module_attribute {
	struct attribute attr;
	ssize_t (*show)(struct module_attribute *, struct module_kobject *,
			char *);
	ssize_t (*store)(struct module_attribute *, struct module_kobject *,
			 const char *, size_t count);
	void (*setup)(struct module *, const char *);
	int (*test)(struct module *);
	void (*free)(struct module *);
};

struct module_version_attribute {
	struct module_attribute mattr;
	const char *module_name;
	const char *version;
} __attribute__ ((__aligned__(sizeof(void *))));

extern ssize_t __modver_version_show(struct module_attribute *,
				     struct module_kobject *, char *);

extern struct module_attribute module_uevent;

/* These are either module local, or the kernel's dummy ones. */
extern int init_module(void);
extern void cleanup_module(void);

#ifndef MODULE
/**
 * module_init() - driver initialization entry point
 * @x: function to be run at kernel boot time or module insertion
 *
 * module_init() will either be called during do_initcalls() (if
 * builtin) or at module insertion time (if a module).  There can only
 * be one per module.
 */
#define module_init(x)	__initcall(x);

/**
 * module_exit() - driver exit entry point
 * @x: function to be run when driver is removed
 *
 * module_exit() will wrap the driver clean-up code
 * with cleanup_module() when used with rmmod when
 * the driver is a module.  If the driver is statically
 * compiled into the kernel, module_exit() has no effect.
 * There can only be one per module.
 */
#define module_exit(x)	__exitcall(x);

#else /* MODULE */

/*
 * In most cases loadable modules do not need custom
 * initcall levels. There are still some valid cases where
 * a driver may be needed early if built in, and does not
 * matter when built as a loadable module. Like bus
 * snooping debug drivers.
 */
#define early_initcall(fn)		module_init(fn)
#define core_initcall(fn)		module_init(fn)
#define core_initcall_sync(fn)		module_init(fn)
#define postcore_initcall(fn)		module_init(fn)
#define postcore_initcall_sync(fn)	module_init(fn)
#define arch_initcall(fn)		module_init(fn)
#define subsys_initcall(fn)		module_init(fn)
#define subsys_initcall_sync(fn)	module_init(fn)
#define fs_initcall(fn)			module_init(fn)
#define fs_initcall_sync(fn)		module_init(fn)
#define rootfs_initcall(fn)		module_init(fn)
#define device_initcall(fn)		module_init(fn)
#define device_initcall_sync(fn)	module_init(fn)
#define late_initcall(fn)		module_init(fn)
#define late_initcall_sync(fn)		module_init(fn)

#define console_initcall(fn)		module_init(fn)

/*
__inittest 仅仅是为了检测定义的函数是否符合 initcall_t 类型，如果不是 __inittest 类型在编译时将会报错。

alias 属性是 gcc 的特有属性，将定义 init_module 为函数 initfn 的别名。
所以 module_init(hello_init) 的作用就是定义一个变量名 init_module，其地址和hello_init 是一样的

因此，用动态加载方式时，可以不使用 module_init 和 module_exit 宏，
而直接定义 init_module 和 cleanup_module 函数，效果是一样的
*/
/* Each module must use one module_init(). */
#define module_init(initfn)					\
	static inline initcall_t __maybe_unused __inittest(void)		\
	{ return initfn; }					\
	int init_module(void) __copy(initfn) __attribute__((alias(#initfn)));

/* This is only required if you want to be unloadable. */
#define module_exit(exitfn)					\
	static inline exitcall_t __maybe_unused __exittest(void)		\
	{ return exitfn; }					\
	void cleanup_module(void) __copy(exitfn) __attribute__((alias(#exitfn)));

#endif

/* This means "can be init if no module support, otherwise module load
   may call it." */
#ifdef CONFIG_MODULES
#define __init_or_module
#define __initdata_or_module
#define __initconst_or_module
#define __INIT_OR_MODULE	.text
#define __INITDATA_OR_MODULE	.data
#define __INITRODATA_OR_MODULE	.section ".rodata","a",%progbits
#else
#define __init_or_module __init
#define __initdata_or_module __initdata
#define __initconst_or_module __initconst
#define __INIT_OR_MODULE __INIT
#define __INITDATA_OR_MODULE __INITDATA
#define __INITRODATA_OR_MODULE __INITRODATA
#endif /*CONFIG_MODULES*/

/* Generic info of form tag = "info" */
#define MODULE_INFO(tag, info) __MODULE_INFO(tag, tag, info)

/* For userspace: you can also call me... */
#define MODULE_ALIAS(_alias) MODULE_INFO(alias, _alias)

/* Soft module dependencies. See man modprobe.d for details.
 * Example: MODULE_SOFTDEP("pre: module-foo module-bar post: module-baz")
 */
#define MODULE_SOFTDEP(_softdep) MODULE_INFO(softdep, _softdep)

/*
 * The following license idents are currently accepted as indicating free
 * software modules
 *
 *	"GPL"				[GNU Public License v2 or later]
 *	"GPL v2"			[GNU Public License v2]
 *	"GPL and additional rights"	[GNU Public License v2 rights and more]
 *	"Dual BSD/GPL"			[GNU Public License v2
 *					 or BSD license choice]
 *	"Dual MIT/GPL"			[GNU Public License v2
 *					 or MIT license choice]
 *	"Dual MPL/GPL"			[GNU Public License v2
 *					 or Mozilla license choice]
 *
 * The following other idents are available
 *
 *	"Proprietary"			[Non free products]
 *
 * There are dual licensed components, but when running with Linux it is the
 * GPL that is relevant so this is a non issue. Similarly LGPL linked with GPL
 * is a GPL combined work.
 *
 * This exists for several reasons
 * 1.	So modinfo can show license info for users wanting to vet their setup
 *	is free
 * 2.	So the community can ignore bug reports including proprietary modules
 * 3.	So vendors can do likewise based on their own policies
 */
#define MODULE_LICENSE(_license) MODULE_INFO(license, _license)

/*
 * Author(s), use "Name <email>" or just "Name", for multiple
 * authors use multiple MODULE_AUTHOR() statements/lines.
 */
#define MODULE_AUTHOR(_author) MODULE_INFO(author, _author)

/* What your module does. */
#define MODULE_DESCRIPTION(_description) MODULE_INFO(description, _description)

#ifdef MODULE
/* Creates an alias so file2alias.c can find device table. */
/* 对于 USB、PCI 等设备驱动，通常会创建一个 MODULE_DEVICE_TABLE，
设备表*/
#define MODULE_DEVICE_TABLE(type, name)					\
extern typeof(name) __mod_##type##__##name##_device_table		\
  __attribute__ ((unused, alias(__stringify(name))))
#else  /* !MODULE */
#define MODULE_DEVICE_TABLE(type, name)
#endif

/* Version of form [<epoch>:]<version>[-<extra-version>].
 * Or for CVS/RCS ID version, everything but the number is stripped.
 * <epoch>: A (small) unsigned integer which allows you to start versions
 * anew. If not mentioned, it's zero.  eg. "2:1.0" is after
 * "1:2.0".

 * <version>: The <version> may contain only alphanumerics and the
 * character `.'.  Ordered by numeric sort for numeric parts,
 * ascii sort for ascii parts (as per RPM or DEB algorithm).

 * <extraversion>: Like <version>, but inserted for local
 * customizations, eg "rh3" or "rusty1".

 * Using this automatically adds a checksum of the .c files and the
 * local headers in "srcversion".
 */

#if defined(MODULE) || !defined(CONFIG_SYSFS)
#define MODULE_VERSION(_version) MODULE_INFO(version, _version)
#else
#define MODULE_VERSION(_version)					\
	static struct module_version_attribute ___modver_attr = {	\
		.mattr	= {						\
			.attr	= {					\
				.name	= "version",			\
				.mode	= S_IRUGO,			\
			},						\
			.show	= __modver_version_show,		\
		},							\
		.module_name	= KBUILD_MODNAME,			\
		.version	= _version,				\
	};								\
	static const struct module_version_attribute			\
	__used __attribute__ ((__section__ ("__modver")))		\
	* __moduleparam_const __modver_attr = &___modver_attr
#endif

/* Optional firmware file (or files) needed by the module
 * format is simply firmware file name.  Multiple firmware
 * files require multiple MODULE_FIRMWARE() specifiers */
#define MODULE_FIRMWARE(_firmware) MODULE_INFO(firmware, _firmware)

struct notifier_block;

#ifdef CONFIG_MODULES

extern int modules_disabled; /* for sysctl */
/* Get/put a kernel symbol (calls must be symmetric) */
void *__symbol_get(const char *symbol);
void *__symbol_get_gpl(const char *symbol);
#define symbol_get(x) ((typeof(&x))(__symbol_get(__stringify(x))))

/* modules using other modules: kdb wants to see this. */
struct module_use {
	struct list_head source_list;
	struct list_head target_list;
	struct module *source, *target;
};

enum module_state {
    /* 正常运行 */
	MODULE_STATE_LIVE,	/* Normal state. */
	/* 装载期间 */
	MODULE_STATE_COMING,	/* Full formed, running module_init. */
	/* 正在移除 */
	MODULE_STATE_GOING,	/* Going away. */
	MODULE_STATE_UNFORMED,	/* Still setting it up. */
};

struct mod_tree_node {
	struct module *mod;
	struct latch_tree_node node;
};

struct module_layout {
	/* The actual code + data. */
	void *base;
	/* Total size. */
	unsigned int size;
	/* The size of the executable code.  */
	unsigned int text_size;
	/* Size of RO section of the module (text+rodata) */
	unsigned int ro_size;
	/* Size of RO after init section */
	unsigned int ro_after_init_size;

#ifdef CONFIG_MODULES_TREE_LOOKUP
	struct mod_tree_node mtn;
#endif
};

#ifdef CONFIG_MODULES_TREE_LOOKUP
/* Only touch one cacheline for common rbtree-for-core-layout case. */
#define __module_layout_align ____cacheline_aligned
#else
#define __module_layout_align
#endif

struct mod_kallsyms {
    /* kallsyms的符号表和字符串表*/
	Elf_Sym *symtab;
	unsigned int num_symtab;
	char *strtab;
};

#ifdef CONFIG_LIVEPATCH
struct klp_modinfo {
	Elf_Ehdr hdr;
	Elf_Shdr *sechdrs;
	char *secstrings;
	unsigned int symndx;
};
#endif

struct module {
	enum module_state state;	/*用于记录模块加载过程中不同阶段的状态*/

	/* Member of list of modules */
	struct list_head list;	/* 用于将模块链接到系统维护的内核模块链表中,
				 * 内核用一个链表来管理系统中所有被成功加载的
				 * 模块
				 */

	/* Unique handle for this module */
	char name[MODULE_NAME_LEN];	/*模块名称*/

	/* Sysfs stuff. */
	struct module_kobject mkobj;
	struct module_attribute *modinfo_attrs;
	const char *version;
	const char *srcversion;
	struct kobject *holders_dir;

	/* Exported symbols */
	const struct kernel_symbol *syms;	/*内核模块导出的符号所在起始地址*/
	const s32 *crcs;	/*内核模块导出的符号的校验码所在的起始地址*/
	unsigned int num_syms;

	/* Kernel parameters. */
#ifdef CONFIG_SYSFS
	struct mutex param_lock;
#endif
	struct kernel_param *kp;	/*内核模块参数所在的起始地址*/
	unsigned int num_kp;

	/* GPL-only exported symbols. */
	unsigned int num_gpl_syms;
	const struct kernel_symbol *gpl_syms;
	const s32 *gpl_crcs;

#ifdef CONFIG_UNUSED_SYMBOLS
	/* unused exported symbols. */
	const struct kernel_symbol *unused_syms;
	const s32 *unused_crcs;
	unsigned int num_unused_syms;

	/* GPL-only, unused exported symbols. */
	unsigned int num_unused_gpl_syms;
	const struct kernel_symbol *unused_gpl_syms;
	const s32 *unused_gpl_crcs;
#endif

#ifdef CONFIG_MODULE_SIG
	/* Signature was verified. */
	bool sig_ok;
#endif

	bool async_probe_requested;

	/* symbols that will be GPL-only in the near future. */
	const struct kernel_symbol *gpl_future_syms;
	const s32 *gpl_future_crcs;
	unsigned int num_gpl_future_syms;

	/* Exception table */
	unsigned int num_exentries;
	struct exception_table_entry *extable;

	/* Startup function. */
	int (*init)(void);	/* 指向内核模块初始化函数的指针，
				 * 在内核模块源码中由module_init宏指定
				 */

	/* Core layout: rbtree is accessed frequently, so keep together. */
	struct module_layout core_layout __module_layout_align;
	struct module_layout init_layout;

	/* Arch-specific module values */
	struct mod_arch_specific arch;

	unsigned long taints;	/* same bits as kernel:taint_flags */

#ifdef CONFIG_GENERIC_BUG
	/* Support for BUG */
	unsigned num_bugs;
	struct list_head bug_list;
	struct bug_entry *bug_table;
#endif

#ifdef CONFIG_KALLSYMS
	/* Protected by RCU and/or module_mutex: use rcu_dereference() */
	struct mod_kallsyms *kallsyms;
	struct mod_kallsyms core_kallsyms;

	/* Section attributes */
	/* 模块中各段的属性 */
	struct module_sect_attrs *sect_attrs;

	/* Notes attributes */
	/* notes属性 */
	struct module_notes_attrs *notes_attrs;
#endif

	/* The command line arguments (may be mangled).  People like
	   keeping pointers to this stuff */
	/* 命令行参数（可能已经改编过）。人们喜欢保留指向该数据的指针 */
	char *args;

#ifdef CONFIG_SMP
	/* Per-cpu data. */
	/* pet-CPU数据 */
	void __percpu *percpu;
	unsigned int percpu_size;
#endif

#ifdef CONFIG_TRACEPOINTS
	unsigned int num_tracepoints;
	tracepoint_ptr_t *tracepoints_ptrs;
#endif
#ifdef CONFIG_BPF_EVENTS
	unsigned int num_bpf_raw_events;
	struct bpf_raw_event_map *bpf_raw_events;
#endif
#ifdef CONFIG_JUMP_LABEL
	struct jump_entry *jump_entries;
	unsigned int num_jump_entries;
#endif
#ifdef CONFIG_TRACING
	unsigned int num_trace_bprintk_fmt;
	const char **trace_bprintk_fmt_start;
#endif
#ifdef CONFIG_EVENT_TRACING
	struct trace_event_call **trace_events;
	unsigned int num_trace_events;
	struct trace_eval_map **trace_evals;
	unsigned int num_trace_evals;
#endif
#ifdef CONFIG_FTRACE_MCOUNT_RECORD
	unsigned int num_ftrace_callsites;
	unsigned long *ftrace_callsites;
#endif

#ifdef CONFIG_LIVEPATCH
	bool klp; /* Is this a livepatch module? */
	bool klp_alive;

	/* Elf information */
	struct klp_modinfo *klp_info;
#endif

#ifdef CONFIG_MODULE_UNLOAD
	/* What modules depend on me? */
	struct list_head source_list;
	/* What modules do I depend on? */
	struct list_head target_list;

	/* Destruction function. */
	void (*exit)(void);

	atomic_t refcnt;
#endif

#ifdef CONFIG_CONSTRUCTORS
	/* Constructor functions. */
	ctor_fn_t *ctors;
	unsigned int num_ctors;
#endif

#ifdef CONFIG_FUNCTION_ERROR_INJECTION
	struct error_injection_entry *ei_funcs;
	unsigned int num_ei_funcs;
#endif
} ____cacheline_aligned __randomize_layout;
#ifndef MODULE_ARCH_INIT
#define MODULE_ARCH_INIT {}
#endif

#ifndef HAVE_ARCH_KALLSYMS_SYMBOL_VALUE
static inline unsigned long kallsyms_symbol_value(const Elf_Sym *sym)
{
	return sym->st_value;
}
#endif

extern struct mutex module_mutex;

/* FIXME: It'd be nice to isolate modules during init, too, so they
   aren't used before they (may) fail.  But presently too much code
   (IDE & SCSI) require entry into the module during init.*/
static inline bool module_is_live(struct module *mod)
{
	return mod->state != MODULE_STATE_GOING;
}

struct module *__module_text_address(unsigned long addr);
struct module *__module_address(unsigned long addr);
bool is_module_address(unsigned long addr);
bool __is_module_percpu_address(unsigned long addr, unsigned long *can_addr);
bool is_module_percpu_address(unsigned long addr);
bool is_module_text_address(unsigned long addr);

static inline bool within_module_core(unsigned long addr,
				      const struct module *mod)
{
	return (unsigned long)mod->core_layout.base <= addr &&
	       addr < (unsigned long)mod->core_layout.base + mod->core_layout.size;
}

static inline bool within_module_init(unsigned long addr,
				      const struct module *mod)
{
	return (unsigned long)mod->init_layout.base <= addr &&
	       addr < (unsigned long)mod->init_layout.base + mod->init_layout.size;
}

static inline bool within_module(unsigned long addr, const struct module *mod)
{
	return within_module_init(addr, mod) || within_module_core(addr, mod);
}

/* Search for module by name: must hold module_mutex. */
struct module *find_module(const char *name);

struct symsearch {
	const struct kernel_symbol *start, *stop;
	const s32 *crcs;
	enum {
		NOT_GPL_ONLY,
		GPL_ONLY,
		WILL_BE_GPL_ONLY,
	} licence;
	bool unused;
};

/*
 * Search for an exported symbol by name.
 *
 * Must be called with module_mutex held or preemption disabled.
 */
const struct kernel_symbol *find_symbol(const char *name,
					struct module **owner,
					const s32 **crc,
					bool gplok,
					bool warn);

/*
 * Walk the exported symbol table
 *
 * Must be called with module_mutex held or preemption disabled.
 */
bool each_symbol_section(bool (*fn)(const struct symsearch *arr,
				    struct module *owner,
				    void *data), void *data);

/* Returns 0 and fills in value, defined and namebuf, or -ERANGE if
   symnum out of range. */
int module_get_kallsym(unsigned int symnum, unsigned long *value, char *type,
			char *name, char *module_name, int *exported);

/* Look for this name: can be of form module:name. */
unsigned long module_kallsyms_lookup_name(const char *name);

int module_kallsyms_on_each_symbol(int (*fn)(void *, const char *,
					     struct module *, unsigned long),
				   void *data);

extern void __noreturn __module_put_and_exit(struct module *mod,
			long code);
#define module_put_and_exit(code) __module_put_and_exit(THIS_MODULE, code)

#ifdef CONFIG_MODULE_UNLOAD
int module_refcount(struct module *mod);
void __symbol_put(const char *symbol);
#define symbol_put(x) __symbol_put(__stringify(x))
void symbol_put_addr(void *addr);

/* Sometimes we know we already have a refcount, and it's easier not
   to handle the error case (which only happens with rmmod --wait). */
extern void __module_get(struct module *module);

/* This is the Right Way to get a module: if it fails, it's being removed,
 * so pretend it's not there. */
extern bool try_module_get(struct module *module);

extern void module_put(struct module *module);

#else /*!CONFIG_MODULE_UNLOAD*/
/*
内核为不同类型的设备定义了struct module*owner域，用来指向管理此设备的模块。
当开始使用某个设备时，内核使用try_module_get（dev->owner）
去增加管理此设备的owner模块的使用计数；
当不再使用此设备时，内核使用module_put（dev->owner）
减少对管理此设备的管理模块的使用计数。
对于设备驱动而言，很少需要亲自调用try_module_get与module_put，
因为此时开发人员所写的驱动通常为支持某具体设备的管理模块，
对此设备owner模块的计数管理由内核里更底层的代码（如总线驱动或是此类设备共用的核心模块）来实现，
从而简化了设备驱动开发。
*/
static inline bool try_module_get(struct module *module)
{
	return !module || module_is_live(module);
}
/*
该函数的功能是将一个特定模块module的引用计数减1 ，这样当一个模块的引用计数因为不为0而不能从内核中卸载时，可以调用此函数一次或多次，实现对模块计数的清零，从而实现模块卸载。*/
static inline void module_put(struct module *module)
{
}
static inline void __module_get(struct module *module)
{
}
#define symbol_put(x) do { } while (0)
#define symbol_put_addr(p) do { } while (0)

#endif /* CONFIG_MODULE_UNLOAD */
int ref_module(struct module *a, struct module *b);

/* This is a #define so the string doesn't get put in every .o file */
#define module_name(mod)			\
({						\
	struct module *__mod = (mod);		\
	__mod ? __mod->name : "kernel";		\
})

/* Dereference module function descriptor */
void *dereference_module_function_descriptor(struct module *mod, void *ptr);

/* For kallsyms to ask for address resolution.  namebuf should be at
 * least KSYM_NAME_LEN long: a pointer to namebuf is returned if
 * found, otherwise NULL. */
const char *module_address_lookup(unsigned long addr,
			    unsigned long *symbolsize,
			    unsigned long *offset,
			    char **modname,
			    char *namebuf);
int lookup_module_symbol_name(unsigned long addr, char *symname);
int lookup_module_symbol_attrs(unsigned long addr, unsigned long *size, unsigned long *offset, char *modname, char *name);

int register_module_notifier(struct notifier_block *nb);
int unregister_module_notifier(struct notifier_block *nb);

extern void print_modules(void);

static inline bool module_requested_async_probing(struct module *module)
{
	return module && module->async_probe_requested;
}

#ifdef CONFIG_LIVEPATCH
static inline bool is_livepatch_module(struct module *mod)
{
	return mod->klp;
}
#else /* !CONFIG_LIVEPATCH */
static inline bool is_livepatch_module(struct module *mod)
{
	return false;
}
#endif /* CONFIG_LIVEPATCH */

bool is_module_sig_enforced(void);

#else /* !CONFIG_MODULES... */

static inline struct module *__module_address(unsigned long addr)
{
	return NULL;
}

static inline struct module *__module_text_address(unsigned long addr)
{
	return NULL;
}

static inline bool is_module_address(unsigned long addr)
{
	return false;
}

static inline bool is_module_percpu_address(unsigned long addr)
{
	return false;
}

static inline bool __is_module_percpu_address(unsigned long addr, unsigned long *can_addr)
{
	return false;
}

static inline bool is_module_text_address(unsigned long addr)
{
	return false;
}

/* Get/put a kernel symbol (calls should be symmetric) */
#define symbol_get(x) ({ extern typeof(x) x __attribute__((weak)); &(x); })
#define symbol_put(x) do { } while (0)
#define symbol_put_addr(x) do { } while (0)

static inline void __module_get(struct module *module)
{
}

static inline bool try_module_get(struct module *module)
{
	return true;
}

static inline void module_put(struct module *module)
{
}

#define module_name(mod) "kernel"

/* For kallsyms to ask for address resolution.  NULL means not found. */
static inline const char *module_address_lookup(unsigned long addr,
					  unsigned long *symbolsize,
					  unsigned long *offset,
					  char **modname,
					  char *namebuf)
{
	return NULL;
}

static inline int lookup_module_symbol_name(unsigned long addr, char *symname)
{
	return -ERANGE;
}

static inline int lookup_module_symbol_attrs(unsigned long addr, unsigned long *size, unsigned long *offset, char *modname, char *name)
{
	return -ERANGE;
}

static inline int module_get_kallsym(unsigned int symnum, unsigned long *value,
					char *type, char *name,
					char *module_name, int *exported)
{
	return -ERANGE;
}

static inline unsigned long module_kallsyms_lookup_name(const char *name)
{
	return 0;
}

static inline int module_kallsyms_on_each_symbol(int (*fn)(void *, const char *,
							   struct module *,
							   unsigned long),
						 void *data)
{
	return 0;
}

static inline int register_module_notifier(struct notifier_block *nb)
{
	/* no events will happen anyway, so this can always succeed */
	return 0;
}

static inline int unregister_module_notifier(struct notifier_block *nb)
{
	return 0;
}

#define module_put_and_exit(code) do_exit(code)

static inline void print_modules(void)
{
}

static inline bool module_requested_async_probing(struct module *module)
{
	return false;
}

static inline bool is_module_sig_enforced(void)
{
	return false;
}

/* Dereference module function descriptor */
static inline
void *dereference_module_function_descriptor(struct module *mod, void *ptr)
{
	return ptr;
}

#endif /* CONFIG_MODULES */

#ifdef CONFIG_SYSFS
extern struct kset *module_kset;
extern struct kobj_type module_ktype;
extern int module_sysfs_initialized;
#endif /* CONFIG_SYSFS */

#define symbol_request(x) try_then_request_module(symbol_get(x), "symbol:" #x)

/* BELOW HERE ALL THESE ARE OBSOLETE AND WILL VANISH */

#define __MODULE_STRING(x) __stringify(x)

#ifdef CONFIG_STRICT_MODULE_RWX
extern void set_all_modules_text_rw(void);
extern void set_all_modules_text_ro(void);
extern void module_enable_ro(const struct module *mod, bool after_init);
extern void module_disable_ro(const struct module *mod);
#else
static inline void set_all_modules_text_rw(void) { }
static inline void set_all_modules_text_ro(void) { }
static inline void module_enable_ro(const struct module *mod, bool after_init) { }
static inline void module_disable_ro(const struct module *mod) { }
#endif

#ifdef CONFIG_GENERIC_BUG
void module_bug_finalize(const Elf_Ehdr *, const Elf_Shdr *,
			 struct module *);
void module_bug_cleanup(struct module *);

#else	/* !CONFIG_GENERIC_BUG */

static inline void module_bug_finalize(const Elf_Ehdr *hdr,
					const Elf_Shdr *sechdrs,
					struct module *mod)
{
}
static inline void module_bug_cleanup(struct module *mod) {}
#endif	/* CONFIG_GENERIC_BUG */

#ifdef CONFIG_RETPOLINE
extern bool retpoline_module_ok(bool has_retpoline);
#else
static inline bool retpoline_module_ok(bool has_retpoline)
{
	return true;
}
#endif

#ifdef CONFIG_MODULE_SIG
static inline bool module_sig_ok(struct module *module)
{
	return module->sig_ok;
}
#else	/* !CONFIG_MODULE_SIG */
static inline bool module_sig_ok(struct module *module)
{
	return true;
}
#endif	/* CONFIG_MODULE_SIG */

#endif /* _LINUX_MODULE_H */
