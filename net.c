#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "debug.h"
#include "net.h"

#define AI_FLAGS_CLIENT AI_NUMERICSERV

int write_all(int fd, const void *buf, size_t count)
{
	const char *s;
	int n;

	s = buf;
	while ((n = write(fd, buf, count)) > 0) {
		count -= n;
		s += n;
	}

	return s - (const char *) buf;
}

int tcp_client(char *host, int port)
{
	struct addrinfo *result, *rp;
	struct addrinfo hints;
	char service[16];
	int retval;
	int sock;
	int tmp;

	snprintf(service, sizeof(service), "%d", port);

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_FLAGS_CLIENT;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	retval = -1;
	result = NULL;
	tmp = getaddrinfo(host, service, &hints, &result);
	if (tmp != 0) {
		msg_warn("getaddrinfo: %s", gai_strerror(tmp));
		goto out;
	}

	tmp = 1;
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sock < 0) {
			msg_err("%s", "socket");
			continue;
		}

		if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
			break;

		close(sock);
	}

	if (rp == NULL) {
		msg_warn("%s", "unable to connect");
		goto out;
	}

	retval = sock;

out:
	if (result != NULL)
		freeaddrinfo(result);

	return retval;
}

