#ifndef _LINUX_IRQRETURN_H
#define _LINUX_IRQRETURN_H

/**
 * enum irqreturn
 * @IRQ_NONE		interrupt was not from this device or was not handled
 * @IRQ_HANDLED		interrupt was handled by this device
 * @IRQ_WAKE_THREAD	handler requests to wake the handler thread
 */
 /*中断处理例程返回值类型*/
enum irqreturn {
    /* 中断处理例程发现正在处理一个不是自己的设备触发的中断（一般可以通过dev 参数判断）,此时唯一要做的就是返回该值*/
	IRQ_NONE		= (0 << 0),
    /* 中断处理例程成功的处理了自己设备的中断,返回该值*/
	IRQ_HANDLED		= (1 << 0),
	/* 如果使用request_threaded_irq 函数注册中断处理函数，并且想在第1 个中断处理函数执行完后在另一个线程中
	   调用第2 个中断处理函数，则第1 个中断处理函数需要返回IRQ_WAKE_THREAD。*/
	IRQ_WAKE_THREAD		= (1 << 1),
};

typedef enum irqreturn irqreturn_t;
#define IRQ_RETVAL(x)	((x) ? IRQ_HANDLED : IRQ_NONE)

#endif
