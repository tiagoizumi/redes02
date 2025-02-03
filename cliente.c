#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "rdt.h"

int main(int argc, char **argv) {
	if (argc != 3) {
		printf("uso: %s <ip_servidor> <porta_servidor> \n", argv[0]);
		return 0;
	}
	int s;
	struct sockaddr_in saddr;
	s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	saddr.sin_port = htons(atoi(argv[2]));
	saddr.sin_family = AF_INET;
	inet_aton(argv[1], &saddr.sin_addr);

	int msg = 1000;
	rdt_send(s, &msg, sizeof(msg), &saddr);

	return 0;
}
