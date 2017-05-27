#ifndef __LINUX_COMPILER_H
#define __LINUX_COMPILER_H

/* 
这个宏通过在编译器中用-D选项中加入，参数AFLAGS也包含该宏定义。
在汇编时编译器会定义__ASSEMBLY__为1。
*/
#ifndef __ASSEMBLY__

/*
    Documentation/sparse.txt
    __CHECKER__宏在通过Sparse（Semantic Parser for C）工具对内核代码进行检查时会定义的。
    在使用make C=1或C=2时便会调用该工具，这个工具可以检查在代码中声明了sparse所能检查到的相关属性的内核函数和变量
*/
#ifdef __CHECKER__
/*
__user特性用来修饰一个变量的地址，该变量必须是非解除参考(no dereference)即地址是有效的，
并且变量所在的地址空间必须为1，这里为(address_space(1))，用户地址空间。
sparse把地址空间分为3部分，
0表示普通地址空间，对内核来说就是地址空间。
1表示用户地址空间。
2表示设备地址映射空间，即设备寄存器的地址空间。
*/
# define __user		__attribute__((noderef, address_space(1)))
/*
__kernel特性修饰变量为内核地址，为内核代码里面默认的地址空间。
*/
# define __kernel	__attribute__((address_space(0)))
/*
__safe特性声明该变量为安全变量，这是为了避免在内核函数未对传入的参数进行校验就使用的情况下，
会导致编译器对其报错或输出告警信息。        通过该特性说明该变量不可能为空。
*/
# define __safe		__attribute__((safe))
/*
__force特性声明该变量是可以强制类型转换的。
*/
# define __force	__attribute__((force))
/*
__nocast声明该变量参数类型与实际参数类型要一致才可以。
*/
# define __nocast	__attribute__((nocast))
/*
指针地址必须在设备地址空间
*/
# define __iomem	__attribute__((noderef, address_space(2)))
# define __must_hold(x)	__attribute__((context(x,1,1)))
/*
__acquires为函数属性定义的修饰，表示函数内，该参数的引用计数值从1变为0。
*/
# define __acquires(x)	__attribute__((context(x,0,1)))
/*
__releases与__acquires相反，这一对修饰符用于Sparse在静态代码检测时，检查调用的次数和匹配请求，
经常用于检测lock的获取和释放。
*/
# define __releases(x)	__attribute__((context(x,1,0)))
/*
__acquire表示增加变量x的计数，增加量为1。
*/
# define __acquire(x)	__context__(x,1)
/*
__release表示减少变量x的计数，减少量为1。这一对与上面的那一对是一样，只是这一对用在函数的执行过程中，
都用于检查代码中出现不平衡的状况。
*/
# define __release(x)	__context__(x,-1)
/*
__cond_lock用于表示条件锁，当c这个值不为0时，计数值加1，并返回1。
*/
# define __cond_lock(x,c)	((c) ? ({ __acquire(x); 1; }) : 0)
# define __percpu	__attribute__((noderef, address_space(3)))
#ifdef CONFIG_SPARSE_RCU_POINTER
# define __rcu		__attribute__((noderef, address_space(4)))
#else /* CONFIG_SPARSE_RCU_POINTER */
# define __rcu
#endif /* CONFIG_SPARSE_RCU_POINTER */
# define __private	__attribute__((noderef))
/*
__chk_user_ptr和__chk_io_ptr在这里只声明函数，没有函数体，
目的就是在编译过程中Sparse能够捕捉到编译错误，检查参数的类型。
*/
extern void __chk_user_ptr(const volatile void __user *);
extern void __chk_io_ptr(const volatile void __iomem *);
# define ACCESS_PRIVATE(p, member) (*((typeof((p)->member) __force *) &(p)->member))
#else /* __CHECKER__ */
# ifdef STRUCTLEAK_PLUGIN
#  define __user __attribute__((user))
# else
# define __user
# endif
# define __kernel
# define __safe
# define __force
# define __nocast
# define __iomem
# define __chk_user_ptr(x) (void)0
# define __chk_io_ptr(x) (void)0
# define __builtin_warning(x, y...) (1)
# define __must_hold(x)
# define __acquires(x)
# define __releases(x)
# define __acquire(x) (void)0
# define __release(x) (void)0
# define __cond_lock(x,c) (c)
# define __percpu
# define __rcu
# define __private
# define ACCESS_PRIVATE(p, member) ((p)->member)
#endif /* __CHECKER__ */

