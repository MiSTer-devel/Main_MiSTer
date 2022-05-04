#define MAX_CONNECTIONS 10

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <strings.h>
#include <linux/input.h>
#include <stdint.h>
#include <string.h>
#include "input.h"

bool initialized = false;

struct pollfd sockets[MAX_CONNECTIONS + 1];

struct input_packet_t {
	uint8_t inputno;
	struct input_event ev;
};

void input_socket_init(void) {
	if (!initialized) {
		int port = 22101;
		struct sockaddr_in addr;
		bzero(&addr, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = INADDR_ANY;
		int reuse = 1;
		int sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
		if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *) &reuse, sizeof(reuse)) < 0) {
			printf("can't setsockopt\n");
			return;
		}
		if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
			printf("can't bind port\n");
			return;
		}
		if (listen(sock, 1) != 0) {
			printf("can't listen to port\n");
			return;
		}
		printf("Listening on port %d\n", port);
		printf("Size of struct input_packet_t: %d\n", sizeof(struct input_packet_t));
		printf("Size of struct input_event:    %d\n", sizeof(struct input_event));
		for (int i = 1; i < MAX_CONNECTIONS + 1; i++) {
			sockets[i].fd = -1;
		}
		sockets[0].fd = sock;
		sockets[0].events = POLLIN;
		initialized = true;
	} else {
		printf("sockets already initialized, reinit unneccesary\n");
	}
}

void input_socket_send(uint8_t inputno, struct input_event *ev, devInput *input) {
	int packet_size = sizeof(struct input_event) + 1 + 2 + 2;
	char packet[packet_size];
	//struct input_packet_t packet;
	//packet.inputno = inputno;
	printf("%04x:%04x", input[inputno].vid, input[inputno].pid);
	packet[0] = inputno;
	memcpy(packet + 1, &input[inputno].vid, sizeof(input[inputno].vid));
	memcpy(packet + 3, &input[inputno].pid, sizeof(input[inputno].pid));
	memcpy(packet + 5, ev, sizeof(struct input_event));
	//memcpy(&(packet.inputno), ev, sizeof(struct input_event));

	for (int i = 1; i < MAX_CONNECTIONS + 1; i++) {
		if (sockets[i].fd >= 0) {
			write(sockets[i].fd, (char *) &packet, packet_size);
		}
	}
}

int input_socket_poll(int timeout) {
	int return_value = poll(sockets, MAX_CONNECTIONS + 1, timeout);
	if (return_value < 0) {
		printf("input_socket_poll polling error!\n");
		return -1;
	} else if (return_value == 0) {
		return 0;
	}
	printf("got stuff to do!\n");
	for (int i = 0; i < MAX_CONNECTIONS + 1; i++) {
		if (sockets[i].fd >= 0) {
			// close and un-slot the socket if it's in an error state
			if (sockets[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				printf("closing socket %d\n", i);
				close(sockets[i].fd);
				sockets[i].fd = -1;
			// does the socket require servicing?
			} else if (sockets[i].revents & POLLIN) {
				// socket 0 is our listen socket; accept a new client.
				if (i == 0) {
					// accept connection
					int client = accept(sockets[i].fd, NULL, NULL);
					// look for an empty slot to put it in
					for (int j = 1; j < MAX_CONNECTIONS + 1; j++) {
						if (sockets[j].fd < 0) {
							printf("accepting socket %d\n", j);
							sockets[j].fd = client;
							sockets[j].events = POLLIN;
							sockets[j].revents = 0;
							client = -1;
							break;
						}
					}
					// no slot? un-accept the connection.
					if (client >= 0) {
						printf("rejecting connection\n");
						close(client);
					}
				// any other socket? I'm sure it has something interesting to say, but we just don't care
				} else {
					printf("ignoring chatter from socket %d\n", i);
					char trash[1024];
					int rlen = read(sockets[i].fd, trash, 1024);
					if (rlen == 0) {
						printf("closing socket %d\n", i);
						close(sockets[i].fd);
						sockets[i].fd = -1;
					}
				}
			}
		}
	}
	return 0;
}

void input_socket_destroy(void) {
	printf("shutting down open sockets\n");
	for (int i = 0; i < MAX_CONNECTIONS + 1; i++) {
		if (sockets[i].fd >= 0) {
			close(sockets[i].fd);
		}
	}
}
