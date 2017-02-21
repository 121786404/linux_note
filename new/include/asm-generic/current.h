#ifndef __ASM_GENERIC_CURRENT_H
#define __ASM_GENERIC_CURRENT_H

#include <linux/thread_info.h>
/*
current�����˵�ǰ���̽���������task_struct�ĵ�ַ��
�õ�ַ����ͨ��current_thread_info��ȷ�� 
current = current_thread_info()->task
*/
#define get_current() (current_thread_info()->task)
#define current get_current()

#endif /* __ASM_GENERIC_CURRENT_H */