/* Indirect macros required for expanded argument pasting, eg. __LINE__. */
#define ___PASTE(a,b) a##b
#define __PASTE(a,b) ___PASTE(a,b)

#ifdef __KERNEL__

#ifdef __GNUC__
#include <linux/compiler-gcc.h>
#endif

/*
宏notrace的定义，这个宏用于修饰函数，说明该函数不被跟踪。
这里所说的跟踪是gcc一个很重要的特性，只要在编译时打开相关的跟踪选择，编译器会加入一些特性，
使得程序在执行完后，可以通过工具（如Graphviz）来查看函数的调用过程。
而对于内核来说，内部采用了ftrace机制，而不采用trace的特性。
*/
#if defined(CC_USING_HOTPATCH) && !defined(__CHECKER__)
#define notrace __attribute__((hotpatch(0,0)))
#else
#define notrace __attribute__((no_instrument_function))
#endif

/* Intel compiler defines __GNUC__. So we will overwrite implementations
 * coming from above header files here
 */
#ifdef __INTEL_COMPILER
# include <linux/compiler-intel.h>
#endif

/* Clang compiler defines __GNUC__. So we will overwrite implementations
 * coming from above header files here
 */
#ifdef __clang__
#include <linux/compiler-clang.h>
#endif

/*
 * Generic compiler-dependent macros required for kernel
 * build go below this comment. Actual compiler/compiler version
 * specific implementations come from the above header files
 */

struct ftrace_branch_data {
	const char *func;
	const char *file;
	unsigned line;
	union {
		struct {
			unsigned long correct;
			unsigned long incorrect;
		};
		struct {
			unsigned long miss;
			unsigned long hit;
		};
		unsigned long miss_hit[2];
	};
};

/*
 * Note: DISABLE_BRANCH_PROFILING can be used by special lowlevel code
 * to disable branch tracing on a per file basis.
 */
#if defined(CONFIG_TRACE_BRANCH_PROFILING) \
    && !defined(DISABLE_BRANCH_PROFILING) && !defined(__CHECKER__)
void ftrace_likely_update(struct ftrace_branch_data *f, int val, int expect);
/*
__builtin_expect 用于为编译器提供分支预测信息，
其返回值是整数表达式EXP 的值，C 的值必须是编译时常数

likely_notrace和unlikely_notrace宏使用__builtin_expect函数，
__builtin_expect告诉编译器程序设计者期望的比较结果，以便编译器对代码进行优化，
改变汇编代码中的判断跳转语句
*/
#define likely_notrace(x)	__builtin_expect(!!(x), 1)
#define unlikely_notrace(x)	__builtin_expect(!!(x), 0)
/*
struct ftrace_branch_data结构体用于记录ftrace branch的trace记录

__branch_check__宏，记录当前trace点，并利用ftrace_likely_update记录likely判断的正确性，
并将结果保存在ring buffer中，之后用户可以通过ftrace的debugfs接口读取分支预测的相关信息。
从而优调整代码，优化性能。
*/
#define __branch_check__(x, expect) ({					\
			int ______r;					\
			static struct ftrace_branch_data		\
				__attribute__((__aligned__(4)))		\
				__attribute__((section("_ftrace_annotated_branch"))) \
				______f = {				\
				.func = __func__,			\
				.file = __FILE__,			\
				.line = __LINE__,			\
			};						\
			______r = likely_notrace(x);			\
			ftrace_likely_update(&______f, ______r, expect); \
			______r;					\
		})

