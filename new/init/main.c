/*
 *  linux/init/main.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  GK 2/5/95  -  Changed to support mounting root fs via NFS
 *  Added initrd & change_root: Werner Almesberger & Hans Lermen, Feb '96
 *  Moan early if gcc is old, avoiding bogus kernels - Paul Gortmaker, May '96
 *  Simplified starting of init:  Michael A. Griffith <grif@acm.org>
 */

#define DEBUG		/* Enable initcall_debug */

#include <linux/types.h>
#include <linux/extable.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/binfmts.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/stackprotector.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/memblock.h>
#include <linux/acpi.h>
#include <linux/console.h>
#include <linux/nmi.h>
#include <linux/percpu.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/kernel_stat.h>
#include <linux/start_kernel.h>
#include <linux/security.h>
#include <linux/smp.h>
#include <linux/profile.h>
#include <linux/rcupdate.h>
#include <linux/moduleparam.h>
#include <linux/kallsyms.h>
#include <linux/writeback.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/cgroup.h>
#include <linux/efi.h>
#include <linux/tick.h>
#include <linux/sched/isolation.h>
#include <linux/interrupt.h>
#include <linux/taskstats_kern.h>
#include <linux/delayacct.h>
#include <linux/unistd.h>
#include <linux/utsname.h>
#include <linux/rmap.h>
#include <linux/mempolicy.h>
#include <linux/key.h>
#include <linux/buffer_head.h>
#include <linux/page_ext.h>
#include <linux/debug_locks.h>
#include <linux/debugobjects.h>
#include <linux/lockdep.h>
#include <linux/kmemleak.h>
#include <linux/pid_namespace.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/init.h>
#include <linux/signal.h>
#include <linux/idr.h>
#include <linux/kgdb.h>
#include <linux/ftrace.h>
#include <linux/async.h>
#include <linux/sfi.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/perf_event.h>
#include <linux/ptrace.h>
#include <linux/pti.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/sched/clock.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/context_tracking.h>
#include <linux/random.h>
#include <linux/list.h>
#include <linux/integrity.h>
#include <linux/proc_ns.h>
#include <linux/io.h>
#include <linux/cache.h>
#include <linux/rodata_test.h>
#include <linux/jump_label.h>
#include <linux/mem_encrypt.h>

#include <asm/io.h>
#include <asm/bugs.h>
#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/cacheflush.h>

#define CREATE_TRACE_POINTS
#include <trace/events/initcall.h>

static int kernel_init(void *);

extern void init_IRQ(void);
extern void radix_tree_init(void);

/*
 * Debug helper: via this flag we know that we are in 'early bootup code'
 * where only the boot processor is running with IRQ disabled.  This means
 * two things - IRQ must not be enabled before the flag is cleared and some
 * operations which are not allowed with IRQ disabled are allowed while the
 * flag is set.
 */
bool early_boot_irqs_disabled __read_mostly;
/*
全局变量，主要用来记录当前系统的状态
*/
enum system_states system_state __read_mostly;
EXPORT_SYMBOL(system_state);

/*
 * Boot command-line arguments
 */
#define MAX_INIT_ARGS CONFIG_INIT_ENV_ARG_LIMIT
#define MAX_INIT_ENVS CONFIG_INIT_ENV_ARG_LIMIT

extern void time_init(void);
/* Default late time init is NULL. archs can override this later. */
void (*__initdata late_time_init)(void);

/* Untouched command line saved by arch-specific code. */
char __initdata boot_command_line[COMMAND_LINE_SIZE];
/* Untouched saved command line (eg. for /proc) */
char *saved_command_line;
/* Command line for parameter parsing */
static char *static_command_line;
/* Command line for per-initcall parameter parsing */
static char *initcall_command_line;

static char *execute_command;
static char *ramdisk_execute_command;

/*
 * Used to generate warnings if static_key manipulation functions are used
 * before jump_label_init is called.
 */
bool static_key_initialized __read_mostly;
EXPORT_SYMBOL_GPL(static_key_initialized);

/*
 * If set, this is an indication to the drivers that reset the underlying
 * device before going ahead with the initialization otherwise driver might
 * rely on the BIOS and skip the reset operation.
 *
 * This is useful if kernel is booting in an unreliable environment.
 * For ex. kdump situation where previous kernel has crashed, BIOS has been
 * skipped and devices will be in unknown state.
 */
unsigned int reset_devices;
EXPORT_SYMBOL(reset_devices);

static int __init set_reset_devices(char *str)
{
	reset_devices = 1;
	return 1;
}

__setup("reset_devices", set_reset_devices);

static const char *argv_init[MAX_INIT_ARGS+2] = { "init", NULL, };
const char *envp_init[MAX_INIT_ENVS+2] = { "HOME=/", "TERM=linux", NULL, };
static const char *panic_later, *panic_param;

extern const struct obs_kernel_param __setup_start[], __setup_end[];

static bool __init obsolete_checksetup(char *line)
{
	const struct obs_kernel_param *p;
	bool had_early_param = false;

	p = __setup_start;
	do {
		int n = strlen(p->str);
		if (parameqn(line, p->str, n)) {
			if (p->early) {
				/* Already done in parse_early_param?
				 * (Needs exact match on param part).
				 * Keep iterating, as we can have early
				 * params and __setups of same names 8( */
				if (line[n] == '\0' || line[n] == '=')
					had_early_param = true;
			} else if (!p->setup_func) {
				pr_warn("Parameter %s is obsolete, ignored\n",
					p->str);
				return true;
			} else if (p->setup_func(line + n))
				return true;
		}
		p++;
	} while (p < __setup_end);

	return had_early_param;
}

