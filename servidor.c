#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "rdt.h"

int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[MAX_MSG_LEN];
    int buf_len;

    // Configuração do socket e do servidor
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080); // Porta do servidor
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    while (1) { // Loop contínuo para receber pacotes
        printf("Aguardando mensagem...\n");
        buf_len = rdt_recv(sockfd, buffer, MAX_MSG_LEN, &client_addr);

        if (buf_len > 0) {
            buffer[buf_len] = '\0';
            printf("Mensagem recebida: %s\n", buffer);
        }
    }

    close(sockfd);
    return 0;
}

