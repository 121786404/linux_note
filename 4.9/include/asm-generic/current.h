#ifndef __ASM_GENERIC_CURRENT_H
#define __ASM_GENERIC_CURRENT_H

#include <linux/thread_info.h>
/*
arch\arm\include\asm\thread_info.h

register unsigned long current_stack_pointer asm ("sp");

static inline struct thread_info *current_thread_info(void)
{
	return (struct thread_info *)(current_stack_pointer & ~(THREAD_SIZE - 1));
}
*/
#define get_current() (current_thread_info()->task)
#define current get_current()

#endif /* __ASM_GENERIC_CURRENT_H */