/*
 * This should be approx 2 Bo*oMips to start (note initial shift), and will
 * still work even if initially too large, it will just take slightly longer
 */
unsigned long loops_per_jiffy = (1<<12);
EXPORT_SYMBOL(loops_per_jiffy);

static int __init debug_kernel(char *str)
{
	console_loglevel = CONSOLE_LOGLEVEL_DEBUG;
	return 0;
}

static int __init quiet_kernel(char *str)
{
	console_loglevel = CONSOLE_LOGLEVEL_QUIET;
	return 0;
}

early_param("debug", debug_kernel);
early_param("quiet", quiet_kernel);

static int __init loglevel(char *str)
{
	int newlevel;

	/*
	 * Only update loglevel value when a correct setting was passed,
	 * to prevent blind crashes (when loglevel being set to 0) that
	 * are quite hard to debug
	 */
	if (get_option(&str, &newlevel)) {
		console_loglevel = newlevel;
		return 0;
	}

	return -EINVAL;
}

early_param("loglevel", loglevel);

/* Change NUL term back to "=", to make "param" the whole string. */
static int __init repair_env_string(char *param, char *val,
				    const char *unused, void *arg)
{
	if (val) {
		/* param=val or param="val"? */
		if (val == param+strlen(param)+1)
			val[-1] = '=';
		else if (val == param+strlen(param)+2) {
			val[-2] = '=';
			memmove(val-1, val, strlen(val)+1);
			val--;
		} else
			BUG();
	}
	return 0;
}

/* Anything after -- gets handed straight to init. */
static int __init set_init_arg(char *param, char *val,
			       const char *unused, void *arg)
{
	unsigned int i;

	if (panic_later)
		return 0;

	repair_env_string(param, val, unused, NULL);

	for (i = 0; argv_init[i]; i++) {
		if (i == MAX_INIT_ARGS) {
			panic_later = "init";
			panic_param = param;
			return 0;
		}
	}
	argv_init[i] = param;
	return 0;
}

/*
 * Unknown boot options get handed to init, unless they look like
 * unused parameters (modprobe will find them in /proc/cmdline).
 */
static int __init unknown_bootoption(char *param, char *val,
				     const char *unused, void *arg)
{
	repair_env_string(param, val, unused, NULL);

	/* Handle obsolete-style parameters */
	if (obsolete_checksetup(param))
		return 0;

	/* Unused module parameter. */
	if (strchr(param, '.') && (!val || strchr(param, '.') < val))
		return 0;

	if (panic_later)
		return 0;

	if (val) {
		/* Environment option */
		unsigned int i;
		for (i = 0; envp_init[i]; i++) {
			if (i == MAX_INIT_ENVS) {
				panic_later = "env";
				panic_param = param;
			}
			if (!strncmp(param, envp_init[i], val - param))
				break;
		}
		envp_init[i] = param;
	} else {
		/* Command line option */
		unsigned int i;
		for (i = 0; argv_init[i]; i++) {
			if (i == MAX_INIT_ARGS) {
				panic_later = "init";
				panic_param = param;
			}
		}
		argv_init[i] = param;
	}
	return 0;
}

static int __init init_setup(char *str)
{
	unsigned int i;

	execute_command = str;
	/*
	 * In case LILO is going to boot us with default command line,
	 * it prepends "auto" before the whole cmdline which makes
	 * the shell think it should execute a script with such name.
	 * So we ignore all arguments entered _before_ init=... [MJ]
	 */
	for (i = 1; i < MAX_INIT_ARGS; i++)
		argv_init[i] = NULL;
	return 1;
}
__setup("init=", init_setup);

static int __init rdinit_setup(char *str)
{
	unsigned int i;

	ramdisk_execute_command = str;
	/* See "auto" comment in init_setup */
	for (i = 1; i < MAX_INIT_ARGS; i++)
		argv_init[i] = NULL;
	return 1;
}
__setup("rdinit=", rdinit_setup);

#ifndef CONFIG_SMP
static const unsigned int setup_max_cpus = NR_CPUS;
static inline void setup_nr_cpu_ids(void) { }
static inline void smp_prepare_cpus(unsigned int maxcpus) { }
#endif

/*
 * We need to store the untouched command line for future reference.
 * We also need to store the touched command line since the parameter
 * parsing is performed in place, and we should allow a component to
 * store reference of name/value for future reference.
 */
static void __init setup_command_line(char *command_line)
{
	//原始命令行
	saved_command_line =
		memblock_alloc(strlen(boot_command_line) + 1, SMP_CACHE_BYTES);
	initcall_command_line =
		memblock_alloc(strlen(boot_command_line) + 1, SMP_CACHE_BYTES);
	//用于参数解析的命令行。
	static_command_line = memblock_alloc(strlen(command_line) + 1,
					     SMP_CACHE_BYTES);
	strcpy(saved_command_line, boot_command_line);
	strcpy(static_command_line, command_line);
}

/*
 * We need to finalize in a non-__init function or else race conditions
 * between the root thread and the init thread may cause start_kernel to
 * be reaped by free_initmem before the root thread has proceeded to
 * cpu_idle.
 *
 * gcc-3.4 accidentally inlines this function, so use noinline.
 */

static __initdata DECLARE_COMPLETION(kthreadd_done);

