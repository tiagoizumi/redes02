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

// Os valores MIN e MAX devem ser os mesmos utilizados no rdt_send para o delay
#define MIN 10000
#define MAX 900000

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port> <file>\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
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
    
    FILE *file = fopen(argv[3], "r");
    if (!file) {
        perror("fopen");
        close(sockfd);
        return EXIT_FAILURE;
    }
    
    printf("Enviando arquivo: %s\n", argv[3]);
    
    int send_status = PART_SENT;
    // Chama rdt_send repetidamente até que o arquivo seja completamente transmitido
    while (send_status == PART_SENT) {
        send_status = rdt_send(sockfd, file, &server_addr);
        if (send_status == SEND_ERROR) {
            fprintf(stderr, "Erro durante a transmissão.\n");
            break;
        }
        // Delay entre chamadas (entre 10 ms e 900 ms)
        
    }
    
    if (send_status == FILE_DONE) {
        printf("Arquivo transmitido corretamente.\n");
    }
    
    fclose(file);
    close(sockfd);
    return EXIT_SUCCESS;
}
