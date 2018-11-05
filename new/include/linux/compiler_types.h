#ifndef __LINUX_COMPILER_TYPES_H
#define __LINUX_COMPILER_TYPES_H

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
# define __rcu		__attribute__((noderef, address_space(4)))
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
#  define __user
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

/* Compiler specific macros. */
#ifdef __clang__
#include <linux/compiler-clang.h>
#elif defined(__INTEL_COMPILER)
#include <linux/compiler-intel.h>
#elif defined(__GNUC__)
/* The above compilers also define __GNUC__, so order is important here. */
#include <linux/compiler-gcc.h>
#else
#error "Unknown compiler"
#endif

/*
 * Some architectures need to provide custom definitions of macros provided
 * by linux/compiler-*.h, and can do so using asm/compiler.h. We include that
 * conditionally rather than using an asm-generic wrapper in order to avoid
 * build failures if any C compilation, which will include this file via an
 * -include argument in c_flags, occurs prior to the asm-generic wrappers being
 * generated.
 */
#ifdef CONFIG_HAVE_ARCH_COMPILER_H
#include <asm/compiler.h>
#endif

/*
 * Generic compiler-independent macros required for kernel
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

struct ftrace_likely_data {
	struct ftrace_branch_data	data;
	unsigned long			constant;
};

/* Don't. Just don't. */
#define __deprecated
#define __deprecated_for_modules

#endif /* __KERNEL__ */

#endif /* __ASSEMBLY__ */

/*
 * The below symbols may be defined for one or more, but not ALL, of the above
 * compilers. We don't consider that to be an error, so set them to nothing.
 * For example, some of them are for compiler specific plugins.
 */
#ifndef __designated_init
# define __designated_init
#endif

#ifndef __latent_entropy
# define __latent_entropy
#endif

#ifndef __randomize_layout
# define __randomize_layout __designated_init
#endif

#ifndef __no_randomize_layout
# define __no_randomize_layout
#endif

#ifndef randomized_struct_fields_start
# define randomized_struct_fields_start
# define randomized_struct_fields_end
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
#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))

/* Is this type a native word size -- useful for atomic operations */
#define __native_word(t) \
	(sizeof(t) == sizeof(char) || sizeof(t) == sizeof(short) || \
	 sizeof(t) == sizeof(int) || sizeof(t) == sizeof(long))

#ifndef __attribute_const__
#define __attribute_const__	__attribute__((__const__))
#endif

#ifndef __noclone
#define __noclone
#endif

/* Helpers for emitting diagnostics in pragmas. */
#ifndef __diag
#define __diag(string)
#endif

#ifndef __diag_GCC
#define __diag_GCC(version, severity, string)
#endif

#define __diag_push()	__diag(push)
#define __diag_pop()	__diag(pop)

#define __diag_ignore(compiler, version, option, comment) \
	__diag_ ## compiler(version, ignore, option)
#define __diag_warn(compiler, version, option, comment) \
	__diag_ ## compiler(version, warn, option)
#define __diag_error(compiler, version, option, comment) \
	__diag_ ## compiler(version, error, option)

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
#define __pure			__attribute__((pure))
#define __aligned(x)		__attribute__((aligned(x)))
#define __aligned_largest	__attribute__((aligned))
#define __printf(a, b)		__attribute__((format(printf, a, b)))
#define __scanf(a, b)		__attribute__((format(scanf, a, b)))
#define __maybe_unused		__attribute__((unused))
#define __always_unused		__attribute__((unused))
#define __mode(x)		__attribute__((mode(x)))
#define __malloc		__attribute__((__malloc__))
#define __used			__attribute__((__used__))
#define __noreturn		__attribute__((noreturn))
#define __packed		__attribute__((packed))
#define __weak			__attribute__((weak))
#define __alias(symbol)		__attribute__((alias(#symbol)))
#define __cold			__attribute__((cold))
#define __section(S)		__attribute__((__section__(#S)))


#ifdef CONFIG_ENABLE_MUST_CHECK
#define __must_check		__attribute__((warn_unused_result))
#else
#define __must_check
#endif
/*
瀹弉otrace鐨勫畾涔夛紝杩欎釜瀹忕敤浜庝慨楗板嚱鏁帮紝璇存槑璇ュ嚱鏁颁笉琚窡韪��
杩欓噷鎵�璇寸殑璺熻釜鏄痝cc涓�涓緢閲嶈鐨勭壒鎬э紝鍙鍦ㄧ紪璇戞椂鎵撳紑鐩稿叧鐨勮窡韪�夋嫨锛岀紪璇戝櫒浼氬姞鍏ヤ竴浜涚壒鎬э紝
浣垮緱绋嬪簭鍦ㄦ墽琛屽畬鍚庯紝鍙互閫氳繃宸ュ叿锛堝Graphviz锛夋潵鏌ョ湅鍑芥暟鐨勮皟鐢ㄨ繃绋嬨��
鑰屽浜庡唴鏍告潵璇达紝鍐呴儴閲囩敤浜唂trace鏈哄埗锛岃�屼笉閲囩敤trace鐨勭壒鎬с��
*/
#if defined(CC_USING_HOTPATCH) && !defined(__CHECKER__)
#define notrace			__attribute__((hotpatch(0, 0)))
#else
#define notrace			__attribute__((no_instrument_function))
#endif

/*
 * it doesn't make sense on ARM (currently the only user of __naked)
 * to trace naked functions because then mcount is called without
 * stack and frame pointer being set up and there is no chance to
 * restore the lr register to the value before mcount was called.
 */
#define __naked			__attribute__((naked)) notrace

#define __compiler_offsetof(a, b)	__builtin_offsetof(a, b)

/*
 * Feature detection for gnu_inline (gnu89 extern inline semantics). Either
 * __GNUC_STDC_INLINE__ is defined (not using gnu89 extern inline semantics,
 * and we opt in to the gnu89 semantics), or __GNUC_STDC_INLINE__ is not
 * defined so the gnu89 semantics are the default.
 */
#ifdef __GNUC_STDC_INLINE__
# define __gnu_inline	__attribute__((gnu_inline))
#else
# define __gnu_inline
#endif

/*
 * Force always-inline if the user requests it so via the .config.
 * GCC does not warn about unused static inline functions for
 * -Wunused-function.  This turns out to avoid the need for complex #ifdef
 * directives.  Suppress the warning in clang as well by using "unused"
 * function attribute, which is redundant but not harmful for gcc.
 * Prefer gnu_inline, so that extern inline functions do not emit an
 * externally visible function. This makes extern inline behave as per gnu89
 * semantics rather than c99. This prevents multiple symbol definition errors
 * of extern inline functions at link time.
 * A lot of inline functions can cause havoc with function tracing.
 */
#if !defined(CONFIG_ARCH_SUPPORTS_OPTIMIZED_INLINING) || \
	!defined(CONFIG_OPTIMIZE_INLINING)
#define inline \
	inline __attribute__((always_inline, unused)) notrace __gnu_inline
#else
#define inline inline	__attribute__((unused)) notrace __gnu_inline
#endif

#define __inline__ inline
#define __inline inline
#define noinline	__attribute__((noinline))

#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif

/*
 * Rather then using noinline to prevent stack consumption, use
 * noinline_for_stack instead.  For documentation reasons.
 */
#define noinline_for_stack noinline

#endif /* __LINUX_COMPILER_TYPES_H */