noinline void __ref rest_init(void)
{
	struct task_struct *tsk;
	int pid;
/*
最后就是设定rcu_scheduler_active=1启动RCU机制.
RCU在多核心架构下,不同的行程要读取同一笔资料内容/结构,
可以提供高效率的同步与正确性.
在这之后就可以使用 rcu_read_lock/rcu_read_unlock了
*/
	rcu_scheduler_starting();
	/*
	 * We need to spawn init first so that it obtains pid 1, however
	 * the init task will end up wanting to create kthreads, which, if
	 * we schedule it before we create kthreadd, will OOPS.
	 */
	//生成init进程，kernel_init进程执行了很多初始化工作。
    // 创建1号内核线程, 该线程随后转向用户空间, 演变为init进程
	pid = kernel_thread(kernel_init, NULL, CLONE_FS);
	/*
	 * Pin init on the boot CPU. Task migration is not properly working
	 * until sched_init_smp() has been run. It will set the allowed
	 * CPUs for init to the non isolated CPUs.
	 */
	rcu_read_lock();
	tsk = find_task_by_pid_ns(pid, &init_pid_ns);
	set_cpus_allowed_ptr(tsk, cpumask_of(smp_processor_id()));
	rcu_read_unlock();
	//初始化当前进程的内存策略。
	numa_default_policy();
	//生成kthreadd守护进程,用于生成内核线程的线程。
	pid = kernel_thread(kthreadd, NULL, CLONE_FS | CLONE_FILES);
	//在查找任务前，获得rcu读锁
	rcu_read_lock();
	//通过pid查找kthreadd线程结构。
    /* 传入参数kthreadd的PID 2与PID NameSpace (struct pid_namespace init_pid_ns)取回PID 2的Task Struct.*/
	kthreadd_task = find_task_by_pid_ns(pid, &init_pid_ns);
	rcu_read_unlock();
	//唤醒等待kthreadd线程创建消息的进程。
	/*
	 * Enable might_sleep() and smp_processor_id() checks.
	 * They cannot be enabled earlier because with CONFIG_PREEMPT=y
	 * kernel_thread() would trigger might_sleep() splats. With
	 * CONFIG_PREEMPT_VOLUNTARY=y the init task might have scheduled
	 * already, but it's stuck on the kthreadd_done completion.
	 */
	system_state = SYSTEM_SCHEDULING;
    /* 会发送kthreadd_done Signal,让 kernel_init(也就是 init task)可以往后继续执行 */
	complete(&kthreadd_done);

	/*
	 * The boot idle thread must execute schedule()
	 * at least once to get things moving:
	 */
	/*
      调用schedule()函数切换当前进程，在调用该函数之前，
      Linux系统中只有两个进程，即0号进程init_task和1号进程kernel_init，
      其中kernel_init进程也是刚刚被创建的。调用该函数后，
      1号进程kernel_init将会运行！
     */
	schedule_preempt_disabled();
	/* Call into cpu_idle with preempt disabled */
/*
    完成工作后, 调用cpu_idle_loop()使得idle进程进入自己的事件处理循环
*/
	cpu_startup_entry(CPUHP_ONLINE);
}

/* Check for early params. */
/**
 * 对未修正的参数行中，某一项参数进行解析
 */
static int __init do_early_param(char *param, char *val,
				 const char *unused, void *arg)
{
	const struct obs_kernel_param *p;

	//__setup_start,__setup_end保存了__setup，early_param宏定义的初始化函数
	for (p = __setup_start; p < __setup_end; p++) {
		if ((p->early && parameq(param, p->str)) ||
		    (strcmp(param, "console") == 0 &&
		     strcmp(p->str, "earlycon") == 0)
		) {
			if (p->setup_func(val) != 0)//调用注册的回调
				pr_warn("Malformed early option '%s'\n", param);
		}
	}
	/* We accept everything at this stage. */
	return 0;
}

//解析参数行，并调用do_early_param对它进行处理
void __init parse_early_options(char *cmdline)
{
	parse_args("early options", cmdline, NULL, 0, 0, 0, NULL,
		   do_early_param);
}

/* Arch code calls this early on, or if not, just before other parsing. */
/**
 * 用未修正的原始参数进行解析
 * 对boot_command_line进行早期的解析
 * 有些參數有較高的優先權，需要先被處理，這類的參數被稱為 early_param
 * 舉例來說，像 log level 的設定會影響訊息的輸出，若太晚生效的話，有些除錯訊息可能就不會被看到
 */
void __init parse_early_param(void)
{
	static int done __initdata;
	static char tmp_cmdline[COMMAND_LINE_SIZE] __initdata;

	if (done) //某些架构可能在架构相关代码中调用了，这里防止多次初始化。
		return;

	/* All fall through to do_early_param. */
	//复制未修正的参数行，并对它进行解析。
	strlcpy(tmp_cmdline, boot_command_line, COMMAND_LINE_SIZE);
	parse_early_options(tmp_cmdline);
	done = 1;
}

void __init __weak arch_post_acpi_subsys_init(void) { }

void __init __weak smp_setup_processor_id(void)
{
}

# if THREAD_SIZE >= PAGE_SIZE
void __init __weak thread_stack_cache_init(void)
{
}
#endif

void __init __weak mem_encrypt_init(void) { }

bool initcall_debug;
core_param(initcall_debug, initcall_debug, bool, 0644);

#ifdef TRACEPOINTS_ENABLED
static void __init initcall_debug_enable(void);
#else
static inline void initcall_debug_enable(void)
{
}
#endif

/*
 * Set up kernel memory allocators
 */
/*
建立了内核的内存分配器,
*/
static void __init mm_init(void)
{
	/*
	 * page_ext requires contiguous pages,
	 * bigger than MAX_ORDER unless SPARSEMEM.
	 */
	page_ext_init_flatmem();
	//将boot内存管理转换为伙伴内存管理
	mem_init();
	/**
	 * 初始化slab/slub内存分配器
	 */
	kmem_cache_init();
	//页表相关的初始化，其实就是创建一个slab分配器，用于页表锁的分配。
	pgtable_init();
	debug_objects_mem_init();
	vmalloc_init();
	ioremap_huge_init();
	/* Should be run before the first non-init thread is created */
	init_espfix_bsp();
	/* Should be run after espfix64 is set up. */
	pti_init();
}

