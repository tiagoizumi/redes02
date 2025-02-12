#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "rdt.h"

#define MIN 10
#define MAX 900
#define SIZE 1000

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("uso: %s <porta_servidor> <nome_arquivo>\n", argv[0]);
        return 0;
    }

    int sockfd;
    struct sockaddr_in saddr, caddr;

    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        perror("Erro ao criar o socket");
        return 1;
    }

    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(atoi(argv[1]));
    saddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        perror("Erro ao fazer bind");
        return 1;
    }

    printf("Servidor aguardando mensagens na porta %s...\n", argv[1]);

    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
        perror("teste");

		FILE* file = fopen(argv[2], "a");
		if(file < 0) return ERROR;

    int i = 0;

    while (1) {
        int r = rdt_recv(sockfd, file, &caddr, SIZE, i);
        if (r < 0) printf("Erro ao receber mensagem.\n");
        else i++;
    }

    return 0;
}
