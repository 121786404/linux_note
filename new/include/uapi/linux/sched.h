#ifndef _UAPI_LINUX_SCHED_H
#define _UAPI_LINUX_SCHED_H

/*
 * cloning flags:
 */
#define CSIGNAL		0x000000ff	/* signal mask to be sent at exit */
/* �����ڴ�������������ҳ��*/
#define CLONE_VM	0x00000100	/* set if VM shared between processes */
/*
CLONE_FS flag���������Ƹ��ӽ����Ƿ����ļ�ϵͳ��Ϣ
�������ļ�ϵͳ��root����ǰ����Ŀ¼�ȣ���
����趨�˸�flag����ô���ӽ��̹����ļ�ϵͳ��Ϣ��
�� �����趨��flag����ô�ӽ�����copy�����̵��ļ�ϵͳ��Ϣ��
֮���ӽ��̵���chroot��chdir��umask���ı��ļ�ϵͳ��Ϣ������Ӱ�쵽 ������
*/
#define CLONE_FS	0x00000200	/* set if fs info shared between processes */
#define CLONE_FILES	0x00000400	/* set if open files shared between processes */
/* ����趨CLONE_SIGHAND���flag��
���ʾ�������ӽ����븸���̹�����ͬ���źŴ���signal handler���� 
����ͬʱ����CLONE_VM��־*/
#define CLONE_SIGHAND	0x00000800	/* set if signal handlers and blocked signals shared */
/* ��������̱�׷�٣���ô�ӽ���Ҳ��׷��*/
#define CLONE_PTRACE	0x00002000	/* set if we want to let tracing continue on the child too */
/* �ڷ���vforkϵͳ����ʱ����*/
#define CLONE_VFORK	0x00004000	/* set if the parent wants the child to wake it up on mm_release */
/* ��fork�Ľ�����Ҫ�ʹ����ý��̵�clonerӵ��ͬ���ĸ����� */
#define CLONE_PARENT	0x00008000	/* set if we want to have the same parent as the cloner */
/*
��һ�������У������CLONE_THREAD��־������clone�����Ľ���
���Ǹý��̵�һ���̣߳������������̣�Linux��ʵû���ϸ�Ľ��̸����
���Ǵ���һ���߳��飬����ͬʱ����CLONE_SIGHAND
*/
#define CLONE_THREAD	0x00010000	/* Same thread group? */
/*
CLONE_NEWNS���flag��������������clone��ʱ��
���ӽ����Ƿ�Ҫ����mount namespace�ġ�
ͨ��fork�����Ľ������Ǻ͸����̹���mount namespace��
����Ȼ�ӽ���Ҳ���Ե���unshare�����������
������clone�������̵�ʱ�򣬿����и��������ԣ�
����ͨ�� CLONE_NEWNS���flag���Բ��͸����̹���mount namespace
��ע�⣺�ӽ��̵����private mount namespace��Ȼ�ø����̵�mount namespace����ʼ����
ֻ��֮���ӽ��̺͸����̵�mount namespace�ͷֵ������ˣ�
��ʱ���ӽ��̵�mount����umount�Ķ���������Ӱ�쵽�����̣�

���������ռ䣬��������ʱ���Խ����ص���ϵͳ���룬
ʹ���������ʱ�����ǿ��Դﵽ chroot �Ĺ��ܣ����ڰ�ȫ�Է���� chroot ����

������cloneʱ���趨��CLONE_NEWNS���ͻᴴ��һ���µ�mount Namespace��
ÿ�����̶�������һ��mount Namespace���棬
mount NamespaceΪ�����ṩ��һ���ļ������ͼ��
������趨���flag���ӽ��̺͸����̽�����һ��mount Namespace��
����ӽ��̵���mount��umount����Ӱ�쵽���и�Namespace�ڵĽ��̡�
����ӽ�����һ��������mount Namespace���棬
�Ϳ��Ե���mount��umount����һ���µ��ļ������ͼ��
��flag���pivot_rootϵͳ���ã�����Ϊ���̴���һ��������Ŀ¼�ռ�
*/
#define CLONE_NEWNS	0x00020000	/* New mount namespace group */
#define CLONE_SYSVSEM	0x00040000	/* share system V SEM_UNDO semantics */
#define CLONE_SETTLS	0x00080000	/* create a new TLS for the child */
#define CLONE_PARENT_SETTID	0x00100000	/* set the TID in the parent */
#define CLONE_CHILD_CLEARTID	0x00200000	/* clear the TID in the child */
#define CLONE_DETACHED		0x00400000	/* Unused, ignored */
#define CLONE_UNTRACED		0x00800000	/* set if the tracing process can't force CLONE_PTRACE on this clone */
#define CLONE_CHILD_SETTID	0x01000000	/* set the TID in the child */

