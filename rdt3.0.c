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
#define WINDOW_SIZE 4 
#define ERROR -1
#define SUCCESS 0
#define PART_SENT  1
#define FILE_DONE  0
#define SEND_ERROR -1
#define INIT_WINDOW_SIZE 1  // Tamanho inicial da janela de congestionamento

typedef uint16_t hsize_t;
typedef uint16_t hcsum_t;
typedef uint16_t hseq_t;
typedef uint8_t  htype_t;

#define PKT_ACK 0
#define PKT_DATA 1

// Controle de sequência
hseq_t _snd_seqnum = 1;
hseq_t _rcv_seqnum = 1;

// Parâmetros de RTT
double EstRTT = 1.0;  
double DevRTT = 0.5;  
double alpha = 0.125; 
double beta = 0.25;   

// Estrutura de cabeçalho do pacote
struct hdr {
    hseq_t  pkt_seq;
    hsize_t pkt_size;
    htype_t pkt_type;
    hcsum_t csum;
};
typedef struct hdr hdr;

// Estrutura do pacote
struct pkt {
    hdr h;
    unsigned char msg[MAX_MSG_LEN];
};
typedef struct pkt pkt;

// Função para calcular checksum do pacote
unsigned short checksum(unsigned short *buf, int nbytes) {
    long sum = 0;
    while (nbytes > 1) {
        sum += *(buf++);
        nbytes -= 2;
    }
    if (nbytes == 1) 
        sum += *(unsigned short *) buf;
    while (sum >> 16) 
        sum = (sum & 0xffff) + (sum >> 16);
    return (unsigned short) ~sum;
}

// Verifica se o pacote está corrompido
int iscorrupted(pkt *pr) {
    pkt pl = *pr;
    pl.h.csum = 0;
    unsigned short csuml = checksum((void *)&pl, pl.h.pkt_size);
    return (csuml != pr->h.csum);
}

// Cria um pacote com os dados fornecidos
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

