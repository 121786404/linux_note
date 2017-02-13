#ifndef _LINUX_IRQRETURN_H
#define _LINUX_IRQRETURN_H

/**
 * enum irqreturn
 * @IRQ_NONE		interrupt was not from this device or was not handled
 * @IRQ_HANDLED		interrupt was handled by this device
 * @IRQ_WAKE_THREAD	handler requests to wake the handler thread
 */
 /*�жϴ������̷���ֵ����*/
enum irqreturn {
/* �жϴ������̷������ڴ���һ�������Լ����豸�������ж�
			 * ,��ʱΨһҪ���ľ��Ƿ��ظ�ֵ*/
	IRQ_NONE		= (0 << 0),
/* �жϴ������̳ɹ��Ĵ������Լ��豸���ж�,���ظ�ֵ*/
	IRQ_HANDLED		= (1 << 0),
	/* �жϴ������̱�����������һ���ȴ�������irq��
			 * ��һ������ʹ��,��ʱ���ظ�ֵ*/
	IRQ_WAKE_THREAD		= (1 << 1),
};

typedef enum irqreturn irqreturn_t;
#define IRQ_RETVAL(x)	((x) ? IRQ_HANDLED : IRQ_NONE)

#endif