/*
��������clone flag������һ��ʹ�ã�Ϊ�����ṩ��һ�����������л�����
LXC����ͨ��cloneʱ�趨��Щflag��Ϊ���̴���һ���ж���PID��IPC��FS��Network��UTS�ռ��container��
һ��container����һ����������л�������container��Ľ�����͸���ģ�
������Ϊ�Լ���ֱ����һ��ϵͳ�����еġ�
һ��container����ͳ���⻯���������һ̨��װ��OS������������ǿ�����С�������Ϊ��ݡ�
*/
#define CLONE_NEWCGROUP		0x02000000	/* New cgroup namespace */
/*
UTS �����ռ䣬��ҪĿ���Ƕ�������������������Ϣ����NIS��
������cloneʱ���趨��CLONE_NEWUTS���ͻᴴ��һ���µ�UTS Namespace��
һ��UTS Namespace����һ�鱻uname���صı�ʶ����
�µ�UTS Namespace�еı�ʶ��ͨ�����Ƶ��ý���������Namespace�ı�ʶ������ʼ����
Clone�����Ľ��̿���ͨ�����ϵͳ���øı���Щ��ʶ����
�������sethostname���ı��Namespace��hostname����һ�ı�Ը�Namespace�ڵ����н��̿ɼ���

CLONE_NEWUTS��CLONE_NEWNETһ��ʹ�ã����������һ���ж���������������ռ�Ļ�����
�͸�������һ̨����������һ����
*/
#define CLONE_NEWUTS		0x04000000	/* New utsname namespace */
/*
���̼�ͨ��(IPC)�������ռ䣬���Խ� SystemV �� IPC �� POSIX ����Ϣ���ж�������
������cloneʱ���趨��CLONE_NEWIPC���ͻᴴ��һ���µ�IPC Namespace��
clone�����Ľ��̽���ΪNamespace��ĵ�һ�����̡�
һ��IPC Namespace��һ��System V IPC objects ��ʶ�����ɣ�
���ʶ����IPC��ص�ϵͳ���ô�����
��һ��IPC Namespace���洴����IPC object�Ը�Namespace�ڵ����н��̿ɼ���
���Ƕ�����Namespace���ɼ���������ʹ�ò�ͬNamespace֮��Ľ��̲���ֱ��ͨ�ţ�
�������ڲ�ͬ��ϵͳ��һ������һ��IPC Namespace�����٣�
��Namespace�ڵ�����IPC object�ᱻ�ں��Զ����١�
PID Namespace��IPC Namespace�����������һ��ʹ�ã�ֻ���ڵ���cloneʱ��
ͬʱָ��CLONE_NEWPID��CLONE_NEWIPC��
�����´�����Namespace����һ��������PID�ռ�����һ��������IPC�ռ䡣
��ͬNamespace�Ľ��̱˴˲��ɼ���Ҳ���ܻ���ͨ�ţ�������ʵ���˽��̼�ĸ��롣
*/
#define CLONE_NEWIPC		0x08000000	/* New ipc namespace */
/*
CLONE_NEWUSER CLONE_NEWPID ������flag�Ǻ�user namespace��صı�ʶ��
��ͨ��clone����fork���̵�ʱ�����ǿ���ѡ��clone֮ǰ��user namespace��
��ȻҲ����ͨ�����ݸñ�ʶ�������µ�user namespace��
�û������ռ䣬ͬ���� ID һ�����û� ID ���� ID �������ռ������ǲ�һ���ģ�
�����ڲ�ͬ�����ռ��ڿ��Դ�����ͬ�� ID
*/
#define CLONE_NEWUSER		0x10000000	/* New user namespace */
/*
���������ռ䡣�ռ��ڵ�PID �Ƕ�������ģ�
��˼���������ռ��ڵ����� PID ���ܻ��������ռ���� PID ���ͻ��
���������ռ��ڵ� PID ӳ�䵽�����ռ���ʱ��ʹ������һ�� PID��
����˵�������ռ��ڵ�һ�� PID Ϊ1�����������ռ�����Ǹ� PID �ѱ� init ������ʹ��

������cloneʱ���趨��CLONE_NEWPID���ͻᴴ��һ���µ�PID Namespace��
clone�������½��̽���ΪNamespace��ĵ�һ�����̡�
һ��PID NamespaceΪ�����ṩ��һ��������PID������
PID Namespace�ڵ�PID����1��ʼ����Namespace�ڵ���fork��vfork��clone��������һ���ڸ�Namespace�ڶ�����PID��
�´�����Namespace��ĵ�һ�������ڸ�Namespace�ڵ�PID��Ϊ1��
����һ��������ϵͳ���init����һ����
��Namespace�ڵĹ¶����̶����Ըý���Ϊ�����̣����ý��̱�����ʱ��
��Namespace�����еĽ��̶��ᱻ������
PID Namespace�ǲ���ԣ��´�����Namespace�����Ǵ�����Namespace�Ľ������ڵ�Namespace����Namespace��
��Namespace�еĽ��̶��ڸ�Namespace�ǿɼ��ģ�һ�����̽�ӵ�в�ֹһ��PID��
���������ڵ�Namespace�Լ�����ֱϵ����Namespace�ж�����һ��PID��
ϵͳ����ʱ���ں˽�����һ��Ĭ�ϵ�PID Namespace��
��Namespace�������Ժ󴴽���Namespace�����ȣ�
���ϵͳ���еĽ����ڸ�Namespace���ǿɼ���
*/
#define CLONE_NEWPID		0x20000000	/* New pid namespace */
/*
���������ռ䣬���ڸ���������Դ��/proc/net��IP ��ַ��������·�ɵȣ�����̨���̿��������ڲ�ͬ�����ռ��ڵ���ͬ�˿��ϣ��û������������һ������
������cloneʱ���趨��CLONE_NEWNET���ͻᴴ��һ���µ�Network Namespace��
һ��Network NamespaceΪ�����ṩ��һ����ȫ����������Э��ջ����ͼ��
���������豸�ӿڣ�IPv4��IPv6Э��ջ��IP·�ɱ�����ǽ����sockets�ȵȡ�
һ��Network Namespace�ṩ��һ�ݶ��������绷�����͸�һ��������ϵͳһ����
һ�������豸ֻ�ܴ�����һ��Network Namespace�У�
���Դ�һ��Namespace�ƶ���һ��Namespace�С�
���������豸(virtual network device)�ṩ��һ�����ƹܵ��ĳ���
�����ڲ�ͬ��Namespace֮�佨��������������⻯�����豸��
���Խ���������Namespace�е������豸���Žӡ���һ��Network Namespace������ʱ��
�����豸�ᱻ�Զ��ƻ�init Network Namespace����ϵͳ�ʼ��Namespace��
*/
#define CLONE_NEWNET		0x40000000	/* New network namespace */
#define CLONE_IO		0x80000000	/* Clone io context */

