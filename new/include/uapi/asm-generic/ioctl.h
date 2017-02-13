#ifndef _UAPI_ASM_GENERIC_IOCTL_H
#define _UAPI_ASM_GENERIC_IOCTL_H

/* ioctl command encoding: 32 bits total, command in lower 16 bits,
 * size of the parameter structure in the lower 14 bits of the
 * upper 16 bits.
 * Encoding the size of the parameter structure in the ioctl request
 * is useful for catching programs compiled with old versions
 * and to avoid overwriting user space outside the user buffer area.
 * The highest 2 bits are reserved for indicating the ``access mode''.
 * NOTE: This limits the max parameter size to 16kB -1 !
 */

/*
 * The following is for compatibility across the various Linux
 * platforms.  The generic ioctl numbering scheme doesn't really enforce
 * a type field.  De facto, however, the top 8 bits of the lower 16
 * bits are indeed used as a type field, so we might just as well make
 * this explicit here.  Please be sure to use the decoding macros
 * below from now on.
 */

/* Ϊ����ioctl��cmd����,�ں�ʹ����һ��32λ�޷�������������ֳ��ĸ�����

   31   29            16 15            8 7              0
   |DIR |     SIZE      |     TYPE      |      NR       |

 * NR:Ϊ���ܺ�,����Ϊ8λ(_IOC_NRBITS)
 * TYPE:ΪһASCII�ַ�,�ٶ���ÿ������������Զ���Ψһ��,������8λ(_IOC_TYPEBITS)
 *      ʵ�ʵĺ궨���г�������MAGIC����,������ʱҲ��Ϊħ��
 * SIZE:��ʾioctl������arg�����Ĵ�С,���ֶεĳ�������ϵ�ܹ����,ͨ����14λ
        (_IOC_SIZEBITS),��ʵ�ں���ioctl�ĵ����в�û���õ����ֶ�
 * DIR:��ʾcmd������:read,write��read-write,������2λ,����ֶ����ڱ�ʾ��ioctl
       ���ù������û��ռ���ں˿ռ����ݴ���ķ���,�˴�����Ķ����Ǵ��û��ռ��
       �ӽǴ���,�ں�Ϊ���ֶζ���ĺ���:_IOC_NONE,��ʾ��ioctl���ù�����,�û��ռ�
       ���ں˿ռ�û����Ҫ���ݵĲ���:_IOC_WRITE,��ʾ��ioctl���ù�����,�û��ռ�
       ��Ҫ���ں˿ռ�д������;_IOC_READ,��ʾ��ioctl���ù�����,�û��ռ���Ҫ���ں�
       �ռ��ȡ����;_IOC_WRITE|_IOC_READ,��ʾ��ioctl���ù�����,�����������û�
       �ռ���ں˿ռ����˫�򴫵�
 */

 /* NR:Ϊ���ܺ�,����Ϊ8λ(_IOC_NRBITS)*/
#define _IOC_NRBITS	8
 /* TYPE:ΪһASCII�ַ�,�ٶ���ÿ������������Զ���Ψһ��,������8λ(_IOC_TYPEBITS)
 *  ʵ�ʵĺ궨���г�������MAGIC����,������ʱҲ��Ϊħ��*/
#define _IOC_TYPEBITS	8

/*
 * Let any architecture override either of the following before
 * including this file.
 */

 /* SIZE:��ʾioctl������arg�����Ĵ�С,���ֶεĳ�������ϵ�ܹ����,ͨ����14λ
  * (_IOC_SIZEBITS),��ʵ�ں���ioctl�ĵ����в�û���õ����ֶ�*/
#ifndef _IOC_SIZEBITS
# define _IOC_SIZEBITS	14
#endif

 /* DIR:��ʾcmd������:read,write��read-write,������2λ,����ֶ����ڱ�ʾ��ioctl
  * ���ù������û��ռ���ں˿ռ����ݴ���ķ���,�˴�����Ķ����Ǵ��û��ռ��
  * �ӽǴ���*/
#ifndef _IOC_DIRBITS
# define _IOC_DIRBITS	2
#endif

#define _IOC_NRMASK	((1 << _IOC_NRBITS)-1)	/*NR�ֶε�����*/
#define _IOC_TYPEMASK	((1 << _IOC_TYPEBITS)-1)/*TYPE�ֶε�����*/
#define _IOC_SIZEMASK	((1 << _IOC_SIZEBITS)-1)/*SIZE�ֶε�����*/
#define _IOC_DIRMASK	((1 << _IOC_DIRBITS)-1)	/*CMD�ֶε�����*/

