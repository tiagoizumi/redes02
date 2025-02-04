#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "rdt.h"

#define SIZE 16

int main(int argc, char **argv) {
	if (argc != 4) {
		printf("uso: %s <ip_servidor> <porta_servidor> <nome_arquivo>\n", argv[0]);
		return 0;
	}
	int s;
	struct sockaddr_in saddr;
	s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	saddr.sin_port = htons(atoi(argv[2]));
	saddr.sin_family = AF_INET;
	inet_aton(argv[1], &saddr.sin_addr);

	//int msg = 1000;
	rdt_send(s, "teste.txt", &saddr);
	return 0;
}
