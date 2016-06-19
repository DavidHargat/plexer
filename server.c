#include "server.h"
#include "socket.h"
#include "settings.h"

socklen_t sockaddr_len = sizeof(struct sockaddr);

void error_fatal(char *reason){
	perror(reason);
	exit(EXIT_FAILURE);
}

void error_warning(char *reason){
	perror(reason);
}

void server_listen(server_t *server){
	if( (server->fd = socket_tcp_listen((struct sockaddr_in *)&server->addr, 8080))
		== SOCKET_ERROR )
	{
		FATAL("server_init socket_tcp_listen");
	}
}

void server_init(server_t *server){
	bzero(server, sizeof(server_t));

	server_listen(server);

	server->pfd.events = POLLIN;
	server->pfd.fd = server->fd;

	server->running = 1;
}


client_t *server_next_client(server_t *server){
	size_t i;

	for(i = 0; i < CLIENT_MAX; i++) {
		if(server->clients[i].status == CLIENT_INACTIVE) {
			server->clients[i].pfd = &server->client_pfds[i];
			return &server->clients[i];
		}
	}
	
	return NULL;
}

void server_client_init(client_t *client){
	client->status = CLIENT_ACTIVE;
	client->pfd->fd = client->fd;
	client->pfd->events = POLLIN | POLLOUT;
}

void server_client_active(server_t *server, client_t *client){
	server->active_clients++;
	server_client_init(client);
}

void server_client_inactive(server_t *server, client_t *client){
	bzero(client, sizeof(client_t));
	server->active_clients--;
}

void server_client_shutdown(server_t *server, client_t *client){
	shutdown(client->fd, SHUT_RDWR);
	close(client->fd);
	server_client_inactive(server, client);
}

void server_accept_blocking(server_t *server){
	client_t *client;
	
	client = server_next_client(server);
	client->fd = accept(server->fd, &client->addr, &sockaddr_len);
	
	if( client->fd == SOCKET_ERROR ) {
		ERROR("server_accept_blocking accept");
	}else{
		server_client_active(server, client);
	}
}

int server_accept_nonblocking(server_t *server){
	int result;

	switch( result = poll(&server->pfd, 1, SERVER_TIMEOUT) ){
	case -1:
		ERROR("server_accept_nonblocking poll");
		break;
	case  0:
		// timeout
		WARNING("server_accept_nonblocking poll timeout");
		break;
	default:
		if(server->pfd.revents & POLLIN) {
			server_accept_blocking(server);
		}else{
			WARNING("server_accept_nonblocking poll revents");
		}
	}
	
	return result;
}

void server_handle_error(server_t *server, client_t *client){
	WARNING("client error");
	server_client_inactive(server, client);
}

void server_handle_closed(server_t *server, client_t *client){
	WARNING("client closed");
	server_client_inactive(server, client);
}

void server_handle_invalid(server_t *server, client_t *client){
	WARNING("client invalid");
	server_client_inactive(server, client);
}

void server_handle_read(server_t *server, client_t *client){
	if(server->event_read) {
		server->event_read(server, client);
	}
}

void server_handle_write(server_t *server, client_t *client){
	if(server->event_write) {
		server->event_write(server, client);
	}
}

void server_register_read(server_t *server, void (*callback)(server_t *, client_t *) ){
	server->event_read = callback;
}

void server_register_write(server_t *server, void (*callback)(server_t *, client_t *) ){
	server->event_write = callback;
}

int server_poll_all_clients(server_t *server){
	return poll(server->client_pfds, CLIENT_MAX, CLIENT_TIMEOUT); 
}

void server_handle_client_events(server_t *server, client_t *client){
	int events;

	events = client->pfd->revents;

	if(events & POLLERR)
		return server_handle_error(server, client);

	if(events & POLLHUP)
		return server_handle_closed(server, client);

	if(events & POLLNVAL)
		return server_handle_invalid(server, client); 

	if(events & POLLOUT)
		if(client->status == CLIENT_ACTIVE)
			server_handle_write(server, client);

	if(events & POLLIN)
		if(client->status == CLIENT_ACTIVE)
			server_handle_read(server, client);

}

void server_handle_client(server_t *server, client_t *client){
	switch( poll(client->pfd, 1, CLIENT_TIMEOUT) ) {
	case -1:
		ERROR("server_handle_client poll");
		break;
	case  0:
		WARNING("server_handle_client poll timeout");
		break;
	default:
		server_handle_client_events(server, client);
	}
}

void server_handle_all_clients(server_t *server){
	size_t i;
	for(i = 0; i < CLIENT_MAX; i++){
		if(server->clients[i].status == CLIENT_ACTIVE) {
			server_handle_client_events(server, &server->clients[i]);
		}
	}
}

ssize_t server_read(server_t *server, client_t *client, char *buf, size_t size){
	ssize_t result;
	result = read(client->fd, buf, size);

	if( result > 0 ) {
		client->bytes_read += result;
	}
	
	return result;
}

ssize_t server_write(server_t *server, client_t *client, char *buf, size_t size){
	ssize_t result;
	result = write(client->fd, buf, size);

	if( result > 0 ) {
		client->bytes_wrote += result;
	}
	
	return result;
}

void server_run(server_t *server){
	size_t i;

	while( server->running ){
		size_t free_clients = (CLIENT_MAX - server->active_clients);

		for(i = 0; i < free_clients; i++) {
			if( server_accept_nonblocking(server) <= 0 ) {
				break;
			}
		}
		
		if( server->active_clients == 0 ) {
			printf("accept blocking\n");
			server_accept_blocking(server);
		}

		printf("active clients: %lu\n", server->active_clients);

		switch( server_poll_all_clients(server) ){
		case -1:
			ERROR("server_run server_poll_clients");
			break;
		case  0: 
			WARNING("server_run poll timeout");
			break;
		default:
			server_handle_all_clients(server);
		}
	}
}
