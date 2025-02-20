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
#define WINDOW_SIZE 16
#define WINDOW_SIZE 16
#define PART_SENT  1
#define FILE_DONE  0
#define SEND_ERROR -1
#define BUFFER_SIZE 32
#define MIN 100000
#define MAX 9000000
#define LOSS_RATE 15

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
double TimeoutInterval = 0.2; // Inicializando um valor padrão
double TimeoutInterval = 0.2; // Inicializando um valor padrão

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


// Add these enum declarations after your existing definitions
enum CongestionState {
    SLOW_START,
    CONGESTION_AVOIDANCE,
};

#include <fcntl.h>
#include <errno.h>

int rdt_send(int sockfd, char *buffer, size_t buffer_size, struct sockaddr_in *dst) {
    pkt sent_packets[2 * WINDOW_SIZE];
    int acked[2 * WINDOW_SIZE] = {0};
    int packets_in_flight = 0;
    int base = _snd_seqnum;
    int bytes_sent = 0;
    int last_ack_received = -1;
    int ssthresh = WINDOW_SIZE/2;
    enum CongestionState state = SLOW_START;
    int window_size = 1;  // Inicializa com window size 1 (Slow Start)

    struct timeval base_sent_time, current_time, timeout;
    fd_set readfds, writefds;

    // Configura o socket para modo não bloqueante
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    while (bytes_sent < buffer_size || packets_in_flight > 0) {
        // Envia pacotes enquanto houver espaço na janela e dados a enviar
        while (packets_in_flight < window_size && bytes_sent < buffer_size) {
            int index = _snd_seqnum % (2 * WINDOW_SIZE);
            int pkt_size = (buffer_size - bytes_sent > MAX_MSG_LEN) ? MAX_MSG_LEN : buffer_size - bytes_sent;
        
            make_pkt(&sent_packets[index], PKT_DATA, _snd_seqnum, buffer + bytes_sent, pkt_size);
            
            // Register the time only for the first packet in the window
            if (packets_in_flight == 0) {
                gettimeofday(&base_sent_time, NULL);
            }
            
            // Simulate packet delay (loss) randomly

            // Inside the sending loop:
            if (rand() % 100 >= 0) { // Only send if not "lost"
                sendto(sockfd, &sent_packets[index], sent_packets[index].h.pkt_size, 0, 
                    (struct sockaddr *)dst, sizeof(struct sockaddr_in));

                printf("Sending packet %d (%d bytes) - Window: %d, State: %s\n", 
                    _snd_seqnum, pkt_size, window_size, 
                    state == SLOW_START ? "SLOW_START" : "CONGESTION_AVOIDANCE");
            } else {
                printf("** Simulating LOSS of packet %d\n", _snd_seqnum);
                usleep(rand() % 200000); // 100000 microseconds = 100ms
            }
                        
            
            
        
            acked[index] = 0;
            _snd_seqnum++;
            can_send--;
            bytes_sent += pkt_size;
            packets_in_flight++;
        }
        

        // Configura o select() com timeout
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_SET(sockfd, &readfds);
        FD_SET(sockfd, &writefds);
        
        timeout.tv_sec = (int)TimeoutInterval;
        timeout.tv_usec = (TimeoutInterval - timeout.tv_sec) * 1e6;
        
        printf("timeout:%ld\n",timeout.tv_usec);
        int ready = select(sockfd + 1, &readfds, &writefds, NULL, &timeout);
        
        // Caso ocorra timeout, reduz a janela e retransmite o pacote base
        if (ready == 0) {
            printf("Timeout on packet base %d. Reducing window...\n", base);
            
            ssthresh = window_size / 2;
            window_size = 1;
            state = SLOW_START;

            gettimeofday(&base_sent_time, NULL);
            sendto(sockfd, &sent_packets[base % (2 * WINDOW_SIZE)],
                   sent_packets[base % (2 * WINDOW_SIZE)].h.pkt_size, 0,
                   (struct sockaddr *)dst, sizeof(struct sockaddr_in));
            continue;
        }

        // Recebe ACK
        struct sockaddr_in dst_ack;
        socklen_t addrlen = sizeof(dst_ack);
        pkt ack;
        int nr = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&dst_ack, &addrlen);

        if (nr > 0 && !iscorrupted(&ack) && ack.h.pkt_type == PKT_ACK) {
            int ack_index = ack.h.pkt_seq % (2 * WINDOW_SIZE);
            
            // Atualiza o último ACK recebido (ignora duplicatas sem acionar fast recovery)
            if (ack.h.pkt_seq != last_ack_received) {
                last_ack_received = ack.h.pkt_seq;
            }

            if (!acked[ack_index]) {
                acked[ack_index] = 1;

                // Atualiza as estimativas de RTT
                gettimeofday(&current_time, NULL);
                double SampleRTT = (current_time.tv_sec - base_sent_time.tv_sec) + 
                                   (current_time.tv_usec - base_sent_time.tv_usec) / 1e6;
                EstRTT = (1 - alpha) * EstRTT + alpha * SampleRTT;
                DevRTT = (1 - beta) * DevRTT + beta * fabs(SampleRTT - EstRTT);
                TimeoutInterval = EstRTT + 4 * DevRTT;

                printf("ACK received for packet %d - RTT: %.3f, Timeout: %.3f\n", 
                       ack.h.pkt_seq, SampleRTT, TimeoutInterval);

                // Ajusta a janela com base no estado de congestionamento
                switch (state) {
                    case SLOW_START:
                        if (window_size >= ssthresh) {
                            state = CONGESTION_AVOIDANCE;
                        } else {
                            window_size *= 2;
                        }
                        break;
                    case CONGESTION_AVOIDANCE:
                        window_size += 1.0;
                        break;
                }

                // Garante que a janela não ultrapasse o máximo permitido
                if (window_size > 2 * WINDOW_SIZE) {
                    window_size = 2 * WINDOW_SIZE;
                }

                // Desliza a janela removendo pacotes já reconhecidos
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
        
        // Se o socket estiver pronto para escrita, o loop interno já cuidará do envio.
    }
    
    if (bytes_sent >= buffer_size && packets_in_flight == 0)
        return FILE_DONE;
    
    return PART_SENT;
}



int rdt_recv(int sockfd, FILE *file, struct sockaddr_in *src) {
    pkt p, ack;
    socklen_t addrlen;
    int window_size = 1;
    int received_count = 0;
    
    // Buffer for out-of-order packets
    pkt recv_buffer[2 * WINDOW_SIZE];    // Buffer size is 2 * WINDOW_SIZE
    int received[2 * WINDOW_SIZE] = {0};

    struct timeval timeout;
    fd_set readfds;

    printf("Receiving file with dynamic window...\n");

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        timeout.tv_sec = (int)TimeoutInterval;
        timeout.tv_usec = (TimeoutInterval - timeout.tv_sec) * 1e6;

        int ready = select(sockfd + 1, &readfds, NULL, NULL, &timeout);
        if (ready == 0) {
            printf("Receiver timeout. Reducing window...\n");
            window_size = (window_size > 1) ? window_size / 2 : 1;
            continue;
        }

        addrlen = sizeof(struct sockaddr_in);
        int ns = recvfrom(sockfd, &p, sizeof(pkt), 0, (struct sockaddr*)src, &addrlen);
        if (ns < 0) {
            perror("recvfrom()");
            return SEND_ERROR;
        }

        int msg_size = p.h.pkt_size - sizeof(hdr);

        // End of transmission packet
        if (msg_size == 0) {
            printf("End of transmission received (seq %d)\n", p.h.pkt_seq);
            make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0);
            sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, sizeof(struct sockaddr_in));
            break;
        }

        // Corrupted packet handling
        if (iscorrupted(&p)) {
            printf("Corrupted packet (seq %d). Resending ACK %d\n", p.h.pkt_seq, _rcv_seqnum - 1);
            make_pkt(&ack, PKT_ACK, _rcv_seqnum - 1, NULL, 0);
            sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, sizeof(struct sockaddr_in));
            continue;
        }

        // Handle in-order packet
        if (p.h.pkt_seq == _rcv_seqnum) {
            printf("In-order packet received %d (%d bytes)\n", p.h.pkt_seq, msg_size);
            fwrite(p.msg, 1, msg_size, file);
            fflush(file);
            _rcv_seqnum++;
            received_count++;

            // Process buffered packets
            while (received[_rcv_seqnum % (2 * WINDOW_SIZE)]) {
                fwrite(recv_buffer[_rcv_seqnum % (2 * WINDOW_SIZE)].msg, 1, 
                       recv_buffer[_rcv_seqnum % (2 * WINDOW_SIZE)].h.pkt_size - sizeof(hdr), file);
                fflush(file);
                received[_rcv_seqnum % (2 * WINDOW_SIZE)] = 0;
                _rcv_seqnum++;
                received_count++;
            }
        }
        // Handle out-of-order packet within window
        else if (p.h.pkt_seq > _rcv_seqnum && p.h.pkt_seq < _rcv_seqnum + (2 * WINDOW_SIZE)) {
            int index = p.h.pkt_seq % (2 * WINDOW_SIZE);
            printf("Out-of-order packet %d buffered\n", p.h.pkt_seq);
            recv_buffer[index] = p;
            received[index] = 1;
        }

        // Send ACK
        make_pkt(&ack, PKT_ACK, _rcv_seqnum - 1, NULL, 0);
        sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, sizeof(struct sockaddr_in));
        printf("ACK sent for packet %d\n", _rcv_seqnum - 1);

        // Window size adjustment
        if (received_count >= window_size) {
            window_size = (window_size < 2 * WINDOW_SIZE) ? window_size + 1 : 2 * WINDOW_SIZE;
            received_count = 0;
        }
    }

    printf("File received successfully!\n");
    return FILE_DONE;
}