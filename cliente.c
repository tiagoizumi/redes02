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
	FILE* fp;
	struct sockaddr_in saddr;
	s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	saddr.sin_port = htons(atoi(argv[2]));
	saddr.sin_family = AF_INET;
	inet_aton(argv[1], &saddr.sin_addr);
	fp = fopen(argv[3], "r");
	if(fp == NULL) {
		perror("error in opening file");
		exit(1);
	}

  char data[SIZE] = {0};
	while(fgets(data, SIZE, fp) != NULL)
	{
		if(rdt_send(s, data, SIZE, &saddr) == -1)
		{
			perror("error in sending data");
			exit(1);
		}
		bzero(data, SIZE);
	}
	/*
	int msg = 1000;
	rdt_send(s, &msg, sizeof(msg), &saddr);
	*/
	return 0;
}
