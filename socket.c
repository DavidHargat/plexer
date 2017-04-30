#include "socket.h"

#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <arpa/inet.h>

#include "log.h"

int
plxr_socket_listen(struct sockaddr_in *addr, int port)
{
	int sockfd;
	static int YES = 1;

	bzero(addr, sizeof(struct sockaddr_in));
	addr->sin_addr.s_addr = INADDR_ANY;
	addr->sin_family = AF_INET;
	addr->sin_port = htons(port);

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0))
		== -1 )
	{
		ERROR("socket");
		goto error;
	}

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &YES, sizeof(YES))
		== -1 )
	{
		ERROR("setsockopt");
		goto error;
	}

	if (bind(sockfd, (struct sockaddr *) addr, sizeof(*addr))
		== -1 )
	{
		ERROR("bind");
		goto error;
	}

	if (listen(sockfd, SOCK_STREAM)
		== -1 )
	{
		ERROR("listen");
		goto error;
	}

	return sockfd;
error:
	close(sockfd); /* avoid leaking descriptors */
	return -1;
}

/* TODO: handle EAGAIN, EWOULDBLOCK. see write(2) */
int
plxr_socket_write_timeout(
	int fd,
	const char *data,
	size_t data_len,
	int timeout_milliseconds
){
	int ret;
	struct pollfd pfd = {
		.fd = fd,
		.events = POLLOUT,
		.revents = 0
	};

	switch (poll(&pfd, 1, timeout_milliseconds)) {

	case -1:
		ERROR("poll");
		return -1;

	case 0:
		WARN("poll timeout");
		return -1;

	case 1:
		if ((ret = write(fd, data, data_len)) == -1)
		{
			ERROR("write");
			return -1;
		}
		if (ret != data_len)
		{
			WARN("wrote less than the desired nbytes");
			return -1;
		}

		return ret; /* success! */
	}

	WARN("shouldn't get here");
	return -1;
}

int
plxr_socket_read_timeout(
	int fd,
	char *data,
	size_t data_max,
	int timeout_milliseconds
){
	int ret;
	struct pollfd pfd = {
		.fd = fd,
		.events = POLLIN,
		.revents = 0
	};

	switch (poll(&pfd, 1, timeout_milliseconds)) {

	case -1:
		ERROR("poll");
		return -1;

	case 0:
		WARN("poll timeout");
		return -1;

	case 1: /* successful poll(2) */
		ret = read(fd, (void *)data, data_max);

		if (ret == -1)
		{
			ERROR("read");
			return -1;
		}

		return ret;

	default:
		break;
	}

	WARN("shouldn't get here");
	return -1;
}

static char ntop_buffer[INET_ADDRSTRLEN];

const char *
plxr_socket_ntop(struct sockaddr *addr)
{
	struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
	bzero(ntop_buffer, sizeof(ntop_buffer));
	inet_ntop(AF_INET, &addr_in->sin_addr, ntop_buffer, INET_ADDRSTRLEN);
	return (const char *) ntop_buffer;
}