// Função principal de envio de pacotes (rdt_send)
int rdt_send(int sockfd, FILE *file, struct sockaddr_in *dst) {
    static int state_initialized = 0;
    static int packets_in_flight = 0;
    static int file_end = 0;
    static hseq_t base = 0;
    static pkt sent_packets[WINDOW_SIZE];
    static int sent_sizes[WINDOW_SIZE];
    static struct timeval sent_time[WINDOW_SIZE];
    static int cwnd = INIT_WINDOW_SIZE;
    static int dup_ack_count = 0;       // Contador de ACKs duplicados

    if (!state_initialized) {
        base = _snd_seqnum;
        packets_in_flight = 0;
        file_end = 0;
        state_initialized = 1;
    }

    // Envio de pacotes dentro da janela
    // cwnd = WINDOW_SIZE;
    while (packets_in_flight < cwnd && !file_end) {
        char data_buffer[MAX_MSG_LEN];
        int bytes_read = fread(data_buffer, 1, MAX_MSG_LEN, file);
        
        if (bytes_read <= 0) { 
            file_end = 1;
            break;
        }

        pkt p;
        memset(&p, 0, sizeof(pkt));
        if (make_pkt(&p, PKT_DATA, _snd_seqnum, data_buffer, bytes_read) < 0)
            return SEND_ERROR;

        int index = _snd_seqnum % cwnd;
        sent_packets[index] = p;
        sent_sizes[index] = bytes_read;
        gettimeofday(&sent_time[index], NULL);

        if (sendto(sockfd, &p, p.h.pkt_size, 0, (struct sockaddr *)dst, sizeof(struct sockaddr_in)) < 0)
            return SEND_ERROR;

        _snd_seqnum++;
        packets_in_flight++;
    }

    // Configuração de timeout
    double TimeoutInterval = EstRTT + 4 * DevRTT;
    struct timeval timeout;
    timeout.tv_sec = (int)TimeoutInterval;
    timeout.tv_usec = (TimeoutInterval - timeout.tv_sec) * 1e6;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in dst_ack;
    int addrlen = sizeof(dst_ack);
    pkt ack;
    int nr = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&dst_ack, (socklen_t *)&addrlen);

    if (nr > 0 && !iscorrupted(&ack) && ack.h.pkt_type == PKT_ACK) {
        int ack_index = ack.h.pkt_seq % WINDOW_SIZE;
        struct timeval current_time;
        gettimeofday(&current_time, NULL);
        
        double SampleRTT = (current_time.tv_sec - sent_time[ack_index].tv_sec) +
                           (current_time.tv_usec - sent_time[ack_index].tv_usec) / 1e6;
        EstRTT = (1 - alpha) * EstRTT + alpha * SampleRTT;
        DevRTT = (1 - beta) * DevRTT + beta * fabs(SampleRTT - EstRTT);

        if (ack.h.pkt_seq >= base) {
            int num_acked = (ack.h.pkt_seq - base) + 1;
            packets_in_flight -= num_acked;
            base = ack.h.pkt_seq + 1;
            if (cwnd < ssthresh) {
                cwnd += num_acked;  // Slow Start
            } else {
                cwnd += 1.0 / cwnd;  // Congestion Avoidance
            }
            dup_ack_count = 0;
        } else {
            dup_ack_count++;
            if (dup_ack_count == 3) {
                // Fast Retransmit
                ssthresh = cwnd / 2;
                cwnd = ssthresh + 3;
                // Reenvia o pacote perdido
                int lost_index = ack.h.pkt_seq % cwnd;
                sendto(sockfd, &sent_packets[lost_index], sent_sizes[lost_index], 0,
                       (struct sockaddr *)dst, sizeof(struct sockaddr_in));
            }
        }
    } else if (nr < 0 && errno == EAGAIN) {
        // Timeout: Reduz a janela de congestionamento
        ssthresh = cwnd / 2;
        cwnd = INIT_WINDOW_SIZE;
        dup_ack_count = 0;
        // Reenvia todos os pacotes na janela
        for (int i = 0; i < cwnd; i++) {
            sendto(sockfd, &sent_packets[i], sent_sizes[i], 0,
                   (struct sockaddr *)dst, sizeof(struct sockaddr_in));
        }
    }

    // Envio do pacote de término
    if (file_end && packets_in_flight == 0) {
        pkt term_pkt;
        memset(&term_pkt, 0, sizeof(pkt));
        if (make_pkt(&term_pkt, PKT_DATA, _snd_seqnum, NULL, 0) < 0)
            return SEND_ERROR;

        for (int i = 0; i < 5; i++) {
            if (sendto(sockfd, &term_pkt, term_pkt.h.pkt_size, 0,
                       (struct sockaddr *)dst, sizeof(struct sockaddr_in)) < 0)
                return SEND_ERROR;
            usleep(100000);
        }
        state_initialized = 0;
        return FILE_DONE;
    }

    return PART_SENT;
}

// Função principal de recepção de pacotes (rdt_recv)
int rdt_recv(int sockfd, struct sockaddr_in *src) {
    pkt p, ack;
    int nr, ns;
    socklen_t addrlen;
    FILE *fp = fopen("output.txt", "w");
    if (!fp) return ERROR;

    while (1) {
        addrlen = sizeof(struct sockaddr_in);
        nr = recvfrom(sockfd, &p, sizeof(pkt), 0, (struct sockaddr*)src, &addrlen);
        if (nr < 0) break;

        int msg_size = p.h.pkt_size - sizeof(hdr);
        if (msg_size == 0) break;

        if (!iscorrupted(&p)) {
            fwrite(p.msg, 1, msg_size, fp);
            fflush(fp);
        }
				
				_rcv_seqnum++;
				
				if (p.h.pkt_seq != _rcv_seqnum) {
          printf("%d %d", p.h.pkt_seq, _rcv_seqnum);
          if (make_pkt(&ack, PKT_ACK, _rcv_seqnum - 1, NULL, 0) < 0) {
            fclose(fp);
            return ERROR;
          }

          ns = sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, sizeof(struct sockaddr_in));

          if (ns < 0) {
            perror("package out-of-order");
            return ERROR;
          }

          continue;
				}

        if (make_pkt(&ack, PKT_ACK, p.h.pkt_seq, NULL, 0) < 0) break;
        sendto(sockfd, &ack, ack.h.pkt_size, 0, (struct sockaddr*)src, sizeof(struct sockaddr_in));
    }

    fclose(fp);
    return SUCCESS;
}
