/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_COMPILER_TYPES_H
#error "Please don't include <linux/compiler-gcc.h> directly, include <linux/compiler.h> instead."
#endif

/*
 * Common definitions for all gcc versions go here.
 */
#define GCC_VERSION (__GNUC__ * 10000		\
		     + __GNUC_MINOR__ * 100	\
		     + __GNUC_PATCHLEVEL__)

#if GCC_VERSION < 40600
# error Sorry, your compiler is too old - please upgrade it.
#endif

/* Optimization barrier */

/* The "volatile" is due to gcc bugs */
#define barrier() __asm__ __volatile__("": : :"memory")
/*
 * This version is i.e. to prevent dead stores elimination on @ptr
 * where gcc and llvm may behave differently when otherwise using
 * normal barrier(): while gcc behavior gets along with a normal
 * barrier(), llvm needs an explicit input variable to be assumed
 * clobbered. The issue is as follows: while the inline asm might
 * access any memory it wants, the compiler could have fit all of
 * @ptr into memory registers instead, and since @ptr never escaped
 * from that, it proved that the inline asm wasn't touching any of
 * it. This version works well with both compilers, i.e. we're telling
 * the compiler that the inline asm absolutely may see the contents
 * of @ptr. See also: https://llvm.org/bugs/show_bug.cgi?id=15495
 */
#define barrier_data(ptr) __asm__ __volatile__("": :"r"(ptr) :"memory")

/*
 * This macro obfuscates arithmetic on a variable address so that gcc
 * shouldn't recognize the original var, and make assumptions about it.
 *
 * This is needed because the C standard makes it undefined to do
 * pointer arithmetic on "objects" outside their boundaries and the
 * gcc optimizers assume this is the case. In particular they
 * assume such arithmetic does not wrap.
 *
 * A miscompilation has been observed because of this on PPC.
 * To work around it we hide the relationship of the pointer and the object
 * using this macro.
 *
 * Versions of the ppc64 compiler before 4.1 had a bug where use of
 * RELOC_HIDE could trash r30. The bug can be worked around by changing
 * the inline assembly constraint from =g to =r, in this particular
 * case either is valid.
 */
#define RELOC_HIDE(ptr, off)						\
({									\
	unsigned long __ptr;						\
	__asm__ ("" : "=r"(__ptr) : "0"(ptr));				\
	(typeof(ptr)) (__ptr + (off));					\
})

/* Make the optimizer believe the variable can be manipulated arbitrarily. */
#define OPTIMIZER_HIDE_VAR(var)						\
	__asm__ ("" : "=r" (var) : "0" (var))

#ifdef __CHECKER__
#define __must_be_array(a)	0
#else
/* &a[0] degrades to a pointer: a different type from an array */
#define __must_be_array(a)	BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))
#endif

/*
 * Force always-inline if the user requests it so via the .config,
 * or if gcc is too old:
 */
#if !defined(CONFIG_ARCH_SUPPORTS_OPTIMIZED_INLINING) ||		\
    !defined(CONFIG_OPTIMIZE_INLINING) || (__GNUC__ < 4)
#define inline		inline		__attribute__((always_inline)) notrace
#define __inline__	__inline__	__attribute__((always_inline)) notrace
#define __inline	__inline	__attribute__((always_inline)) notrace
#else
/* A lot of inline functions can cause havoc with function tracing */
#define inline		inline		notrace
#define __inline__	__inline__	notrace
#define __inline	__inline	notrace
#endif

#define __always_inline	inline __attribute__((always_inline))
#define  noinline	__attribute__((noinline))
/*
函数标记为deprecated，gcc在编译时会产生警告，提示尽快把这个函数移除。
*/
#define __deprecated	__attribute__((deprecated))
/*
__packed用于修饰struct，union，enum类型定义，使得修饰后的类型占用最少的内存空间。
对于类型为enum时，会选择采用最小内存的int指针。
*/
#define __packed	__attribute__((packed))
/*
__weak修改符会将相应的声明的为弱符号，这经常会用在库函数里面，
在库函数里面定义的弱函数可以在用户的应用的代码中的实现覆盖它。
*/
#define __weak		__attribute__((weak))
#define __alias(symbol)	__attribute__((alias(#symbol)))

/*
 * it doesn't make sense on ARM (currently the only user of __naked)
 * to trace naked functions because then mcount is called without
 * stack and frame pointer being set up and there is no chance to
 * restore the lr register to the value before mcount was called.
 *
 * The asm() bodies of naked functions often depend on standard calling
 * conventions, therefore they must be noinline and noclone.
 *
 * GCC 4.[56] currently fail to enforce this, so we must do so ourselves.
 * See GCC PR44290.
 */
 /*
 naked属性使编译器不产生函数的入口和出口，只编译函数体。
 naked把应用程序控制器交给了用户，怎样保存函数的上下文都由用户决定
*/
#define __naked		__attribute__((naked)) noinline __noclone notrace
/*
noruten属性说明该函数不会返回
*/
#define __noreturn	__attribute__((noreturn))