void __init __weak arch_call_rest_init(void)
{
	rest_init();
}
/**
 * 内核初始化，在ARM架构中，从__mmap_switched汇编函数中调用此函数。
 * asmlinkage支持从汇编调用此函数
 */
asmlinkage __visible void __init start_kernel(void)
{
	char *command_line; // a pointer to the kernel command line
	char *after_dashes; // a pointer to the kernel command line after "--", which will be passed to the init process

	//设置init任务的堆栈魔法数，用于故障诊断
	set_task_stack_end_magic(&init_task);
	//设置启动CPU，ARM架构也支持从非0 CPU启动了
	smp_setup_processor_id();
	/**
	 * 初始化内核对象跟踪模块用到的哈希表及自旋锁
	 */
	debug_objects_early_init();

	/**
	 * 初始化cgroup子系统
	 */
	cgroup_init_early();

	//关中断，
	local_irq_disable();
	//诊断用，表示目前当前处于boot阶段，并且中断被关闭了。
	early_boot_irqs_disabled = true;

/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
 	/**
 	 * 在CPU状态位图中注册当前CPU
 	 */
	boot_cpu_init();
	/**
	 * 当开启高端内存选项时，初始化vmalloc用到的虚拟地址空间数据结构。
	 */
	page_address_init();
	pr_notice("%s", linux_banner);
	//处理CPU体系架构相关的事务。
	setup_arch(&command_line);
	/*
	 * Set up the the initial canary and entropy after arch
	 * and after adding latent and command line entropy.
	 */
	add_latent_entropy();
	add_device_randomness(command_line, strlen(command_line));
	boot_init_stack_canary();
	mm_init_cpumask(&init_mm);
	//保存命令行参数
	setup_command_line(command_line);
	// set "nr_cpu_ids" according to the last bit in possible mask
	setup_nr_cpu_ids();
    // per cpu memory allocator
	/* 完成per_CPU变量副本的生成，
	 * 而且会对per-CPU变量的动态分配机制进行初始化*/
	setup_per_cpu_areas();
	//体系结构相关的SMP初始化，还是设置每cpu数据相关的东西
	smp_prepare_boot_cpu();	/* arch-specific boot-cpu hooks */
	boot_cpu_hotplug_init();
	//继续初始化伙伴系统中的pglist_data，重点是初始化它的node_zonelist成员
	build_all_zonelists(NULL);
	//为CPU热插拨注册内存通知链
	page_alloc_init();

	pr_notice("Kernel command line: %s\n", boot_command_line);
	//解析内核参数，第一次解析
	parse_early_param();
	//第二次解析,static_command_line中是在第一阶段中未处理的参数
	after_dashes = parse_args("Booting kernel",
				  static_command_line, __start___param,
				  __stop___param - __start___param,
				  -1, -1, NULL, &unknown_bootoption);
	if (!IS_ERR_OR_NULL(after_dashes))
		parse_args("Setting init args", after_dashes, NULL, 0, -1, -1,
			   NULL, set_init_arg);

    // Jump label: https://lwn.net/Articles/412072/
	jump_label_init();

	/*
	 * These use large bootmem allocations and must precede
	 * kmem_cache_init()
	 */
	// buf for printk
	setup_log_buf(0);
	//进程pid管理用到的数据结构初始化。
	//初始化目录项和索引节点缓存
	// allocate and caches initialize for hash tables of dcache and inode
	vfs_caches_init_early();
	//对异常表进行排序，以减少异常修复入口的查找时间
	// sort the kernel's built-in exception table (for page faults)
	sort_main_extable();
	// architecture-specific, interrupt vector table, handle hardware traps, exceptions and faults.
	trap_init();
	// memory management
	mm_init();

	ftrace_init();

	/* trace_printk can be enabled here */
	early_trace_init();

	/*
	 * Set up the scheduler prior starting any interrupts (such as the
	 * timer interrupt). Full topology setup happens at smp_init()
	 * time - but meanwhile we still have a functioning scheduler.
	 */
	//初始化调度器
	sched_init();
	/*
	 * Disable preemption - early bootup scheduling is extremely
	 * fragile until we cpu_idle() for the first time.
	 */
	preempt_disable();
	if (WARN(!irqs_disabled(),
		 "Interrupts were enabled *very* early, fixing it\n"))
		local_irq_disable();
	radix_tree_init();
	//idr用于管理整数ID，为POSIX计时器相关系统所用，生成特定计时器对象的ID.
	/*
	 * Set up housekeeping before setting up workqueues to allow the unbound
	 * workqueue to take non-housekeeping into account.
	 */
	housekeeping_init();

	/*
	 * Allow workqueue creation and work item queueing/cancelling
	 * early.  Work item execution depends on kthreads and starts after
	 * workqueue_init().
	 */
	workqueue_init_early();

	rcu_init();

	/* Trace events are available after this */
	trace_init();

	if (initcall_debug)
		initcall_debug_enable();

	context_tracking_init();
	/* init some links before init_ISA_irqs() */
	early_irq_init();
	//中断初始化，注册bad_irq_desc
	// architecture-specific, initialize kernel's interrupt subsystem and the interrupt controllers.
	init_IRQ();
	/**
	 * 初始化时钟
	 * initialize the tick control
	 */
	tick_init();
	rcu_init_nohz();
	//初始化计时器
	// init timer stats, register cpu notifier, and open softirq for timer
	init_timers();
	//高分辨率时钟初始化。
	// high-resolution timer, https://www.kernel.org/doc/Documentation/timers/hrtimers.txt
	hrtimers_init();
	//软中断初始化
	// initialize tasklet_vec and open softirq for tasklet
	softirq_init();
	//初始化xtime
	// https://www.kernel.org/doc/Documentation/timers/timekeeping.txt
	timekeeping_init();
	//初始化硬件时钟并设置计时器，注册处理函数
	// architecture-specific, timer initialization
	time_init();
	//调度器使用的时间系统初始化。
	// start the high-resolution timer to keep sched_clock() properly updated and sets the initial epoch
	printk_safe_init();
	// perf is a profiler tool for Linux, https://perf.wiki.kernel.org/index.php/Tutorial
	perf_event_init();
	// initializes basic kernel profiler
	profile_init();
	//smp中，为管理核间回调函数的初始化。
	// SMP initializes call_single_queue and register notifier
	call_function_init();
	WARN(!irqs_disabled(), "Interrupts were enabled early\n");

	early_boot_irqs_disabled = false;
	//中断已经初始化完毕，现在要开启控制台了，打开中断。
	local_irq_enable();
    // post-initialization of cache (slab)
	kmem_cache_init_late();

	/*
	 * HACK ALERT! This is early. We're enabling the console before
	 * we've done PCI setups etc, and console_init() must be aware of
	 * this. But we do want output early, in case something goes wrong.
	 */
	//初始化控制台。
	// call console initcalls to initialize the console device, usually it's tty device.
	console_init();
	if (panic_later)
		panic("Too many boot %s vars at `%s'", panic_later,
		      panic_param);

	//输出锁依赖信息
	lockdep_init();

	/*
	 * Need to run this when irqs are enabled, because it wants
	 * to self-test [hard/soft]-irqs on/off lock inversion bugs
	 * too:
	 */
	locking_selftest();//测试锁依赖模块

	/*
	 * This needs to be called before any devices perform DMA
	 * operations that might use the SWIOTLB bounce buffers. It will
	 * mark the bounce buffers as decrypted so that their usage will
	 * not cause "plain-text" data to be decrypted when accessed.
	 */
	mem_encrypt_init();

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start && !initrd_below_start_ok &&
	    page_to_pfn(virt_to_page((void *)initrd_start)) < min_low_pfn) {
		pr_crit("initrd overwritten (0x%08lx < 0x%08lx) - disabling it.\n",
		    page_to_pfn(virt_to_page((void *)initrd_start)),
		    min_low_pfn);
		initrd_start = 0;
	}
