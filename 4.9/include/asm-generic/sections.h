#ifndef _ASM_GENERIC_SECTIONS_H_
#define _ASM_GENERIC_SECTIONS_H_

/* References to section boundaries */

#include <linux/compiler.h>
#include <linux/types.h>

/*
 * Usage guidelines:
 * _text, _data: architecture specific, don't use them in arch-independent code
 * [_stext, _etext]: contains .text.* sections, may also contain .rodata.*
 *                   and/or .init.* sections
 * [_sdata, _edata]: contains .data.* sections, may also contain .rodata.*
 *                   and/or .init.* sections.
 * [__start_rodata, __end_rodata]: contains .rodata.* sections
 * [__start_data_ro_after_init, __end_data_ro_after_init]:
 *		     contains data.ro_after_init section
 * [__init_begin, __init_end]: contains .init.* sections, but .init.text.*
 *                   may be out of this range on some architectures.
 * [_sinittext, _einittext]: contains .init.text.* sections
 * [__bss_start, __bss_stop]: contains BSS sections
 *
 * Following global variables are optional and may be unavailable on some
 * architectures and/or kernel configurations.
 *	_text, _data
 *	__kprobes_text_start, __kprobes_text_end
 *	__entry_text_start, __entry_text_end
 *	__ctors_start, __ctors_end
 */

/* _text:80008000,_stext:80100000,_etext:80800000 */
/*
_text  代码段的起始地址
_etext 代码段的结束地址
包含了编译后的内核代码
*/
extern char _text[], _stext[], _etext[];
/* _data:80c00000,_sdata:80c00000,_edata:80c6f728 */
/*
_sdata 数据段的起始地址
_edata 数据段的结束地址
保存大部分内核的变量
*/
extern char _data[], _sdata[], _edata[];
/* __bss_start:80c71000 __bss_stop:80ce609c */
/*
__bss_start BSS 段的结束地址
__bss_stop  BSS 段的结束地址
包含初始化为0的所有静态全局变量
*/
extern char __bss_start[], __bss_stop[];
/* __init_begin:80b00000 __init_end:80c00000 */
/*
__init_begin  init 段的起始地址
__init_end    init 段的结束地址
包含了大部分模块初始化的数据
*/
extern char __init_begin[], __init_end[];
/* _sinittext:80b002e0 _einittext:80b457b4 */
extern char _sinittext[], _einittext[];
/* __start_data_ro_after_init:809bdf28,__end_data_ro_after_init:809bf378 */
extern char __start_data_ro_after_init[], __end_data_ro_after_init[];
extern char _end[];
/* __per_cpu_load:80b86000 __per_cpu_start:80b86000 __per_cpu_end:80b8ec8c */
extern char __per_cpu_load[], __per_cpu_start[], __per_cpu_end[];
/* __kprobes_text_start:80723260 __kprobes_text_end:80725a04 */
extern char __kprobes_text_start[], __kprobes_text_end[];
extern char __entry_text_start[], __entry_text_end[];
/* __start_rodata:80800000  __end_rodata:809fb000*/
extern char __start_rodata[], __end_rodata[];

/* Start and end of .ctors section - used for constructor calls. */
extern char __ctors_start[], __ctors_end[];

extern __visible const void __nosave_begin, __nosave_end;

/* function descriptor handling (if any).  Override
 * in asm/sections.h */
#ifndef dereference_function_descriptor
#define dereference_function_descriptor(p) (p)
#endif

/* random extra sections (if any).  Override
 * in asm/sections.h */
#ifndef arch_is_kernel_text
static inline int arch_is_kernel_text(unsigned long addr)
{
	return 0;
}
#endif

#ifndef arch_is_kernel_data
static inline int arch_is_kernel_data(unsigned long addr)
{
	return 0;
}
#endif

/**
 * memory_contains - checks if an object is contained within a memory region
 * @begin: virtual address of the beginning of the memory region
 * @end: virtual address of the end of the memory region
 * @virt: virtual address of the memory object
 * @size: size of the memory object
 *
 * Returns: true if the object specified by @virt and @size is entirely
 * contained within the memory region defined by @begin and @end, false
 * otherwise.
 */
static inline bool memory_contains(void *begin, void *end, void *virt,
				   size_t size)
{
	return virt >= begin && virt + size <= end;
}

/**
 * memory_intersects - checks if the region occupied by an object intersects
 *                     with another memory region
 * @begin: virtual address of the beginning of the memory regien
 * @end: virtual address of the end of the memory region
 * @virt: virtual address of the memory object
 * @size: size of the memory object
 *
 * Returns: true if an object's memory region, specified by @virt and @size,
 * intersects with the region specified by @begin and @end, false otherwise.
 */
static inline bool memory_intersects(void *begin, void *end, void *virt,
				     size_t size)
{
	void *vend = virt + size;

	return (virt >= begin && virt < end) || (vend >= begin && vend < end);
}

/**
 * init_section_contains - checks if an object is contained within the init
 *                         section
 * @virt: virtual address of the memory object
 * @size: size of the memory object
 *
 * Returns: true if the object specified by @virt and @size is entirely
 * contained within the init section, false otherwise.
 */
static inline bool init_section_contains(void *virt, size_t size)
{
	return memory_contains(__init_begin, __init_end, virt, size);
}

/**
 * init_section_intersects - checks if the region occupied by an object
 *                           intersects with the init section
 * @virt: virtual address of the memory object
 * @size: size of the memory object
 *
 * Returns: true if an object's memory region, specified by @virt and @size,
 * intersects with the init section, false otherwise.
 */
static inline bool init_section_intersects(void *virt, size_t size)
{
	return memory_intersects(__init_begin, __init_end, virt, size);
}

#endif /* _ASM_GENERIC_SECTIONS_H_ */
