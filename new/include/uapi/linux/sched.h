#ifndef _UAPI_LINUX_SCHED_H
#define _UAPI_LINUX_SCHED_H

/*
 * cloning flags:
 */
#define CSIGNAL		0x000000ff	/* signal mask to be sent at exit */
/* 父子进程共享内存空间*/
#define CLONE_VM	0x00000100	/* set if VM shared between processes */
/*
父子进程共享文件系统信息，（例如文件系统的root、当前工作目录等），
如果设定了该flag，那么父子进程共享文件系统信息，
如果不设定该flag，那么子进程则copy父进程的文件系统信息，
之后，子进程调用chroot，chdir，umask来改变文件系统信息将不会影响到父进程
*/
#define CLONE_FS	0x00000200	/* set if fs info shared between processes */
/*
子进程与父进程共享相同的文件描述符（file descriptor）表,即共享打开的文件
*/
#define CLONE_FILES 0x00000400	/* set if open files shared between processes */
/* 表示创建的子进程与父进程共享相同的信号处理（signal handler）表 ，必须同时设置CLONE_VM标志*/
#define CLONE_SIGHAND	0x00000800	/* set if signal handlers and blocked signals shared */
/* 如果父进程被追踪，那么子进程也被追踪*/
#define CLONE_PTRACE	0x00002000	/* set if we want to let tracing continue on the child too */
/* 在发出vfork系统调用时设置*/
#define CLONE_VFORK 0x00004000	/* set if the parent wants the child to wake it up on mm_release */
/* 创建的子进程的父进程是调用者的父进程，
     新进程与创建它的进程成了“兄弟”而不是“父子” */
#define CLONE_PARENT	0x00008000	/* set if we want to have the same parent as the cloner */
/*
子进程与父进程处于一个线程组，必须同时设置CLONE_SIGHAND
*/
#define CLONE_THREAD	0x00010000	/* Same thread group? */
/*
CLONE_NEWNS这个flag就是用来控制在clone的时候，
父子进程是否要共享mount namespace的。
通过fork创建的进程总是和父进程共享mount namespace的
（当然子进程也可以调用unshare来解除共享）。
当调用clone创建进程的时候，可以有更多的灵活性，
可以通过 CLONE_NEWNS这个flag可以不和父进程共享mount namespace
（注意：子进程的这个private mount namespace仍然用父进程的mount namespace来初始化，
只是之后，子进程和父进程的mount namespace就分道扬镳了，
这时候，子进程的mount或者umount的动作将不会影响到父进程）

挂载命名空间，进程运行时可以将挂载点与系统分离，
使用这个功能时，我们可以达到 chroot 的功能，而在安全性方面比 chroot 更高

当调用clone时，设定了CLONE_NEWNS，就会创建一个新的mount Namespace。
每个进程都存在于一个mount Namespace里面，
mount Namespace为进程提供了一个文件层次视图。
如果不设定这个flag，子进程和父进程将共享一个mount Namespace，
其后子进程调用mount或umount将会影响到所有该Namespace内的进程。
如果子进程在一个独立的mount Namespace里面，
就可以调用mount或umount建立一份新的文件层次视图。
该flag配合pivot_root系统调用，可以为进程创建一个独立的目录空间
*/
#define CLONE_NEWNS 0x00020000	/* New mount namespace group */
/* 父子进程共享system V SEM_UNDO 语义*/
#define CLONE_SYSVSEM	0x00040000	/* share system V SEM_UNDO semantics */
/*
为子进程创建新的TLS
*/
#define CLONE_SETTLS	0x00080000	/* create a new TLS for the child */
#define CLONE_PARENT_SETTID	0x00100000	/* set the TID in the parent */
#define CLONE_CHILD_CLEARTID	0x00200000	/* clear the TID in the child */
#define CLONE_DETACHED		0x00400000	/* Unused, ignored */
/*
如果用户进程 在创建的时候有携带CLONE_UNTRACED的flag，
那么该进程则不能被trace。
*/
#define CLONE_UNTRACED		0x00800000	/* set if the tracing process can't force CLONE_PTRACE on this clone */
/* 将TID 回写至用户空间 */
#define CLONE_CHILD_SETTID	0x01000000	/* set the TID in the child */

