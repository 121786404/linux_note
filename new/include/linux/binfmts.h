/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BINFMTS_H
#define _LINUX_BINFMTS_H

#include <linux/sched.h>
#include <linux/unistd.h>
#include <asm/exec.h>
#include <uapi/linux/binfmts.h>

struct filename;

#define CORENAME_MAX_SIZE 128

/*
 * This structure is used to hold the arguments that are used when loading binaries.
 用来保存要要执行的文件相关的信息, 
 包括可执行程序的路径, 参数和环境变量的信息
 */
struct linux_binprm {
/*
保存可执行文件的头128字节
*/
	char buf[BINPRM_BUF_SIZE];
#ifdef CONFIG_MMU
	struct vm_area_struct *vma;
	unsigned long vma_pages;
#else
# define MAX_ARG_PAGES	32
	struct page *page[MAX_ARG_PAGES];
#endif
	struct mm_struct *mm;
/*
	当前内存页最高地址
*/
	unsigned long p; /* current top of mem */
	unsigned int
		/*
		 * True after the bprm_set_creds hook has been called once
		 * (multiple calls can be made via prepare_binprm() for
		 * binfmt_script/misc).
		 */
		called_set_creds:1,
		/*
		 * True if most recent call to the commoncaps bprm_set_creds
		 * hook (due to multiple prepare_binprm() calls from the
		 * binfmt_script/misc handlers) resulted in elevated
		 * privileges.
		 */
		cap_elevated:1,
		/*
		 * Set by bprm_set_creds hook to indicate a privilege-gaining
		 * exec has happened. Used to sanitize execution environment
		 * and to set AT_SECURE auxv for glibc.
		 */
		secureexec:1;
#ifdef __alpha__
	unsigned int taso:1;
#endif
	unsigned int recursion_depth; /* only for search_binary_handler() */
	 /*  要执行的文件  */
	struct file * file;
	struct cred *cred;	/* new credentials */
	int unsafe;		/* how unsafe this exec is (mask of LSM_UNSAFE_*) */
	unsigned int per_clear;	/* bits to clear in current->personality */
	/*  命令行参数和环境变量数目  */
	int argc, envc;
/*
	要执行的文件的名称
*/
	const char * filename;	/* Name of binary as seen by procps */
/*
	要执行的文件的真实名称，通常和filename相同
*/
	const char * interp;	/* Name of the binary really executed. Most
				   of the time same as filename, but could be
				   different for binfmt_{misc,script} */
	unsigned interp_flags;
	unsigned interp_data;
	unsigned long loader, exec;

	struct rlimit rlim_stack; /* Saved RLIMIT_STACK used during exec. */
} __randomize_layout;

#define BINPRM_FLAGS_ENFORCE_NONDUMP_BIT 0
#define BINPRM_FLAGS_ENFORCE_NONDUMP (1 << BINPRM_FLAGS_ENFORCE_NONDUMP_BIT)

/* fd of the binary should be passed to the interpreter */
#define BINPRM_FLAGS_EXECFD_BIT 1
#define BINPRM_FLAGS_EXECFD (1 << BINPRM_FLAGS_EXECFD_BIT)

/* filename of the binary will be inaccessible after exec */
#define BINPRM_FLAGS_PATH_INACCESSIBLE_BIT 2
#define BINPRM_FLAGS_PATH_INACCESSIBLE (1 << BINPRM_FLAGS_PATH_INACCESSIBLE_BIT)

/* Function parameter for binfmt->coredump */
struct coredump_params {
	const siginfo_t *siginfo;
	struct pt_regs *regs;
	struct file *file;
	unsigned long limit;
	unsigned long mm_flags;
	loff_t written;
	loff_t pos;
};

/*
 * This structure defines the functions that are used to load the binary formats that
 * linux accepts.
 */
struct linux_binfmt {
	struct list_head lh;
	struct module *module;
/*
	通过读存放在可执行文件中的信息为当前进程建立一个新的执行环境
*/
	int (*load_binary)(struct linux_binprm *);
/*
	用于动态的把一个共享库捆绑到一个已经在运行的进程, 这是由uselib()系统调用激活的
*/
	int (*load_shlib)(struct file *);
/*
    在名为core的文件中, 存放当前进程的执行上下文. 这个文件通常是在进程接收到一个缺省操作为”dump”的信号时被创建的, 其格式取决于被执行程序的可执行类型
*/
	int (*core_dump)(struct coredump_params *cprm); 
	unsigned long min_coredump;	/* minimal dump size */
} __randomize_layout;

extern void __register_binfmt(struct linux_binfmt *fmt, int insert);

/* Registration of default binfmt handlers */
static inline void register_binfmt(struct linux_binfmt *fmt)
{
	__register_binfmt(fmt, 0);
}
/* Same as above, but adds a new binfmt at the top of the list */
static inline void insert_binfmt(struct linux_binfmt *fmt)
{
	__register_binfmt(fmt, 1);
}

extern void unregister_binfmt(struct linux_binfmt *);

extern int prepare_binprm(struct linux_binprm *);
extern int __must_check remove_arg_zero(struct linux_binprm *);
extern int search_binary_handler(struct linux_binprm *);
extern int flush_old_exec(struct linux_binprm * bprm);
extern void setup_new_exec(struct linux_binprm * bprm);
extern void finalize_exec(struct linux_binprm *bprm);
extern void would_dump(struct linux_binprm *, struct file *);

extern int suid_dumpable;

/* Stack area protections */
#define EXSTACK_DEFAULT   0	/* Whatever the arch defaults to */
#define EXSTACK_DISABLE_X 1	/* Disable executable stacks */
#define EXSTACK_ENABLE_X  2	/* Enable executable stacks */

extern int setup_arg_pages(struct linux_binprm * bprm,
			   unsigned long stack_top,
			   int executable_stack);
extern int transfer_args_to_stack(struct linux_binprm *bprm,
				  unsigned long *sp_location);
extern int bprm_change_interp(const char *interp, struct linux_binprm *bprm);
extern int copy_strings_kernel(int argc, const char *const *argv,
			       struct linux_binprm *bprm);
extern int prepare_bprm_creds(struct linux_binprm *bprm);
extern void install_exec_creds(struct linux_binprm *bprm);
extern void set_binfmt(struct linux_binfmt *new);
extern ssize_t read_code(struct file *, unsigned long, loff_t, size_t);

extern int do_execve(struct filename *,
		     const char __user * const __user *,
		     const char __user * const __user *);
extern int do_execveat(int, struct filename *,
		       const char __user * const __user *,
		       const char __user * const __user *,
		       int);
int do_execve_file(struct file *file, void *__argv, void *__envp);

#endif /* _LINUX_BINFMTS_H */
