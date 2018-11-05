/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* const.h: Macros for dealing with constants.  */

#ifndef _UAPI_LINUX_CONST_H
#define _UAPI_LINUX_CONST_H

/* Some constant macros are used in both assembler and
 * C code.  Therefore we cannot annotate them always with
 * 'UL' and other type specifiers unilaterally.  We
 * use the following macros to deal with this.
 *
 * Similarly, _AT() will cast an expression with a type in C, but
 * leave it unchanged in asm.

某些常量宏会同时被C和asm引用，
而C与asm在对立即数符号的处理上是不同的。

asm中通过指令来区分其操作数是有符号还是无符号的，
而不是通过操作数。而C中是通过变量的属性，而不是通过操作符。

C中如果要指明常量有无符号，必须为常量添加后缀，
而asm则通过使用不同的指令来指明。

如此，当一个常量被C和asm同时包含时，必须做不同的处理

_AC(X, Y)在__ASSEMBLY__下直接反回X，在非__ASSEMBLY__下返回X与Y的拼接符号?_AT(T, X)相当于函数调用，其中T可以为函数名或者函数指针，X为相应该的参数表? */

#ifdef __ASSEMBLY__
#define _AC(X,Y)	X
#define _AT(T,X)	X
#else
#define __AC(X,Y)	(X##Y)
#define _AC(X,Y)	__AC(X,Y)
#define _AT(T,X)	((T)(X))
#endif

#define _UL(x)		(_AC(x, UL))
#define _ULL(x)		(_AC(x, ULL))

#define _BITUL(x)	(_UL(1) << (x))
#define _BITULL(x)	(_ULL(1) << (x))

#endif /* _UAPI_LINUX_CONST_H */
