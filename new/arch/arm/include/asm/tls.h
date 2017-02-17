#ifndef __ASMARM_TLS_H
#define __ASMARM_TLS_H

#include <linux/compiler.h>
#include <asm/thread_info.h>

/*
�����е�ȫ�ֱ����뺯���ڶ���ľ�̬(static)������
�Ǹ����̶߳����Է��ʵĹ��������

��һ���߳��޸ĵ��ڴ����ݣ��������̶߳���Ч��
����һ���ŵ�Ҳ��һ��ȱ�㡣

˵�����ŵ㣬�̵߳����ݽ�����÷ǳ���ݡ�
˵����ȱ�㣬һ���߳������ˣ������߳�Ҳ��������; 

����̷߳��ʹ������ݣ���Ҫ�����ͬ��������
Ҳ�������ͬ����ص�BUG��

�����Ҫ��һ���߳��ڲ��ĸ����������ö��ܷ��ʡ�
�������̲߳��ܷ��ʵı���������Ϊstatic memory local to a thread �ֲ߳̾���̬��������
����Ҫ�µĻ�����ʵ�֡������TLS��

�ֲ߳̾��洢�ڲ�ͬ��ƽ̨�в�ͬ��ʵ�֣�����ֲ�Բ�̫�á�
�Һ�Ҫʵ���ֲ߳̾��洢�����ѣ���򵥵İ취���ǽ���һ��ȫ�ֱ�

ͨ����ǰ�߳�IDȥ��ѯ��Ӧ�����ݣ���Ϊ�����̵߳�ID��ͬ��
�鵽��������ȻҲ��ͬ�ˡ�
����Ҫ��Ϊ�˱������߳�ͬʱ�ô�ͬһȫ�ֱ������߾�̬����ʱ
�����µĳ�ͻ�������Ƕ���߳�ͬʱ��Ҫ�޸���һ����ʱ��
Ϊ�˽��������⣬���ǿ���ͨ��TLS���ƣ�
Ϊÿһ��ʹ�ø�ȫ�ֱ������̶߳��ṩһ������ֵ�ĸ�����
ÿһ���߳̾����Զ����ظı��Լ��ĸ�����
������������̵߳ĸ�����ͻ��

���̵߳ĽǶȿ����ͺ���ÿһ���̶߳���ȫӵ�иñ�����
����ȫ�ֱ����ĽǶ���������
�ͺ���һ��ȫ�ֱ�������¡���˶�ݸ�����
��ÿһ�ݸ��������Ա�һ���̶߳����ظı䡣

tp_value����Ϊ������TLS register��ֵ,�ڶ��߳�Ӧ�ó���
����һ�����̹�����ͬ�ĵ�ַ�ռ��е������̣߳�
���о���������Ҫά����������Ψһ��һ���̡߳�

TLS���̱߳��ش洢���������̳߳���ĸ��
����һ�ֿ��ٺ���Ч�ķ�ʽ���洢ÿ���̵߳ı������ݡ�
�̵߳ı������ݵ�ƫ������ͨ��TLS�Ĵ���(H/W��S/W��)��
��ָ���̸߳��Ե��߳̿��ƿ���ʡ�

֮ǰARM�ںˣ�����ARM9��ARM11���ĵ�һЩ���߱�����TLSע�������Ͽ��á�
����ϵͳ��Linux�����￪ʼ����ҪЧ�µ������
��һ����ARM�ںˡ�Cortex-AX��ȷʵ����TLS�ļĴ������ã�CP15����
�ں˶�TLS��Ҫ�����������ܹ����û�̬����ͨ����nptl����һ��pthread��ʵ�֣�
��ĳ��ʱ���ܹ������߳�Ψһ�Ļ�ֵַ���ں˵��߳���Ϣ�ṹ�ڡ�
*/
#ifdef __ASSEMBLY__
#include <asm/asm-offsets.h>
	.macro switch_tls_none, base, tp, tpuser, tmp1, tmp2
	.endm

	.macro switch_tls_v6k, base, tp, tpuser, tmp1, tmp2
	mrc	p15, 0, \tmp2, c13, c0, 2	@ get the user r/w register
	mcr	p15, 0, \tp, c13, c0, 3		@ set TLS register
	mcr	p15, 0, \tpuser, c13, c0, 2	@ and the user r/w register
	str	\tmp2, [\base, #TI_TP_VALUE + 4] @ save it
	.endm

	.macro switch_tls_v6, base, tp, tpuser, tmp1, tmp2
	ldr	\tmp1, =elf_hwcap
	ldr	\tmp1, [\tmp1, #0]
	mov	\tmp2, #0xffff0fff
	tst	\tmp1, #HWCAP_TLS		@ hardware TLS available?
	streq	\tp, [\tmp2, #-15]		@ set TLS value at 0xffff0ff0
	mrcne	p15, 0, \tmp2, c13, c0, 2	@ get the user r/w register
	mcrne	p15, 0, \tp, c13, c0, 3		@ yes, set TLS register
	mcrne	p15, 0, \tpuser, c13, c0, 2	@ set user r/w register
	strne	\tmp2, [\base, #TI_TP_VALUE + 4] @ save it
	.endm

	.macro switch_tls_software, base, tp, tpuser, tmp1, tmp2
	mov	\tmp1, #0xffff0fff
	str	\tp, [\tmp1, #-15]		@ set TLS value at 0xffff0ff0
	.endm
#endif

#ifdef CONFIG_TLS_REG_EMUL
#define tls_emu		1
#define has_tls_reg		1
#define switch_tls	switch_tls_none
#elif defined(CONFIG_CPU_V6)
#define tls_emu		0
#define has_tls_reg		(elf_hwcap & HWCAP_TLS)
#define switch_tls	switch_tls_v6
#elif defined(CONFIG_CPU_32v6K)
#define tls_emu		0
#define has_tls_reg		1
#define switch_tls	switch_tls_v6k
#else
#define tls_emu		0
#define has_tls_reg		0
#define switch_tls	switch_tls_software
#endif

#ifndef __ASSEMBLY__

static inline void set_tls(unsigned long val)
{
	struct thread_info *thread;

	thread = current_thread_info();

	thread->tp_value[0] = val;

	/*
	 * This code runs with preemption enabled and therefore must
	 * be reentrant with respect to switch_tls.
	 *
	 * We need to ensure ordering between the shadow state and the
	 * hardware state, so that we don't corrupt the hardware state
	 * with a stale shadow state during context switch.
	 *
	 * If we're preempted here, switch_tls will load TPIDRURO from
	 * thread_info upon resuming execution and the following mcr
	 * is merely redundant.
	 */
	barrier();

	if (!tls_emu) {
		if (has_tls_reg) {
			asm("mcr p15, 0, %0, c13, c0, 3"
			    : : "r" (val));
		} else {
#ifdef CONFIG_KUSER_HELPERS
			/*
			 * User space must never try to access this
			 * directly.  Expect your app to break
			 * eventually if you do so.  The user helper
			 * at 0xffff0fe0 must be used instead.  (see
			 * entry-armv.S for details)
			 */
			*((unsigned int *)0xffff0ff0) = val;
#endif
		}

	}
}

static inline unsigned long get_tpuser(void)
{
	unsigned long reg = 0;

	if (has_tls_reg && !tls_emu)
		__asm__("mrc p15, 0, %0, c13, c0, 2" : "=r" (reg));

	return reg;
}

static inline void set_tpuser(unsigned long val)
{
	/* Since TPIDRURW is fully context-switched (unlike TPIDRURO),
	 * we need not update thread_info.
	 */
	if (has_tls_reg && !tls_emu) {
		asm("mcr p15, 0, %0, c13, c0, 2"
		    : : "r" (val));
	}
}

static inline void flush_tls(void)
{
	set_tls(0);
	set_tpuser(0);
}

#endif
#endif	/* __ASMARM_TLS_H */
