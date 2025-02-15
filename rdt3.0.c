#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <sys/time.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>

#define MAX_MSG_LEN 16
#define ERROR -1
#define TRUE 1
#define FALSE 0
#define SUCCESS 0
#define WINDOW_SIZE 4
#define PART_SENT  1
#define FILE_DONE  0
#define SEND_ERROR -1
#define MIN_DELAY 10000    // 10ms
#define MAX_DELAY 900000  // 900ms
#define BUFFER_SIZE 32
#define MIN 100000
#define MAX 9000000

int biterror_inject = FALSE;

typedef uint16_t hsize_t;
typedef uint16_t hcsum_t;
typedef uint16_t hseq_t;
typedef uint8_t  htype_t;

#define PKT_ACK 0
#define PKT_DATA 1

hseq_t _snd_seqnum = 1;
hseq_t _rcv_seqnum = 1;

double EstRTT = 0.1;  
double DevRTT = 0.05;  
double alpha = 0.125; 
double beta = 0.25;   
double TimeoutInterval = 0.5; // Inicializando um valor padrão

struct hdr {
    hseq_t  pkt_seq;
    hsize_t pkt_size;
    htype_t pkt_type;
    hcsum_t csum;
};

typedef struct hdr hdr;

struct pkt {
    hdr h;
    unsigned char msg[MAX_MSG_LEN];
};
typedef struct pkt pkt;

unsigned short checksum(unsigned short *buf, int nbytes) {
    register long sum = 0;
    while (nbytes > 1) {
        sum += *(buf++);
        nbytes -= 2;
    }
    if (nbytes == 1)
        sum += *(unsigned char *) buf;
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    return (unsigned short) ~sum;
}

int iscorrupted(pkt *pr) {
    pkt pl = *pr;
    pl.h.csum = 0;
    return checksum((void *)&pl, pl.h.pkt_size) != pr->h.csum;
}

int make_pkt(pkt *p, htype_t type, hseq_t seqnum, void *msg, int msg_len) {
    if (msg_len > MAX_MSG_LEN) return ERROR;

    p->h.pkt_size = sizeof(hdr);
    p->h.csum = 0;
    p->h.pkt_type = type;
    p->h.pkt_seq = seqnum;
    
    if (msg_len > 0) {
        p->h.pkt_size += msg_len;
        memset(p->msg, 0, MAX_MSG_LEN);
        memcpy(p->msg, msg, msg_len);
    }
    
    p->h.csum = checksum((unsigned short *)p, p->h.pkt_size);
    return SUCCESS;
}


int rdt_send(int sockfd, char *buffer, size_t buffer_size, struct sockaddr_in *dst) {
    pkt sent_packets[2 * WINDOW_SIZE];        
    int acked[2 * WINDOW_SIZE] = {0};           
    int packets_in_flight = 0, base = _snd_seqnum, bytes_sent = 0;

    int window_size = 1;  // Inicializa a janela dinâmica

    struct timeval base_sent_time, current_time, timeout;
    fd_set readfds;

    while (bytes_sent < buffer_size || packets_in_flight > 0) {
        while (packets_in_flight < window_size && bytes_sent < buffer_size) {
            int index = _snd_seqnum % (2 * WINDOW_SIZE);
            int pkt_size = (buffer_size - bytes_sent > MAX_MSG_LEN) ? MAX_MSG_LEN : buffer_size - bytes_sent;

            make_pkt(&sent_packets[index], PKT_DATA, _snd_seqnum, buffer + bytes_sent, pkt_size);
            if (packets_in_flight == 0)  
                gettimeofday(&base_sent_time, NULL);

            sendto(sockfd, &sent_packets[index], sent_packets[index].h.pkt_size, 0, 
                   (struct sockaddr *)dst, sizeof(struct sockaddr_in));

            printf("Enviando pacote %d (%d bytes) - Janela: %d\n", _snd_seqnum, pkt_size, window_size);

            acked[index] = 0;  
            _snd_seqnum++;
            packets_in_flight++;
            bytes_sent += pkt_size;
        }

        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        timeout.tv_sec = (int)TimeoutInterval;
        timeout.tv_usec = (TimeoutInterval - timeout.tv_sec) * 1e6;  

        int ready = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
        if (ready == 0) { 
            printf("Timeout no pacote base %d. Reduzindo janela...\n", base);
            window_size = (window_size > 1) ? window_size / 2 : 1;
            gettimeofday(&base_sent_time, NULL);
            sendto(sockfd, &sent_packets[base % (2 * WINDOW_SIZE)], 
                   sent_packets[base % (2 * WINDOW_SIZE)].h.pkt_size, 
                   0, (struct sockaddr *)dst, sizeof(struct sockaddr_in));
            continue;
        }

        struct sockaddr_in dst_ack;
        socklen_t addrlen = sizeof(dst_ack);
        pkt ack;
        int nr = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&dst_ack, &addrlen);

        if (nr > 0 && !iscorrupted(&ack) && ack.h.pkt_type == PKT_ACK) {
            int ack_index = ack.h.pkt_seq % (2 * WINDOW_SIZE);
            if (!acked[ack_index]) { 
                acked[ack_index] = 1;  

                gettimeofday(&current_time, NULL);
                double SampleRTT = (current_time.tv_sec - base_sent_time.tv_sec) + 
                                   (current_time.tv_usec - base_sent_time.tv_usec) / 1e6;
                EstRTT = (1 - alpha) * EstRTT + alpha * SampleRTT;
                DevRTT = (1 - beta) * DevRTT + beta * fabs(SampleRTT - EstRTT);
                TimeoutInterval = EstRTT + 4 * DevRTT;

                printf("ACK recebido para o pacote %d\n", ack.h.pkt_seq);

                if (window_size < 2 * WINDOW_SIZE) {
                    window_size++;
                }

                while (packets_in_flight > 0 && acked[base % (2 * WINDOW_SIZE)]) {
                    acked[base % (2 * WINDOW_SIZE)] = 0;
                    packets_in_flight--;
                    base++;
                }
            }
        }

        if (bytes_sent >= buffer_size && packets_in_flight == 0) {
            return FILE_DONE;
        }
    }

    return PART_SENT;
}

