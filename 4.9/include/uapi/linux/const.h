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

Ä³Ğ©³£Á¿ºê»áÍ¬Ê±±»CºÍasmÒıÓÃ£¬
¶øCÓëasmÔÚ¶ÔÁ¢¼´Êı·ûºÅµÄ´¦ÀíÉÏÊÇ²»Í¬µÄ¡£

asmÖĞÍ¨¹ıÖ¸ÁîÀ´Çø·ÖÆä²Ù×÷ÊıÊÇÓĞ·ûºÅ»¹ÊÇÎŞ·ûºÅµÄ£¬
¶ø²»ÊÇÍ¨¹ı²Ù×÷Êı¡£¶øCÖĞÊÇÍ¨¹ı±äÁ¿µÄÊôĞÔ£¬¶ø²»ÊÇÍ¨¹ı²Ù×÷·û¡£

CÖĞÈç¹ûÒªÖ¸Ã÷³£Á¿ÓĞÎŞ·ûºÅ£¬±ØĞëÎª³£Á¿Ìí¼Óºó×º£¬
¶øasmÔòÍ¨¹ıÊ¹ÓÃ²»Í¬µÄÖ¸ÁîÀ´Ö¸Ã÷¡£

Èç´Ë£¬µ±Ò»¸ö³£Á¿±»CºÍasmÍ¬Ê±°üº¬Ê±£¬±ØĞë×ö²»Í¬µÄ´¦Àí

_AC(X, Y)ÔÚ__ASSEMBLY__ÏÂÖ±½Ó·´»ØX£¬ÔÚ·Ç__ASSEMBLY__ÏÂ·µ»ØXÓëYµÄÆ´½Ó·ûºÅ¡
_AT(T, X)Ïàµ±ÓÚº¯Êıµ÷ÓÃ£¬ÆäÖĞT¿ÉÒÔÎªº¯ÊıÃû»òÕßº¯ÊıÖ¸Õë£¬XÎªÏàÓ¦¸ÃµÄ²ÎÊı±í£
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