/*
 * From the GCC manual:
 *
 * Many functions have no effects except the return value and their
 * return value depends only on the parameters and/or global
 * variables.  Such a function can be subject to common subexpression
 * elimination and loop optimization just as an arithmetic operator
 * would be.
 * [...]
 */
 /*
 pure属性修饰函数，说明该函数的输出完全依赖于函数的输入或全局的变量，
 这样的函数容易被通用的子表达式替换或优化成数学运算。
 */
#define __pure			__attribute__((pure))
/*  aligend为内存对齐修饰符 */
#define __aligned(x)		__attribute__((aligned(x)))
#define __printf(a, b)		__attribute__((format(printf, a, b)))
#define __scanf(a, b)		__attribute__((format(scanf, a, b)))
#define __attribute_const__	__attribute__((__const__))
/*
__maybe_unused 修饰该函数可能不会被使用，gcc不会为这个函数产生警告。
*/
#define __maybe_unused		__attribute__((unused))
#define __always_unused		__attribute__((unused))

/* gcc version specific checks */

#if GCC_VERSION < 30200
# error Sorry, your compiler is too old - please upgrade it.
#endif

#if GCC_VERSION < 30300
# define __used			__attribute__((__unused__))
#else
# define __used			__attribute__((__used__))
#endif

#ifdef CONFIG_GCOV_KERNEL
# if GCC_VERSION < 30400
#   error "GCOV profiling support for gcc versions below 3.4 not included"
# endif /* __GNUC_MINOR__ */
#endif /* CONFIG_GCOV_KERNEL */

#if GCC_VERSION >= 30400
/*
利用gcc编译器的_attribute_给函数加上了属性，这个属性的作用是： 
让编译器检查所有调用者是否都检查函数的结果。
这确保调用者适当地检验函数结果，从而能够适当地处理错误。
也就是说，调用者在拿到结果后必须要进行检查是否返回了正确的结构，
否则编译器会给出提示信息
*/

/*
__must_check宏的作用是告诉编译器当调用者调用这个函数时不使用返回结果产生一个告警。
*/
#define __must_check		__attribute__((warn_unused_result))
#define __malloc		__attribute__((__malloc__))
#endif

#if GCC_VERSION >= 40000

/* GCC 4.1.[01] miscompiles __weak */
#ifdef __KERNEL__
# if GCC_VERSION >= 40100 &&  GCC_VERSION <= 40101
#  error Your version of gcc miscompiles the __weak directive
# endif
#endif

#define __used			__attribute__((__used__))
#define __compiler_offsetof(a, b)					\
	__builtin_offsetof(a, b)

#if GCC_VERSION >= 40100
# define __compiletime_object_size(obj) __builtin_object_size(obj, 0)
#endif

#if GCC_VERSION >= 40300
/* Mark functions as cold. gcc will assume any path leading to a call
 * to them will be unlikely.  This means a lot of manual unlikely()s
 * are unnecessary now for any paths leading to the usual suspects
 * like BUG(), printk(), panic() etc. [but let's keep them for now for
 * older compilers]
 *
 * Early snapshots of gcc 4.3 don't support this and we can't detect this
 * in the preprocessor, but we can live with this because they're unreleased.
 * Maketime probing would be overkill here.
 *
 * gcc also has a __attribute__((__hot__)) to move hot functions into
 * a special section, but I don't see any sense in this right now in
 * the kernel context
 */
 /*
 作用是告诉编译器，这个函数可能不怎么用到，编译器会将这一类的函数放在速度低些的
 某个子section里面，而把非__cold函数放在速度比较优的section里。
*/
#define __cold			__attribute__((__cold__))

#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)

#ifndef __CHECKER__
#define __compiletime_warning(message) __attribute__((warning(message)))
#define __compiletime_error(message) __attribute__((error(message)))

#ifdef LATENT_ENTROPY_PLUGIN
#define __latent_entropy __attribute__((latent_entropy))
#endif
#endif /* __CHECKER__ */

/*
 * calling noreturn functions, __builtin_unreachable() and __builtin_trap()
 * confuse the stack allocation in gcc, leading to overly large stack
 * frames, see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=82365
 *
 * Adding an empty inline assembly before it works around the problem
 */
#define barrier_before_unreachable() asm volatile("")

