#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "rdt.h"

#define SEND_ERROR -1
#define FILE_DONE  0
#define PART_SENT  1
#define BUFFER_SIZE 4096  // Define o tamanho do buffer de leitura

 // 900ms

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <server_ip> <server_port> <file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Erro ao criar socket");
        return EXIT_FAILURE;
    }
    
    // Configura o socket para modo não bloqueante
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl F_GETFL");
        close(sockfd);
        return EXIT_FAILURE;
    }
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl F_SETFL");
        close(sockfd);
        return EXIT_FAILURE;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    if (inet_aton(argv[1], &server_addr.sin_addr) == 0) {
        fprintf(stderr, "IP inválido.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }
    
    FILE *file = fopen(argv[3], "rb");  // Abrir arquivo em modo binário para evitar problemas
    if (!file) {
        perror("Erro ao abrir arquivo");
        close(sockfd);
        return EXIT_FAILURE;
    }
    
    printf("Iniciando transmissão do arquivo: %s\n", argv[3]);
    
    char buffer[BUFFER_SIZE];
    int send_status = PART_SENT;
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        send_status = rdt_send(sockfd, buffer, bytes_read, &server_addr);
        
        if (send_status == SEND_ERROR) {
            fprintf(stderr, "Erro durante a transmissão.\n");
            break;
        }
        
        // Pequeno delay para evitar sobrecarga na rede
    }

    if (send_status == FILE_DONE) {
        printf("Arquivo transmitido corretamente.\n");
    }
    
    fclose(file);
    close(sockfd);
    return EXIT_SUCCESS;
}
