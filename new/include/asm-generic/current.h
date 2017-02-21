#ifndef __ASM_GENERIC_CURRENT_H
#define __ASM_GENERIC_CURRENT_H

#include <linux/thread_info.h>
/*
current给出了当前进程进程描述符task_struct的地址，
该地址往往通过current_thread_info来确定 
current = current_thread_info()->task
*/
#define get_current() (current_thread_info()->task)
#define current get_current()

#endif /* __ASM_GENERIC_CURRENT_H */