#define _IOC_NRSHIFT	0				/*NR�ֶε�λ��*/
#define _IOC_TYPESHIFT	(_IOC_NRSHIFT+_IOC_NRBITS)	/*TYPE�ֶε�λ��*/
#define _IOC_SIZESHIFT	(_IOC_TYPESHIFT+_IOC_TYPEBITS)	/*SIZE�ֶε�λ��*/
#define _IOC_DIRSHIFT	(_IOC_SIZESHIFT+_IOC_SIZEBITS)	/*CMD�ֶε�λ��*/

/*
 * Direction bits, which any architecture can choose to override
 * before including this file.
 */

       
       
/*��ʾ��ioctl���ù�����,�û��ռ���ں˿ռ�û����Ҫ���ݵĲ���.*/
#ifndef _IOC_NONE
# define _IOC_NONE	0U
#endif

/*��ʾ��ioctl���ù�����,�û��ռ���Ҫ���ں˿ռ�д������*/
#ifndef _IOC_WRITE
# define _IOC_WRITE	1U
#endif

/*��ʾ��ioctl���ù�����,�û��ռ���Ҫ���ں˿ռ��ȡ����*/
#ifndef _IOC_READ
# define _IOC_READ	2U
#endif

/*��_IOC��NR,TYPE,SIZE��DIR��ϳ�һ��cmd����*/
#define _IOC(dir,type,nr,size) \
	(((dir)  << _IOC_DIRSHIFT) | \
	 ((type) << _IOC_TYPESHIFT) | \
	 ((nr)   << _IOC_NRSHIFT) | \
	 ((size) << _IOC_SIZESHIFT))

#ifndef __KERNEL__
#define _IOC_TYPECHECK(t) (sizeof(t))
#endif

/**
 * EXAMPLE:DEMODEV_IOCINT,��������û��ռ����ں˿ռ䴫��һ��int�Ͳ���
 *  
 */
/* used to create numbers */
/*�����޲�����������*/
#define _IO(type,nr)		_IOC(_IOC_NONE,(type),(nr),0)
/*��������������ж�ȡ���ݵ�������*/
#define _IOR(type,nr,size)	_IOC(_IOC_READ,(type),(nr),(_IOC_TYPECHECK(size)))
/*����������������д�����ݵ�������*/
#define _IOW(type,nr,size)	_IOC(_IOC_WRITE,(type),(nr),(_IOC_TYPECHECK(size)))
/*����˫����*/
#define _IOWR(type,nr,size)	_IOC(_IOC_READ|_IOC_WRITE,(type),(nr),(_IOC_TYPECHECK(size)))
#define _IOR_BAD(type,nr,size)	_IOC(_IOC_READ,(type),(nr),sizeof(size))
#define _IOW_BAD(type,nr,size)	_IOC(_IOC_WRITE,(type),(nr),sizeof(size))
#define _IOWR_BAD(type,nr,size)	_IOC(_IOC_READ|_IOC_WRITE,(type),(nr),sizeof(size))

/* used to decode ioctl numbers.. */
/*����������н��������ݷ��򣬼�д�����Ƕ���*/
#define _IOC_DIR(nr)		(((nr) >> _IOC_DIRSHIFT) & _IOC_DIRMASK)
/*����������н���������type*/
#define _IOC_TYPE(nr)		(((nr) >> _IOC_TYPESHIFT) & _IOC_TYPEMASK)
/*����������н���������number*/
#define _IOC_NR(nr)		(((nr) >> _IOC_NRSHIFT) & _IOC_NRMASK)
/*����������н������û����ݴ�С*/
#define _IOC_SIZE(nr)		(((nr) >> _IOC_SIZESHIFT) & _IOC_SIZEMASK)

/* ...and for the drivers/sound files... */

#define IOC_IN		(_IOC_WRITE << _IOC_DIRSHIFT)
#define IOC_OUT		(_IOC_READ << _IOC_DIRSHIFT)
#define IOC_INOUT	((_IOC_WRITE|_IOC_READ) << _IOC_DIRSHIFT)
#define IOCSIZE_MASK	(_IOC_SIZEMASK << _IOC_SIZESHIFT)
#define IOCSIZE_SHIFT	(_IOC_SIZESHIFT)

#endif /* _UAPI_ASM_GENERIC_IOCTL_H */