/*
 * Mark a position in code as unreachable.  This can be used to
 * suppress control flow warnings after asm blocks that transfer
 * control elsewhere.
 *
 * Early snapshots of gcc 4.5 don't support this and we can't detect
 * this in the preprocessor, but we can live with this because they're
 * unreleased.  Really, we need to have autoconf for the kernel.
 */
#define unreachable() \
	do {					\
		annotate_unreachable();		\
		barrier_before_unreachable();	\
		__builtin_unreachable();	\
	} while (0)

/* Mark a function definition as prohibited from being cloned. */
#define __noclone	__attribute__((__noclone__, __optimize__("no-tracer")))

#if defined(RANDSTRUCT_PLUGIN) && !defined(__CHECKER__)
#define __randomize_layout __attribute__((randomize_layout))
#define __no_randomize_layout __attribute__((no_randomize_layout))
/* This anon struct can add padding, so only enable it under randstruct. */
#define randomized_struct_fields_start	struct {
#define randomized_struct_fields_end	} __randomize_layout;
#endif

/*
 * When used with Link Time Optimization, gcc can optimize away C functions or
 * variables which are referenced only from assembly code.  __visible tells the
 * optimizer that something else uses this function or variable, thus preventing
 * this.
 */
#define __visible	__attribute__((externally_visible))

/* gcc version specific checks */

#if GCC_VERSION >= 40900 && !defined(__CHECKER__)
/*
 * __assume_aligned(n, k): Tell the optimizer that the returned
 * pointer can be assumed to be k modulo n. The second argument is
 * optional (default 0), so we use a variadic macro to make the
 * shorthand.
 *
 * Beware: Do not apply this to functions which may return
 * ERR_PTRs. Also, it is probably unwise to apply it to functions
 * returning extra information in the low bits (but in that case the
 * compiler should see some alignment anyway, when the return value is
 * massaged by 'flags = ptr & 3; ptr &= ~3;').
 */
#define __assume_aligned(a, ...) __attribute__((__assume_aligned__(a, ## __VA_ARGS__)))
#endif

/*
 * GCC 'asm goto' miscompiles certain code sequences:
 *
 *   http://gcc.gnu.org/bugzilla/show_bug.cgi?id=58670
 *
 * Work it around via a compiler barrier quirk suggested by Jakub Jelinek.
 *
 * (asm goto is automatically volatile - the naming reflects this.)
 */
#define asm_volatile_goto(x...)	do { asm goto(x); asm (""); } while (0)

/*
 * sparse (__CHECKER__) pretends to be gcc, but can't do constant
 * folding in __builtin_bswap*() (yet), so don't set these for it.
 */
#if defined(CONFIG_ARCH_USE_BUILTIN_BSWAP) && !defined(__CHECKER__)
#define __HAVE_BUILTIN_BSWAP32__
#define __HAVE_BUILTIN_BSWAP64__
#if GCC_VERSION >= 40800
#define __HAVE_BUILTIN_BSWAP16__
#endif
#endif /* CONFIG_ARCH_USE_BUILTIN_BSWAP && !__CHECKER__ */

#if GCC_VERSION >= 70000
#define KASAN_ABI_VERSION 5
#elif GCC_VERSION >= 50000
#define KASAN_ABI_VERSION 4
#elif GCC_VERSION >= 40902
#define KASAN_ABI_VERSION 3
#endif

#if GCC_VERSION >= 40902
/*
 * Tell the compiler that address safety instrumentation (KASAN)
 * should not be applied to that function.
 * Conflicts with inlining: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=67368
 */
#define __no_sanitize_address __attribute__((no_sanitize_address))
#endif

#if GCC_VERSION >= 50100
/*
 * Mark structures as requiring designated initializers.
 * https://gcc.gnu.org/onlinedocs/gcc/Designated-Inits.html
 */
#define __designated_init __attribute__((designated_init))
#define COMPILER_HAS_GENERIC_BUILTIN_OVERFLOW 1
#endif

#if !defined(__noclone)
#define __noclone	/* not needed */
#endif

#if !defined(__no_sanitize_address)
#define __no_sanitize_address
#endif

/*
 * Turn individual warnings and errors on and off locally, depending
 * on version.
 */
#define __diag_GCC(version, severity, s) \
	__diag_GCC_ ## version(__diag_GCC_ ## severity s)

/* Severity used in pragma directives */
#define __diag_GCC_ignore	ignored
#define __diag_GCC_warn		warning
#define __diag_GCC_error	error

#define __diag_str1(s)		#s
#define __diag_str(s)		__diag_str1(s)
#define __diag(s)		_Pragma(__diag_str(GCC diagnostic s))

#if GCC_VERSION >= 80000
#define __diag_GCC_8(s)		__diag(s)
#else
#define __diag_GCC_8(s)
#endif