#endif
	kmemleak_init();
	//设置pageset，即每个zone上面的每cpu页面缓存
	// allocate and initialize per cpu pagesets
	setup_per_cpu_pageset();
	//将16M以上的内存节点指定为交叉节点，并设置当前进程的模式
    // allocate caches and do initialization for NUMA memory policy
	numa_policy_init();
	acpi_early_init();
	//延后的时钟初始化操作
	// default late_time_init is NULL. archs can override it
	if (late_time_init)
		late_time_init(); // architecture-specific
	sched_clock_init();
	//测试BogoMIPS值，计算每个jiffy内消耗掉多少CPU周期。
	// calibrate the delay loop
	calibrate_delay();
	//快速执行pid分配，分配pid位图并生成slab缓存.
	// initialize PID map for initial PID namespace
	pid_idr_init();
	//为anon_vma生成slab分配器。
	// allocate a cache for "anon_vma" (anonymous memory), http://lwn.net/Kernel/Index/#anon_vma
	anon_vma_init();
	// initialize ACPI subsystem and populate the ACPI namespace
#ifdef CONFIG_X86
    // Extensible Firmware Interface
	if (efi_enabled(EFI_RUNTIME_SERVICES))
		efi_enter_virtual_mode();
#endif
	thread_stack_cache_init();
	//审计初始化，用于确定对象是否有执行某种操作的资格。
	cred_init();
	//初始化fork中使用的资源相关数据结构。
	fork_init();
	//用于生成进程管理所需要的slab管理器
	proc_caches_init();
	uts_ns_init();
	//初始化buffer,用于缓存从块设备中读取的块。为其构建slab缓存管理器。
	buffer_init();
	//密钥服务初始化。
	// initialize the authentication token and access key management
	key_init();
	//安全子系统初始化。
	security_init();
	// late init for kgdb
	dbg_late_init();
	// file system, including kernfs, sysfs, rootfs, mount tree
	vfs_caches_init();
	pagecache_init();
	//初始化以准备使用进程信号。
	signals_init();
	seq_file_init();
	/* rootfs populating might need page-writeback */
	//初始化页回写机制。
	// set the ratio limits for the dirty pages
	//注册proc文件系统并生成一些默认的proc文件
	proc_root_init();
	// mount pseudo-filesystem: nsfs
	nsfs_init();
	//初始化cpuset子系统。设置top_cpuset并将cpuset注册到文件系统。
	cpuset_init();
	//继续初始化cgroup，在proc中注册cgroup
	cgroup_init();
	//taskstats是向用户空间传递任务与进程的状态信息，初始化它的netlink接口。
	taskstats_init_early();
	//delayacct模块对任务的io延迟进行统计，用于分析。这里对其初始化。
	delayacct_init();

	/**
	 * CPU缺陷检查。
	 * 对arm架构来说，主要是测试写缓存别名。
	 */
	check_bugs();

    // enable ACPI subsystem ,arm不支持
	acpi_subsystem_init();
	arch_post_acpi_subsys_init();
	sfi_init_late();

	/* Do the rest non-__init'ed, we're now alive */
	//生成init进程和内核线程。
	arch_call_rest_init();
}