/*
以下所有clone flag都可以一起使用，为进程提供了一个独立的运行环境。
LXC正是通过clone时设定这些flag，为进程创建一个有独立PID，IPC，FS，Network，UTS空间的container。
一个container就是一个虚拟的运行环境，对container里的进程是透明的，
它会以为自己是直接在一个系统上运行的。
一个container就像传统虚拟化技术里面的一台安装了OS的虚拟机，但是开销更小，部署更为便捷。
*/
#define CLONE_NEWCGROUP		0x02000000	/* New cgroup namespace */
/*
UTS 命名空间，主要目的是独立出主机名和网络信息服务（NIS）
当调用clone时，设定了CLONE_NEWUTS，就会创建一个新的UTS Namespace。
一个UTS Namespace就是一组被uname返回的标识符。
新的UTS Namespace中的标识符通过复制调用进程所属的Namespace的标识符来初始化。
Clone出来的进程可以通过相关系统调用改变这些标识符，
比如调用sethostname来改变该Namespace的hostname。这一改变对该Namespace内的所有进程可见。

CLONE_NEWUTS和CLONE_NEWNET一起使用，可以虚拟出一个有独立主机名和网络空间的环境，
就跟网络上一台独立的主机一样。
*/
#define CLONE_NEWUTS		0x04000000	/* New utsname namespace */
/*
进程间通信(IPC)的命名空间，可以将 SystemV 的 IPC 和 POSIX 的消息队列独立出来
当调用clone时，设定了CLONE_NEWIPC，就会创建一个新的IPC Namespace，
clone出来的进程将成为Namespace里的第一个进程。
一个IPC Namespace有一组System V IPC objects 标识符构成，
这标识符有IPC相关的系统调用创建。
在一个IPC Namespace里面创建的IPC object对该Namespace内的所有进程可见，
但是对其他Namespace不可见，这样就使得不同Namespace之间的进程不能直接通信，
就像是在不同的系统里一样。当一个IPC Namespace被销毁，
该Namespace内的所有IPC object会被内核自动销毁。
PID Namespace和IPC Namespace可以组合起来一起使用，只需在调用clone时，
同时指定CLONE_NEWPID和CLONE_NEWIPC，
这样新创建的Namespace既是一个独立的PID空间又是一个独立的IPC空间。
不同Namespace的进程彼此不可见，也不能互相通信，这样就实现了进程间的隔离。
*/
#define CLONE_NEWIPC		0x08000000	/* New ipc namespace */
/*
CLONE_NEWUSER CLONE_NEWPID 这两个flag是和user namespace相关的标识，
在通过clone函数fork进程的时候，我们可以选择clone之前的user namespace，
当然也可以通过传递该标识来创建新的user namespace。
用户命名空间，同进程 ID 一样，用户 ID 和组 ID 在命名空间内外是不一样的，
并且在不同命名空间内可以存在相同的 ID
*/
#define CLONE_NEWUSER		0x10000000	/* New user namespace */
/*
进程命名空间。空间内的PID 是独立分配的，
意思就是命名空间内的虚拟 PID 可能会与命名空间外的 PID 相冲突，
于是命名空间内的 PID 映射到命名空间外时会使用另外一个 PID。
比如说，命名空间内第一个 PID 为1，而在命名空间外就是该 PID 已被 init 进程所使用

当调用clone时，设定了CLONE_NEWPID，就会创建一个新的PID Namespace，
clone出来的新进程将成为Namespace里的第一个进程。
一个PID Namespace为进程提供了一个独立的PID环境，
PID Namespace内的PID将从1开始，在Namespace内调用fork，vfork或clone都将产生一个在该Namespace内独立的PID。
新创建的Namespace里的第一个进程在该Namespace内的PID将为1，
就像一个独立的系统里的init进程一样。
该Namespace内的孤儿进程都将以该进程为父进程，当该进程被结束时，
该Namespace内所有的进程都会被结束。
PID Namespace是层次性，新创建的Namespace将会是创建该Namespace的进程属于的Namespace的子Namespace。
子Namespace中的进程对于父Namespace是可见的，一个进程将拥有不止一个PID，
而是在所在的Namespace以及所有直系祖先Namespace中都将有一个PID。
系统启动时，内核将创建一个默认的PID Namespace，
该Namespace是所有以后创建的Namespace的祖先，
因此系统所有的进程在该Namespace都是可见的
*/
#define CLONE_NEWPID		0x20000000	/* New pid namespace */
/*
网络命名空间，用于隔离网络资源（/proc/net、IP 地址、网卡、路由等）。后台进程可以运行在不同命名空间内的相同端口上，用户还可以虚拟出一块网卡
当调用clone时，设定了CLONE_NEWNET，就会创建一个新的Network Namespace。
一个Network Namespace为进程提供了一个完全独立的网络协议栈的视图。
包括网络设备接口，IPv4和IPv6协议栈，IP路由表，防火墙规则，sockets等等。
一个Network Namespace提供了一份独立的网络环境，就跟一个独立的系统一样。
一个物理设备只能存在于一个Network Namespace中，
可以从一个Namespace移动另一个Namespace中。
虚拟网络设备(virtual network device)提供了一种类似管道的抽象，
可以在不同的Namespace之间建立隧道。利用虚拟化网络设备，
可以建立到其他Namespace中的物理设备的桥接。当一个Network Namespace被销毁时，
物理设备会被自动移回init Network Namespace，即系统最开始的Namespace。
*/
#define CLONE_NEWNET		0x40000000	/* New network namespace */
#define CLONE_IO		0x80000000	/* Clone io context */

