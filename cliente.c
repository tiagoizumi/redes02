#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "rdt.h"
#define MIN 10000
#define MAX 900000

#define PART_SENT  1
#define FILE_DONE  0
#define SEND_ERROR -1

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

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    if (inet_aton(argv[1], &server_addr.sin_addr) == 0) {
        fprintf(stderr, "Invalid IP address.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    FILE *file = fopen(argv[3], "r");
    if (!file) {
        perror("fopen");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Sending file: %s\n", argv[3]);

    int send_status;
    do {
        send_status = rdt_send(sockfd, file, &server_addr);
        if (send_status == SEND_ERROR) {
            fprintf(stderr, "Error during transmission.\n");
            break;
        }
        // Wait 100 ms before sending the next part
		float delay = (rand() % (MAX - MIN + 1) + MIN);
		printf("delay: %f\n",delay);
		usleep(delay);
    } while (send_status == PART_SENT);

    if (send_status == FILE_DONE) {
        printf("File transmission completed successfully.\n");
    }

    fclose(file);
    close(sockfd);
    return EXIT_SUCCESS;
}