/* Call all constructor functions linked into the kernel. */
static void __init do_ctors(void)
{
#ifdef CONFIG_CONSTRUCTORS
	ctor_fn_t *fn = (ctor_fn_t *) __ctors_start;

	for (; fn < (ctor_fn_t *) __ctors_end; fn++)
		(*fn)();
#endif
}

#ifdef CONFIG_KALLSYMS
struct blacklist_entry {
	struct list_head next;
	char *buf;
};

static __initdata_or_module LIST_HEAD(blacklisted_initcalls);

static int __init initcall_blacklist(char *str)
{
	char *str_entry;
	struct blacklist_entry *entry;

	/* str argument is a comma-separated list of functions */
	do {
		str_entry = strsep(&str, ",");
		if (str_entry) {
			pr_debug("blacklisting initcall %s\n", str_entry);
			entry = memblock_alloc(sizeof(*entry),
					       SMP_CACHE_BYTES);
			entry->buf = memblock_alloc(strlen(str_entry) + 1,
						    SMP_CACHE_BYTES);
			strcpy(entry->buf, str_entry);
			list_add(&entry->next, &blacklisted_initcalls);
		}
	} while (str_entry);

	return 0;
}

static bool __init_or_module initcall_blacklisted(initcall_t fn)
{
	struct blacklist_entry *entry;
	char fn_name[KSYM_SYMBOL_LEN];
	unsigned long addr;

	if (list_empty(&blacklisted_initcalls))
		return false;

	addr = (unsigned long) dereference_function_descriptor(fn);
	sprint_symbol_no_offset(fn_name, addr);

	/*
	 * fn will be "function_name [module_name]" where [module_name] is not
	 * displayed for built-in init functions.  Strip off the [module_name].
	 */
	strreplace(fn_name, ' ', '\0');

	list_for_each_entry(entry, &blacklisted_initcalls, next) {
		if (!strcmp(fn_name, entry->buf)) {
			pr_debug("initcall %s blacklisted\n", fn_name);
			return true;
		}
	}

	return false;
}
#else
static int __init initcall_blacklist(char *str)
{
	pr_warn("initcall_blacklist requires CONFIG_KALLSYMS\n");
	return 0;
}

static bool __init_or_module initcall_blacklisted(initcall_t fn)
{
	return false;
}
#endif
__setup("initcall_blacklist=", initcall_blacklist);

static __init_or_module void
trace_initcall_start_cb(void *data, initcall_t fn)
{
	ktime_t *calltime = (ktime_t *)data;

	printk(KERN_DEBUG "calling  %pF @ %i\n", fn, task_pid_nr(current));
	*calltime = ktime_get();
}

static __init_or_module void
trace_initcall_finish_cb(void *data, initcall_t fn, int ret)
{
	ktime_t *calltime = (ktime_t *)data;
	ktime_t delta, rettime;
	unsigned long long duration;

	rettime = ktime_get();
	delta = ktime_sub(rettime, *calltime);
	duration = (unsigned long long) ktime_to_ns(delta) >> 10;
	printk(KERN_DEBUG "initcall %pF returned %d after %lld usecs\n",
		 fn, ret, duration);
}

static ktime_t initcall_calltime;

#ifdef TRACEPOINTS_ENABLED
static void __init initcall_debug_enable(void)
{
	int ret;

	ret = register_trace_initcall_start(trace_initcall_start_cb,
					    &initcall_calltime);
	ret |= register_trace_initcall_finish(trace_initcall_finish_cb,
					      &initcall_calltime);
	WARN(ret, "Failed to register initcall tracepoints\n");
}
# define do_trace_initcall_start	trace_initcall_start
# define do_trace_initcall_finish	trace_initcall_finish
#else
static inline void do_trace_initcall_start(initcall_t fn)
{
	if (!initcall_debug)
		return;
	trace_initcall_start_cb(&initcall_calltime, fn);
}
static inline void do_trace_initcall_finish(initcall_t fn, int ret)
{
	if (!initcall_debug)
		return;
	trace_initcall_finish_cb(&initcall_calltime, fn, ret);
}
#endif /* !TRACEPOINTS_ENABLED */

int __init_or_module do_one_initcall(initcall_t fn)
{
	int count = preempt_count();
	char msgbuf[64];
	int ret;

	if (initcall_blacklisted(fn))
		return -EPERM;

	do_trace_initcall_start(fn);
	ret = fn();
	do_trace_initcall_finish(fn, ret);

	msgbuf[0] = 0;

	if (preempt_count() != count) {
		sprintf(msgbuf, "preemption imbalance ");
		preempt_count_set(count);
	}
	if (irqs_disabled()) {
		strlcat(msgbuf, "disabled interrupts ", sizeof(msgbuf));
		local_irq_enable();
	}
	WARN(msgbuf[0], "initcall %pF returned with %s\n", fn, msgbuf);

	add_latent_entropy();
	return ret;
}


extern initcall_entry_t __initcall_start[];
extern initcall_entry_t __initcall0_start[];
extern initcall_entry_t __initcall1_start[];
extern initcall_entry_t __initcall2_start[];
extern initcall_entry_t __initcall3_start[];
extern initcall_entry_t __initcall4_start[];
extern initcall_entry_t __initcall5_start[];
extern initcall_entry_t __initcall6_start[];
extern initcall_entry_t __initcall7_start[];
extern initcall_entry_t __initcall_end[];

static initcall_entry_t *initcall_levels[] __initdata = {
	__initcall0_start,
	__initcall1_start,
	__initcall2_start,
	__initcall3_start,
	__initcall4_start,
	__initcall5_start,
	__initcall6_start,
	__initcall7_start,
	__initcall_end,
};

