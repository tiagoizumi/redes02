#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "rdt.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("uso: %s <porta_servidor>\n", argv[0]);
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

    while (1) {
        int msg;
        socklen_t caddr_len = sizeof(caddr);

        // sleep(6);

        int r = rdt_recv(sockfd, &msg, sizeof(msg), &caddr);
        if (r < 0) {
            printf("Erro ao receber mensagem.\n");
        } else {
            printf("Mensagem recebida: %d\n", msg);
        }
    }

    return 0;
}
