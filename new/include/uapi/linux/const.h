/* const.h: Macros for dealing with constants.  */

#ifndef _LINUX_CONST_H
#define _LINUX_CONST_H

/* Some constant macros are used in both assembler and
 * C code.  Therefore we cannot annotate them always with
 * 'UL' and other type specifiers unilaterally.  We
 * use the following macros to deal with this.
 *
 * Similarly, _AT() will cast an expression with a type in C, but
 * leave it unchanged in asm.

ĳЩ�������ͬʱ��C��asm���ã�
��C��asm�ڶ����������ŵĴ������ǲ�ͬ�ġ�

asm��ͨ��ָ������������������з��Ż����޷��ŵģ�
������ͨ������������C����ͨ�����������ԣ�������ͨ����������

C�����Ҫָ���������޷��ţ�����Ϊ������Ӻ�׺��
��asm��ͨ��ʹ�ò�ͬ��ָ����ָ����

��ˣ���һ��������C��asmͬʱ����ʱ����������ͬ�Ĵ���

_AC(X, Y)��__ASSEMBLY__��ֱ�ӷ���X���ڷ�__ASSEMBLY__�·���X��Y��ƴ�ӷ��š
_AT(T, X)�൱�ں������ã�����T����Ϊ���������ߺ���ָ�룬XΪ��Ӧ�õĲ�����
 */

#ifdef __ASSEMBLY__
#define _AC(X,Y)	X
#define _AT(T,X)	X
#else
#define __AC(X,Y)	(X##Y)
#define _AC(X,Y)	__AC(X,Y)
#define _AT(T,X)	((T)(X))
#endif

#define _BITUL(x)	(_AC(1,UL) << (x))
#define _BITULL(x)	(_AC(1,ULL) << (x))

#endif /* !(_LINUX_CONST_H) */