/* Keep these in sync with initcalls in include/linux/init.h */
static const char *initcall_level_names[] __initdata = {
	"pure",
	"core",
	"postcore",
	"arch",
	"subsys",
	"fs",
	"device",
	"late",
};

static void __init do_initcall_level(int level)
{
	initcall_entry_t *fn;

	strcpy(initcall_command_line, saved_command_line);
	parse_args(initcall_level_names[level],
		   initcall_command_line, __start___param,
		   __stop___param - __start___param,
		   level, level,
		   NULL, &repair_env_string);

	trace_initcall_level(initcall_level_names[level]);
	for (fn = initcall_levels[level]; fn < initcall_levels[level+1]; fn++)
		do_one_initcall(initcall_from_entry(fn));
}

static void __init do_initcalls(void)
{
	int level;
    /* 执行介於Symbol early_initcall_end与initcall_end之间的函式呼叫 */
	for (level = 0; level < ARRAY_SIZE(initcall_levels) - 1; level++)
		do_initcall_level(level);
}

/*
 * Ok, the machine is now initialized. None of the devices
 * have been touched yet, but the CPU subsystem is up and
 * running, and memory and process management works.
 *
 * Now we can finally start doing some real work..
 */
static void __init do_basic_setup(void)
{
	//初始化cpuset子系统的top_cpuset
	cpuset_init_smp();
	shmem_init();
    // init driver model. (kobject, kset)
	driver_init();
	//初始化 “/proc/irq”与其下的File Nodes.
	init_irq_proc();
    /* 执行位於Symbol ctors_start 到 ctors_end间属於Section “.ctors” 的Constructor函式 */
	do_ctors();
	//允许khelper workqueue生效，这个队列允许用户态为内核态执行一些辅助工作。
	usermodehelper_enable();
	//执行介於Symbol __early_initcall_end与__initcall_end之间的函式呼叫
	// call init functions in .initcall[0~9].init sections
	do_initcalls();
}
/* 执行Symbol中 initcall_start与early_initcall_end之间的函数 */
static void __init do_pre_smp_initcalls(void)
{
	initcall_entry_t *fn;

	trace_initcall_level("early");
	for (fn = __initcall_start; fn < __initcall0_start; fn++)
		do_one_initcall(initcall_from_entry(fn));
}

static int run_init_process(const char *init_filename)
{
	argv_init[0] = init_filename;
	pr_info("Run %s as init process\n", init_filename);
/*
	1号进程调用do_execve运行可执行程序init，
	并演变成用户态1号进程，即init进程
	一号内核进程调用execve()从文件/etc/inittab中加载可执行程序init并执行，
	这个过程并没有使用调用do_fork()，因此两个进程都是1号进程
*/
	return do_execve(getname_kernel(init_filename),
		(const char __user *const __user *)argv_init,
		(const char __user *const __user *)envp_init);
}

static int try_to_run_init_process(const char *init_filename)
{
	int ret;

	ret = run_init_process(init_filename);

	if (ret && ret != -ENOENT) {
		pr_err("Starting init: %s exists but couldn't execute it (error %d)\n",
		       init_filename, ret);
	}

	return ret;
}

static noinline void __init kernel_init_freeable(void);

#if defined(CONFIG_STRICT_KERNEL_RWX) || defined(CONFIG_STRICT_MODULE_RWX)
bool rodata_enabled __ro_after_init = true;
static int __init set_debug_rodata(char *str)
{
	return strtobool(str, &rodata_enabled);
}
__setup("rodata=", set_debug_rodata);
#endif

#ifdef CONFIG_STRICT_KERNEL_RWX
static void mark_readonly(void)
{
	if (rodata_enabled) {
		/*
		 * load_module() results in W+X mappings, which are cleaned up
		 * with call_rcu_sched().  Let's make sure that queued work is
		 * flushed so that we don't hit false positives looking for
		 * insecure pages which are W+X.
		 */
		rcu_barrier();
		mark_rodata_ro();
		rodata_test();
	} else
		pr_info("Kernel memory protection disabled.\n");
}
#else
static inline void mark_readonly(void)
{
	pr_warn("This architecture does not have kernel memory protection.\n");
}
#endif

