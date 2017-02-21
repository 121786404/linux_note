/*
 *  arch/arm/include/asm/domain.h
 *
 *  Copyright (C) 1999 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_PROC_DOMAIN_H
#define __ASM_PROC_DOMAIN_H

#ifndef __ASSEMBLY__
#include <asm/barrier.h>
#include <asm/thread_info.h>
#endif

/*
 * Domain numbers
 *
 *  DOMAIN_IO     - domain 2 includes all IO only
 *  DOMAIN_USER   - domain 1 includes all user memory only
 *  DOMAIN_KERNEL - domain 0 includes all kernel memory only
 *
 * The domain numbering depends on whether we support 36 physical
 * address for I/O or not.  Addresses above the 32 bit boundary can
 * only be mapped using supersections and supersections can only
 * be set for domain 0.  We could just default to DOMAIN_IO as zero,
 * but there may be systems with supersection support and no 36-bit
 * addressing.  In such cases, we want to map system memory with
 * supersections to reduce TLB misses and footprint.
 *
 * 36-bit addressing and supersections are only available on
 * CPUs based on ARMv6+ or the Intel XSC3 core.
 */
/*
Linux 中只是用了16个域中的三个域D0-D2。
在系统引导时初始化MMU的过程中
将对这三个域设置域访问权限。以下是内存空间和域的对应表：

0. 设备空间 DOMAIN_IO
1. 内部高速SRAM空间/内部MINI Cache空间  DOMAIN_KERNEL
2. RAM内存空间/ROM内存空间   DOMAIN_KERNEL
3. 高低端中断向量空间  DOMAIN_USER

在ARM处理器中，MMU中的每个域的访问权限分别由
CP15的C3寄存器中的两位来设定，c3寄存器的小为32bits，
刚好可以设置16个域的访问权限。


0b00 无访问权限 此时访问该域将产生访问失效

0b01 用户(client) 根据CP15的C1控制寄存器中的R和S位
        以及页表中地址变换条目中的访问权限
　   控制位 AP来确定是否允许各种系统工作模式的存储访问

0b10 保留 使用该值会产生不可预知的结果

0b11 管理者(Manager) 不考虑CP15的C1控制寄存器中的R和S位
        以及页表中地址变换条目中的 访问权限控制位AP，
        在这种情况下不管系统工作在特权模式还是用户
　   模式都不会产生访问失效
*/
#ifndef CONFIG_IO_36
#define DOMAIN_KERNEL	0
#define DOMAIN_USER	1
#define DOMAIN_IO	2
#else
#define DOMAIN_KERNEL	2
#define DOMAIN_USER	1
#define DOMAIN_IO	0
#endif
#define DOMAIN_VECTORS	3

/*
 * Domain types
 */

#define DOMAIN_NOACCESS	0
#define DOMAIN_CLIENT	1
#ifdef CONFIG_CPU_USE_DOMAINS
#define DOMAIN_MANAGER	3
#else
#define DOMAIN_MANAGER	1
#endif

#define domain_mask(dom)	((3) << (2 * (dom)))
#define domain_val(dom,type)	((type) << (2 * (dom)))

#ifdef CONFIG_CPU_SW_DOMAIN_PAN
#define DACR_INIT \
	(domain_val(DOMAIN_USER, DOMAIN_NOACCESS) | \
	 domain_val(DOMAIN_KERNEL, DOMAIN_MANAGER) | \
	 domain_val(DOMAIN_IO, DOMAIN_CLIENT) | \
	 domain_val(DOMAIN_VECTORS, DOMAIN_CLIENT))
#else
#define DACR_INIT \
	(domain_val(DOMAIN_USER, DOMAIN_CLIENT) | \
	 domain_val(DOMAIN_KERNEL, DOMAIN_MANAGER) | \
	 domain_val(DOMAIN_IO, DOMAIN_CLIENT) | \
	 domain_val(DOMAIN_VECTORS, DOMAIN_CLIENT))
#endif

#define __DACR_DEFAULT \
	domain_val(DOMAIN_KERNEL, DOMAIN_CLIENT) | \
	domain_val(DOMAIN_IO, DOMAIN_CLIENT) | \
	domain_val(DOMAIN_VECTORS, DOMAIN_CLIENT)

#define DACR_UACCESS_DISABLE	\
	(__DACR_DEFAULT | domain_val(DOMAIN_USER, DOMAIN_NOACCESS))
#define DACR_UACCESS_ENABLE	\
	(__DACR_DEFAULT | domain_val(DOMAIN_USER, DOMAIN_CLIENT))

#ifndef __ASSEMBLY__

#ifdef CONFIG_CPU_CP15_MMU
static inline unsigned int get_domain(void)
{
	unsigned int domain;

	asm(
	"mrc	p15, 0, %0, c3, c0	@ get domain"
	 : "=r" (domain)
	 : "m" (current_thread_info()->cpu_domain));

	return domain;
}

static inline void set_domain(unsigned val)
{
	asm volatile(
	"mcr	p15, 0, %0, c3, c0	@ set domain"
	  : : "r" (val) : "memory");
	isb();
}
#else
static inline unsigned int get_domain(void)
{
	return 0;
}

static inline void set_domain(unsigned val)
{
}
#endif

/*
Linux在系统引导设置MMU时初始化c3寄存器来实现对内存域的访问控制。

对DOMAIN_USER，DOMAIN_KERNEL和DOMAIN_TABLE均设置DOMAIN_MANAGER权限;
对DOMAIN_IO设置DOMAIN_CLIENT权限。
如果此时读取c3寄存器，它的值应该是0x1f。

在系统的引导过程中对这3个域的访问控制位并不是一成不变的，
它提供了一个名为modify_domain的宏来修改域访问控制位。
系统在setup_arch中调用early_trap_init后，DOMAIN_USER的权限位将被设置成
DOMAIN_CLIENT，此时它的值应该是0x17。
*/

#ifdef CONFIG_CPU_USE_DOMAINS
#define modify_domain(dom,type)					\
	do {							\
		unsigned int domain = get_domain();		\
		domain &= ~domain_mask(dom);			\
		domain = domain | domain_val(dom, type);	\
		set_domain(domain);				\
	} while (0)

#else
static inline void modify_domain(unsigned dom, unsigned type)	{ }
#endif

/*
 * Generate the T (user) versions of the LDR/STR and related
 * instructions (inline assembly)
 */
#ifdef CONFIG_CPU_USE_DOMAINS
#define TUSER(instr)	#instr "t"
#else
#define TUSER(instr)	#instr
#endif

#else /* __ASSEMBLY__ */

/*
 * Generate the T (user) versions of the LDR/STR and related
 * instructions
 */
#ifdef CONFIG_CPU_USE_DOMAINS
#define TUSER(instr)	instr ## t
#else
#define TUSER(instr)	instr
#endif

#endif /* __ASSEMBLY__ */

#endif /* !__ASM_PROC_DOMAIN_H */