int rdt_recv(int sockfd, FILE *file, struct sockaddr_in *src) {
    pkt p, ack;
    socklen_t addrlen;

    // Configuração da janela dinâmica
    int window_size = 1; // Começa com 1 (Slow Start)
    int received_count = 0;
    
    // Buffer para pacotes fora de ordem
    pkt recv_buffer[2 * WINDOW_SIZE];
    int received[2 * WINDOW_SIZE] = {0};

    struct timeval timeout, current_time;
    fd_set readfds;

    printf("Recebendo arquivo com janela dinâmica...\n");

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        timeout.tv_sec = (int)TimeoutInterval;
        timeout.tv_usec = (TimeoutInterval - timeout.tv_sec) * 1e6; 

        int ready = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
        if (ready == 0) {
            printf("Timeout detectado. Reduzindo janela...\n");
            window_size = (window_size > 1) ? window_size / 2 : 1;  // Diminui a janela após timeout
            continue;
        }

        addrlen = sizeof(struct sockaddr_in);
        int ns = recvfrom(sockfd, &p, sizeof(pkt), 0, (struct sockaddr*)src, &addrlen);
        if (ns < 0) {
            perror("recvfrom()");
            return SEND_ERROR;
        }

        int msg_size = p.h.pkt_size - sizeof(hdr);

        // **Caso seja um pacote de fim de transmissão**
        if (msg_size == 0) {
            printf("Fim da transmissão recebido (seq %d). Encerrando.\n", p.h.pkt_seq);
            make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0);
            sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, sizeof(struct sockaddr_in));
            break;
        }

        // **Se o pacote estiver corrompido, reenvia o último ACK válido**
        if (iscorrupted(&p)) {
            printf("Pacote corrompido (seq %d). Reenviando último ACK %d.\n", p.h.pkt_seq, _rcv_seqnum - 1);
            make_pkt(&ack, PKT_ACK, _rcv_seqnum - 1, NULL, 0);
            sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, sizeof(struct sockaddr_in));
            continue;
        }

        // **Se o pacote for o esperado, escreve no arquivo**
        if (p.h.pkt_seq == _rcv_seqnum) {
            printf("Pacote esperado recebido %d (%d bytes).\n", p.h.pkt_seq, msg_size);
            fwrite(p.msg, 1, msg_size, file);
            fflush(file);
            _rcv_seqnum++;
            received_count++;

            // **Verifica se há pacotes armazenados no buffer para entrega em ordem**
            while (received[_rcv_seqnum % (2 * WINDOW_SIZE)]) {
                fwrite(recv_buffer[_rcv_seqnum % (2 * WINDOW_SIZE)].msg, 1, 
                       recv_buffer[_rcv_seqnum % (2 * WINDOW_SIZE)].h.pkt_size - sizeof(hdr), file);
                fflush(file);
                received[_rcv_seqnum % (2 * WINDOW_SIZE)] = 0;
                _rcv_seqnum++;
                received_count++;
            }
        } 
        // **Se o pacote estiver dentro da janela, mas fora de ordem, armazena no buffer**
        else if (p.h.pkt_seq > _rcv_seqnum && p.h.pkt_seq < _rcv_seqnum + (2 * WINDOW_SIZE)) {
            int index = p.h.pkt_seq % (2 * WINDOW_SIZE);
            printf("Pacote fora de ordem %d armazenado para entrega futura.\n", p.h.pkt_seq);
            recv_buffer[index] = p;
            received[index] = 1;
        }

        // **Enviar ACK para o pacote recebido**
        make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0);
        sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, sizeof(struct sockaddr_in));
        printf("ACK enviado para o pacote %d\n", p.h.pkt_seq);

        // **Ajuste da Janela Dinâmica**
        if (received_count >= window_size) {
            window_size = (window_size < 2 * WINDOW_SIZE) ? window_size + 1 : 2 * WINDOW_SIZE;
            received_count = 0;
        }
    }

    printf("Arquivo recebido com sucesso!\n");
    return FILE_DONE;
}