/*
 * Using __builtin_constant_p(x) to ignore cases where the return
 * value is always the same.  This idea is taken from a similar patch
 * written by Daniel Walker.
 */
/*
 __builtin_constant_p也是GCC的内置函数，函数原型为int  __builtin_constant_p(exp)，
 用于判断表达式exp在编译时是否是一个常量，如果是则函数的值为整数1，否则为0
*/
# ifndef likely
#  define likely(x)	(__builtin_constant_p(x) ? !!(x) : __branch_check__(x, 1))
# endif
# ifndef unlikely
#  define unlikely(x)	(__builtin_constant_p(x) ? !!(x) : __branch_check__(x, 0))
# endif

#ifdef CONFIG_PROFILE_ALL_BRANCHES
/*
 * "Define 'is'", Bill Clinton
 * "Define 'if'", Steven Rostedt
 */
#define if(cond, ...) __trace_if( (cond , ## __VA_ARGS__) )
#define __trace_if(cond) \
	if (__builtin_constant_p(!!(cond)) ? !!(cond) :			\
	({								\
		int ______r;						\
		static struct ftrace_branch_data			\
			__attribute__((__aligned__(4)))			\
			__attribute__((section("_ftrace_branch")))	\
			______f = {					\
				.func = __func__,			\
				.file = __FILE__,			\
				.line = __LINE__,			\
			};						\
		______r = !!(cond);					\
		______f.miss_hit[______r]++;					\
		______r;						\
	}))
#endif /* CONFIG_PROFILE_ALL_BRANCHES */

#else
# define likely(x)	__builtin_expect(!!(x), 1)
# define unlikely(x)	__builtin_expect(!!(x), 0)
#endif

/* Optimization barrier */
#ifndef barrier
# define barrier() __memory_barrier()
#endif

#ifndef barrier_data
# define barrier_data(ptr) barrier()
#endif

/* Unreachable code */
#ifndef unreachable
# define unreachable() do { } while (1)
#endif

/*
 * KENTRY - kernel entry point
 * This can be used to annotate symbols (functions or data) that are used
 * without their linker symbol being referenced explicitly. For example,
 * interrupt vector handlers, or functions in the kernel image that are found
 * programatically.
 *
 * Not required for symbols exported with EXPORT_SYMBOL, or initcalls. Those
 * are handled in their own way (with KEEP() in linker scripts).
 *
 * KENTRY can be avoided if the symbols in question are marked as KEEP() in the
 * linker script. For example an architecture could KEEP() its entire
 * boot/exception vector code rather than annotate each function and data.
 */