/*
 * Scheduling policies
 */
/* 普通进程使用的调度策略，现在此调度策略使用的是CFS调度器 */
#define SCHED_NORMAL		0
/*
实时进程使用的调度策略，此调度策略的进程一旦使用CPU则一直运行，
直到有比其更高优先级的实时进程进入队列，或者其自动放弃CPU，
适用于时间性要求比较高，但每次运行时间比较短的进程
*/
#define SCHED_FIFO		1
/*
实时进程使用的时间片轮转法策略，实时进程的时间片用完后，
调度器将其放到队列末尾，这样每个实时进程都可以执行一段时间。
适用于每次运行时间比较长的实时进程

轮流调度算法（实时调度策略），后者提供 Roound-Robin 语义，
采用时间片，相同优先级的任务当用完时间片会被放到队列尾部，
以保证公平性，同样，高优先级的任务可以抢占低优先级的任务。
不同要求的实时任务可以根据需要用sched_setscheduler设置策略
*/
#define SCHED_RR		2
/*
SCHED_NORMAL普通进程策略的分化版本。采用分时策略，
根据动态优先级(可用nice()API设置），分配 CPU 运算资源。
注意：这类进程比上述两类实时进程优先级低，
换言之，在有实时进程存在时，实时进程优先调度。
但针对吞吐量优化
*/
#define SCHED_BATCH		3
/* SCHED_ISO: reserved but not implemented yet */
/*
优先级最低，在系统空闲时才跑这类进程
(如利用闲散计算机资源跑地外文明搜索，蛋白质结构分析等任务，
是此调度策略的适用者）
*/
#define SCHED_IDLE		5
/*
新支持的实时进程调度策略，针对突发型计算，
且对延迟和完成时间高度敏感的任务适用。
基于Earliest Deadline First (EDF) 调度算法
*/
#define SCHED_DEADLINE		6

/* Can be ORed in to make sure the process is reverted back to SCHED_NORMAL on fork */
#define SCHED_RESET_ON_FORK     0x40000000

/*
 * For the sched_{set,get}attr() calls
 */
#define SCHED_FLAG_RESET_ON_FORK	0x01

#endif /* _UAPI_LINUX_SCHED_H */
