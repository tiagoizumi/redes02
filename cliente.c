#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "rdt.h"

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[MAX_MSG_LEN];
    int buf_len;

    // Configuração do socket e do servidor
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080); // Porta do servidor
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    while (1) { // Loop contínuo para envio de pacotes
        printf("Digite a mensagem para enviar: ");
        fgets(buffer, MAX_MSG_LEN, stdin);
        buf_len = strlen(buffer);

        // Enviar a mensagem
        rdt_send(sockfd, buffer, buf_len, &server_addr);

        printf("Mensagem enviada: %s\n", buffer);
    }

    close(sockfd);
    return 0;
}
