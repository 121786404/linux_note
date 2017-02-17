#ifndef _LINUX_UN_H
#define _LINUX_UN_H

#define UNIX_PATH_MAX	108

struct sockaddr_un {
	sa_family_t sun_family;	/* AF_UNIX */
        /* 套接字绑定的文件路径名 */
	char sun_path[UNIX_PATH_MAX];	/* pathname */
};

#endif /* _LINUX_UN_H */