/*
 * Scheduling policies
 */
/* ��ͨ����ʹ�õĵ��Ȳ��ԣ����ڴ˵��Ȳ���ʹ�õ���CFS������ */
#define SCHED_NORMAL		0
/*
ʵʱ����ʹ�õĵ��Ȳ��ԣ��˵��Ȳ��ԵĽ���һ��ʹ��CPU��һֱ���У�
ֱ���б���������ȼ���ʵʱ���̽�����У��������Զ�����CPU��
������ʱ����Ҫ��Ƚϸߣ���ÿ������ʱ��Ƚ϶̵Ľ���
*/
#define SCHED_FIFO		1
/*
ʵʱ����ʹ�õ�ʱ��Ƭ��ת�����ԣ�ʵʱ���̵�ʱ��Ƭ�����
����������ŵ�����ĩβ������ÿ��ʵʱ���̶�����ִ��һ��ʱ�䡣
������ÿ������ʱ��Ƚϳ���ʵʱ����

���������㷨��ʵʱ���Ȳ��ԣ����� ���ṩ Roound-Robin ���壬
����ʱ��Ƭ����ͬ���ȼ�����������ʱ��Ƭ�ᱻ�ŵ�����β����
�Ա�֤��ƽ�ԣ�ͬ���������ȼ������������ռ�����ȼ�������
��ͬҪ���ʵʱ������Ը�����Ҫ��sched_setscheduler()API ���ò���
*/
#define SCHED_RR		2
/* 
SCHED_NORMAL��ͨ���̲��Եķֻ��汾�����÷�ʱ���ԣ�
���ݶ�̬���ȼ�(����nice()API���ã������� CPU ������Դ��
ע�⣺������̱���������ʵʱ�������ȼ��ͣ�
����֮������ʵʱ���̴���ʱ��ʵʱ�������ȵ��ȡ�������������Ż�
*/
#define SCHED_BATCH		3
/* SCHED_ISO: reserved but not implemented yet */
/*
���ȼ���ͣ���ϵͳ����ʱ�����������
(��������ɢ�������Դ�ܵ������������������ʽṹ����������
�Ǵ˵��Ȳ��Ե������ߣ�
*/
#define SCHED_IDLE		5
/*
��֧�ֵ�ʵʱ���̵��Ȳ��ԣ����ͻ���ͼ��㣬
�Ҷ��ӳٺ����ʱ��߶����е��������á�
����Earliest Deadline First (EDF) �����㷨
*/
#define SCHED_DEADLINE		6

/* Can be ORed in to make sure the process is reverted back to SCHED_NORMAL on fork */
#define SCHED_RESET_ON_FORK     0x40000000

/*
 * For the sched_{set,get}attr() calls
 */
#define SCHED_FLAG_RESET_ON_FORK	0x01

#endif /* _UAPI_LINUX_SCHED_H */
