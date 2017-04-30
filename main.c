#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>

#include "server.h"
#include "socket.h"
#include "connection.h"
#include "version.h"
#include "log.h"

struct options {
	char *port;
	char verbose;
	int help;
};

static struct options opts = {0};

void parse_args(int argc, char *argv[])
{
	int ch;

	while ((ch = getopt(argc, argv, "p:hv")) != -1)
	{
		switch (ch)
		{
		case 'v':
			opts.verbose = 1;
			break;
		case 'p':
			opts.port = optarg;
			break;
		case 'h':
		default:
			opts.help = 1;
		}
	}
}

void sigpipe_handler(int sig)
{
	if (opts.verbose) {
		printf("[!] caught SIGPIPE\n");
	}
}

void print_usage(char *name)
{
	printf(
		"usage: %s [-hv] [-p port]\n"
		"  -h  display this help page\n"
		"  -v  verbose\n"
		"  -p  port number\n",
		name
	);
}

static socklen_t socklen = sizeof(struct sockaddr);

int main(int argc, char *argv[])
{
	struct sockaddr_in listen_addr;
	struct plxr_connection conn = {0};
	int listen_fd;
	int port = 8080;

	parse_args(argc, argv);

	if (opts.help) {
		print_usage(argv[0]);
		return 0;
	}

	if (opts.port) {
		port = atoi(opts.port);
	}

	/* ignore SIGPIPE */
	signal(SIGPIPE, sigpipe_handler);

	/* disable buffered output */
	setvbuf(stdout, NULL, _IONBF, 0);

	listen_fd = plxr_socket_listen(&listen_addr, port);
	if (listen_fd == -1)
	{
		WARN("plxr_socket_listen: failed");
		return 1;
	}

	if (opts.verbose) {
		printf("version: %s\n", PLXR_VERSION_STRING);
		printf("listening on port: %d\n", port);
	}

	do {
		bzero(&conn, sizeof(conn));

		conn.fd = accept(listen_fd, &conn.addr, &socklen);
		if (conn.fd == -1)
		{
			ERROR("accept");
			continue;
		}

		while (plxr_connection_read(&conn) == 0) {
			if (plxr_serve_file_or_dir(&conn) == 0) {
				/* TODO: this probably isn't very safe since uri is user input.
				 * also these are logstalgia compatible logs :)
				 */
				if (opts.verbose)
					printf("%lu|%s|%s|%d|%lu\n",
						conn.resp_timestamp,
						plxr_socket_ntop(&conn.addr),
						conn.request.uri,
						conn.resp_status_code,
						conn.resp_len);
			}
		}

		shutdown(conn.fd, SHUT_RDWR);
		close(conn.fd);
	}
	while (1);

	return 0;
}
