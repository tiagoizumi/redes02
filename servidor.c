#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "rdt.h"
#define FILE_DONE  0
int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Uso: %s <porta_servidor>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    
    // Criar socket UDP
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Erro ao criar socket");
        return EXIT_FAILURE;
    }

    // Configurar endereço do servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[1]));  
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Associar socket à porta
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erro ao fazer bind");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Servidor aguardando arquivos na porta %s...\n", argv[1]);

    // Abrir arquivo de saída
    FILE *output_file = fopen("output.txt", "wb");
    if (!output_file) {
        perror("Erro ao abrir arquivo de saída");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // Receber arquivo via RDT
    int recv_status = rdt_recv(sockfd, output_file, &client_addr);

    if (recv_status == FILE_DONE) {
        printf("Arquivo recebido com sucesso e salvo como output.txt\n");
    } else {
        fprintf(stderr, "Erro ao receber arquivo.\n");
    }

    // Fechar arquivo e socket
    fclose(output_file);
    close(sockfd);

    return EXIT_SUCCESS;
}
