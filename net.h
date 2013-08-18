#ifndef __NET_H
#define __NET_H

#include <sys/types.h>

int tcp_client(char *host, int port);
int write_all(int fd, const void *buf, size_t count);

#endif
