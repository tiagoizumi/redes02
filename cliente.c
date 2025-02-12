#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "rdt.h"

int main(int argc, char **argv) {
	if (argc != 4) {
		printf("uso: %s <ip_servidor> <porta_servidor> <nome_arquivo>\n", argv[0]);
		return 0;
	}
	int s, bytes_read, status;
  char msg[SIZE];
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
  
  int i = 0;

  while((bytes_read = fread(msg, 1, SIZE, fp)) > 0) {
    status = rdt_send(s, msg, &saddr, SIZE, bytes_read, i);
    if(status == -1) {
      perror("error in sending data");
      exit(1);
    } else i++;
    bzero(msg, SIZE);
    usleep(rand() % (MAX - MIN + 1) * 1000);
  }
  
	return 0;
}