#ifndef KENTRY
# define KENTRY(sym)						\
	extern typeof(sym) sym;					\
	static const unsigned long __kentry_##sym		\
	__used							\
	__attribute__((section("___kentry" "+" #sym ), used))	\
	= (unsigned long)&sym;
#endif

#ifndef RELOC_HIDE
# define RELOC_HIDE(ptr, off)					\
  ({ unsigned long __ptr;					\
     __ptr = (unsigned long) (ptr);				\
    (typeof(ptr)) (__ptr + (off)); })
#endif

#ifndef OPTIMIZER_HIDE_VAR
#define OPTIMIZER_HIDE_VAR(var) barrier()
#endif

/* Not-quite-unique ID. */
#ifndef __UNIQUE_ID
# define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __LINE__)
#endif

#include <uapi/linux/types.h>

#define __READ_ONCE_SIZE						\
({									\
	switch (size) {							\
	case 1: *(__u8 *)res = *(volatile __u8 *)p; break;		\
	case 2: *(__u16 *)res = *(volatile __u16 *)p; break;		\
	case 4: *(__u32 *)res = *(volatile __u32 *)p; break;		\
	case 8: *(__u64 *)res = *(volatile __u64 *)p; break;		\
	default:							\
		barrier();						\
		__builtin_memcpy((void *)res, (const void *)p, size);	\
		barrier();						\
	}								\
})

static __always_inline
void __read_once_size(const volatile void *p, void *res, int size)
{
	__READ_ONCE_SIZE;
}

#ifdef CONFIG_KASAN
/*
 * This function is not 'inline' because __no_sanitize_address confilcts
 * with inlining. Attempt to inline it may cause a build failure.
 * 	https://gcc.gnu.org/bugzilla/show_bug.cgi?id=67368
 * '__maybe_unused' allows us to avoid defined-but-not-used warnings.
 */
static __no_sanitize_address __maybe_unused
void __read_once_size_nocheck(const volatile void *p, void *res, int size)
{
	__READ_ONCE_SIZE;
}
#else
static __always_inline
void __read_once_size_nocheck(const volatile void *p, void *res, int size)
{
	__READ_ONCE_SIZE;
}
#endif

static __always_inline void __write_once_size(volatile void *p, void *res, int size)
{
	switch (size) {
	case 1: *(volatile __u8 *)p = *(__u8 *)res; break;
	case 2: *(volatile __u16 *)p = *(__u16 *)res; break;
	case 4: *(volatile __u32 *)p = *(__u32 *)res; break;
	case 8: *(volatile __u64 *)p = *(__u64 *)res; break;
	default:
		barrier();
		__builtin_memcpy((void *)p, (const void *)res, size);
		barrier();
	}
}

/*
 * Prevent the compiler from merging or refetching reads or writes. The
 * compiler is also forbidden from reordering successive instances of
 * READ_ONCE, WRITE_ONCE and ACCESS_ONCE (see below), but only when the
 * compiler is aware of some particular ordering.  One way to make the
 * compiler aware of ordering is to put the two invocations of READ_ONCE,
 * WRITE_ONCE or ACCESS_ONCE() in different C statements.
 *
 * In contrast to ACCESS_ONCE these two macros will also work on aggregate
 * data types like structs or unions. If the size of the accessed data
 * type exceeds the word size of the machine (e.g., 32 bits or 64 bits)
 * READ_ONCE() and WRITE_ONCE() will fall back to memcpy(). There's at
 * least two memcpy()s: one for the __builtin_memcpy() and then one for
 * the macro doing the copy of variable - '__u' allocated on the stack.
 *
 * Their two major use cases are: (1) Mediating communication between
 * process-level code and irq/NMI handlers, all running on the same CPU,
 * and (2) Ensuring that the compiler does not  fold, spindle, or otherwise
 * mutilate accesses that either do not require ordering or that interact
 * with an explicit memory barrier or atomic instruction that provides the
 * required ordering.
 */

#define __READ_ONCE(x, check)						\
({									\
	union { typeof(x) __val; char __c[1]; } __u;			\
	if (check)							\
		__read_once_size(&(x), __u.__c, sizeof(x));		\
	else								\
		__read_once_size_nocheck(&(x), __u.__c, sizeof(x));	\
	__u.__val;							\
})
#define READ_ONCE(x) __READ_ONCE(x, 1)

/*
 * Use READ_ONCE_NOCHECK() instead of READ_ONCE() if you need
 * to hide memory access from KASAN.
 */
#define READ_ONCE_NOCHECK(x) __READ_ONCE(x, 0)

#define WRITE_ONCE(x, val) \
({							\
	union { typeof(x) __val; char __c[1]; } __u =	\
		{ .__val = (__force typeof(x)) (val) }; \
	__write_once_size(&(x), __u.__c, sizeof(x));	\
	__u.__val;					\
})

#endif /* __KERNEL__ */

#endif /* __ASSEMBLY__ */

#ifdef __KERNEL__
/*
 * Allow us to mark functions as 'deprecated' and have gcc emit a nice
 * warning for each use, in hopes of speeding the functions removal.
 * Usage is:
 * 		int __deprecated foo(void)
 */
#ifndef __deprecated
# define __deprecated		/* unimplemented */
#endif

#ifdef MODULE
#define __deprecated_for_modules __deprecated
#else
#define __deprecated_for_modules
#endif

#ifndef __must_check
#define __must_check
#endif

#ifndef CONFIG_ENABLE_MUST_CHECK
#undef __must_check
#define __must_check
#endif
#ifndef CONFIG_ENABLE_WARN_DEPRECATED
#undef __deprecated
#undef __deprecated_for_modules
#define __deprecated
#define __deprecated_for_modules
#endif

#ifndef __malloc
#define __malloc
#endif

/*
 * Allow us to avoid 'defined but not used' warnings on functions and data,
 * as well as force them to be emitted to the assembly file.
 *
 * As of gcc 3.4, static functions that are not marked with attribute((used))
 * may be elided from the assembly file.  As of gcc 3.4, static data not so
 * marked will not be elided, but this may change in a future gcc version.
 *
 * NOTE: Because distributions shipped with a backported unit-at-a-time
 * compiler in gcc 3.3, we must define __used to be __attribute__((used))
 * for gcc >=3.3 instead of 3.4.
 *
 * In prior versions of gcc, such functions and data would be emitted, but
 * would be warned about except with attribute((unused)).
 *
 * Mark functions that are referenced only in inline assembly as __used so
 * the code is emitted even though it appears to be unreferenced.
 */
#ifndef __used
# define __used			/* unimplemented */
#endif

#ifndef __maybe_unused
# define __maybe_unused		/* unimplemented */
#endif

#ifndef __always_unused
# define __always_unused	/* unimplemented */
#endif

#ifndef noinline
#define noinline
#endif

/*
 * Rather then using noinline to prevent stack consumption, use
 * noinline_for_stack instead.  For documentation reasons.
 */
#define noinline_for_stack noinline

#ifndef __always_inline
#define __always_inline inline
#endif

#endif /* __KERNEL__ */

/*
 * From the GCC manual:
 *
 * Many functions do not examine any values except their arguments,
 * and have no effects except the return value.  Basically this is
 * just slightly more strict class than the `pure' attribute above,
 * since function is not allowed to read global memory.
 *
 * Note that a function that has pointer arguments and examines the
 * data pointed to must _not_ be declared `const'.  Likewise, a
 * function that calls a non-`const' function usually must not be
 * `const'.  It does not make sense for a `const' function to return
 * `void'.
 */
#ifndef __attribute_const__
# define __attribute_const__	/* unimplemented */
#endif

#ifndef __latent_entropy
# define __latent_entropy
#endif

/*
 * Tell gcc if a function is cold. The compiler will assume any path
 * directly leading to the call is unlikely.
 */

#ifndef __cold
#define __cold
#endif

/* Simple shorthand for a section definition */
#ifndef __section
# define __section(S) __attribute__ ((__section__(#S)))
#endif

#ifndef __visible
#define __visible
#endif

/*
 * Assume alignment of return value.
 */
#ifndef __assume_aligned
#define __assume_aligned(a, ...)
#endif


/* Are two types/vars the same type (ignoring qualifiers)? */
#ifndef __same_type
# define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))
#endif

/* Is this type a native word size -- useful for atomic operations */
#ifndef __native_word
# define __native_word(t) (sizeof(t) == sizeof(char) || sizeof(t) == sizeof(short) || sizeof(t) == sizeof(int) || sizeof(t) == sizeof(long))
#endif

/* Compile time object size, -1 for unknown */
#ifndef __compiletime_object_size
# define __compiletime_object_size(obj) -1
#endif
#ifndef __compiletime_warning
# define __compiletime_warning(message)
#endif
#ifndef __compiletime_error
# define __compiletime_error(message)
/*
 * Sparse complains of variable sized arrays due to the temporary variable in
 * __compiletime_assert. Unfortunately we can't just expand it out to make
 * sparse see a constant array size without breaking compiletime_assert on old
 * versions of GCC (e.g. 4.2.4), so hide the array from sparse altogether.
 */
# ifndef __CHECKER__
#  define __compiletime_error_fallback(condition) \
	do { ((void)sizeof(char[1 - 2 * condition])); } while (0)
# endif
#endif
#ifndef __compiletime_error_fallback
# define __compiletime_error_fallback(condition) do { } while (0)
#endif

#define __compiletime_assert(condition, msg, prefix, suffix)		\
	do {								\
		bool __cond = !(condition);				\
		extern void prefix ## suffix(void) __compiletime_error(msg); \
		if (__cond)						\
			prefix ## suffix();				\
		__compiletime_error_fallback(__cond);			\
	} while (0)

#define _compiletime_assert(condition, msg, prefix, suffix) \
	__compiletime_assert(condition, msg, prefix, suffix)

/**
 * compiletime_assert - break build and emit msg if condition is false
 * @condition: a compile-time constant condition to check
 * @msg:       a message to emit if condition is false
 *
 * In tradition of POSIX assert, this macro will break the build if the
 * supplied condition is *false*, emitting the supplied error message if the
 * compiler has support to do so.
 */
#define compiletime_assert(condition, msg) \
	_compiletime_assert(condition, msg, __compiletime_assert_, __LINE__)

#define compiletime_assert_atomic_type(t)				\
	compiletime_assert(__native_word(t),				\
		"Need native word sized stores/loads for atomicity.")

/*
 * Prevent the compiler from merging or refetching accesses.  The compiler
 * is also forbidden from reordering successive instances of ACCESS_ONCE(),
 * but only when the compiler is aware of some particular ordering.  One way
 * to make the compiler aware of ordering is to put the two invocations of
 * ACCESS_ONCE() in different C statements.
 *
 * ACCESS_ONCE will only work on scalar types. For union types, ACCESS_ONCE
 * on a union member will work as long as the size of the member matches the
 * size of the union and the size is smaller than word size.
 *
 * The major use cases of ACCESS_ONCE used to be (1) Mediating communication
 * between process-level code and irq/NMI handlers, all running on the same CPU,
 * and (2) Ensuring that the compiler does not  fold, spindle, or otherwise
 * mutilate accesses that either do not require ordering or that interact
 * with an explicit memory barrier or atomic instruction that provides the
 * required ordering.
 *
 * If possible use READ_ONCE()/WRITE_ONCE() instead.
 */
/*
 ACCESS_ONCE就是用来让c code告诉编译器，
 这里的内存访问不要优化,
 不要将多次内存访问合并,
 也不要将一次内存访问分拆成多次
 
 本身的实现就是增加了volatile这个关键字，
 它确保编译器每次访问变量x都是从内存中获取
*/
#define __ACCESS_ONCE(x) ({ \
	 __maybe_unused typeof(x) __var = (__force typeof(x)) 0; \
	(volatile typeof(x) *)&(x); })
#define ACCESS_ONCE(x) (*__ACCESS_ONCE(x))

/**
 * lockless_dereference() - safely load a pointer for later dereference
 * @p: The pointer to load
 *
 * Similar to rcu_dereference(), but for situations where the pointed-to
 * object's lifetime is managed by something other than RCU.  That
 * "something other" might be reference counting or simple immortality.
 *
 * The seemingly unused variable ___typecheck_p validates that @p is
 * indeed a pointer type by using a pointer to typeof(*p) as the type.
 * Taking a pointer to typeof(*p) again is needed in case p is void *.
 */
#define lockless_dereference(p) \
({ \
	typeof(p) _________p1 = READ_ONCE(p); \
	typeof(*(p)) *___typecheck_p __maybe_unused; \
	smp_read_barrier_depends(); /* Dependency order vs. p above. */ \
	(_________p1); \
})

/* Ignore/forbid kprobes attach on very low level functions marked by this attribute: */
#ifdef CONFIG_KPROBES
# define __kprobes	__attribute__((__section__(".kprobes.text")))
# define nokprobe_inline	__always_inline
#else
# define __kprobes
# define nokprobe_inline	inline
#endif
#endif /* __LINUX_COMPILER_H */
