#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/md5.h>

#include "rdt.h"
#include "hash.h"

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
	FILE* fp = fopen(argv[3], "r");

	if(fp == NULL) {
		perror("error in opening file");
		exit(1);
	}

	if(rdt_send(s, fp, &saddr) == -1) {
		perror("error in sending data");
		exit(1);
	}

	return 0;
}