/**
init进程应该是一个用户空间的进程, 但是这里却是通过
kernel_thread的方式创建的, 哪岂不是式一个永远运行在内核态的
内核线程么, 它是怎么演变为真正意义上用户空间的init进程的？

 1号kernel_init进程完成linux的各项配置(包括启动AP)后，
就会在/sbin,/etc,/bin寻找init程序来运行。
该init程序会替换kernel_init进程（注意：并不是创建一个新的进程来运行init程序，
而是一次变身，使用sys_execve函数改变核心进程的正文段，
将核心进程kernel_init转换成用户进程init），
此时处于内核态的1号kernel_init进程将会转换为用户空间内的1号进程init。
户进程init将根据/etc/inittab中提供的信息完成应用程序的初始化调用。
然后init进程会执行/bin/sh产生shell界面提供给用户来与Linux系统进行交互
调用init_post()创建用户模式1号进程

主处理器上的idle由原始进程(pid=0)演变而来。
从处理器上的idle由init进程fork得到，但是它们的pid都为0
init进程为每个从处理器(运行队列)创建出一个idle进程(pid=0)，
然后演化成/sbin/init。
*/
static int __ref kernel_init(void *unused)
{
	int ret;

	// 初始化 device, driver, rootfs, 掛載 /dev, /sys 等虛擬檔案系統目錄，開啟 /dev/console 做為訊息輸出
	// 不必在主核上运行。
	kernel_init_freeable();
	/* need to finish all async __init code before freeing the memory */
    /* 用以同步所有非同步函式呼叫的执行,在这函数中会等待
          List async_running与async_pending都清空后,才会返回.
          Asynchronously called functions主要设计用来加速Linux Kernel开机的效率,
          避免在开机流程中等待硬体反应延迟,影响到开机完成的时间 */
    // waits until all asynchronous function calls have been done
	async_synchronize_full();
	ftrace_free_init_mem();
	free_initmem();
	mark_readonly();
    /* 设置运行状态SYSTEM_RUNNING */
	/*
	 * Kernel mappings are now finalized - update the userspace page-table
	 * to finalize PTI.
	 */
	pti_finalize();

	system_state = SYSTEM_RUNNING;
	numa_default_policy();

	rcu_end_inkernel_boot();

	//根据boot参数，确定init进程是谁，并启动它。
	if (ramdisk_execute_command) {
		ret = run_init_process(ramdisk_execute_command);
		if (!ret)
			return 0;
		pr_err("Failed to execute %s (error %d)\n",
		       ramdisk_execute_command, ret);
	}

	/*
	 * We try each of these until one succeeds.
	 *
	 * The Bourne shell can be used instead of init if we are
	 * trying to recover a really broken machine.
	 */
	if (execute_command) {
		ret = run_init_process(execute_command);
		if (!ret)
			return 0;
		panic("Requested init %s failed (error %d).",
		      execute_command, ret);
	}
	if (!try_to_run_init_process("/sbin/init") ||
	    !try_to_run_init_process("/etc/init") ||
	    !try_to_run_init_process("/bin/init") ||
	    !try_to_run_init_process("/bin/sh"))
		return 0;

	panic("No working init found.  Try passing init= option to kernel. "
	      "See Linux Documentation/admin-guide/init.rst for guidance.");
}

static noinline void __init kernel_init_freeable(void)
{
	/*
	 * Wait until kthreadd is all set-up.
	 */
	//等待Kernel Thread kthreadd (PID=2)创建完毕kthreadd_done Signal。
	wait_for_completion(&kthreadd_done);

	/* Now the scheduler is fully set up and can do blocking allocations */
    /*
    __GFP_BITS_MASK;设置bitmask, 使得init进程可以使用PM并且允许I/O阻塞操作
    */
	gfp_allowed_mask = __GFP_BITS_MASK;

	/*
	 * init can allocate pages on any node
	 */
	//允许当前进程在任何节点分配内存
    // init进程可以分配物理页面
	set_mems_allowed(node_states[N_MEMORY]);

	//保存能执行cad的进程id,安全方面的考虑。
    /* 设置到目前运行进程init的pid号给cad_pid
        (cad_pid是用来接收ctrl-alt-del reboot signal的进程,
        如果设置C_A_D=1就表示可以处理来自ctl-alt-del的动作)，
        最后会调用 ctrl_alt_del(void)并确认C_A_D是否为1,
        确认完成后将执行cad_work=deferred_cad,执行kernel_restart */
	cad_pid = task_pid(current);

	//准备激活并使用其他CPU  设定支援的最大CPU数量
	smp_prepare_cpus(setup_max_cpus);

	workqueue_init();
	
	init_mm_internals();
/*
    会透过函式do_one_initcall,执行Symbol中 __initcall_start与__early_initcall_end之间的函数
*/
	/**
	 * 执行模块注册的初始化函数。
	 * 这些函数主要是初始化多核间调度需要的队列。
	 */
	do_pre_smp_initcalls();
	lockup_detector_init();
/*
    由Bootstrap处理器,进行Active多核心架构下其它的处理器
*/
	smp_init();
	//SMP调度初始化函数。
	sched_init_smp();

	page_alloc_init_late();
	/* Initialize page ext after all struct pages are initialized. */
	page_ext_init();

	//几个子系统的初始化。
	do_basic_setup();

	/* Open the /dev/console on the rootfs, this should never fail */
    /*
    实例在fs/fcntl.c中,”SYSCALL_DEFINE1(dup, unsigned int, fildes)”,
    在这会连续执行两次sys_dup,复制两个sys_open开啟/dev/console所產生的档案描述0 (也就是会多生出两个1与2),
    只是都对应到”/dev/console”,我们在System V streams下的Standard Stream一般而言会有如下的对应
    0:Standard input (stdin)
    1:Standard output (stdout)
    2:Standard error (stderr)
    */
	if (ksys_open((const char __user *) "/dev/console", O_RDWR, 0) < 0)
		pr_err("Warning: unable to open an initial console.\n");

	(void) ksys_dup(0);
	(void) ksys_dup(0);
	/*
	 * check if there is an early userspace init.  If yes, let it do all
	 * the work
	 */
	//确定初始化进程名称
	if (!ramdisk_execute_command)
		ramdisk_execute_command = "/init";

    /* 如果sys_access确认档案ramdisk_execute_command 失败,
          就把ramdisk_execute_command 设定為0,然后呼叫prepare_namespace去mount root FileSystem. */
	if (ksys_access((const char __user *)
			ramdisk_execute_command, 0) != 0) {
		ramdisk_execute_command = NULL;
		prepare_namespace();
	}

	/*
	 * Ok, we have completed the initial bootup, and
	 * we're essentially up and running. Get rid of the
	 * initmem segments and start the user-mode stuff..
	 *
	 * rootfs is available now, try loading the public keys
	 * and default modules
	 */
    /* 至此我们初始化工作完成, 文件系统也已经准备好了，
          那么接下来加载 load integrity keys hook*/
	integrity_load_keys();


}
