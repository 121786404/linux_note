/*
 *  arch/arm/include/asm/unistd.h
 *
 *  Copyright (C) 2001-2005 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Please forward _all_ changes to this file to rmk@arm.linux.org.uk,
 * no matter what the change is.  Thanks!
 */
#ifndef __ASM_ARM_UNISTD_H
#define __ASM_ARM_UNISTD_H

#include <uapi/asm/unistd.h>

/*
 * This may need to be greater than __NR_last_syscall+1 in order to
 * account for the padding in the syscall table
 */
#define __NR_syscalls  (396)

#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_NICE
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_SIGPROCMASK
#define __ARCH_WANT_SYS_OLD_MMAP
#define __ARCH_WANT_SYS_OLD_SELECT

#if !defined(CONFIG_AEABI) || defined(CONFIG_OABI_COMPAT)
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_SYS_IPC
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_OLD_GETRLIMIT
#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_SYS_SOCKETCALL
#endif
#define __ARCH_WANT_SYS_FORK
/*
POSIX标准中对vfork描述如下：
Applications are recommended to use the fork( ) function instead of this function。
也就是说，标准不建议实现vfork，
但是linux kernel还是保留了该系统调用，
一方面是有些应用对performance特别敏感，
vfork可以获得一些的性能优势。此外，在没有MMU支持的CPU上，
vfork＋exec来可以用来实现fork
*/
#define __ARCH_WANT_SYS_VFORK
#define __ARCH_WANT_SYS_CLONE

/*
 * Unimplemented (or alternatively implemented) syscalls
 */
#define __IGNORE_fadvise64_64
#define __IGNORE_migrate_pages

#endif /* __ASM_ARM_UNISTD_H */
