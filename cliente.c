#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "rdt.h"
#define MIN 10000
#define MAX 900000

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("Uso: %s <ip_servidor> <porta_servidor> <arquivo>\n", argv[0]);
        return 0;
    }

    int sockfd;
    struct sockaddr_in server_addr;
    FILE *file;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Erro ao criar socket");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    inet_aton(argv[1], &server_addr.sin_addr);

    file = fopen(argv[3], "r");
    if (file == NULL) {
        perror("Erro ao abrir arquivo");
        return 1;
    }

    printf("Enviando arquivo: %s\n", argv[3]);
	usleep(rand() % (MAX - MIN + 1) + MIN);
	printf("asdasd ");
    rdt_send(sockfd, file, &server_addr);

    fclose(file);
    close(sockfd);
    return 0;
}
