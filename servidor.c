#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "rdt.h"



int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Uso: %s <porta_servidor>\n", argv[0]);
        return 0;
    }

    int sockfd;
    struct sockaddr_in server_addr, client_addr;

    // Criar socket UDP
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Erro ao criar socket");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[1]));  // Porta passada como argumento
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Fazer bind do socket à porta especificada
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erro ao fazer bind");
        return 1;
    }
    

    printf("Servidor aguardando mensagens na porta %s...\n", argv[1]);

    // Chamar `rdt_recv()` para receber os caracteres dos pacotes
    rdt_recv(sockfd, &client_addr);

    // Fechar socket após a recepção
    close(sockfd);
    return 0;
}
