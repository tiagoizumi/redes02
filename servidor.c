#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "rdt.h"

#define MIN 10
#define MAX 900

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("uso: %s <porta_servidor>\n", argv[0]);
        return 0;
    }

    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[MAX_MSG_LEN];
    int buf_len;

    // Configuração do socket e do servidor
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080); // Porta do servidor
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        perror("Erro ao fazer bind");
        return 1;
    }

    printf("Servidor aguardando mensagens na porta %s...\n", argv[1]);

    while (1) {
        int msg;
        socklen_t caddr_len = sizeof(caddr);

        usleep(rand() % (MAX - MIN + 1) + MIN);

        int r = rdt_recv(sockfd, &msg, caddr_len, &caddr);
        if (r < 0) {
            printf("Erro ao receber mensagem.\n");
        } else {
            printf("Mensagem recebida: %d\n", msg);
        }
    }

    close(sockfd);
    return 0;
}

