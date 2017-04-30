
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "connection.h"
#include "socket.h"
#include "log.h"

/* since we're single threaded, we can use the same buffer for everyone */
static char request_buffer[4096];

int
plxr_connection_read(struct plxr_connection *conn)
{
	ssize_t ret;
	bzero(request_buffer, sizeof(request_buffer));

	ret = plxr_socket_read_timeout(
		conn->fd, request_buffer, sizeof(request_buffer), 500);

	if (ret <= 0)
	{
		return -1; /* read failed or was empty; don't bother parsing */
	}

	if (plxr_http_parse(&conn->request, request_buffer) == 0)
	{
		WARN("plxr_http_parse: failed");
		return -1;
	}

	return 0; /* success */
}

